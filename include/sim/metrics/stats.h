#pragma once
#include "sim/common/constants.h"
#include "sim/metrics/histogram.h"
#include <cstdint>
#include <array>
#include <sstream>
#include <string>
#include <unordered_set>

namespace sim {

enum class NoMigrateReason : int {
    NO_BATCH_FORMED = 0,
    LOW_CONFIDENCE = 1,
    LOW_EXPECTED_GAIN = 2,
    DST_RESERVATION_HIGH = 3,
    DST_TAIL_HARM = 4,
    SATURATION_GUARD = 5,
    BUDGET_EXHAUSTED = 6,
    SPARSE_BLOCKING_NOT_BATCHABLE = 7
};

struct MetricsCollector {
    Histogram latency_hist;
    uint64_t total_finished    = 0;
    uint64_t slo_violations    = 0;
    uint64_t short_finished    = 0;
    uint64_t short_slo_violations = 0;
    uint64_t long_finished     = 0;
    uint64_t long_slo_violations = 0;
    uint64_t mice_finished     = 0;
    uint64_t mice_slo_violations = 0;
    uint64_t elephant_finished = 0;
    uint64_t elephant_slo_violations = 0;
    uint64_t total_migrations  = 0;
    uint64_t invalid_migrations = 0;
    uint64_t intra_move_count = 0;
    uint64_t invalid_intra_moves = 0;
    uint64_t steal_attempt_count = 0;
    uint64_t steal_success_count = 0;
    uint64_t stolen_task_count = 0;
    uint64_t proactive_intra_attempt_count = 0;
    uint64_t proactive_intra_success_count = 0;
    uint64_t rescue_attempt_count = 0;
    uint64_t rescue_candidate_count = 0;
    uint64_t locally_doomed_count = 0;
    uint64_t remote_feasible_count = 0;
    uint64_t target_safe_count = 0;
    uint64_t rescue_success_count = 0;
    double rescue_moved_work_us = 0.0;
    uint64_t target_unsafe_reject_count = 0;
    uint64_t remote_infeasible_reject_count = 0;
    uint64_t needless_migration_count = 0;
    uint64_t unsaved_migration_count = 0;
    uint64_t beneficial_migration_count = 0;
    uint64_t harmful_migration_count = 0;
    uint64_t predicted_target_unsafe_accept_count = 0;
    uint64_t target_harm_watch_count = 0;
    uint64_t harmful_actual_count = 0;
    uint64_t target_induced_miss_actual = 0;
    uint64_t relief_attempt_count = 0;
    uint64_t relief_success_count = 0;
    uint64_t relief_beneficial_count = 0;
    uint64_t relief_useless_count = 0;
    double relief_moved_work_us = 0.0;
    double intra_moved_work_us = 0.0;
    double migrated_work_us = 0.0;
    uint64_t batch_candidate_count = 0;
    uint64_t batch_selected_count = 0;
    uint64_t batch_move_count = 0;
    uint64_t summary_update_count = 0;
    uint64_t reservation_reject_count = 0;
    uint64_t saturation_guard_count = 0;
    uint64_t batch_size_1_count = 0;
    uint64_t batch_size_2_7_count = 0;
    uint64_t batch_size_8_31_count = 0;
    uint64_t batch_size_32_plus_count = 0;
    uint64_t batch_type_generic_count = 0;
    uint64_t batch_type_short_count = 0;
    uint64_t batch_type_mice_count = 0;
    uint64_t batch_type_slow_count = 0;
    uint64_t batch_type_distribution_count = 0;
    uint64_t target_plan_reject_count = 0;
    uint64_t no_batch_formed_count = 0;
    uint64_t low_confidence_count = 0;
    uint64_t low_expected_gain_count = 0;
    uint64_t dst_reservation_high_count = 0;
    uint64_t dst_tail_harm_count = 0;
    uint64_t budget_exhausted_count = 0;
    uint64_t sparse_blocking_not_batchable_count = 0;
    double target_harm_est_us = 0.0;
    double destination_virtual_occupancy_sum_us = 0.0;
    uint64_t destination_virtual_occupancy_samples = 0;
    double source_queue_work_sum_us = 0.0;
    uint64_t source_queue_depth_sum = 0;
    uint64_t source_queue_samples = 0;
    std::array<uint64_t, DQB_MAX_TASKS_PER_BATCH + 2> exact_batch_size_hist{};
    std::unordered_set<uint64_t> harmful_actual_migration_ids;
    uint64_t warmup_remaining  = 0;
    bool     recording         = false;

    void init(int warmup) {
        warmup_remaining = warmup;
        recording = false;
        latency_hist.reset();
        total_finished = slo_violations = total_migrations = invalid_migrations = 0;
        intra_move_count = invalid_intra_moves = 0;
        steal_attempt_count = steal_success_count = stolen_task_count = 0;
        proactive_intra_attempt_count = proactive_intra_success_count = 0;
        rescue_attempt_count = rescue_candidate_count = locally_doomed_count = 0;
        remote_feasible_count = target_safe_count = rescue_success_count = 0;
        rescue_moved_work_us = 0.0;
        target_unsafe_reject_count = remote_infeasible_reject_count = 0;
        needless_migration_count = unsaved_migration_count = 0;
        beneficial_migration_count = harmful_migration_count = 0;
        predicted_target_unsafe_accept_count = target_harm_watch_count = 0;
        harmful_actual_count = target_induced_miss_actual = 0;
        relief_attempt_count = relief_success_count = 0;
        relief_beneficial_count = relief_useless_count = 0;
        relief_moved_work_us = 0.0;
        intra_moved_work_us = 0.0;
        short_finished = short_slo_violations = 0;
        long_finished = long_slo_violations = 0;
        mice_finished = mice_slo_violations = 0;
        elephant_finished = elephant_slo_violations = 0;
        migrated_work_us = 0.0;
        batch_candidate_count = batch_selected_count = batch_move_count = 0;
        summary_update_count = reservation_reject_count = saturation_guard_count = 0;
        batch_size_1_count = batch_size_2_7_count = 0;
        batch_size_8_31_count = batch_size_32_plus_count = 0;
        batch_type_generic_count = batch_type_short_count = batch_type_mice_count = 0;
        batch_type_slow_count = batch_type_distribution_count = 0;
        target_plan_reject_count = 0;
        no_batch_formed_count = low_confidence_count = low_expected_gain_count = 0;
        dst_reservation_high_count = dst_tail_harm_count = budget_exhausted_count = 0;
        sparse_blocking_not_batchable_count = 0;
        target_harm_est_us = 0.0;
        destination_virtual_occupancy_sum_us = 0.0;
        destination_virtual_occupancy_samples = 0;
        source_queue_work_sum_us = 0.0;
        source_queue_depth_sum = 0;
        source_queue_samples = 0;
        exact_batch_size_hist.fill(0);
        harmful_actual_migration_ids.clear();
    }

    void on_task_finish(double latency_us, double slo_us, double service_us) {
        if (warmup_remaining > 0) { --warmup_remaining; return; }
        if (!recording) recording = true;
        latency_hist.record(latency_us);
        ++total_finished;
        bool violated = latency_us > slo_us;
        if (violated) ++slo_violations;

        if (service_us <= SLO_SHORT_SERVICE_THRESHOLD_US) {
            ++short_finished;
            ++mice_finished;
            if (violated) {
                ++short_slo_violations;
                ++mice_slo_violations;
            }
        } else {
            ++long_finished;
            if (violated) ++long_slo_violations;
        }

        if (service_us >= DQB_ELEPHANT_SERVICE_US) {
            ++elephant_finished;
            if (violated) ++elephant_slo_violations;
        }
    }

    void on_migration(bool invalid) {
        if (!recording) return;
        ++total_migrations;
        if (invalid) ++invalid_migrations;
    }

    void on_migration_scheduled_work(double work_us) {
        if (!recording) return;
        migrated_work_us += work_us;
    }

    void on_steal_attempt() {
        if (!recording) return;
        ++steal_attempt_count;
    }

    void on_steal_success(double work_us) {
        if (!recording) return;
        ++steal_success_count;
        ++stolen_task_count;
        ++intra_move_count;
        intra_moved_work_us += work_us;
    }

    void on_proactive_intra_attempt() {
        if (!recording) return;
        ++proactive_intra_attempt_count;
    }

    void on_proactive_intra_success(double work_us) {
        if (!recording) return;
        ++proactive_intra_success_count;
        ++intra_move_count;
        intra_moved_work_us += work_us;
    }

    void on_proactive_intra_finish(bool invalid) {
        if (!recording) return;
        if (invalid) ++invalid_intra_moves;
    }

    void on_rescue_attempt() {
        if (!recording) return;
        ++rescue_attempt_count;
    }

    void on_rescue_candidate() {
        if (!recording) return;
        ++rescue_candidate_count;
    }

    void on_rescue_locally_doomed() {
        if (!recording) return;
        ++locally_doomed_count;
    }

    void on_rescue_remote_feasible() {
        if (!recording) return;
        ++remote_feasible_count;
    }

    void on_rescue_target_safe() {
        if (!recording) return;
        ++target_safe_count;
    }

    void on_rescue_remote_infeasible_reject() {
        if (!recording) return;
        ++remote_infeasible_reject_count;
    }

    void on_rescue_target_unsafe_reject() {
        if (!recording) return;
        ++target_unsafe_reject_count;
    }

    void on_relief_attempt() {
        if (!recording) return;
        ++relief_attempt_count;
    }

    void on_rescue_success(double work_us, bool predicted_harmful, bool relief = false) {
        if (!recording) return;
        ++rescue_success_count;
        ++intra_move_count;
        rescue_moved_work_us += work_us;
        intra_moved_work_us += work_us;
        if (predicted_harmful) ++predicted_target_unsafe_accept_count;
        if (relief) {
            ++relief_success_count;
            relief_moved_work_us += work_us;
        }
    }

    void on_rescue_finish(double predicted_local_latency_us,
                          double actual_latency_us,
                          double slo_us,
                          bool relief = false) {
        if (!recording) return;
        if (relief) {
            if (actual_latency_us + 1e-9 < predicted_local_latency_us) {
                ++relief_beneficial_count;
            } else {
                ++relief_useless_count;
            }
        }
        if (predicted_local_latency_us <= slo_us) {
            ++needless_migration_count;
        } else if (actual_latency_us <= slo_us) {
            ++beneficial_migration_count;
        } else {
            ++unsaved_migration_count;
        }
    }

    void on_rescue_target_harm_watch(uint64_t watched_tasks) {
        if (!recording) return;
        target_harm_watch_count += watched_tasks;
    }

    void on_rescue_target_induced_miss(uint64_t migration_id) {
        if (!recording || migration_id == 0) return;
        ++target_induced_miss_actual;
        if (harmful_actual_migration_ids.insert(migration_id).second) {
            ++harmful_actual_count;
            ++harmful_migration_count;
        }
    }

    void on_batch_candidates(uint64_t candidates, uint64_t summaries) {
        if (!recording) return;
        batch_candidate_count += candidates;
        summary_update_count += summaries;
    }

    void on_batch_selected(uint64_t batches, uint64_t moved_tasks) {
        if (!recording) return;
        batch_selected_count += batches;
        batch_move_count += moved_tasks;
    }

    void on_batch_selected_detail(uint64_t moved_tasks, int batch_type) {
        if (!recording) return;
        uint64_t hist_idx = moved_tasks;
        if (hist_idx >= exact_batch_size_hist.size())
            hist_idx = exact_batch_size_hist.size() - 1;
        ++exact_batch_size_hist[hist_idx];
        if (moved_tasks <= 1) ++batch_size_1_count;
        else if (moved_tasks <= 7) ++batch_size_2_7_count;
        else if (moved_tasks <= 31) ++batch_size_8_31_count;
        else ++batch_size_32_plus_count;

        switch (batch_type) {
            case 0: ++batch_type_generic_count; break;
            case 1: ++batch_type_short_count; break;
            case 2: ++batch_type_mice_count; break;
            case 3: ++batch_type_slow_count; break;
            case 4: ++batch_type_distribution_count; break;
            default: break;
        }
    }

    void on_reservation_reject() {
        if (!recording) return;
        ++reservation_reject_count;
        ++dst_reservation_high_count;
    }

    void on_saturation_guard() {
        if (!recording) return;
        ++saturation_guard_count;
    }

    void on_target_plan_reject() {
        if (!recording) return;
        ++target_plan_reject_count;
    }

    void on_no_migrate(NoMigrateReason reason) {
        if (!recording) return;
        switch (reason) {
            case NoMigrateReason::NO_BATCH_FORMED: ++no_batch_formed_count; break;
            case NoMigrateReason::LOW_CONFIDENCE: ++low_confidence_count; break;
            case NoMigrateReason::LOW_EXPECTED_GAIN: ++low_expected_gain_count; break;
            case NoMigrateReason::DST_RESERVATION_HIGH: ++dst_reservation_high_count; break;
            case NoMigrateReason::DST_TAIL_HARM: ++dst_tail_harm_count; break;
            case NoMigrateReason::SATURATION_GUARD: ++saturation_guard_count; break;
            case NoMigrateReason::BUDGET_EXHAUSTED: ++budget_exhausted_count; break;
            case NoMigrateReason::SPARSE_BLOCKING_NOT_BATCHABLE:
                ++sparse_blocking_not_batchable_count;
                break;
        }
    }

    void on_target_harm_est(double harm_us) {
        if (!recording) return;
        if (harm_us > 0.0) target_harm_est_us += harm_us;
    }

    void on_destination_virtual_occupancy(double occupancy_us) {
        if (!recording) return;
        destination_virtual_occupancy_sum_us += occupancy_us;
        ++destination_virtual_occupancy_samples;
    }

    void on_source_queue_diag(uint64_t depth, double work_us) {
        if (!recording) return;
        source_queue_depth_sum += depth;
        source_queue_work_sum_us += work_us;
        ++source_queue_samples;
    }

    double p99()  const { return latency_hist.percentile(0.99); }
    double p999() const { return latency_hist.percentile(0.999); }
    double slo_violation_rate() const {
        return total_finished > 0 ? static_cast<double>(slo_violations) / total_finished : 0.0;
    }
    double short_slo_violation_rate() const {
        return short_finished > 0 ? static_cast<double>(short_slo_violations) / short_finished : 0.0;
    }
    double long_slo_violation_rate() const {
        return long_finished > 0 ? static_cast<double>(long_slo_violations) / long_finished : 0.0;
    }
    double mice_slo_violation_rate() const {
        return mice_finished > 0 ? static_cast<double>(mice_slo_violations) / mice_finished : 0.0;
    }
    double elephant_slo_violation_rate() const {
        return elephant_finished > 0
            ? static_cast<double>(elephant_slo_violations) / elephant_finished : 0.0;
    }
    double migration_rate(uint64_t total_generated) const {
        return total_generated > 0 ? static_cast<double>(total_migrations) / total_generated : 0.0;
    }
    double migration_work_rate(double total_generated_work_us) const {
        return total_generated_work_us > 0.0 ? migrated_work_us / total_generated_work_us : 0.0;
    }
    double invalid_migration_ratio() const {
        return total_migrations > 0 ? static_cast<double>(invalid_migrations) / total_migrations : 0.0;
    }
    double intra_move_rate(uint64_t total_generated) const {
        return total_generated > 0 ? static_cast<double>(intra_move_count) / total_generated : 0.0;
    }
    double invalid_intra_move_ratio() const {
        return proactive_intra_success_count > 0
            ? static_cast<double>(invalid_intra_moves) / proactive_intra_success_count : 0.0;
    }
    double beneficial_migration_ratio() const {
        return rescue_success_count > 0
            ? static_cast<double>(beneficial_migration_count) / rescue_success_count : 0.0;
    }
    double useless_migration_ratio() const {
        if (rescue_success_count == 0) return 0.0;
        uint64_t useless = needless_migration_count
                          + unsaved_migration_count
                          + harmful_actual_count;
        return static_cast<double>(useless) / rescue_success_count;
    }
    double rescue_per_migration() const {
        return beneficial_migration_ratio();
    }
    double relief_beneficial_migration_ratio() const {
        return relief_success_count > 0
            ? static_cast<double>(relief_beneficial_count) / relief_success_count : 0.0;
    }
    double relief_useless_migration_ratio() const {
        return relief_success_count > 0
            ? static_cast<double>(relief_useless_count) / relief_success_count : 0.0;
    }
    double harmful_actual_ratio() const {
        return rescue_success_count > 0
            ? static_cast<double>(harmful_actual_count) / rescue_success_count : 0.0;
    }
    double avg_destination_virtual_occupancy_us() const {
        return destination_virtual_occupancy_samples > 0
            ? destination_virtual_occupancy_sum_us
                / static_cast<double>(destination_virtual_occupancy_samples)
            : 0.0;
    }
    double avg_source_queue_work_us() const {
        return source_queue_samples > 0
            ? source_queue_work_sum_us / static_cast<double>(source_queue_samples)
            : 0.0;
    }
    double avg_source_queue_depth() const {
        return source_queue_samples > 0
            ? static_cast<double>(source_queue_depth_sum)
                / static_cast<double>(source_queue_samples)
            : 0.0;
    }
    double summary_update_cost_est_us() const {
        return static_cast<double>(summary_update_count) * 0.02;
    }
    double batch_estimation_cost_est_us() const {
        return static_cast<double>(batch_candidate_count) * 0.03;
    }
    double target_selection_cost_est_us() const {
        return static_cast<double>(target_plan_reject_count + batch_selected_count) * 0.005;
    }
    std::string exact_batch_size_histogram() const {
        std::ostringstream out;
        bool first = true;
        for (uint64_t i = 0; i < exact_batch_size_hist.size(); ++i) {
            uint64_t count = exact_batch_size_hist[i];
            if (count == 0) continue;
            if (!first) out << ";";
            first = false;
            if (i == exact_batch_size_hist.size() - 1)
                out << ">=" << i << ":" << count;
            else
                out << i << ":" << count;
        }
        return out.str();
    }
};

} // namespace sim
