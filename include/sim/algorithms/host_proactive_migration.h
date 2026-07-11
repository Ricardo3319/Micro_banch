#pragma once
#include "sim/algorithms/scheduler.h"
#include "sim/common/constants.h"
#include <random>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace sim {

// M0: Proactive predictive migration.
class ProactiveMigrationScheduler : public IScheduler {
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

    MethodType method() const override { return MethodType::M0_PROACTIVE_MIGRATION; }

    void set_config(const M0Config& cfg) {
        cfg_ = cfg;
        effective_margin_us_ = cfg_.margin_us;
    }

    struct MigrationDecision {
        int   dst_host = -1;
        Task* task     = nullptr;
        double benefit_est_us = 0.0;
    };

    struct AqbCandidate {
        int src_host = -1;
        int src_core = -1;
        Task* task = nullptr;
        double local_latency_us = 0.0;
        double urgency = 0.0;
        double src_work_us = 0.0;
    };

    bool host_ready(int host_id, double now_us) const {
        auto it = last_migrate_per_host_.find(host_id);
        return it == last_migrate_per_host_.end()
            || now_us >= it->second + B2_T_COOLDOWN_US;
    }

    void mark_host_migrated(int host_id, double now_us, int count = 1) {
        last_migrate_per_host_[host_id] = now_us;
        migrations_per_host_[host_id] += static_cast<uint64_t>(count);
    }

    // Periodic proactive scan on a single core's wait queue.
    // Uses ONLY stale_view (no cross-host real-time data).
    // node_capacities: per-node capacity factor for heterogeneous awareness.
    MigrationDecision check_core(Core& core, Node& src_node,
                                  const std::vector<int>& stale_view,
                                  const std::vector<double>& node_capacities,
                                  double now_us,
                                  std::mt19937_64& rng) {
        MigrationDecision dec;

        // Per-host cooldown.
        double& last = last_migrate_per_host_[src_node.node_id];
        if (now_us < last + B2_T_COOLDOWN_US) return dec;

        // Per-host budget: limit migrations per measurement window.
        uint64_t& host_mig = migrations_per_host_[src_node.node_id];

        double residual_us = 0.0;
        if (!core.idle && core.running) {
            residual_us = core.finish_time_us - now_us;
            if (residual_us < 0.0) residual_us = 0.0;
        }

        double cumulative_wait_us = residual_us;
        Task* cur = core.wait_queue.begin();
        while (cur) {
            Task* nxt = cur->next;
            double E = cur->expected_service_time_us;
            double local_exec_us = E / core.capacity + T_host_us;
            cumulative_wait_us += local_exec_us;

            double t_elapsed_us = now_us - cur->generate_time_us;
            double local_total_us = t_elapsed_us + cumulative_wait_us;

            // (1) Risk constraint (runtime alpha).
            if (local_total_us > cur->deadline_budget_us * cfg_.alpha) {
                int src_qlen = src_node.local_total_queue_len();
                int best_dst = find_best_remote(src_node.node_id, stale_view, src_qlen, rng);
                if (best_dst >= 0) {
                    // Use actual destination capacity for heterogeneous awareness.
                    double dst_cap = node_capacities[best_dst];
                    double remote_wait_us = estimate_remote_wait_stale(
                        stale_view[best_dst], E, dst_cap);
                    double remote_exec_us = E / dst_cap + T_host_us;
                    double remote_total_us = t_elapsed_us + T_net_oneway_us + T_host_us
                                             + remote_wait_us + remote_exec_us;
                    // (2) Benefit constraint.
                    if (local_total_us > remote_total_us) {
                        // (3) Hysteresis / margin constraint.
                        if (local_total_us > remote_total_us + effective_margin_us_) {
                            core.remove_waiting(cur);
                            dec.dst_host = best_dst;
                            dec.task = cur;
                            dec.benefit_est_us = local_total_us - remote_total_us;
                            dec.task->estimated_local_latency_us = local_total_us;
                            last = now_us;
                            ++host_mig;
                            return dec;
                        }
                    }
                }
            }
            cur = nxt;
        }
        return dec;
    }

    void adjust_margin(double /*invalid_ratio*/) {
        // Flat margin: multi-level scaling was harmful (iterations #12-13).
        effective_margin_us_ = cfg_.margin_us;
    }

    void collect_aqb_candidates(Core& core, Node& src_node,
                                double now_us,
                                std::vector<AqbCandidate>& out) {
        if (!host_ready(src_node.node_id, now_us)) return;

        double residual_us = 0.0;
        if (!core.idle && core.running) {
            residual_us = core.finish_time_us - now_us;
            if (residual_us < 0.0) residual_us = 0.0;
        }

        double cumulative_wait_us = residual_us;
        int depth = 0;
        Task* cur = core.wait_queue.begin();
        while (cur && depth < AQB_SCAN_DEPTH) {
            double local_exec_us = cur->expected_service_time_us / core.capacity + T_host_us;
            double t_elapsed_us = now_us - cur->generate_time_us;
            double local_total_us = t_elapsed_us + cumulative_wait_us + local_exec_us;
            double risk = cur->deadline_budget_us > 0.0
                ? local_total_us / cur->deadline_budget_us : 0.0;
            double urgency = risk - cfg_.alpha;
            if (!cur->migrated && urgency > 0.0) {
                AqbCandidate cand;
                cand.src_host = src_node.node_id;
                cand.src_core = core.core_id;
                cand.task = cur;
                cand.local_latency_us = local_total_us;
                cand.urgency = urgency;
                cand.src_work_us = local_exec_us;
                out.push_back(cand);
            }
            cumulative_wait_us += local_exec_us;
            cur = cur->next;
            ++depth;
        }
    }

    void reset() {
        last_migrate_per_host_.clear();
        migrations_per_host_.clear();
        effective_margin_us_ = cfg_.margin_us;
    }

private:
    int find_best_remote(int src_id,
                          const std::vector<int>& stale_view,
                          int /*src_local_qlen*/,
                          std::mt19937_64& rng) {
        int n = static_cast<int>(stale_view.size());
        std::uniform_int_distribution<int> dist(0, n - 1);
        int best = -1;
        int best_q = std::numeric_limits<int>::max();
        for (int i = 0; i < cfg_.k_dst; ++i) {
            int c = dist(rng);
            if (c == src_id) continue;
            if (stale_view[c] < best_q) {
                best = c; best_q = stale_view[c];
            }
        }
        return best;
    }

    // Estimate remote wait using stale total queue length (fair: no cross-host real-time).
    double estimate_remote_wait_stale(int stale_total_qlen, double E, double capacity) const {
        double per_core = static_cast<double>(stale_total_qlen) / CORES_PER_HOST;
        return per_core * (E / capacity + T_host_us);
    }

    M0Config cfg_;
    std::unordered_map<int, double>   last_migrate_per_host_;
    std::unordered_map<int, uint64_t> migrations_per_host_;
    double effective_margin_us_ = M0_T_MARGIN_US;
};

} // namespace sim
