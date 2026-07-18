#include "physical/bounded_queue.h"
#include "physical/runtime_support.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void test_saturating_idle_elapsed() {
    require(physical::saturating_elapsed_ns(100, 40) == 60,
            "normal elapsed calculation failed");
    require(physical::saturating_elapsed_ns(40, 100) == 0,
            "idle elapsed underflow was not saturated");
}

void test_absolute_epoch_skip() {
    const auto early = physical::advance_absolute_epoch(99, 100, 10);
    require(early.scheduled == 0 && early.missed == 0
            && early.next_deadline_ns == 100,
            "early absolute epoch advanced");
    const auto stale = physical::advance_absolute_epoch(155, 100, 10);
    require(stale.scheduled == 6 && stale.missed == 5
            && stale.lag_ns == 55 && stale.next_deadline_ns == 160,
            "stale epochs were not skipped to the next future deadline");
}

void test_cpu_time_service_matrix() {
    const int cpu = physical::process_allowed_cpu_ids().front();
    for (bool contended : {false, true}) {
        for (double target_us : {5.0, 24.0, 100.0}) {
            std::atomic<bool> contender_ready{false};
            std::atomic<bool> stop{false};
            bool contender_affinity = true;
            std::thread contender;
            if (contended) {
                contender = std::thread([&] {
                    contender_affinity = physical::pin_current_thread_to_cpu(cpu);
                    contender_ready.store(true, std::memory_order_release);
                    while (!stop.load(std::memory_order_acquire)) {
                        for (int index = 0; index < 256; ++index)
                            std::atomic_signal_fence(std::memory_order_seq_cst);
                    }
                });
                while (!contender_ready.load(std::memory_order_acquire))
                    std::this_thread::yield();
            }
            physical::CpuWorkMeasurement measurement;
            bool worker_affinity = false;
            std::thread worker([&] {
                worker_affinity = physical::pin_current_thread_to_cpu(cpu);
                measurement = physical::execute_thread_cpu_work(target_us);
            });
            worker.join();
            stop.store(true, std::memory_order_release);
            if (contender.joinable()) contender.join();
            require(worker_affinity && contender_affinity,
                    "CPU service test affinity failed");
            const double tolerance = std::max(2.0, target_us * 0.05);
            require(std::abs(measurement.actual_cpu_us - target_us) <= tolerance,
                    "thread CPU service error exceeded WP2 threshold target="
                    + std::to_string(target_us) + " actual="
                    + std::to_string(measurement.actual_cpu_us));
            require(measurement.wall_us + 0.05 >= measurement.actual_cpu_us,
                    "service wall time was smaller than thread CPU time");
        }
    }
}

std::vector<physical::CpuTopologyEntry> fake_topology() {
    return {
        {0, 0, 0, true}, {1, 0, 1, true},
        {2, 0, 0, true}, {3, 0, 1, false},
        {4, 1, 0, true}, {5, 1, 0, true}
    };
}

void test_topology_negative_cases() {
    const auto topology = fake_topology();
    auto validate = [&](std::vector<int> workers, std::vector<int> control,
                        std::vector<int> irq, bool allow_siblings = false) {
        physical::TopologyValidationRequest request;
        request.worker_cpus = std::move(workers);
        request.control_cpus = std::move(control);
        request.irq_cpus = std::move(irq);
        request.allow_control_irq_smt_siblings = allow_siblings;
        return physical::validate_cpu_topology(topology, request);
    };
    require(!validate({0, 0}, {}, {}).pass, "duplicate logical worker CPU accepted");
    require(!validate({0, 2}, {}, {}).pass, "SMT sibling workers accepted");
    require(!validate({3}, {}, {}).pass, "offline worker CPU accepted");
    require(!validate({0}, {0}, {}).pass, "worker/control overlap accepted");
    require(!validate({0}, {2}, {}).pass, "control SMT sibling accepted without exception");
    require(validate({0}, {2}, {4}, true).pass,
            "explicit control/IRQ SMT sibling exception was rejected");
    require(!validate({0}, {}, {2}, false).pass,
            "IRQ SMT sibling accepted without exception");
    require(!validate({0}, {4}, {4}, true).pass,
            "control/IRQ logical CPU overlap accepted");
    require(!validate({0}, {4}, {5}, false).pass,
            "control/IRQ SMT sibling accepted without exception");
    require(validate({0}, {4}, {5}, true).pass,
            "explicit control/IRQ SMT sibling exception was rejected");
}

void test_response_queue_failure_injection_primitive() {
    physical::BoundedQueue<int> queue(1);
    require(queue.try_push(1), "first bounded response enqueue failed");
    require(!queue.try_push(2), "bounded response queue overflow was silent");
    require(queue.high_watermark() == 1, "response queue high watermark mismatch");
    int value = 0;
    require(queue.wait_pop(&value) && value == 1, "response queue pop mismatch");
    queue.close();
    require(!queue.try_push(3), "closed response queue accepted an item");
    require(!queue.wait_pop(&value), "closed empty response queue did not terminate");
}

} // namespace

int main() {
    try {
        test_saturating_idle_elapsed();
        test_absolute_epoch_skip();
        test_cpu_time_service_matrix();
        test_topology_negative_cases();
        test_response_queue_failure_injection_primitive();
        std::cout << "WP2 runtime support tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "WP2 runtime support tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
