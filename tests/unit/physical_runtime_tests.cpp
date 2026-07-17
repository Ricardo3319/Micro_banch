#include "physical/runtime.h"
#include "physical/trace.h"
#include "sim/workloads/trace.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

fs::path temp_path(const std::string& name) {
    return fs::temp_directory_path()
        / ("rescuesched-physical-test-" + name + "-"
           + std::to_string(std::chrono::steady_clock::now()
               .time_since_epoch().count()));
}

std::string embedded_hash() {
    return std::string(64, 'a');
}

void write_v2_trace(const fs::path& path, const std::string& rows) {
    std::ofstream csv(path);
    csv << "trace_version,trace_sha256,id,generate_time_us,rpc_method,"
           "service_time_us,deadline_budget_us,initial_core,burst\n"
        << rows;
}

void write_v3_trace(const fs::path& path, const std::string& rows) {
    std::ofstream csv(path);
    csv << "trace_version,trace_sha256,placement_mode,id,flow_id,generate_time_us,"
           "rpc_method,service_time_us,deadline_budget_us,initial_core,burst\n"
        << rows;
}

void test_strict_trace_loader() {
    const fs::path valid = temp_path("trace-valid.csv");
    write_v2_trace(valid,
        "rescuesched-trace-v2," + embedded_hash() + ",1,0,short,5,40,0,0\n"
        "rescuesched-trace-v2," + embedded_hash() + ",2,10,long,100,200,1,0\n");
    physical::FrozenTrace trace = physical::FrozenTrace::load_csv(valid.string(), 2);
    require(trace.entries().size() == 2, "valid trace row count mismatch");
    require(trace.embedded_sha256() == embedded_hash(), "embedded hash mismatch");
    require(trace.input_file_sha256().size() == 64, "input file hash missing");

    const fs::path duplicate = temp_path("trace-duplicate.csv");
    write_v2_trace(duplicate,
        "rescuesched-trace-v2," + embedded_hash() + ",1,0,short,5,40,0,0\n"
        "rescuesched-trace-v2," + embedded_hash() + ",1,10,long,100,200,1,0\n");
    bool rejected = false;
    try {
        (void)physical::FrozenTrace::load_csv(duplicate.string(), 2);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "duplicate trace ID was accepted");

    fs::remove(valid);
    fs::remove(duplicate);
}

void test_method_ewma_has_no_current_request_input() {
    physical::MethodEwmaEstimator estimator(0.5, 10.0, 42.0);
    auto initial = estimator.snapshot(sim::RpcMethod::SHORT_RPC);
    require(initial.first == 10.0 && initial.second == 0,
            "initial EWMA snapshot mismatch");
    estimator.observe(sim::RpcMethod::SHORT_RPC, 30.0);
    auto updated = estimator.snapshot(sim::RpcMethod::SHORT_RPC);
    require(updated.first == 20.0 && updated.second == 1,
            "completion EWMA update mismatch");
    auto long_value = estimator.snapshot(sim::RpcMethod::LONG_RPC);
    require(long_value.first == 42.0 && long_value.second == 0,
            "method-key isolation failed");
}

physical::RuntimeResult run_trace(const fs::path& trace_path,
                                  physical::PolicyKind policy,
                                  int warmup = 0) {
    physical::RuntimeConfig config;
    config.policy = policy;
    config.worker_count = 2;
    config.strict_affinity = false;
    config.warmup_requests = warmup;
    config.time_scale = 100.0;
    config.check_period_us = 1.0;
    config.scan_depth = 16;
    config.max_candidates = 8;
    config.target_count = 2;
    config.moves_per_check = 1;
    config.epsilon_us = 0.0;
    config.handoff_estimate_us = 0.0;
    config.host_overhead_us = 0.0;
    physical::FrozenTrace trace = physical::FrozenTrace::load_csv(
        trace_path.string(), config.worker_count);
    physical::PhysicalRuntime runtime(std::move(trace), config);
    return runtime.run();
}

void test_all_policies_share_runtime_and_drain() {
    const fs::path trace_path = temp_path("all-policies.csv");
    write_v2_trace(trace_path,
        "rescuesched-trace-v2," + embedded_hash() + ",1,0,short,100,500,0,0\n"
        "rescuesched-trace-v2," + embedded_hash() + ",2,1,short,5,80,0,0\n"
        "rescuesched-trace-v2," + embedded_hash() + ",3,2,short,5,80,0,0\n"
        "rescuesched-trace-v2," + embedded_hash() + ",4,3,short,5,80,0,0\n");

    const physical::PolicyKind policies[] = {
        physical::PolicyKind::L0_RANDOM_CORE,
        physical::PolicyKind::L1_WORK_STEALING_POLLING,
        physical::PolicyKind::M0_ALTO_THRESHOLD,
        physical::PolicyKind::M1_RESCUE_SCHED
    };
    for (physical::PolicyKind policy : policies) {
        physical::RuntimeResult result = run_trace(trace_path, policy);
        require(result.summary.invariants_pass,
                std::string("runtime invariants failed for ")
                + physical::policy_name(policy));
        require(result.summary.completed_requests == 4,
                "runtime did not drain all requests");
        require(result.summary.duplicate_execution_count == 0,
                "request executed more than once");
        require(result.summary.duplicate_completion_count == 0,
                "request completed more than once");
    }
    fs::remove(trace_path);
}

void test_rescue_migrates_only_queued_and_appends_tail() {
    const fs::path trace_path = temp_path("rescue-queued-only.csv");
    write_v2_trace(trace_path,
        "rescuesched-trace-v2," + embedded_hash() + ",1,0,long,200,500,0,0\n"
        "rescuesched-trace-v2," + embedded_hash() + ",2,1,long,100,500,0,0\n"
        "rescuesched-trace-v2," + embedded_hash() + ",3,2,long,100,500,0,0\n"
        "rescuesched-trace-v2," + embedded_hash() + ",4,3,short,5,80,0,0\n"
        "rescuesched-trace-v2," + embedded_hash() + ",5,4,short,5,200,1,0\n");
    physical::RuntimeResult result = run_trace(
        trace_path, physical::PolicyKind::M1_RESCUE_SCHED);
    require(result.summary.invariants_pass, "RescueSched runtime invariants failed");
    require(result.summary.migration_count >= 1,
            "RescueSched test did not exercise handoff");
    bool migrated_candidate = false;
    for (const auto& migration : result.migrations) {
        require(migration.request_id != 1,
                "running descriptor was migrated");
        require(migration.outcome == "committed_append_tail",
                "migration did not use append-tail commit");
        if (migration.request_id == 4) migrated_candidate = true;
    }
    require(migrated_candidate,
            "deterministic queued doomed request was not migrated");
    fs::remove(trace_path);
}

void test_running_cancel_is_non_preemptive() {
    const fs::path trace_path = temp_path("running-cancel.csv");
    write_v2_trace(trace_path,
        "rescuesched-trace-v2," + embedded_hash() + ",1,0,short,5000,10000,0,0\n");
    physical::RuntimeConfig config;
    config.policy = physical::PolicyKind::L0_RANDOM_CORE;
    config.worker_count = 1;
    config.strict_affinity = false;
    config.time_scale = 20.0;
    config.host_overhead_us = 0.0;
    physical::FrozenTrace trace = physical::FrozenTrace::load_csv(
        trace_path.string(), config.worker_count);
    physical::PhysicalRuntime runtime(std::move(trace), config);
    std::atomic<bool> callback_seen{false};
    runtime.set_completion_callback([&](const physical::RequestOutcome&) {
        callback_seen.store(true, std::memory_order_release);
    });
    physical::RuntimeResult result;
    std::thread runner([&] { result = runtime.run(); });
    const auto running_deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(2);
    while (runtime.request_state(1) != physical::DescriptorState::RUNNING
           && std::chrono::steady_clock::now() < running_deadline) {
        std::this_thread::yield();
    }
    require(runtime.request_state(1) == physical::DescriptorState::RUNNING,
            "request did not enter RUNNING before cancellation deadline");
    require(runtime.request_cancel(1), "running cancel request was rejected");
    runner.join();
    require(result.requests.front().state == physical::DescriptorState::DONE,
            "running request was preemptively cancelled");
    require(result.requests.front().cancel_requested_after_start,
            "running cancel was not recorded");
    require(callback_seen.load(std::memory_order_acquire),
            "completion callback did not fire");
    fs::remove(trace_path);
}

void test_terminal_cancel_callback_fires_once() {
    const fs::path trace_path = temp_path("terminal-cancel.csv");
    write_v2_trace(trace_path,
        "rescuesched-trace-v2," + embedded_hash() + ",1,0,short,5,100,0,0\n");
    physical::RuntimeConfig config;
    config.policy = physical::PolicyKind::L0_RANDOM_CORE;
    config.worker_count = 1;
    config.strict_affinity = false;
    config.time_scale = 20.0;
    config.host_overhead_us = 0.0;
    physical::FrozenTrace trace = physical::FrozenTrace::load_csv(
        trace_path.string(), config.worker_count);
    physical::PhysicalRuntime runtime(std::move(trace), config);
    std::atomic<int> callback_count{0};
    physical::RequestOutcome callback_outcome;
    runtime.set_completion_callback([&](const physical::RequestOutcome& outcome) {
        callback_outcome = outcome;
        callback_count.fetch_add(1, std::memory_order_acq_rel);
    });

    require(runtime.request_cancel(1), "pending cancel request was rejected");
    require(!runtime.request_cancel(1), "terminal request accepted a second cancel");
    physical::RuntimeResult result = runtime.run();
    require(callback_count.load(std::memory_order_acquire) == 1,
            "terminal cancel callback did not fire exactly once");
    require(callback_outcome.state == physical::DescriptorState::CANCELLED,
            "terminal cancel callback received the wrong outcome");
    require(result.requests.front().state == physical::DescriptorState::CANCELLED,
            "cancelled request did not remain terminal");
    require(result.summary.invariants_pass,
            "terminal cancel violated runtime invariants");
    fs::remove(trace_path);
}

void test_network_ingress_identity_and_lifecycle() {
    const fs::path trace_path = temp_path("network-ingress.csv");
    write_v3_trace(trace_path,
        "rescuesched-trace-v3," + embedded_hash()
            + ",flow_affine,1,101,0,short,5,100,0,0\n"
        "rescuesched-trace-v3," + embedded_hash()
            + ",flow_affine,2,202,10,long,10,200,1,0\n");
    physical::RuntimeConfig config;
    config.arrival_mode = physical::ArrivalMode::NETWORK_INGRESS;
    config.policy = physical::PolicyKind::L0_RANDOM_CORE;
    config.worker_count = 2;
    config.strict_affinity = false;
    config.host_overhead_us = 0.0;
    physical::FrozenTrace trace = physical::FrozenTrace::load_csv(
        trace_path.string(), config.worker_count);
    physical::PhysicalRuntime runtime(std::move(trace), config);

    runtime.start_network_ingress();
    require(runtime.submit_network_request(99, 101, 0)
                == physical::NetworkSubmitStatus::UNKNOWN_REQUEST,
            "unknown network request was accepted");
    require(runtime.submit_network_request(1, 999, 0)
                == physical::NetworkSubmitStatus::FLOW_MISMATCH,
            "network request with the wrong flow was accepted");
    require(runtime.submit_network_request(1, 101, 1)
                == physical::NetworkSubmitStatus::ACCEPTED,
            "valid network request was rejected");
    require(runtime.submit_network_request(1, 101, 1)
                == physical::NetworkSubmitStatus::DUPLICATE_OR_TERMINAL,
            "duplicate network request was accepted");

    const physical::RuntimeResult result = runtime.finish_network_ingress(true);
    require(result.summary.invariants_pass, "network ingress invariants failed");
    require(result.summary.completed_requests == 1,
            "accepted network request did not complete");
    require(result.summary.cancelled_requests == 1,
            "unreceived network request was not cancelled");
    const auto& first = result.requests.front();
    require(first.initial_core == 1 && first.final_core == 1,
            "actual ingress shard was not preserved as initial placement");
    require(first.trace_arrival_us == 0.0 && first.planned_arrival_us >= 0.0,
            "trace and physical arrival timestamps were not separated");
    fs::remove(trace_path);
}

} // namespace

int main() {
    try {
        test_strict_trace_loader();
        test_method_ewma_has_no_current_request_input();
        test_all_policies_share_runtime_and_drain();
        test_rescue_migrates_only_queued_and_appends_tail();
        test_running_cancel_is_non_preemptive();
        test_terminal_cancel_callback_fires_once();
        test_network_ingress_identity_and_lifecycle();
        std::cout << "physical runtime tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "physical runtime tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
