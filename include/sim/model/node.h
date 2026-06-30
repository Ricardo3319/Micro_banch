#pragma once
#include "sim/model/core.h"
#include "sim/common/constants.h"
#include <vector>

namespace sim {

struct Node {
    int    node_id = 0;
    std::vector<Core> cores;

    // Stale global view of queue lengths (updated by SYNC_LOAD).
    int stale_total_queue_len = 0;

    void init(int id, int num_cores, double capacity = 1.0) {
        node_id = id;
        cores.clear();
        cores.resize(num_cores);
        for (int i = 0; i < num_cores; ++i) {
            cores[i].core_id  = i;
            cores[i].host_id  = id;
            cores[i].capacity = capacity;
        }
    }

    // Real-time local total queue length (allowed to read for own node).
    int local_total_queue_len() const {
        int s = 0;
        for (auto& c : cores) {
            s += static_cast<int>(c.wait_queue.size());
            if (!c.idle) ++s;
        }
        return s;
    }

    double local_total_workload_us(double now_us) const {
        double s = 0.0;
        for (auto& c : cores) s += c.local_workload_us(now_us);
        return s;
    }

    // Find a local core with shortest wait queue.
    int shortest_queue_core() const {
        int best = 0;
        size_t best_len = cores[0].wait_queue.size();
        for (int i = 1; i < static_cast<int>(cores.size()); ++i) {
            size_t l = cores[i].wait_queue.size();
            if (cores[i].idle && !cores[best].idle) { best = i; best_len = l; continue; }
            if (!cores[i].idle && cores[best].idle) continue;
            if (l < best_len) { best = i; best_len = l; }
        }
        return best;
    }
};

} // namespace sim
