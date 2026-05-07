#pragma once
#include "sim/metrics/histogram.h"
#include <cstdint>

namespace sim {

struct MetricsCollector {
    Histogram latency_hist;
    uint64_t total_finished    = 0;
    uint64_t slo_violations    = 0;
    uint64_t total_migrations  = 0;
    uint64_t invalid_migrations = 0;
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
    uint64_t warmup_remaining  = 0;
    bool     recording         = false;

    void init(int warmup) {
        warmup_remaining = warmup;
        recording = false;
        latency_hist.reset();
        total_finished = slo_violations = total_migrations = invalid_migrations = 0;
        batch_candidate_count = batch_selected_count = batch_move_count = 0;
        summary_update_count = reservation_reject_count = saturation_guard_count = 0;
        batch_size_1_count = batch_size_2_7_count = 0;
        batch_size_8_31_count = batch_size_32_plus_count = 0;
        batch_type_generic_count = batch_type_short_count = batch_type_mice_count = 0;
        batch_type_slow_count = batch_type_distribution_count = 0;
        target_plan_reject_count = 0;
    }

    void on_task_finish(double latency_us, double slo_us) {
        if (warmup_remaining > 0) { --warmup_remaining; return; }
        if (!recording) recording = true;
        latency_hist.record(latency_us);
        ++total_finished;
        if (latency_us > slo_us) ++slo_violations;
    }

    void on_migration(bool invalid) {
        if (!recording) return;
        ++total_migrations;
        if (invalid) ++invalid_migrations;
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
    }

    void on_saturation_guard() {
        if (!recording) return;
        ++saturation_guard_count;
    }

    void on_target_plan_reject() {
        if (!recording) return;
        ++target_plan_reject_count;
    }

    double p99()  const { return latency_hist.percentile(0.99); }
    double p999() const { return latency_hist.percentile(0.999); }
    double slo_violation_rate() const {
        return total_finished > 0 ? static_cast<double>(slo_violations) / total_finished : 0.0;
    }
    double migration_rate(uint64_t total_generated) const {
        return total_generated > 0 ? static_cast<double>(total_migrations) / total_generated : 0.0;
    }
    double invalid_migration_ratio() const {
        return total_migrations > 0 ? static_cast<double>(invalid_migrations) / total_migrations : 0.0;
    }
};

} // namespace sim
