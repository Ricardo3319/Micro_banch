#pragma once
#include "sim/common/types.h"
#include "sim/common/constants.h"
#include <cstdint>

namespace sim {

struct Task {
    TaskId  id            = 0;
    double  generate_time_us = 0.0;
    double  arrive_time_us   = 0.0;
    RpcMethod rpc_method     = RpcMethod::SHORT_RPC;
    double  base_service_time_us = 0.0;
    double  expected_service_time_us = 0.0; // EWMA or known E
    double  deadline_budget_us = 0.0;
    int     initial_core = -1;
    bool    arrival_burst = false;
    bool    measurement_eligible = false;
    bool    migration_in_flight = false;
    int     assigned_host = -1;
    int     assigned_core = -1;
    bool    migrated      = false;
    bool    intra_moved   = false;
    bool    proactive_intra_moved = false;
    bool    proactive_intra_recorded = false;
    bool    rescue_intra_moved = false;
    bool    rescue_intra_recorded = false;
    bool    rescue_relief_moved = false;
    bool    rescue_predicted_harmful = false;
    bool    target_harm_watch_active = false;
    bool    target_harm_watch_recorded = false;
    int     src_host      = -1;
    int     src_core      = -1;
    double  estimated_local_latency_us = 0.0; // predicted latency if NOT migrated
    double  rescue_predicted_remote_latency_us = 0.0;
    int     rescue_predicted_target_delta_risk = 0;
    uint64_t rescue_migration_id = 0;
    uint64_t target_harm_watch_migration_id = 0;
    double  target_harm_counterfactual_latency_us = 0.0;
    uint64_t migration_batch_id = 0;
    int     reserved_dst_host = -1;
    int     reserved_dst_core = -1;
    double  pending_reserved_work_us = 0.0;

    // Intrusive doubly-linked list pointers for O(1) queue removal.
    Task*   prev = nullptr;
    Task*   next = nullptr;

    double deadline_abs_us() const {
        return generate_time_us + deadline_budget_us;
    }
};

} // namespace sim
