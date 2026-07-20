#include "physical/runtime.h"
#include "physical/trace.h"
#include "sim/workloads/trace.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
namespace fs = std::filesystem;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

fs::path temp_path(const std::string& name) {
    return fs::temp_directory_path()
        / ("rescuesched-wp2-test-" + name + "-"
           + std::to_string(std::chrono::steady_clock::now()
               .time_since_epoch().count()));
}

std::string embedded_hash() { return std::string(64, 'b'); }

void write_v2(const fs::path& path, const std::string& rows) {
    std::ofstream csv(path);
    csv << "trace_version,trace_sha256,id,generate_time_us,rpc_method,"
           "service_time_us,deadline_budget_us,initial_core,burst\n" << rows;
}

void write_v3(const fs::path& path, const std::string& rows) {
    std::ofstream csv(path);
    csv << "trace_version,trace_sha256,placement_mode,id,flow_id,generate_time_us,"
           "rpc_method,service_time_us,deadline_budget_us,initial_core,burst\n" << rows;
}

physical::RuntimeConfig base_config(physical::PolicyKind policy, int workers = 2) {
    physical::RuntimeConfig config;
    config.policy = policy;
    config.worker_count = workers;
    config.strict_affinity = false;
    config.host_overhead_us = 0.0;
    config.check_period_us = 20.0;
    config.scan_depth = 32;
    config.max_candidates = 16;
    config.target_count = workers;
    config.moves_per_check = 1;
    config.epsilon_us = 0.0;
    config.handoff_estimate_us = 0.0;
    return config;
}

void test_receiver_lifecycle_race_100_each_policy() {
    const fs::path trace_path = temp_path("race.csv");
    write_v3(trace_path,
        "rescuesched-trace-v3," + embedded_hash()
            + ",flow_affine,1,101,0,short,5,1000,0,0\n"
        "rescuesched-trace-v3," + embedded_hash()
            + ",flow_affine,2,202,0,short,5,1000,1,0\n");
    const physical::PolicyKind policies[] = {
        physical::PolicyKind::L0_RANDOM_CORE,
        physical::PolicyKind::L1_WORK_STEALING_POLLING,
        physical::PolicyKind::M0_ALTO_THRESHOLD,
        physical::PolicyKind::M1_RESCUE_SCHED
    };
    for (const auto policy : policies) {
        for (int repetition = 0; repetition < 100; ++repetition) {
            auto config = base_config(policy);
            config.arrival_mode = physical::ArrivalMode::NETWORK_INGRESS;
            physical::FrozenTrace trace = physical::FrozenTrace::load_csv(
                trace_path.string(), config.worker_count);
            physical::PhysicalRuntime runtime(std::move(trace), config);
            runtime.start_network_ingress();
            std::atomic<int> accepted{0};
            std::thread first([&] {
                if (runtime.submit_network_request(1, 101, 0)
                        == physical::NetworkSubmitStatus::ACCEPTED)
                    accepted.fetch_add(1, std::memory_order_relaxed);
            });
            std::thread second([&] {
                if (runtime.submit_network_request(2, 202, 1)
                        == physical::NetworkSubmitStatus::ACCEPTED)
                    accepted.fetch_add(1, std::memory_order_relaxed);
            });
            first.join();
            second.join();
            const auto result = runtime.finish_network_ingress(true);
            require(accepted.load() == 2, "concurrent ingress submit was lost");
            require(result.summary.invariants_pass
                    && result.summary.completed_requests == 2,
                std::string("concurrent lifecycle failed policy=")
                + physical::policy_name(policy) + " repetition="
                + std::to_string(repetition));
        }
    }
    fs::remove(trace_path);
}

void test_measured_cpu_service_updates_only_after_completion() {
    const fs::path trace_path = temp_path("cpu-ewma.csv");
    write_v3(trace_path,
        "rescuesched-trace-v3," + embedded_hash()
            + ",flow_affine,1,101,0,short,24,1000,0,0\n"
        "rescuesched-trace-v3," + embedded_hash()
            + ",flow_affine,2,202,0,short,5,1000,0,0\n");
    auto config = base_config(physical::PolicyKind::L0_RANDOM_CORE, 1);
    config.arrival_mode = physical::ArrivalMode::NETWORK_INGRESS;
    config.initial_short_service_us = 10.0;
    config.ewma_alpha = 1.0;
    physical::FrozenTrace trace = physical::FrozenTrace::load_csv(
        trace_path.string(), config.worker_count);
    physical::PhysicalRuntime runtime(std::move(trace), config);

    std::mutex completion_mutex;
    std::condition_variable completion_cv;
    bool first_completed = false;
    runtime.set_completion_callback([&](const physical::RequestOutcome& outcome) {
        if (outcome.id != 1 || outcome.state != physical::DescriptorState::DONE) return;
        {
            std::lock_guard<std::mutex> lock(completion_mutex);
            first_completed = true;
        }
        completion_cv.notify_one();
    });

    runtime.start_network_ingress();
    require(runtime.submit_network_request(1, 101, 0)
                == physical::NetworkSubmitStatus::ACCEPTED,
            "first EWMA request was not accepted");
    {
        std::unique_lock<std::mutex> lock(completion_mutex);
        require(completion_cv.wait_for(lock, std::chrono::seconds(5), [&] {
                    return first_completed;
                }),
                "first EWMA request did not complete before timeout");
    }
    require(runtime.submit_network_request(2, 202, 0)
                == physical::NetworkSubmitStatus::ACCEPTED,
            "second EWMA request was not accepted");
    const auto result = runtime.finish_network_ingress(false);

    require(result.summary.invariants_pass, "CPU service runtime invariants failed");
    const auto& first = result.requests.at(0);
    const auto& second = result.requests.at(1);
    require(first.estimated_service_us == 10.0
            && first.estimator_prior_samples == 0,
            "scheduler observed hidden current-request service");
    require(std::abs(first.actual_thread_cpu_service_us - 24.0) <= 2.0,
            "runtime CPU service measurement exceeded threshold");
    require(first.service_wall_us + 0.05 >= first.actual_thread_cpu_service_us,
            "runtime wall service was smaller than CPU service");
    require(second.estimator_prior_samples == 1
            && std::abs(second.estimated_service_us
                        - first.actual_thread_cpu_service_us) < 0.001,
            "EWMA did not consume the completed measured CPU service");
    fs::remove(trace_path);
}

void test_distributed_l1_polling_and_bounded_logging() {
    const fs::path trace_path = temp_path("l1.csv");
    std::string rows;
    for (int id = 1; id <= 16; ++id) {
        rows += "rescuesched-trace-v2," + embedded_hash() + ","
            + std::to_string(id) + ",0,short,100,10000,0,0\n";
    }
    write_v2(trace_path, rows);
    auto config = base_config(physical::PolicyKind::L1_WORK_STEALING_POLLING);
    config.check_period_us = 5.0;
    config.decision_sample_cap = 3;
    config.decision_bucket_us = 1000.0;
    physical::FrozenTrace trace = physical::FrozenTrace::load_csv(
        trace_path.string(), config.worker_count);
    physical::PhysicalRuntime runtime(std::move(trace), config);
    const auto result = runtime.run();
    require(result.summary.invariants_pass, "distributed L1 invariants failed");
    require(result.summary.scheduler_epochs_executed == 0,
            "L1 used the central scheduler");
    require(result.summary.l1_poll_attempts > 0
            && result.summary.l1_poll_successes > 0,
            "idle workers did not initiate distributed steals");
    require(result.summary.l1_poll_attempts
                <= result.summary.l1_poll_epochs_scheduled,
            "more than one L1 attempt occurred per scheduled idle-worker epoch");
    require(result.summary.l1_poll_successes <= result.summary.l1_poll_attempts
            && result.summary.l1_moved_work_us > 0.0,
            "L1 success/moved-work counters are inconsistent");
    require(result.summary.decision_records_total >= result.decisions.size()
            && result.decisions.size() <= 3,
            "bounded deterministic decision sample cap failed");
    const uint64_t aggregate_count = std::accumulate(
        result.decision_aggregates.begin(), result.decision_aggregates.end(),
        uint64_t{0}, [](uint64_t total, const auto& row) {
            return total + row.count;
        });
    require(aggregate_count == result.summary.decision_records_total,
            "decision aggregates do not cover all decisions");
    require(result.migrations.size() == result.summary.l1_poll_successes,
            "L1 did not reuse the production handoff primitive");
    fs::remove(trace_path);
}

void test_scheduler_epoch_accounting_and_ingress_progress() {
    const fs::path trace_path = temp_path("stress.csv");
    std::string rows;
    for (int id = 1; id <= 200; ++id) {
        const int core = id % 2;
        rows += "rescuesched-trace-v3," + embedded_hash()
            + ",flow_affine," + std::to_string(id) + ","
            + std::to_string(1000 + id) + ",0,short,20,5000,"
            + std::to_string(core) + ",0\n";
    }
    write_v3(trace_path, rows);
    auto config = base_config(physical::PolicyKind::M1_RESCUE_SCHED);
    config.arrival_mode = physical::ArrivalMode::NETWORK_INGRESS;
    config.check_period_us = 2.0;
    config.decision_sample_cap = 16;
    physical::FrozenTrace trace = physical::FrozenTrace::load_csv(
        trace_path.string(), config.worker_count);
    physical::PhysicalRuntime runtime(std::move(trace), config);
    runtime.start_network_ingress();
    std::atomic<uint64_t> next{1};
    std::atomic<uint64_t> accepted{0};
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&] {
            while (true) {
                const uint64_t id = next.fetch_add(1, std::memory_order_relaxed);
                if (id > 200) return;
                const auto status = runtime.submit_network_request(
                    id, 1000 + id, static_cast<int>(id % 2));
                if (status == physical::NetworkSubmitStatus::ACCEPTED)
                    accepted.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& producer : producers) producer.join();
    const auto result = runtime.finish_network_ingress(true);
    require(accepted.load() == 200 && result.summary.completed_requests == 200
            && result.summary.invariants_pass,
            "concurrent ingress did not make continuous progress");
    require(result.summary.scheduler_epochs_scheduled
                == result.summary.scheduler_epochs_executed
                 + result.summary.scheduler_epochs_missed,
            "absolute scheduler epoch accounting identity failed");
    require(result.summary.decision_records_sampled <= 16,
            "scheduler stress exceeded bounded decision sample cap");
    fs::remove(trace_path);
}

} // namespace

int main() {
    try {
        test_receiver_lifecycle_race_100_each_policy();
        test_measured_cpu_service_updates_only_after_completion();
        test_distributed_l1_polling_and_bounded_logging();
        test_scheduler_epoch_accounting_and_ingress_progress();
        std::cout << "WP2 runtime concurrency tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WP2 runtime concurrency tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
