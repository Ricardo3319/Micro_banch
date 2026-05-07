#pragma once
#include "sim/common/types.h"

namespace sim {

struct Event {
    double    timestamp_us = 0.0;
    EventType type         = EventType::TASK_GENERATE;
    int       host_id      = -1;
    int       core_id      = -1;
    uint64_t  task_id      = 0;

    // For priority queue: smaller timestamp first;
    // at same timestamp, smaller EventType enum value = higher priority.
    bool operator>(const Event& o) const {
        if (timestamp_us != o.timestamp_us)
            return timestamp_us > o.timestamp_us;
        return static_cast<int>(type) > static_cast<int>(o.type);
    }
};

} // namespace sim
