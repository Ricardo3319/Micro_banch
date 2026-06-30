#pragma once
#include "sim/algorithms/scheduler.h"
#include "sim/common/constants.h"
#include <random>
#include <unordered_map>

namespace sim {

// B2: Reactive migration — dispatch with Power-of-2, plus threshold-triggered migration.
class ReactiveMigrationScheduler : public IScheduler {
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

    MethodType method() const override { return MethodType::B2_REACTIVE_MIGRATION; }

    struct MigrationDecision {
        int   dst_host = -1;
        Task* task     = nullptr;
    };

    MigrationDecision check_migration(Node& src,
                                       const std::vector<int>& stale_view,
                                       int q_hi, int q_lo,
                                       double now_us,
                                       std::mt19937_64& rng) {
        MigrationDecision dec;
        // Per-host cooldown.
        double& last = last_migrate_per_host_[src.node_id];
        if (now_us < last + B2_T_COOLDOWN_US) return dec;
        int src_qlen = src.local_total_queue_len();
        if (src_qlen < q_hi) return dec;

        int n = static_cast<int>(stale_view.size());
        std::uniform_int_distribution<int> dist(0, n - 1);
        int best_dst = -1;
        int best_q = src_qlen;
        for (int i = 0; i < B2_K_DST; ++i) {
            int c = dist(rng);
            if (c == src.node_id) continue;
            if (stale_view[c] < best_q && stale_view[c] <= q_lo) {
                best_dst = c; best_q = stale_view[c];
            }
        }
        if (best_dst < 0) return dec;

        int worst_core = -1;
        size_t worst_len = 0;
        for (auto& c : src.cores) {
            if (c.wait_queue.size() > worst_len) {
                worst_len = c.wait_queue.size();
                worst_core = c.core_id;
            }
        }
        if (worst_core < 0 || worst_len == 0) return dec;

        Task* cur = src.cores[worst_core].wait_queue.begin();
        Task* tail = nullptr;
        while (cur) { tail = cur; cur = cur->next; }
        if (!tail) return dec;

        // Estimate local latency if not migrated: elapsed + remaining wait + execution.
        double cumulative_wait_us = 0.0;
        if (!src.cores[worst_core].idle && src.cores[worst_core].running) {
            double r = src.cores[worst_core].finish_time_us - now_us;
            if (r > 0.0) cumulative_wait_us = r;
        }
        Task* w = src.cores[worst_core].wait_queue.begin();
        while (w) {
            double ex = w->base_service_time_us / src.cores[worst_core].capacity + T_host_us;
            cumulative_wait_us += ex;
            w = w->next;
        }
        double t_elapsed = now_us - tail->generate_time_us;
        tail->estimated_local_latency_us = t_elapsed + cumulative_wait_us;

        src.cores[worst_core].remove_waiting(tail);
        dec.dst_host = best_dst;
        dec.task = tail;
        last = now_us;
        return dec;
    }

    void reset() { last_migrate_per_host_.clear(); }

private:
    std::unordered_map<int, double> last_migrate_per_host_;
};

} // namespace sim
