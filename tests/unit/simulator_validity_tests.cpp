#include "sim/core/simulator.h"
#include "sim/metrics/histogram.h"
#include "sim/workloads/trace.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

namespace sim {

class SimulatorValidityTest {
public:
    static double estimate(Simulator& simulator, RpcMethod method, double actual_us) {
        return simulator.estimate_service_time(method, actual_us);
    }

    static void update(Simulator& simulator, RpcMethod method, double actual_us) {
        simulator.update_service_estimator(method, actual_us);
    }

    static bool try_move_running_task(Simulator& simulator) {
        while (!simulator.event_queue_.empty()) simulator.event_queue_.pop();
        Task* task = simulator.alloc_task();
        task->expected_service_time_us = 5.0;
        task->base_service_time_us = 5.0;
        task->deadline_budget_us = SLO_SHORT_US;
        task->assigned_host = 0;
        task->assigned_core = 0;
        Core& source = simulator.nodes_[0].cores[0];
        source.idle = false;
        source.running = task;
        source.finish_time_us = simulator.now_us_ + 5.0;
        return simulator.move_rescue_task_intra_host(
            0, 0, 1, task, 50.0, 20.0, 0, false);
    }

    static bool append_tail_and_release_reservation(Simulator& simulator) {
        while (!simulator.event_queue_.empty()) simulator.event_queue_.pop();
        simulator.metrics_.start_recording();
        auto make_task = [&](int core_id) {
            Task* task = simulator.alloc_task();
            task->rpc_method = RpcMethod::SHORT_RPC;
            task->expected_service_time_us = 5.0;
            task->base_service_time_us = 5.0;
            task->deadline_budget_us = SLO_SHORT_US;
            task->assigned_host = 0;
            task->assigned_core = core_id;
            task->measurement_eligible = true;
            return task;
        };
        Task* source_task = make_task(0);
        Task* first = make_task(1);
        Task* second = make_task(1);
        Task* source_running = make_task(0);
        Task* destination_running = make_task(1);

        Core& source = simulator.nodes_[0].cores[0];
        source.idle = false;
        source.running = source_running;
        source.finish_time_us = simulator.now_us_ + 10.0;
        source.push_waiting(source_task);

        Core& destination = simulator.nodes_[0].cores[1];
        destination.idle = false;
        destination.running = destination_running;
        destination.finish_time_us = simulator.now_us_ + 10.0;
        destination.push_waiting(first);
        destination.push_waiting(second);

        bool moved = simulator.move_rescue_task_intra_host(
            0, 0, 1, source_task, 50.0, 20.0, 0, false);
        if (!moved || simulator.incoming_core_reservation_[0][1] <= 0.0)
            return false;
        if (simulator.event_queue_.empty()) return false;
        Event arrival = simulator.event_queue_.top();
        simulator.event_queue_.pop();
        simulator.now_us_ = arrival.timestamp_us;
        simulator.process_event(arrival);

        Task* head = destination.wait_queue.begin();
        bool order_ok = head == first && head->next == second
            && head->next->next == source_task && source_task->next == nullptr;
        bool reservation_released =
            simulator.incoming_core_reservation_[0][1] == 0.0
            && simulator.incoming_reservation_[0] == 0.0
            && source_task->pending_reserved_work_us == 0.0
            && !source_task->migration_in_flight;
        return order_ok && reservation_released;
    }

    static bool all_measurement_tasks_completed_once(const Simulator& simulator) {
        uint64_t completed = 0;
        std::set<uint64_t> ids;
        for (const auto& owned : simulator.task_pool_) {
            const Task& task = *owned;
            if (!ids.insert(task.id).second) return false;
            if (task.measurement_eligible && task.completed) ++completed;
            if (task.completed && !task.execution_started) return false;
        }
        return completed == static_cast<uint64_t>(simulator.measurement_target_);
    }
};

} // namespace sim

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(double actual, double expected, double tolerance,
                  const std::string& message) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + ": actual=" + std::to_string(actual)
                                 + " expected=" + std::to_string(expected));
    }
}

sim::TraceConfig trace_config(sim::WorkloadType workload, unsigned seed,
                              int measurement_requests) {
    sim::TraceConfig config;
    config.workload = workload;
    config.rho = 0.85;
    config.seed = seed;
    config.core_count = sim::CORES_PER_HOST;
    config.effective_core_capacity = sim::CORES_PER_HOST;
    config.warmup_requests = 0;
    config.measurement_requests = measurement_requests;
    return config;
}

double empirical_offered_load(const sim::WorkloadTrace& trace) {
    const auto& entries = trace.entries();
    require(entries.size() > 1, "offered-load trace too small");
    double work_us = 0.0;
    for (const auto& entry : entries) work_us += entry.service_time_us + sim::T_host_us;
    double duration_us = entries.back().generate_time_us - entries.front().generate_time_us;
    return work_us / (duration_us * trace.config().effective_core_capacity);
}

void test_sha256_and_trace_identity() {
    require(sim::sha256_hex("abc") ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            "SHA-256 implementation failed known vector");

    auto config = trace_config(sim::WorkloadType::W3_POISSON_LOGNORMAL, 11, 2000);
    auto first = sim::WorkloadTrace::generate(config);
    auto second = sim::WorkloadTrace::generate(config);
    require(first.sha256() == second.sha256(), "same trace config changed hash");
    require(first.sha256() ==
            "9df8b7b6532ac12a70477b76ca849d8add4bc9849995a625ac1f176c5a3dbe93",
            "legacy request-random trace identity changed");
    config.seed = 12;
    auto different = sim::WorkloadTrace::generate(config);
    require(first.sha256() != different.sha256(), "different seed reused trace hash");
}

void test_flow_affine_trace_identity_and_placement() {
    auto config = trace_config(sim::WorkloadType::W3_POISSON_LOGNORMAL, 11, 5000);
    config.placement_mode = sim::PlacementMode::FLOW_AFFINE;
    config.flow_count = 64;
    config.flow_zipf_alpha = 1.1;
    auto first = sim::WorkloadTrace::generate(config);
    auto second = sim::WorkloadTrace::generate(config);
    require(std::string(first.version()) == sim::RESCUE_FLOW_TRACE_VERSION,
            "flow-affine trace did not use a new version");
    require(first.sha256() == second.sha256(),
            "flow-affine trace is not deterministic");

    std::vector<int> core_by_flow(static_cast<size_t>(config.flow_count + 1), -1);
    for (const auto& entry : first.entries()) {
        require(entry.flow_id > 0
                    && entry.flow_id <= static_cast<uint64_t>(config.flow_count),
                "flow id outside configured range");
        int& prior = core_by_flow[static_cast<size_t>(entry.flow_id)];
        if (prior < 0) prior = entry.initial_core;
        require(prior == entry.initial_core,
                "one flow mapped to multiple cores");
    }
    config.flow_hash_seed += 1;
    auto rehashed = sim::WorkloadTrace::generate(config);
    require(first.sha256() != rehashed.sha256(),
            "flow hash seed did not change trace identity");
}

void test_trace_rejects_non_finite_parameters() {
    auto config = trace_config(sim::WorkloadType::W3_POISSON_LOGNORMAL, 11, 10);
    config.rho = std::numeric_limits<double>::quiet_NaN();
    bool rejected = false;
    try {
        (void)sim::WorkloadTrace::generate(config);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "trace API accepted non-finite rho");

    config = trace_config(sim::WorkloadType::W3_POISSON_LOGNORMAL, 11, 10);
    config.placement_mode = sim::PlacementMode::FLOW_AFFINE;
    config.flow_zipf_alpha = std::numeric_limits<double>::infinity();
    rejected = false;
    try {
        (void)sim::WorkloadTrace::generate(config);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "trace API accepted non-finite Zipf alpha");
}

void test_w3_method_first_distribution() {
    auto config = trace_config(sim::WorkloadType::W3_POISSON_LOGNORMAL, 23, 200000);
    auto trace = sim::WorkloadTrace::generate(config);
    double service_sum = 0.0;
    size_t short_count = 0;
    for (const auto& entry : trace.entries()) {
        service_sum += entry.service_time_us;
        if (entry.rpc_method == sim::RpcMethod::SHORT_RPC) {
            ++short_count;
            require(entry.service_time_us <= sim::SLO_SHORT_SERVICE_THRESHOLD_US,
                    "short RPC escaped conditional distribution");
            require(entry.deadline_budget_us == sim::SLO_SHORT_US,
                    "short RPC deadline mismatch");
        } else {
            require(entry.service_time_us > sim::SLO_SHORT_SERVICE_THRESHOLD_US,
                    "long RPC escaped conditional distribution");
            require(entry.deadline_budget_us == sim::SLO_LONG_US,
                    "long RPC deadline mismatch");
        }
    }
    double mean = service_sum / static_cast<double>(trace.entries().size());
    require_near(mean, sim::W3_MEAN_SERVICE_US, sim::W3_MEAN_SERVICE_US * 0.01,
                 "W3 marginal mean changed");
    double expected_short = 0.5 * std::erfc(
        -(std::log(sim::SLO_SHORT_SERVICE_THRESHOLD_US) - sim::W3_LOGNORMAL_MU)
        / (sim::W3_LOGNORMAL_SIGMA * std::sqrt(2.0)));
    double observed_short = static_cast<double>(short_count) / trace.entries().size();
    require_near(observed_short, expected_short, 0.01,
                 "W3 short-method probability changed");
}

void test_estimator_does_not_read_hidden_service() {
    auto config = trace_config(sim::WorkloadType::W3_POISSON_LOGNORMAL, 37, 100);
    auto trace = std::make_shared<const sim::WorkloadTrace>(
        sim::WorkloadTrace::generate(config));
    sim::M0Config policy;
    policy.service_estimate_mode = sim::SERVICE_ESTIMATE_EWMA;
    sim::Simulator simulator;
    simulator.configure(sim::MethodType::M1_RESCUE_SCHED, config.rho, config.seed,
                        config.workload, sim::ClusterProfile::HOMOGENEOUS,
                        policy, trace);

    double short_a = sim::SimulatorValidityTest::estimate(
        simulator, sim::RpcMethod::SHORT_RPC, 1.0);
    double short_b = sim::SimulatorValidityTest::estimate(
        simulator, sim::RpcMethod::SHORT_RPC, 19.0);
    require_near(short_a, short_b, 0.0,
                 "EWMA estimate leaked current hidden service");
    double long_before = sim::SimulatorValidityTest::estimate(
        simulator, sim::RpcMethod::LONG_RPC, 21.0);
    sim::SimulatorValidityTest::update(simulator, sim::RpcMethod::SHORT_RPC, 15.0);
    double long_after = sim::SimulatorValidityTest::estimate(
        simulator, sim::RpcMethod::LONG_RPC, 500.0);
    require_near(long_before, long_after, 0.0,
                 "short-method completion modified long-method estimator");
}

void test_load_calibration() {
    for (sim::WorkloadType workload : {
             sim::WorkloadType::W1_POISSON_BIMODAL,
             sim::WorkloadType::W3_POISSON_LOGNORMAL}) {
        auto config = trace_config(workload, 47, 300000);
        auto trace = sim::WorkloadTrace::generate(config);
        require_near(empirical_offered_load(trace), config.rho, config.rho * 0.01,
                     "Poisson offered-load calibration failed");
    }
    auto w2_config = trace_config(sim::WorkloadType::W2_MMPP_BIMODAL, 59, 1000000);
    auto w2 = sim::WorkloadTrace::generate(w2_config);
    require_near(empirical_offered_load(w2), w2_config.rho, w2_config.rho * 0.02,
                 "MMPP time-average offered-load calibration failed");
}

void test_exact_percentile_above_ten_ms() {
    sim::Histogram histogram;
    histogram.record(5.0);
    histogram.record(15000.0);
    histogram.record(25000.0);
    require(histogram.percentile(0.99) == 25000.0,
            "exact percentile clipped values above 10 ms");
}

void test_measurement_cohort_and_delayed_migration() {
    sim::TraceConfig config = trace_config(
        sim::WorkloadType::W3_POISSON_LOGNORMAL, 11, 200);
    config.warmup_requests = 20;
    auto trace = std::make_shared<const sim::WorkloadTrace>(
        sim::WorkloadTrace::generate(config));
    sim::M0Config policy;
    policy.rescue_migration_cost_us = 3.0;
    policy.rescue_budget_per_check = 4;
    sim::Simulator simulator;
    simulator.configure(sim::MethodType::M1_RESCUE_SCHED, config.rho, config.seed,
                        config.workload, sim::ClusterProfile::HOMOGENEOUS,
                        policy, trace);
    require(simulator.run() == 0, "RescueSched synthetic run failed");
    const auto& metrics = simulator.metrics();
    require(simulator.total_generated() == 220, "trace cohort generation mismatch");
    require(metrics.total_finished == 200, "measurement cohort did not drain");
    require(metrics.latency_hist.count() == 200, "warmup leaked into latency samples");
    require(metrics.rescue_success_count > 0, "synthetic run produced no migrations");
    require(metrics.migration_handoff_count == metrics.rescue_success_count,
            "committed migration did not reach target");
    require_near(metrics.average_migration_handoff_us(), 3.0, 1e-9,
                 "migration cost was not paid as elapsed time");
    require_near(metrics.migration_handoff_max_us, 3.0, 1e-9,
                 "migration handoff duration varied unexpectedly");
    require(metrics.max_rescue_commits_per_check <= 4,
            "per-check migration budget was exceeded");
    require(metrics.max_target_reservation_work_us > 0.0,
            "target reservation was not recorded");
    require(metrics.rescue_queue_entries_inspected_count
                >= metrics.rescue_candidate_count,
            "queue-entry diagnostics undercounted candidate scans");
    require(metrics.rescue_accepted_candidate_count <= metrics.rescue_candidate_count,
            "accepted candidate count exceeds inspected candidates");
    require(metrics.rescue_target_evaluation_count
                <= metrics.rescue_accepted_candidate_count
                    * static_cast<uint64_t>(policy.rescue_h_targets),
            "target bound was exceeded");
    require(metrics.rescue_short_move_count + metrics.rescue_long_move_count
                == metrics.rescue_success_count,
            "migrated method breakdown mismatch");
    require(metrics.rescue_burst_move_count + metrics.rescue_nonburst_move_count
                == metrics.rescue_success_count,
            "migrated burst breakdown mismatch");
    require(metrics.rescue_migrated_finished + metrics.rescue_nonmigrated_finished
                == metrics.total_finished,
            "migrated outcome cohorts do not cover finished requests");
    require(metrics.estimator_observation_count == metrics.total_finished,
            "estimator diagnostics do not cover measurement cohort");
    require(sim::SimulatorValidityTest::all_measurement_tasks_completed_once(simulator),
            "measurement request was lost or completed more than once");
}

void test_running_request_and_append_tail_invariants() {
    auto config = trace_config(sim::WorkloadType::W3_POISSON_LOGNORMAL, 11, 10);
    auto trace = std::make_shared<const sim::WorkloadTrace>(
        sim::WorkloadTrace::generate(config));

    sim::Simulator running_simulator;
    running_simulator.configure(
        sim::MethodType::M1_RESCUE_SCHED, config.rho, config.seed,
        config.workload, sim::ClusterProfile::HOMOGENEOUS, sim::M0Config{}, trace);
    require(!sim::SimulatorValidityTest::try_move_running_task(running_simulator),
            "running request descriptor was migrated");

    sim::M0Config zero_handoff;
    zero_handoff.rescue_migration_cost_us = 0.0;
    sim::Simulator ordering_simulator;
    ordering_simulator.configure(
        sim::MethodType::M1_RESCUE_SCHED, config.rho, config.seed,
        config.workload, sim::ClusterProfile::HOMOGENEOUS,
        zero_handoff, trace);
    require(sim::SimulatorValidityTest::append_tail_and_release_reservation(
                ordering_simulator),
            "append-tail ordering or reservation release failed");
}

void test_estimator_and_control_cost_accounting() {
    sim::MetricsCollector metrics;
    metrics.init(2);
    metrics.start_recording();
    metrics.on_estimator_observation(
        8.0, 10.0, sim::RpcMethod::SHORT_RPC, 0, true);
    metrics.on_estimator_observation(
        14.0, 10.0, sim::RpcMethod::LONG_RPC, 3, true);
    require_near(metrics.estimator_mean_signed_error_us(), 1.0, 1e-12,
                 "estimator signed error mismatch");
    require_near(metrics.estimator_mae_us(), 3.0, 1e-12,
                 "estimator MAE mismatch");
    require_near(metrics.estimator_rmse_us(), std::sqrt(10.0), 1e-12,
                 "estimator RMSE mismatch");
    require(metrics.estimator_underestimate_count == 1
                && metrics.estimator_overestimate_count == 1,
            "estimator direction counts mismatch");
    require(metrics.estimator_cold_start_count == 1,
            "estimator cold-start count mismatch");

    metrics.on_control_check(0.1);
    metrics.on_control_queue_entry(0.2);
    metrics.on_control_candidate(0.3);
    metrics.on_control_target(0.4);
    metrics.on_control_estimator_update(0.5);
    metrics.on_control_poll(0.6);
    require_near(metrics.configured_control_cost_sum_us(), 2.1, 1e-12,
                 "configured control cost accounting mismatch");
}

void test_strong_baselines_pay_common_handoff() {
    sim::TraceConfig config = trace_config(
        sim::WorkloadType::W3_POISSON_LOGNORMAL, 11, 300);
    config.warmup_requests = 20;
    auto trace = std::make_shared<const sim::WorkloadTrace>(
        sim::WorkloadTrace::generate(config));

    for (sim::MethodType method : {
             sim::MethodType::L1_WORK_STEALING_POLLING,
             sim::MethodType::M0_ALTO_THRESHOLD}) {
        sim::M0Config policy;
        policy.rescue_migration_cost_us = 2.0;
        sim::Simulator simulator;
        simulator.configure(method, config.rho, config.seed, config.workload,
                            sim::ClusterProfile::HOMOGENEOUS, policy, trace);
        require(simulator.run() == 0, "strong baseline run failed");
        const auto& metrics = simulator.metrics();
        require(metrics.total_finished == 300, "strong baseline did not drain");
        require(metrics.descriptor_handoff_count > 0,
                "strong baseline performed no paid descriptor handoff");
        require_near(metrics.average_descriptor_handoff_us(), 2.0, 1e-9,
                     "strong baseline did not pay common handoff cost");
        if (method == sim::MethodType::L1_WORK_STEALING_POLLING) {
            require(metrics.steal_poll_count > 0, "polling work stealing never polled");
            require(metrics.descriptor_handoff_count == metrics.steal_success_count,
                    "work-steal handoff accounting mismatch");
        } else {
            require(metrics.descriptor_handoff_count
                        == metrics.proactive_intra_success_count,
                    "ALTO-style handoff accounting mismatch");
        }
    }
}

} // namespace

int main() {
    try {
        test_sha256_and_trace_identity();
        test_flow_affine_trace_identity_and_placement();
        test_trace_rejects_non_finite_parameters();
        test_w3_method_first_distribution();
        test_estimator_does_not_read_hidden_service();
        test_load_calibration();
        test_exact_percentile_above_ten_ms();
        test_measurement_cohort_and_delayed_migration();
        test_running_request_and_append_tail_invariants();
        test_estimator_and_control_cost_accounting();
        test_strong_baselines_pay_common_handoff();
        std::cout << "simulator validity tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "simulator validity tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
