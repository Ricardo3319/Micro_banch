#pragma once
#include "sim/common/types.h"
#include "sim/common/constants.h"
#include <cstdint>

namespace sim {

struct Task {
    TaskId  id            = 0;
    double  generate_time_us = 0.0;
    double  arrive_time_us   = 0.0;
    double  base_service_time_us = 0.0;
    double  expected_service_time_us = 0.0; // EWMA or known E
    double  slo_target_us = 0.0;
    int     assigned_host = -1;
    int     assigned_core = -1;
    bool    migrated      = false;
    bool    intra_moved   = false;
    bool    proactive_intra_moved = false;
    bool    proactive_intra_recorded = false;
    bool    rescue_intra_moved = false;
    bool    rescue_intra_recorded = false;
    bool    rescue_predicted_harmful = false;
    int     src_host      = -1;
    int     src_core      = -1;
    double  estimated_local_latency_us = 0.0; // predicted latency if NOT migrated
    double  rescue_predicted_remote_latency_us = 0.0;
    int     rescue_predicted_target_delta_risk = 0;
    uint64_t migration_batch_id = 0;
    int     reserved_dst_host = -1;
    int     reserved_dst_core = -1;
    double  pending_reserved_work_us = 0.0;

    // Intrusive doubly-linked list pointers for O(1) queue removal.
    Task*   prev = nullptr;
    Task*   next = nullptr;

    double slo_for_service() const {
        return (base_service_time_us <= SLO_SHORT_SERVICE_THRESHOLD_US)
            ? SLO_SHORT_US : SLO_LONG_US;
    }
};

} // namespace sim
