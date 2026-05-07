#pragma once
#include "sim/algorithms/scheduler.h"
#include <random>
#include <algorithm>

namespace sim {

// B1: Power-of-k choices (k=2 default), dispatch only, no migration.
class PowerOfKScheduler : public IScheduler {
public:
    explicit PowerOfKScheduler(int k = 2) : k_(k) {}

    int on_task_dispatch(const std::vector<Node>& /*nodes*/,
                         const std::vector<int>& stale_view,
                         double /*service_est_us*/,
                         std::mt19937_64& rng) override {
        int n = static_cast<int>(stale_view.size());
        std::uniform_int_distribution<int> dist(0, n - 1);
        int best = dist(rng);
        for (int i = 1; i < k_; ++i) {
            int c = dist(rng);
            if (stale_view[c] < stale_view[best]) best = c;
        }
        return best;
    }

    MethodType method() const override { return MethodType::B1_POWER_OF_K; }

private:
    int k_;
};

} // namespace sim
