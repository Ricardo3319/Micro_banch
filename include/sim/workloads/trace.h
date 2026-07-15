#pragma once

#include "sim/common/constants.h"
#include "sim/common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace sim {

inline constexpr const char* RESCUE_TRACE_VERSION = "rescuesched-trace-v2";
inline constexpr const char* RESCUE_FLOW_TRACE_VERSION = "rescuesched-trace-v3";

struct TraceConfig {
    WorkloadType workload = WorkloadType::W3_POISSON_LOGNORMAL;
    double rho = 0.85;
    unsigned seed = 11;
    int core_count = CORES_PER_HOST;
    double effective_core_capacity = CORES_PER_HOST;
    int warmup_requests = WARMUP_REQUESTS;
    int measurement_requests = MEASUREMENT_REQUESTS;
    int w2_hot_core_count = 4;
    double w2_hot_dispatch_prob = 0.5;
    PlacementMode placement_mode = PlacementMode::REQUEST_RANDOM;
    int flow_count = 4096;
    double flow_zipf_alpha = 0.0;
    unsigned flow_hash_seed = 0x52535331U;
};

struct WorkloadTraceEntry {
    uint64_t id = 0;
    double generate_time_us = 0.0;
    RpcMethod rpc_method = RpcMethod::SHORT_RPC;
    double service_time_us = 0.0;
    double deadline_budget_us = 0.0;
    int initial_core = -1;
    uint64_t flow_id = 0;
    bool burst = false;
};

class WorkloadTrace {
public:
    static WorkloadTrace generate(const TraceConfig& config);

    const TraceConfig& config() const { return config_; }
    const std::vector<WorkloadTraceEntry>& entries() const { return entries_; }
    const std::string& sha256() const { return sha256_; }
    const char* version() const { return version_.c_str(); }
    double measured_actual_work_us() const { return measured_actual_work_us_; }
    bool write_csv(const std::string& path) const;

private:
    TraceConfig config_;
    std::vector<WorkloadTraceEntry> entries_;
    std::string version_ = RESCUE_TRACE_VERSION;
    std::string sha256_;
    double measured_actual_work_us_ = 0.0;
};

const char* rpc_method_name(RpcMethod method);
const char* placement_mode_name(PlacementMode mode);
double rpc_deadline_budget_us(RpcMethod method);
std::string sha256_hex(const std::string& bytes);

} // namespace sim
