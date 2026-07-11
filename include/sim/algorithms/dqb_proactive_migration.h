#pragma once

#include "sim/algorithms/scheduler.h"
#include "sim/common/constants.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <unordered_map>
#include <vector>

namespace sim {

enum class DqbBatchType {
    GenericPressure,
    ShortBehindLong,
    MiceBehindElephant,
    SlowNodeBatchPressure,
    DistributionWindow
};

struct QueueBucketSummary {
    int count = 0;
    double work_us = 0.0;
    double oldest_age_us = 0.0;
    double min_slack_us = std::numeric_limits<double>::infinity();

    void add(double work, double age, double slack) {
        ++count;
        work_us += work;
        if (age > oldest_age_us) oldest_age_us = age;
        if (slack < min_slack_us) min_slack_us = slack;
    }
};

struct QueueSummary {
    int scanned_tasks = 0;
    double residual_running_us = 0.0;
    double scanned_work_us = 0.0;
    bool running_long = false;
    bool running_elephant = false;
    QueueBucketSummary short_bucket;
    QueueBucketSummary long_bucket;
    QueueBucketSummary mice_bucket;
    QueueBucketSummary medium_bucket;
    QueueBucketSummary elephant_bucket;
};

struct QueueBatchCandidate {
    int src_host = -1;
    int src_core = -1;
    DqbBatchType type = DqbBatchType::GenericPressure;
    std::vector<Task*> tasks;
    std::vector<double> local_finish_us_per_task;
    std::vector<int> src_core_for_task;
    std::vector<int> src_cores;
    int move_count = 0;
    double batch_service_us = 0.0;
    double batch_work_src_us = 0.0;
    double work_before_us = 0.0;
    double oldest_age_us = 0.0;
    double min_slo_us = std::numeric_limits<double>::infinity();
    double estimated_local_tail_us = 0.0;
    double risk_mass = 0.0;
    double blocking_score = 0.0;
    double estimate_confidence = 1.0;
    int short_count = 0;
    int elephant_count = 0;
    int host_fragment_count = 1;
};

// M2/DQB-PM: distribution-aware queue-batch proactive migration.
// The online control path scans a fixed queue prefix, partitions it into large
// contiguous micro-batches, scores only batch summaries, and commits whole
// batches rather than individual request picks.
class DqbProactiveMigrationScheduler : public IScheduler {
public:
    int on_task_dispatch(const std::vector<Node>& /*nodes*/,
                         const std::vector<int>& stale_view,
                         double /*service_est_us*/,
                         std::mt19937_64& rng) override {
        int n = static_cast<int>(stale_view.size());
        std::uniform_int_distribution<int> dist(0, n - 1);
        int a = dist(rng), b = dist(rng);
        return (stale_view[a] <= stale_view[b]) ? a : b;
    }

    MethodType method() const override { return MethodType::M2_DQB_PROACTIVE_MIGRATION; }

    void set_config(const M0Config& cfg) { cfg_ = cfg; }

    bool host_ready(int host_id, double now_us) const {
        auto it = last_migrate_per_host_.find(host_id);
        return it == last_migrate_per_host_.end()
            || now_us >= it->second + B2_T_COOLDOWN_US;
    }

    void mark_host_migrated(int host_id, double now_us, int moved_tasks) {
        last_migrate_per_host_[host_id] = now_us;
        migrations_per_host_[host_id] += static_cast<uint64_t>(moved_tasks);
    }

    void reset() {
        last_migrate_per_host_.clear();
        migrations_per_host_.clear();
        next_batch_id_ = 0;
    }

    uint64_t next_batch_id() { return ++next_batch_id_; }

    void collect_batch_candidate(Core& core, Node& src_node,
                                 WorkloadType workload_type,
                                 double now_us,
                                 std::vector<QueueBatchCandidate>& out) const {
        if (!host_ready(src_node.node_id, now_us)) return;
        int min_tasks = min_tasks_for(workload_type);
        double min_batch_work_us = min_batch_work_for(workload_type);

        QueueSummary summary;
        std::vector<ScanItem> items;
        scan_core(core, now_us, summary, items);
        if (items.size() < static_cast<size_t>(min_tasks)) return;

        std::vector<BatchRegion> regions;
        build_batch_regions(summary, items, workload_type, regions);
        if (regions.empty()) return;

        std::vector<QueueBatchCandidate> options;
        options.reserve(regions.size());
        for (int i = 0; i < static_cast<int>(regions.size()); ++i) {
            auto cand = make_region_candidate(
                core, src_node, workload_type, regions, items, i);
            if (cand.move_count >= min_tasks
                && cand.batch_work_src_us >= min_batch_work_us
                && cand.risk_mass > 0.0) {
                options.push_back(cand);
            }
        }

        auto score_of = [](const QueueBatchCandidate& cand) {
            double type_weight = 1.0;
            switch (cand.type) {
                case DqbBatchType::ShortBehindLong:     type_weight = 1.30; break;
                case DqbBatchType::MiceBehindElephant:  type_weight = 1.20; break;
                case DqbBatchType::SlowNodeBatchPressure: type_weight = 1.10; break;
                case DqbBatchType::DistributionWindow:  type_weight = 1.05; break;
                case DqbBatchType::GenericPressure:     type_weight = 0.90; break;
            }
            return type_weight * cand.risk_mass
                 + 0.05 * cand.blocking_score
                 + 10.0 * cand.estimate_confidence
                 + 0.25 * static_cast<double>(cand.move_count);
        };

        auto best = std::max_element(
            options.begin(), options.end(),
            [&](const QueueBatchCandidate& a, const QueueBatchCandidate& b) {
                return score_of(a) < score_of(b);
            });
        if (best != options.end() && best->risk_mass > 0.0)
            out.push_back(*best);
    }

    void collect_host_batch_candidate(Node& src_node,
                                      WorkloadType workload_type,
                                      double now_us,
                                      std::vector<QueueBatchCandidate>& out) const {
        if (workload_type != WorkloadType::W3_POISSON_LOGNORMAL) return;
        if (!host_ready(src_node.node_id, now_us)) return;

        struct HostFragment {
            QueueBatchCandidate cand;
            DqbBatchType type = DqbBatchType::GenericPressure;
        };

        std::vector<HostFragment> fragments;
        fragments.reserve(CORES_PER_HOST * 4);

        for (auto& core : src_node.cores) {
            QueueSummary summary;
            std::vector<ScanItem> items;
            scan_core(core, now_us, summary, items);
            if (items.empty()) continue;

            std::vector<BatchRegion> regions;
            build_batch_regions(summary, items, workload_type, regions);
            for (int ri = 0; ri < static_cast<int>(regions.size()); ++ri) {
                const auto& region = regions[ri];
                if (region.task_count < DQB_W3_MIN_FRAGMENT_TASKS) continue;

                double short_ratio = region.task_count > 0
                    ? static_cast<double>(region.short_count) / region.task_count : 0.0;
                DqbBatchType type = classify_region(region, workload_type, core);
                bool accept = false;
                if (short_ratio >= DQB_W3_MICE_DOMINANT_RATIO
                    && region.blocking_elephant_work_us
                        >= 0.50 * blocking_threshold_for(workload_type)) {
                    type = DqbBatchType::MiceBehindElephant;
                    accept = true;
                } else if (type == DqbBatchType::DistributionWindow) {
                    accept = (short_ratio >= 0.50
                        && region.confidence >= 0.55
                        && region.risk_mass > 0.0);
                }
                if (!accept) continue;

                auto frag = make_candidate_from_region_range(
                    core, src_node, workload_type, regions, items, ri, ri, type);
                if (frag.move_count <= 0) continue;
                fragments.push_back(HostFragment{frag, type});
            }
        }

        if (fragments.empty()) return;

        auto fragment_score = [](const QueueBatchCandidate& cand) {
            double type_weight = 1.0;
            switch (cand.type) {
                case DqbBatchType::MiceBehindElephant: type_weight = 1.25; break;
                case DqbBatchType::DistributionWindow: type_weight = 1.10; break;
                case DqbBatchType::GenericPressure:    type_weight = 0.95; break;
                case DqbBatchType::ShortBehindLong:    type_weight = 1.05; break;
                case DqbBatchType::SlowNodeBatchPressure: type_weight = 1.0; break;
            }
            return type_weight * cand.risk_mass
                 + 0.05 * cand.blocking_score
                 + 8.0 * cand.estimate_confidence
                 + 0.20 * static_cast<double>(cand.move_count);
        };

        std::sort(fragments.begin(), fragments.end(),
                  [&](const HostFragment& a, const HostFragment& b) {
                      if (a.type != b.type) {
                          return static_cast<int>(a.type) < static_cast<int>(b.type);
                      }
                      if (a.cand.oldest_age_us != b.cand.oldest_age_us) {
                          return a.cand.oldest_age_us > b.cand.oldest_age_us;
                      }
                      return fragment_score(a.cand) > fragment_score(b.cand);
                  });

        auto combine_host_fragments =
            [&](DqbBatchType type, int begin, int end) -> QueueBatchCandidate {
                QueueBatchCandidate agg;
                agg.src_host = src_node.node_id;
                agg.src_core = -1;
                agg.type = type;
                agg.work_before_us = std::numeric_limits<double>::infinity();

                double confidence_weighted = 0.0;
                for (int i = begin; i <= end; ++i) {
                    const auto& frag = fragments[i].cand;
                    agg.host_fragment_count += (i == begin) ? 0 : 1;
                    agg.batch_service_us += frag.batch_service_us;
                    agg.batch_work_src_us += frag.batch_work_src_us;
                    agg.risk_mass += frag.risk_mass;
                    agg.blocking_score = std::max(agg.blocking_score, frag.blocking_score);
                    agg.oldest_age_us = std::max(agg.oldest_age_us, frag.oldest_age_us);
                    agg.min_slo_us = std::min(agg.min_slo_us, frag.min_slo_us);
                    agg.estimated_local_tail_us =
                        std::max(agg.estimated_local_tail_us, frag.estimated_local_tail_us);
                    agg.short_count += frag.short_count;
                    agg.elephant_count += frag.elephant_count;
                    agg.work_before_us = std::min(agg.work_before_us, frag.work_before_us);
                    confidence_weighted += frag.estimate_confidence
                        * static_cast<double>(frag.move_count);

                    for (size_t ti = 0; ti < frag.tasks.size(); ++ti) {
                        agg.tasks.push_back(frag.tasks[ti]);
                        agg.local_finish_us_per_task.push_back(frag.local_finish_us_per_task[ti]);
                        agg.src_core_for_task.push_back(frag.src_core_for_task[ti]);
                    }
                    for (int src_core : frag.src_cores) {
                        if (std::find(agg.src_cores.begin(), agg.src_cores.end(), src_core)
                            == agg.src_cores.end()) {
                            agg.src_cores.push_back(src_core);
                        }
                    }
                }

                agg.move_count = static_cast<int>(agg.tasks.size());
                if (agg.move_count > 0) {
                    agg.estimate_confidence =
                        confidence_weighted / static_cast<double>(agg.move_count);
                }
                if (!std::isfinite(agg.work_before_us)) agg.work_before_us = 0.0;
                return agg;
            };

        QueueBatchCandidate best;
        double best_score = -std::numeric_limits<double>::infinity();

        for (int seed = 0; seed < static_cast<int>(fragments.size()); ++seed) {
            DqbBatchType type = fragments[seed].type;
            int tasks = 0;
            double work = 0.0;
            int used = 0;
            double max_age = fragments[seed].cand.oldest_age_us;
            double min_age = fragments[seed].cand.oldest_age_us;
            int end = seed - 1;

            for (int i = seed; i < static_cast<int>(fragments.size()); ++i) {
                const auto& frag = fragments[i];
                if (frag.type != type) {
                    if (used > 0) break;
                    continue;
                }
                double next_max_age = std::max(max_age, frag.cand.oldest_age_us);
                double next_min_age = std::min(min_age, frag.cand.oldest_age_us);
                if (used > 0 && next_max_age - next_min_age > DQB_W3_HOST_AGE_SPREAD_US) break;
                if (tasks + frag.cand.move_count > cfg_.dqb_max_tasks_per_batch) break;
                if (work + frag.cand.batch_work_src_us > DQB_MAX_BATCH_WORK_US) break;

                max_age = next_max_age;
                min_age = next_min_age;
                tasks += frag.cand.move_count;
                work += frag.cand.batch_work_src_us;
                end = i;
                ++used;

                if (tasks >= DQB_W3_HOST_TARGET_TASKS
                    || used >= DQB_W3_HOST_MAX_FRAGMENTS) {
                    break;
                }
            }

            if (end < seed) continue;

            auto cand = combine_host_fragments(type, seed, end);
            if (cand.move_count < DQB_W3_HOST_MIN_TASKS) continue;
            if (cand.batch_work_src_us < DQB_W3_MIN_BATCH_WORK_US) continue;
            if (cand.src_cores.empty()) continue;

            double score = fragment_score(cand)
                         + 0.50 * static_cast<double>(cand.host_fragment_count)
                         + 0.10 * static_cast<double>(cand.src_cores.size());
            if (score > best_score) {
                best_score = score;
                best = cand;
            }
        }

        if (best.move_count >= DQB_W3_HOST_MIN_TASKS && best.risk_mass > 0.0) {
            out.push_back(best);
        }
    }

private:
    struct ScanItem {
        Task* task = nullptr;
        double service_us = 0.0;
        double exec_src_us = 0.0;
        double age_us = 0.0;
        double prefix_work_us = 0.0;
        double slo_us = 0.0;
        bool is_short = false;
        bool is_elephant = false;
    };

    struct BatchRegion {
        int begin_index = 0;
        int end_index = -1;
        int task_count = 0;
        int short_count = 0;
        int medium_count = 0;
        int elephant_count = 0;
        double work_before_us = 0.0;
        double batch_service_us = 0.0;
        double batch_work_src_us = 0.0;
        double short_work_us = 0.0;
        double elephant_work_us = 0.0;
        double oldest_age_us = 0.0;
        double min_slo_us = std::numeric_limits<double>::infinity();
        double min_slack_us = std::numeric_limits<double>::infinity();
        double estimated_local_tail_us = 0.0;
        double risk_mass = 0.0;
        double blocking_long_work_us = 0.0;
        double blocking_elephant_work_us = 0.0;
        double confidence = 1.0;
    };

    static bool is_short(double service_us) {
        return service_us <= SLO_SHORT_SERVICE_THRESHOLD_US;
    }

    static bool is_elephant(double service_us) {
        return service_us >= DQB_ELEPHANT_SERVICE_US;
    }

    static int min_tasks_for(WorkloadType workload_type) {
        return (workload_type == WorkloadType::W3_POISSON_LOGNORMAL)
            ? DQB_W3_MIN_TASKS_PER_BATCH
            : DQB_MIN_TASKS_PER_BATCH;
    }

    static int segment_target_for(WorkloadType workload_type) {
        return (workload_type == WorkloadType::W3_POISSON_LOGNORMAL)
            ? DQB_W3_SEGMENT_TARGET_TASKS
            : DQB_SEGMENT_TARGET_TASKS;
    }

    static double min_batch_work_for(WorkloadType workload_type) {
        return (workload_type == WorkloadType::W3_POISSON_LOGNORMAL)
            ? DQB_W3_MIN_BATCH_WORK_US
            : DQB_MIN_BATCH_WORK_US;
    }

    static double mice_dominant_ratio_for(WorkloadType workload_type) {
        return (workload_type == WorkloadType::W3_POISSON_LOGNORMAL)
            ? DQB_W3_MICE_DOMINANT_RATIO
            : DQB_MICE_DOMINANT_RATIO;
    }

    static double blocking_threshold_for(WorkloadType workload_type) {
        return (workload_type == WorkloadType::W3_POISSON_LOGNORMAL)
            ? DQB_W3_BLOCKING_WORK_THRESHOLD_US
            : DQB_BLOCKING_WORK_THRESHOLD_US;
    }

    void scan_core(Core& core, double now_us,
                   QueueSummary& summary,
                   std::vector<ScanItem>& items) const {
        if (!core.idle && core.running) {
            double residual = core.finish_time_us - now_us;
            if (residual > 0.0) summary.residual_running_us = residual;
            summary.running_long =
                core.running->expected_service_time_us > SLO_SHORT_SERVICE_THRESHOLD_US;
            summary.running_elephant =
                is_elephant(core.running->expected_service_time_us);
        }

        double prefix_work_us = summary.residual_running_us;
        Task* cur = core.wait_queue.begin();
        int scanned = 0;
        while (cur && scanned < DQB_SUMMARY_SCAN_LIMIT) {
            ScanItem item;
            item.task = cur;
            item.service_us = cur->expected_service_time_us;
            item.exec_src_us = item.service_us / core.capacity + T_host_us;
            item.age_us = now_us - cur->generate_time_us;
            item.prefix_work_us = prefix_work_us;
            item.slo_us = cur->deadline_budget_us;
            item.is_short = is_short(item.service_us);
            item.is_elephant = is_elephant(item.service_us);

            double est_finish_us = item.age_us + item.prefix_work_us + item.exec_src_us;
            double slack_us = item.slo_us - est_finish_us;
            if (item.is_short) summary.short_bucket.add(item.exec_src_us, item.age_us, slack_us);
            else summary.long_bucket.add(item.exec_src_us, item.age_us, slack_us);

            if (item.is_short) summary.mice_bucket.add(item.exec_src_us, item.age_us, slack_us);
            else if (item.is_elephant) {
                summary.elephant_bucket.add(item.exec_src_us, item.age_us, slack_us);
            } else {
                summary.medium_bucket.add(item.exec_src_us, item.age_us, slack_us);
            }

            items.push_back(item);
            prefix_work_us += item.exec_src_us;
            summary.scanned_work_us += item.exec_src_us;
            ++summary.scanned_tasks;
            cur = cur->next;
            ++scanned;
        }
    }

    double prior_short_fraction(WorkloadType workload_type) const {
        if (workload_type == WorkloadType::W3_POISSON_LOGNORMAL) {
            double z = (std::log(SLO_SHORT_SERVICE_THRESHOLD_US) - W3_LOGNORMAL_MU)
                     / (W3_LOGNORMAL_SIGMA * std::sqrt(2.0));
            return 0.5 * (1.0 + std::erf(z));
        }
        return BIMODAL_SHORT_PROB;
    }

    double estimate_distribution_confidence(int batch_tasks,
                                            int short_like_tasks,
                                            WorkloadType workload_type) const {
        if (batch_tasks <= 0) return 0.0;
        double n = static_cast<double>(batch_tasks);
        double observed_short = static_cast<double>(short_like_tasks) / n;
        double prior_short = prior_short_fraction(workload_type);
        double denom = std::max(0.05, std::max(prior_short, 1.0 - prior_short));
        double mix_fit = 1.0 - std::min(
            1.0, std::abs(observed_short - prior_short) / denom);
        double size_conf = std::min(
            1.0, std::sqrt(n / static_cast<double>(segment_target_for(workload_type))));
        return std::max(0.35, std::min(1.0, 0.40 + 0.40 * size_conf + 0.20 * mix_fit));
    }

    void add_item_to_region(const ScanItem& item, BatchRegion& region,
                            WorkloadType workload_type) const {
        if (region.task_count == 0) region.work_before_us = item.prefix_work_us;

        region.end_index = item.task ? region.end_index + 1 : region.end_index;
        ++region.task_count;
        region.batch_service_us += item.service_us;
        region.batch_work_src_us += item.exec_src_us;
        if (item.is_short) {
            ++region.short_count;
            region.short_work_us += item.exec_src_us;
        } else if (item.is_elephant) {
            ++region.elephant_count;
            region.elephant_work_us += item.exec_src_us;
        } else {
            ++region.medium_count;
        }
        if (item.age_us > region.oldest_age_us) region.oldest_age_us = item.age_us;
        if (item.slo_us < region.min_slo_us) region.min_slo_us = item.slo_us;

        double local_finish_us = item.age_us + item.prefix_work_us + item.exec_src_us;
        if (local_finish_us > region.estimated_local_tail_us)
            region.estimated_local_tail_us = local_finish_us;
        double slack_us = item.slo_us - local_finish_us;
        if (slack_us < region.min_slack_us) region.min_slack_us = slack_us;

        double excess_us = local_finish_us - cfg_.alpha * item.slo_us;
        if (excess_us > 0.0) region.risk_mass += excess_us;

        region.confidence = estimate_distribution_confidence(
            region.task_count,
            (workload_type == WorkloadType::W3_POISSON_LOGNORMAL)
                ? region.short_count
                : region.short_count,
            workload_type);
    }

    void recompute_region_tail(BatchRegion& region) const {
        if (region.task_count == 0) return;
        region.risk_mass *= region.confidence;
    }

    void build_batch_regions(const QueueSummary& summary,
                             const std::vector<ScanItem>& items,
                             WorkloadType workload_type,
                             std::vector<BatchRegion>& out) const {
        if (items.empty()) return;
        int min_tasks = min_tasks_for(workload_type);

        const int base_target =
            std::min(cfg_.dqb_max_tasks_per_batch, segment_target_for(workload_type));
        const double base_work_limit =
            DQB_MAX_BATCH_WORK_US / static_cast<double>(DQB_SEGMENT_EXPAND_LIMIT);

        int i = 0;
        while (i < static_cast<int>(items.size())) {
            BatchRegion region;
            region.begin_index = i;
            region.end_index = i - 1;

            while (i < static_cast<int>(items.size())) {
                const auto& item = items[i];
                if (region.task_count > 0) {
                    bool count_reached = region.task_count >= base_target;
                    bool work_reached =
                        region.batch_work_src_us + item.exec_src_us > base_work_limit;
                    if (count_reached || work_reached) break;
                }
                add_item_to_region(item, region, workload_type);
                ++i;
            }

            recompute_region_tail(region);
            if (region.task_count > 0) out.push_back(region);
        }

        if (out.size() >= 2 && out.back().task_count < min_tasks) {
            BatchRegion tail = out.back();
            BatchRegion& prev = out[out.size() - 2];
            if (prev.task_count + tail.task_count <= cfg_.dqb_max_tasks_per_batch
                && prev.batch_work_src_us + tail.batch_work_src_us <= DQB_MAX_BATCH_WORK_US) {
                prev.end_index = tail.end_index;
                prev.task_count += tail.task_count;
                prev.short_count += tail.short_count;
                prev.medium_count += tail.medium_count;
                prev.elephant_count += tail.elephant_count;
                prev.batch_service_us += tail.batch_service_us;
                prev.batch_work_src_us += tail.batch_work_src_us;
                prev.short_work_us += tail.short_work_us;
                prev.elephant_work_us += tail.elephant_work_us;
                if (tail.oldest_age_us > prev.oldest_age_us) prev.oldest_age_us = tail.oldest_age_us;
                if (tail.min_slo_us < prev.min_slo_us) prev.min_slo_us = tail.min_slo_us;
                if (tail.min_slack_us < prev.min_slack_us) prev.min_slack_us = tail.min_slack_us;
                if (tail.estimated_local_tail_us > prev.estimated_local_tail_us) {
                    prev.estimated_local_tail_us = tail.estimated_local_tail_us;
                }
                prev.risk_mass += tail.risk_mass;
                prev.confidence = estimate_distribution_confidence(
                    prev.task_count, prev.short_count, workload_type);
                out.pop_back();
            }
        }

        double long_blocking_us = summary.running_long ? summary.residual_running_us : 0.0;
        double elephant_blocking_us =
            summary.running_elephant ? summary.residual_running_us : 0.0;
        for (auto& region : out) {
            region.blocking_long_work_us = long_blocking_us;
            region.blocking_elephant_work_us = elephant_blocking_us;
            double short_ratio = region.task_count > 0
                ? static_cast<double>(region.short_count) / region.task_count : 0.0;
            double structural_risk = 0.0;
            if (workload_type == WorkloadType::W3_POISSON_LOGNORMAL
                && short_ratio >= mice_dominant_ratio_for(workload_type)) {
                structural_risk = std::max(
                    0.0, region.blocking_elephant_work_us - blocking_threshold_for(workload_type));
            }
            region.risk_mass += structural_risk * region.confidence;
            if (region.task_count > region.short_count) {
                long_blocking_us += region.batch_work_src_us - region.short_work_us;
            }
            elephant_blocking_us += region.elephant_work_us;
        }
    }

    DqbBatchType classify_region(const BatchRegion& region,
                                 WorkloadType workload_type,
                                 Core& core) const {
        int target_tasks = segment_target_for(workload_type);
        double blocking_threshold = blocking_threshold_for(workload_type);
        double short_ratio = region.task_count > 0
            ? static_cast<double>(region.short_count) / region.task_count : 0.0;
        double elephant_ratio = region.task_count > 0
            ? static_cast<double>(region.elephant_count) / region.task_count : 0.0;

        if (workload_type == WorkloadType::W3_POISSON_LOGNORMAL
            && short_ratio >= mice_dominant_ratio_for(workload_type)
            && region.blocking_elephant_work_us >= blocking_threshold) {
            return DqbBatchType::MiceBehindElephant;
        }
        if (workload_type != WorkloadType::W3_POISSON_LOGNORMAL
            && short_ratio >= DQB_SHORT_DOMINANT_RATIO
            && region.blocking_long_work_us >= blocking_threshold) {
            return DqbBatchType::ShortBehindLong;
        }
        if (core.capacity <= DQB_SLOW_CAPACITY_THRESHOLD
            && region.work_before_us + region.batch_work_src_us >= SLO_LONG_US) {
            return DqbBatchType::SlowNodeBatchPressure;
        }
        if (region.task_count >= target_tasks
            && elephant_ratio < 0.40
            && region.confidence >= 0.60) {
            return DqbBatchType::DistributionWindow;
        }
        return DqbBatchType::GenericPressure;
    }

    QueueBatchCandidate make_candidate_from_region_range(
        Core& core, Node& src_node, WorkloadType workload_type,
        const std::vector<BatchRegion>& regions,
        const std::vector<ScanItem>& items,
        int start_idx, int end_idx, DqbBatchType type) const {
        QueueBatchCandidate cand;
        cand.src_host = src_node.node_id;
        cand.src_core = core.core_id;
        cand.type = type;
        cand.work_before_us = regions[start_idx].work_before_us;
        cand.src_cores.push_back(core.core_id);

        double confidence_sum = 0.0;
        for (int ri = start_idx; ri <= end_idx; ++ri) {
            const auto& region = regions[ri];
            cand.blocking_score = std::max(
                cand.blocking_score,
                std::max(region.blocking_long_work_us, region.blocking_elephant_work_us));
            cand.risk_mass += region.risk_mass;
            confidence_sum += region.confidence;

            for (int ii = region.begin_index; ii <= region.end_index; ++ii) {
                const auto& item = items[ii];
                cand.tasks.push_back(item.task);
                cand.local_finish_us_per_task.push_back(
                    item.age_us + item.prefix_work_us + item.exec_src_us);
                cand.src_core_for_task.push_back(core.core_id);
                cand.batch_service_us += item.service_us;
                cand.batch_work_src_us += item.exec_src_us;
                if (item.is_short) ++cand.short_count;
                if (item.is_elephant) ++cand.elephant_count;
                if (item.age_us > cand.oldest_age_us) cand.oldest_age_us = item.age_us;
                if (item.slo_us < cand.min_slo_us) cand.min_slo_us = item.slo_us;
                double local_finish_us =
                    item.age_us + item.prefix_work_us + item.exec_src_us;
                if (local_finish_us > cand.estimated_local_tail_us)
                    cand.estimated_local_tail_us = local_finish_us;
            }
        }

        cand.move_count = static_cast<int>(cand.tasks.size());
        if (cand.move_count > 0) {
            cand.estimate_confidence =
                confidence_sum / static_cast<double>(end_idx - start_idx + 1);
        }
        return cand;
    }

    QueueBatchCandidate make_region_candidate(
        Core& core, Node& src_node, WorkloadType workload_type,
        const std::vector<BatchRegion>& regions,
        const std::vector<ScanItem>& items,
        int seed_idx) const {
        const auto& seed = regions[seed_idx];
        DqbBatchType type = classify_region(seed, workload_type, core);
        int min_tasks = min_tasks_for(workload_type);
        int target_tasks = segment_target_for(workload_type);
        double min_batch_work_us = min_batch_work_for(workload_type);
        double short_ratio = seed.task_count > 0
            ? static_cast<double>(seed.short_count) / seed.task_count : 0.0;
        if (seed.task_count < min_tasks) return QueueBatchCandidate{};
        if (seed.batch_work_src_us < min_batch_work_us) return QueueBatchCandidate{};
        if (workload_type != WorkloadType::W3_POISSON_LOGNORMAL
            && type == DqbBatchType::GenericPressure
            && short_ratio < 0.50) {
            return QueueBatchCandidate{};
        }
        if (workload_type != WorkloadType::W3_POISSON_LOGNORMAL
            && type == DqbBatchType::DistributionWindow
            && short_ratio < 0.60) {
            return QueueBatchCandidate{};
        }

        int end_idx = seed_idx;
        int tasks = 0;
        double work = 0.0;
        for (int ri = seed_idx;
             ri < static_cast<int>(regions.size())
             && ri < seed_idx + DQB_SEGMENT_EXPAND_LIMIT;
             ++ri) {
            const auto& region = regions[ri];
            if (tasks + region.task_count > cfg_.dqb_max_tasks_per_batch) break;
            if (work + region.batch_work_src_us > DQB_MAX_BATCH_WORK_US) break;

            bool type_compatible = false;
            if (type == DqbBatchType::DistributionWindow
                || type == DqbBatchType::GenericPressure) {
                type_compatible = true;
            } else {
                auto region_type = classify_region(region, workload_type, core);
                type_compatible =
                    (region_type == type)
                    || (region_type == DqbBatchType::DistributionWindow)
                    || (region_type == DqbBatchType::GenericPressure);
            }
            if (!type_compatible) break;

            tasks += region.task_count;
            work += region.batch_work_src_us;
            end_idx = ri;

            if (tasks >= target_tasks * 2
                && work >= min_batch_work_us * 2.0) {
                break;
            }
        }

        return make_candidate_from_region_range(
            core, src_node, workload_type, regions, items, seed_idx, end_idx, type);
    }

    M0Config cfg_;
    std::unordered_map<int, double> last_migrate_per_host_;
    std::unordered_map<int, uint64_t> migrations_per_host_;
    uint64_t next_batch_id_ = 0;
};

} // namespace sim
