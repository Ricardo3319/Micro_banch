#pragma once

#include "physical/trace.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace physical {

enum class DescriptorState {
    PENDING,
    QUEUED,
    IN_FLIGHT,
    RUNNING,
    DONE,
    CANCELLED
};

enum class PolicyKind {
    L0_RANDOM_CORE,
    L1_WORK_STEALING_POLLING,
    M0_ALTO_THRESHOLD,
    M1_RESCUE_SCHED
};

enum class ArrivalMode {
    TRACE_REPLAY,
    NETWORK_INGRESS
};

enum class NetworkSubmitStatus {
    ACCEPTED,
    UNKNOWN_REQUEST,
    FLOW_MISMATCH,
    DUPLICATE_OR_TERMINAL
};

const char* descriptor_state_name(DescriptorState state);
const char* policy_name(PolicyKind policy);
PolicyKind parse_policy(const std::string& value);

struct DescriptorView {
    uint64_t id = 0;
    sim::RpcMethod method = sim::RpcMethod::SHORT_RPC;
    double planned_arrival_us = 0.0;
    double deadline_abs_us = 0.0;
    double estimated_service_us = 0.0;
    uint64_t estimator_prior_samples = 0;
    int initial_core = -1;
    int current_core = -1;
    DescriptorState state = DescriptorState::PENDING;
    uint32_t migration_count = 0;
};

struct RuntimeConfig {
    PolicyKind policy = PolicyKind::M1_RESCUE_SCHED;
    ArrivalMode arrival_mode = ArrivalMode::TRACE_REPLAY;
    int worker_count = 16;
    std::vector<int> cpu_ids;
    bool strict_affinity = true;
    int warmup_requests = 0;
    double time_scale = 1.0;
    double check_period_us = 100.0;
    int scan_depth = 64;
    int max_candidates = 16;
    int target_count = 4;
    int moves_per_check = 1;
    double epsilon_us = 2.0;
    double handoff_estimate_us = 0.5;
    double host_overhead_us = 2.1;
    double alto_queue_threshold_us = 40.0;
    double alto_min_gain_us = 0.0;
    double ewma_alpha = 0.05;
    double initial_short_service_us = 10.0;
    double initial_long_service_us = 42.0;
    std::string workload_label = "UNSPECIFIED";
    std::string rho_label = "UNSPECIFIED";
    std::string seed_label = "UNSPECIFIED";
    int repetition = 0;
    std::string output_dir;
};

struct RequestOutcome {
    uint64_t id = 0;
    DescriptorState state = DescriptorState::PENDING;
    int initial_core = -1;
    int final_core = -1;
    uint32_t migration_count = 0;
    uint32_t execution_count = 0;
    uint32_t completion_count = 0;
    double planned_arrival_us = 0.0;
    double trace_arrival_us = 0.0;
    double enqueue_us = 0.0;
    double start_us = 0.0;
    double finish_us = 0.0;
    double deadline_abs_us = 0.0;
    double server_completion_us = 0.0;
    double synthetic_service_us = 0.0;
    double estimated_service_us = 0.0;
    uint64_t estimator_prior_samples = 0;
    bool measurement_eligible = false;
    bool deadline_violation = false;
    bool cancel_requested_after_start = false;
};

struct DecisionRecord {
    uint64_t check_id = 0;
    double timestamp_us = 0.0;
    uint64_t request_id = 0;
    int source_core = -1;
    int target_core = -1;
    int scanned_entries = 0;
    int evaluated_targets = 0;
    double predicted_local_completion_us = 0.0;
    double predicted_remote_completion_us = 0.0;
    double deadline_abs_us = 0.0;
    std::string reason;
    uint64_t decision_cycles = 0;
    uint64_t decision_duration_ns = 0;
};

struct MigrationRecord {
    uint64_t request_id = 0;
    int source_core = -1;
    int target_core = -1;
    double start_us = 0.0;
    double end_us = 0.0;
    uint64_t handoff_duration_ns = 0;
    std::string outcome;
};

struct RuntimeSummary {
    uint64_t total_requests = 0;
    uint64_t measurement_requests = 0;
    uint64_t completed_requests = 0;
    uint64_t cancelled_requests = 0;
    uint64_t deadline_violations = 0;
    uint64_t migrated_requests = 0;
    uint64_t migration_count = 0;
    uint64_t duplicate_execution_count = 0;
    uint64_t duplicate_completion_count = 0;
    uint64_t lost_descriptor_count = 0;
    uint64_t nonzero_reservation_count = 0;
    uint64_t affinity_failure_count = 0;
    double deadline_violation_rate = 0.0;
    double goodput_rps = 0.0;
    double p50_server_completion_us = 0.0;
    double p99_server_completion_us = 0.0;
    double p999_server_completion_us = 0.0;
    double max_submit_lag_us = 0.0;
    double mean_submit_lag_us = 0.0;
    bool invariants_pass = false;
};

struct RuntimeResult {
    RuntimeSummary summary;
    std::vector<RequestOutcome> requests;
    std::vector<DecisionRecord> decisions;
    std::vector<MigrationRecord> migrations;
    std::vector<int> worker_cpu_ids;
    std::vector<bool> worker_affinity_ok;
};

class MethodEwmaEstimator {
public:
    MethodEwmaEstimator(double alpha, double initial_short_us,
                        double initial_long_us);
    std::pair<double, uint64_t> snapshot(sim::RpcMethod method) const;
    void observe(sim::RpcMethod method, double completed_service_us);

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

class PhysicalRuntime {
public:
    using CompletionCallback = std::function<void(const RequestOutcome&)>;

    PhysicalRuntime(FrozenTrace trace, RuntimeConfig config);
    ~PhysicalRuntime();
    PhysicalRuntime(const PhysicalRuntime&) = delete;
    PhysicalRuntime& operator=(const PhysicalRuntime&) = delete;

    RuntimeResult run();
    void start_network_ingress();
    NetworkSubmitStatus submit_network_request(
        uint64_t request_id, uint64_t flow_id, int ingress_core);
    RuntimeResult finish_network_ingress(bool cancel_unreceived = true);
    bool request_cancel(uint64_t request_id);
    DescriptorState request_state(uint64_t request_id) const;
    void set_completion_callback(CompletionCallback callback);
    void write_outputs(const RuntimeResult& result) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace physical
