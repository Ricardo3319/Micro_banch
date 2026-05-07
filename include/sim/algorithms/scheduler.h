#pragma once
#include "sim/model/node.h"
#include "sim/common/types.h"
#include <vector>

namespace sim {

// Interface for host-level scheduling algorithms.
struct IScheduler {
    virtual ~IScheduler() = default;
    // Choose target host for a newly generated task. Returns host_id.
    virtual int on_task_dispatch(const std::vector<Node>& nodes,
                                 const std::vector<int>& stale_view,
                                 double service_est_us,
                                 std::mt19937_64& rng) = 0;
    virtual MethodType method() const = 0;
};

} // namespace sim
