#include "physical/runtime.h"

#include "sim/workloads/trace.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

namespace physical {
namespace {

using Clock = std::chrono::steady_clock;

uint64_t read_cycles() {
#if defined(__x86_64__) || defined(_M_X64)
    unsigned low = 0;
    unsigned high = 0;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (static_cast<uint64_t>(high) << 32U) | low;
#else
    return 0;
#endif
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool pin_current_thread(int cpu_id) {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu_id, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    (void)cpu_id;
    return false;
#endif
}

std::vector<int> allowed_cpu_ids() {
    std::vector<int> cpus;
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
            if (CPU_ISSET(cpu, &set)) cpus.push_back(cpu);
        }
    }
#endif
    if (cpus.empty()) {
        const unsigned count = std::max(1U, std::thread::hardware_concurrency());
        for (unsigned cpu = 0; cpu < count; ++cpu)
            cpus.push_back(static_cast<int>(cpu));
    }
    return cpus;
}

void execute_synthetic_work(double duration_us) {
    if (!(duration_us > 0.0)) return;
    const auto duration_ns = static_cast<int64_t>(std::ceil(duration_us * 1000.0));
    const auto deadline = Clock::now() + std::chrono::nanoseconds(duration_ns);
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    while (Clock::now() < deadline) {
        state ^= state << 7U;
        state ^= state >> 9U;
        state *= 0xbf58476d1ce4e5b9ULL;
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    if (state == 0) std::atomic_signal_fence(std::memory_order_seq_cst);
}

double percentile(std::vector<double> values, double quantile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double rank = std::ceil(quantile * static_cast<double>(values.size()));
    const size_t index = static_cast<size_t>(std::max(1.0, rank)) - 1;
    return values[std::min(index, values.size() - 1)];
}

std::string join_ints(const std::vector<int>& values) {
    std::ostringstream out;
    for (size_t index = 0; index < values.size(); ++index) {
        if (index) out << ';';
        out << values[index];
    }
    return out.str();
}

const char* method_name(sim::RpcMethod method) {
    return method == sim::RpcMethod::SHORT_RPC ? "short" : "long";
}

} // namespace

const char* descriptor_state_name(DescriptorState state) {
    switch (state) {
        case DescriptorState::PENDING: return "PENDING";
        case DescriptorState::QUEUED: return "QUEUED";
        case DescriptorState::IN_FLIGHT: return "IN_FLIGHT";
        case DescriptorState::RUNNING: return "RUNNING";
        case DescriptorState::DONE: return "DONE";
        case DescriptorState::CANCELLED: return "CANCELLED";
    }
    return "UNKNOWN";
}

const char* policy_name(PolicyKind policy) {
    switch (policy) {
        case PolicyKind::L0_RANDOM_CORE: return "L0_RandomCore";
        case PolicyKind::L1_WORK_STEALING_POLLING: return "L1_WorkStealingPolling";
        case PolicyKind::M0_ALTO_THRESHOLD: return "M0_AltoThreshold";
        case PolicyKind::M1_RESCUE_SCHED: return "M1_RescueSched";
    }
    return "UNKNOWN";
}

PolicyKind parse_policy(const std::string& input) {
    std::string value = lower_copy(input);
    value.erase(std::remove_if(value.begin(), value.end(), [](char ch) {
        return ch == '_' || ch == '-' || ch == ' ';
    }), value.end());
    if (value == "l0" || value == "random" || value == "l0randomcore")
        return PolicyKind::L0_RANDOM_CORE;
    if (value == "l1" || value == "polling" || value == "workstealing"
        || value == "l1workstealingpolling")
        return PolicyKind::L1_WORK_STEALING_POLLING;
    if (value == "m0" || value == "alto" || value == "m0altothreshold")
        return PolicyKind::M0_ALTO_THRESHOLD;
    if (value == "m1" || value == "rescue" || value == "rescuesched"
        || value == "m1rescuesched")
        return PolicyKind::M1_RESCUE_SCHED;
    throw std::invalid_argument("unknown physical runtime policy: " + input);
}

struct MethodEwmaEstimator::Impl {
    mutable std::mutex mutex;
    double alpha = 0.05;
    double short_us = 10.0;
    double long_us = 42.0;
    uint64_t short_samples = 0;
    uint64_t long_samples = 0;
};

MethodEwmaEstimator::MethodEwmaEstimator(
        double alpha, double initial_short_us, double initial_long_us)
    : impl_(std::make_shared<Impl>()) {
    if (!std::isfinite(alpha) || !(alpha > 0.0) || alpha > 1.0)
        throw std::invalid_argument("EWMA alpha must be in (0,1]");
    if (!std::isfinite(initial_short_us) || !std::isfinite(initial_long_us)
        || !(initial_short_us > 0.0) || !(initial_long_us > 0.0))
        throw std::invalid_argument("initial EWMA values must be finite and positive");
    impl_->alpha = alpha;
    impl_->short_us = initial_short_us;
    impl_->long_us = initial_long_us;
}

std::pair<double, uint64_t> MethodEwmaEstimator::snapshot(
        sim::RpcMethod method) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (method == sim::RpcMethod::SHORT_RPC)
        return {impl_->short_us, impl_->short_samples};
    return {impl_->long_us, impl_->long_samples};
}

void MethodEwmaEstimator::observe(
        sim::RpcMethod method, double completed_service_us) {
    if (!std::isfinite(completed_service_us) || !(completed_service_us > 0.0))
        throw std::invalid_argument("completed service observation must be positive");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (method == sim::RpcMethod::SHORT_RPC) {
        impl_->short_us = (1.0 - impl_->alpha) * impl_->short_us
                        + impl_->alpha * completed_service_us;
        ++impl_->short_samples;
    } else {
        impl_->long_us = (1.0 - impl_->alpha) * impl_->long_us
                       + impl_->alpha * completed_service_us;
        ++impl_->long_samples;
    }
}

struct PhysicalRuntime::Impl {
    struct Descriptor {
        DescriptorView view;
        double trace_arrival_us = 0.0;
        double enqueue_us = 0.0;
        double start_us = 0.0;
        double finish_us = 0.0;
        uint32_t execution_count = 0;
        uint32_t completion_count = 0;
        bool measurement_eligible = false;
        bool cancel_pending = false;
        bool cancel_requested_after_start = false;
    };

    struct Choice {
        std::optional<size_t> descriptor;
        int source_core = -1;
        int target_core = -1;
        int scanned_entries = 0;
        int evaluated_targets = 0;
        double local_completion_us = 0.0;
        double remote_completion_us = 0.0;
        std::string reason;
    };

    FrozenTrace trace;
    RuntimeConfig config;
    MethodEwmaEstimator estimator;
    std::vector<Descriptor> descriptors;
    std::vector<double> synthetic_payload_service_us;
    std::unordered_map<uint64_t, size_t> index_by_id;
    std::vector<std::deque<size_t>> queues;
    std::vector<std::optional<size_t>> running;
    std::vector<double> running_estimated_finish_us;
    std::vector<double> reservations_us;
    std::vector<std::unique_ptr<std::condition_variable>> worker_cvs;
    std::vector<std::thread> workers;
    std::thread scheduler;
    std::vector<int> worker_cpu_ids;
    std::vector<bool> worker_affinity_ok;
    std::mutex mutex;
    std::condition_variable ready_cv;
    std::condition_variable terminal_cv;
    std::mutex scheduler_wait_mutex;
    std::condition_variable scheduler_wait_cv;
    CompletionCallback completion_callback;
    Clock::time_point start_time;
    std::atomic<bool> stopping{false};
    bool started = false;
    bool submission_done = false;
    size_t ready_workers = 0;
    uint64_t terminal_count = 0;
    uint64_t duplicate_execution_count = 0;
    uint64_t duplicate_completion_count = 0;
    double submit_lag_sum_us = 0.0;
    double max_submit_lag_us = 0.0;
    uint64_t submitted_count = 0;
    uint64_t check_counter = 0;
    std::vector<DecisionRecord> decisions;
    std::vector<MigrationRecord> migrations;

    Impl(FrozenTrace input_trace, RuntimeConfig input_config)
        : trace(std::move(input_trace)),
          config(std::move(input_config)),
          estimator(config.ewma_alpha, config.initial_short_service_us,
                    config.initial_long_service_us) {
        validate_config();
        queues.resize(static_cast<size_t>(config.worker_count));
        running.resize(static_cast<size_t>(config.worker_count));
        running_estimated_finish_us.assign(static_cast<size_t>(config.worker_count), 0.0);
        reservations_us.assign(static_cast<size_t>(config.worker_count), 0.0);
        worker_cvs.reserve(static_cast<size_t>(config.worker_count));
        for (int worker = 0; worker < config.worker_count; ++worker)
            worker_cvs.push_back(std::make_unique<std::condition_variable>());

        worker_cpu_ids = config.cpu_ids.empty() ? allowed_cpu_ids() : config.cpu_ids;
        if (worker_cpu_ids.size() < static_cast<size_t>(config.worker_count))
            throw std::invalid_argument("not enough allowed CPU IDs for requested workers");
        worker_cpu_ids.resize(static_cast<size_t>(config.worker_count));
        std::sort(worker_cpu_ids.begin(), worker_cpu_ids.end());
        if (std::adjacent_find(worker_cpu_ids.begin(), worker_cpu_ids.end())
            != worker_cpu_ids.end())
            throw std::invalid_argument("worker CPU IDs must be unique");
        worker_affinity_ok.assign(static_cast<size_t>(config.worker_count), false);

        descriptors.reserve(trace.entries().size());
        synthetic_payload_service_us.reserve(trace.entries().size());
        for (size_t index = 0; index < trace.entries().size(); ++index) {
            const auto& entry = trace.entries()[index];
            Descriptor descriptor;
            descriptor.view.id = entry.id;
            descriptor.view.method = entry.method;
            descriptor.view.planned_arrival_us = entry.arrival_us;
            descriptor.view.deadline_abs_us = entry.arrival_us + entry.deadline_budget_us;
            descriptor.trace_arrival_us = entry.arrival_us;
            descriptor.view.initial_core = entry.initial_core;
            descriptor.view.current_core = entry.initial_core;
            descriptor.measurement_eligible =
                index >= static_cast<size_t>(config.warmup_requests);
            index_by_id.emplace(entry.id, index);
            descriptors.push_back(descriptor);
            synthetic_payload_service_us.push_back(entry.synthetic_service_us);
        }
    }

    void validate_config() const {
        const auto valid_label = [](const std::string& label) {
            return !label.empty()
                && std::all_of(label.begin(), label.end(), [](unsigned char ch) {
                    return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
                });
        };
        const double finite_values[] = {
            config.time_scale, config.check_period_us, config.epsilon_us,
            config.handoff_estimate_us, config.host_overhead_us,
            config.alto_queue_threshold_us, config.alto_min_gain_us,
            config.ewma_alpha, config.initial_short_service_us,
            config.initial_long_service_us
        };
        if (std::any_of(std::begin(finite_values), std::end(finite_values),
                        [](double value) { return !std::isfinite(value); }))
            throw std::invalid_argument("runtime configuration must be finite");
        if (config.worker_count <= 0 || config.warmup_requests < 0
            || config.warmup_requests >= static_cast<int>(trace.entries().size())
            || config.repetition < 0)
            throw std::invalid_argument("invalid worker or warmup request count");
        if (!valid_label(config.workload_label) || !valid_label(config.rho_label)
            || !valid_label(config.seed_label))
            throw std::invalid_argument(
                "runtime audit labels must be non-empty CSV-safe tokens");
        if (!(config.time_scale > 0.0) || !(config.check_period_us > 0.0)
            || config.scan_depth <= 0 || config.max_candidates <= 0
            || config.target_count <= 0 || config.moves_per_check <= 0)
            throw std::invalid_argument("runtime periods and bounds must be positive");
        if (config.epsilon_us < 0.0 || config.handoff_estimate_us < 0.0
            || config.host_overhead_us < 0.0
            || config.alto_queue_threshold_us < 0.0
            || config.alto_min_gain_us < 0.0)
            throw std::invalid_argument("runtime costs and thresholds must be non-negative");
        for (const auto& entry : trace.entries()) {
            if (entry.initial_core < 0 || entry.initial_core >= config.worker_count)
                throw std::invalid_argument("trace initial_core exceeds worker_count");
        }
    }

    double now_us() const {
        if (!started) return 0.0;
        const double wall_us = std::chrono::duration<double, std::micro>(
            Clock::now() - start_time).count();
        return wall_us / config.time_scale;
    }

    double estimated_work(const Descriptor& descriptor) const {
        return descriptor.view.estimated_service_us + config.host_overhead_us;
    }

    double core_work_us(int core, double now) const {
        double work = reservations_us[static_cast<size_t>(core)];
        if (running[static_cast<size_t>(core)]) {
            work += std::max(0.0,
                running_estimated_finish_us[static_cast<size_t>(core)] - now);
        }
        for (size_t index : queues[static_cast<size_t>(core)])
            work += estimated_work(descriptors[index]);
        return work;
    }

    bool queue_contains(int core, size_t descriptor_index) const {
        const auto& queue = queues[static_cast<size_t>(core)];
        return std::find(queue.begin(), queue.end(), descriptor_index) != queue.end();
    }

    bool remove_from_queue(int core, size_t descriptor_index) {
        auto& queue = queues[static_cast<size_t>(core)];
        auto found = std::find(queue.begin(), queue.end(), descriptor_index);
        if (found == queue.end()) return false;
        queue.erase(found);
        return true;
    }

    double local_completion_us(int core, size_t descriptor_index, double now) const {
        double completion = now;
        if (running[static_cast<size_t>(core)]) {
            completion += std::max(0.0,
                running_estimated_finish_us[static_cast<size_t>(core)] - now);
        }
        for (size_t index : queues[static_cast<size_t>(core)]) {
            completion += estimated_work(descriptors[index]);
            if (index == descriptor_index) return completion;
        }
        return std::numeric_limits<double>::infinity();
    }

    int target_risk_before(int core, double now, int* scanned) const {
        int risk = 0;
        int depth = 0;
        double completion = now;
        if (running[static_cast<size_t>(core)]) {
            completion += std::max(0.0,
                running_estimated_finish_us[static_cast<size_t>(core)] - now);
        }
        for (size_t index : queues[static_cast<size_t>(core)]) {
            if (depth++ >= config.scan_depth) break;
            ++(*scanned);
            const Descriptor& descriptor = descriptors[index];
            completion += estimated_work(descriptor);
            if (completion > descriptor.view.deadline_abs_us) ++risk;
        }
        return risk;
    }

    Choice choose_work_stealing(double now) const {
        Choice choice;
        choice.reason = "no_idle_target";
        int target = -1;
        for (int core = 0; core < config.worker_count; ++core) {
            if (!running[static_cast<size_t>(core)]
                && queues[static_cast<size_t>(core)].empty()
                && reservations_us[static_cast<size_t>(core)] == 0.0) {
                target = core;
                break;
            }
        }
        if (target < 0) return choice;

        int source = -1;
        double largest_work = -1.0;
        for (int core = 0; core < config.worker_count; ++core) {
            if (core == target || queues[static_cast<size_t>(core)].empty()) continue;
            const double work = core_work_us(core, now);
            if (work > largest_work) {
                largest_work = work;
                source = core;
            }
        }
        if (source < 0) {
            choice.reason = "no_queued_source";
            return choice;
        }
        choice.descriptor = queues[static_cast<size_t>(source)].front();
        choice.source_core = source;
        choice.target_core = target;
        choice.scanned_entries = 1;
        choice.local_completion_us = local_completion_us(
            source, *choice.descriptor, now);
        choice.remote_completion_us = now + config.handoff_estimate_us
            + reservations_us[static_cast<size_t>(target)]
            + estimated_work(descriptors[*choice.descriptor]);
        choice.reason = "commit_idle_pull";
        return choice;
    }

    Choice choose_alto(double now) const {
        Choice best;
        best.reason = "no_locally_late_improving_move";
        double best_gain = -std::numeric_limits<double>::infinity();
        int total_scanned = 0;
        int total_targets = 0;

        for (int source = 0; source < config.worker_count; ++source) {
            if (core_work_us(source, now) < config.alto_queue_threshold_us) continue;
            int depth = 0;
            int candidates = 0;
            for (size_t index : queues[static_cast<size_t>(source)]) {
                if (depth++ >= config.scan_depth || candidates >= config.max_candidates) break;
                ++total_scanned;
                const Descriptor& descriptor = descriptors[index];
                if (descriptor.view.migration_count > 0) continue;
                const double local = local_completion_us(source, index, now);
                if (local <= descriptor.view.deadline_abs_us) continue;
                ++candidates;
                for (int target = 0; target < config.worker_count; ++target) {
                    if (target == source) continue;
                    ++total_targets;
                    const double remote = now + config.handoff_estimate_us
                        + core_work_us(target, now) + estimated_work(descriptor);
                    const double gain = local - remote;
                    if (gain + 1e-9 < config.alto_min_gain_us || gain <= best_gain) continue;
                    best_gain = gain;
                    best.descriptor = index;
                    best.source_core = source;
                    best.target_core = target;
                    best.local_completion_us = local;
                    best.remote_completion_us = remote;
                    best.reason = "commit_predicted_gain";
                }
            }
        }
        best.scanned_entries = total_scanned;
        best.evaluated_targets = total_targets;
        return best;
    }

    Choice choose_rescue(double now) const {
        struct Target {
            int core = -1;
            int risk = 0;
            double work_us = 0.0;
        };
        std::vector<Target> targets;
        int total_scanned = 0;
        for (int core = 0; core < config.worker_count; ++core) {
            int risk_scanned = 0;
            targets.push_back(Target{
                core, target_risk_before(core, now, &risk_scanned),
                core_work_us(core, now)
            });
            total_scanned += risk_scanned;
        }
        std::sort(targets.begin(), targets.end(), [](const Target& lhs, const Target& rhs) {
            if (lhs.risk != rhs.risk) return lhs.risk < rhs.risk;
            if (lhs.work_us != rhs.work_us) return lhs.work_us < rhs.work_us;
            return lhs.core < rhs.core;
        });

        Choice best;
        best.reason = "no_locally_doomed_remote_feasible_move";
        double best_score = -std::numeric_limits<double>::infinity();
        int total_targets = 0;
        for (int source = 0; source < config.worker_count; ++source) {
            int depth = 0;
            int candidates = 0;
            for (size_t index : queues[static_cast<size_t>(source)]) {
                if (depth++ >= config.scan_depth || candidates >= config.max_candidates) break;
                ++total_scanned;
                const Descriptor& descriptor = descriptors[index];
                if (descriptor.view.migration_count > 0) continue;
                const double local = local_completion_us(source, index, now);
                if (local <= descriptor.view.deadline_abs_us) continue;
                ++candidates;

                int tried = 0;
                for (const Target& target : targets) {
                    if (target.core == source) continue;
                    if (tried++ >= config.target_count) break;
                    ++total_targets;
                    const double remote = now + config.handoff_estimate_us
                        + target.work_us + estimated_work(descriptor);
                    if (remote + config.epsilon_us > descriptor.view.deadline_abs_us)
                        continue;
                    if (target.risk != 0) continue;
                    const double local_lateness = std::max(
                        0.0, local - descriptor.view.deadline_abs_us);
                    const double score = descriptor.view.deadline_abs_us - remote
                                       + 0.10 * local_lateness
                                       - config.handoff_estimate_us;
                    if (score <= best_score) continue;
                    best_score = score;
                    best.descriptor = index;
                    best.source_core = source;
                    best.target_core = target.core;
                    best.local_completion_us = local;
                    best.remote_completion_us = remote;
                    best.reason = "commit_local_doom_remote_feasible";
                }
            }
        }
        best.scanned_entries = total_scanned;
        best.evaluated_targets = total_targets;
        return best;
    }

    Choice choose(double now) const {
        switch (config.policy) {
            case PolicyKind::L1_WORK_STEALING_POLLING:
                return choose_work_stealing(now);
            case PolicyKind::M0_ALTO_THRESHOLD:
                return choose_alto(now);
            case PolicyKind::M1_RESCUE_SCHED:
                return choose_rescue(now);
            case PolicyKind::L0_RANDOM_CORE:
                return Choice{};
        }
        return Choice{};
    }

    bool handoff(const Choice& choice) {
        if (!choice.descriptor || choice.source_core < 0 || choice.target_core < 0)
            return false;
        const size_t index = *choice.descriptor;
        const auto wall_start = Clock::now();
        const double logical_start = now_us();
        CompletionCallback callback;
        RequestOutcome cancelled_outcome;
        {
            std::unique_lock<std::mutex> lock(mutex);
            Descriptor& descriptor = descriptors[index];
            if (descriptor.view.state != DescriptorState::QUEUED
                || descriptor.view.current_core != choice.source_core
                || !remove_from_queue(choice.source_core, index)) {
                return false;
            }
            descriptor.view.state = DescriptorState::IN_FLIGHT;
            descriptor.view.current_core = -1;
            reservations_us[static_cast<size_t>(choice.target_core)] +=
                estimated_work(descriptor);
        }

        std::atomic_thread_fence(std::memory_order_seq_cst);
        std::this_thread::yield();
        std::atomic_thread_fence(std::memory_order_seq_cst);

        std::string outcome = "committed_append_tail";
        {
            std::unique_lock<std::mutex> lock(mutex);
            Descriptor& descriptor = descriptors[index];
            reservations_us[static_cast<size_t>(choice.target_core)] = std::max(
                0.0, reservations_us[static_cast<size_t>(choice.target_core)]
                       - estimated_work(descriptor));
            if (descriptor.cancel_pending) {
                descriptor.view.state = DescriptorState::CANCELLED;
                descriptor.finish_us = now_us();
                ++terminal_count;
                outcome = "cancelled_in_flight";
                cancelled_outcome = outcome_for(descriptor);
                callback = completion_callback;
                terminal_cv.notify_all();
            } else {
                descriptor.view.current_core = choice.target_core;
                ++descriptor.view.migration_count;
                queues[static_cast<size_t>(choice.target_core)].push_back(index);
                descriptor.view.state = DescriptorState::QUEUED;
                worker_cvs[static_cast<size_t>(choice.target_core)]->notify_one();
            }
            const auto wall_end = Clock::now();
            migrations.push_back(MigrationRecord{
                descriptor.view.id,
                choice.source_core,
                choice.target_core,
                logical_start,
                now_us(),
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    wall_end - wall_start).count()),
                outcome
            });
        }
        if (callback) callback(cancelled_outcome);
        return outcome == "committed_append_tail";
    }

    void scheduler_loop() {
        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double, std::micro>(
                config.check_period_us * config.time_scale));
        auto next_check = Clock::now() + period;
        while (!stopping.load(std::memory_order_acquire)) {
            {
                std::unique_lock<std::mutex> wait_lock(scheduler_wait_mutex);
                scheduler_wait_cv.wait_until(wait_lock, next_check, [&] {
                    return stopping.load(std::memory_order_acquire);
                });
            }
            if (stopping.load(std::memory_order_acquire)) break;
            next_check += period;

            for (int move = 0; move < config.moves_per_check; ++move) {
                const auto decision_start = Clock::now();
                const uint64_t cycles_start = read_cycles();
                Choice choice;
                double logical_now = 0.0;
                uint64_t check_id = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    logical_now = now_us();
                    check_id = ++check_counter;
                    choice = choose(logical_now);
                }
                const uint64_t cycles_end = read_cycles();
                const auto decision_end = Clock::now();
                DecisionRecord record;
                record.check_id = check_id;
                record.timestamp_us = logical_now;
                record.source_core = choice.source_core;
                record.target_core = choice.target_core;
                record.scanned_entries = choice.scanned_entries;
                record.evaluated_targets = choice.evaluated_targets;
                record.predicted_local_completion_us = choice.local_completion_us;
                record.predicted_remote_completion_us = choice.remote_completion_us;
                record.reason = choice.reason.empty() ? "no_candidate" : choice.reason;
                record.decision_cycles = cycles_end >= cycles_start
                    ? cycles_end - cycles_start : 0;
                record.decision_duration_ns = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        decision_end - decision_start).count());
                if (choice.descriptor) {
                    std::lock_guard<std::mutex> lock(mutex);
                    record.request_id = descriptors[*choice.descriptor].view.id;
                    record.deadline_abs_us =
                        descriptors[*choice.descriptor].view.deadline_abs_us;
                }
                bool committed = false;
                if (choice.descriptor) committed = handoff(choice);
                if (choice.descriptor && !committed) record.reason = "source_revalidation_reject";
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    decisions.push_back(std::move(record));
                }
                if (!choice.descriptor || !committed) break;
            }
        }
    }

    RequestOutcome outcome_for(const Descriptor& descriptor) const {
        RequestOutcome outcome;
        outcome.id = descriptor.view.id;
        outcome.state = descriptor.view.state;
        outcome.initial_core = descriptor.view.initial_core;
        outcome.final_core = descriptor.view.current_core;
        outcome.migration_count = descriptor.view.migration_count;
        outcome.execution_count = descriptor.execution_count;
        outcome.completion_count = descriptor.completion_count;
        outcome.planned_arrival_us = descriptor.view.planned_arrival_us;
        outcome.trace_arrival_us = descriptor.trace_arrival_us;
        outcome.enqueue_us = descriptor.enqueue_us;
        outcome.start_us = descriptor.start_us;
        outcome.finish_us = descriptor.finish_us;
        outcome.deadline_abs_us = descriptor.view.deadline_abs_us;
        outcome.server_completion_us = descriptor.finish_us > 0.0
            ? descriptor.finish_us - descriptor.view.planned_arrival_us : 0.0;
        const auto found = index_by_id.find(descriptor.view.id);
        outcome.synthetic_service_us = found == index_by_id.end()
            ? 0.0 : synthetic_payload_service_us[found->second];
        outcome.estimated_service_us = descriptor.view.estimated_service_us;
        outcome.estimator_prior_samples = descriptor.view.estimator_prior_samples;
        outcome.measurement_eligible = descriptor.measurement_eligible;
        outcome.deadline_violation = descriptor.view.state == DescriptorState::DONE
            && descriptor.finish_us > descriptor.view.deadline_abs_us;
        outcome.cancel_requested_after_start = descriptor.cancel_requested_after_start;
        return outcome;
    }

    void worker_loop(int worker_id) {
        const bool affinity_ok = pin_current_thread(
            worker_cpu_ids[static_cast<size_t>(worker_id)]);
        {
            std::lock_guard<std::mutex> lock(mutex);
            worker_affinity_ok[static_cast<size_t>(worker_id)] = affinity_ok;
            ++ready_workers;
            ready_cv.notify_all();
        }

        while (true) {
            size_t index = 0;
            double work_us = 0.0;
            {
                std::unique_lock<std::mutex> lock(mutex);
                worker_cvs[static_cast<size_t>(worker_id)]->wait(lock, [&] {
                    return stopping.load(std::memory_order_acquire)
                        || !queues[static_cast<size_t>(worker_id)].empty();
                });
                if (stopping.load(std::memory_order_acquire)
                    && queues[static_cast<size_t>(worker_id)].empty())
                    return;
                index = queues[static_cast<size_t>(worker_id)].front();
                queues[static_cast<size_t>(worker_id)].pop_front();
                Descriptor& descriptor = descriptors[index];
                if (descriptor.view.state != DescriptorState::QUEUED
                    || descriptor.view.current_core != worker_id) {
                    ++duplicate_execution_count;
                    continue;
                }
                descriptor.view.state = DescriptorState::RUNNING;
                descriptor.start_us = now_us();
                ++descriptor.execution_count;
                if (descriptor.execution_count > 1) ++duplicate_execution_count;
                running[static_cast<size_t>(worker_id)] = index;
                running_estimated_finish_us[static_cast<size_t>(worker_id)] =
                    descriptor.start_us + estimated_work(descriptor);
                work_us = (synthetic_payload_service_us[index]
                           + config.host_overhead_us)
                        * config.time_scale;
            }

            execute_synthetic_work(work_us);

            CompletionCallback callback;
            RequestOutcome completed_outcome;
            {
                std::unique_lock<std::mutex> lock(mutex);
                Descriptor& descriptor = descriptors[index];
                if (descriptor.view.state != DescriptorState::RUNNING
                    || !running[static_cast<size_t>(worker_id)]
                    || *running[static_cast<size_t>(worker_id)] != index) {
                    ++duplicate_completion_count;
                    continue;
                }
                estimator.observe(
                    descriptor.view.method, synthetic_payload_service_us[index]);
                descriptor.finish_us = now_us();
                ++descriptor.completion_count;
                if (descriptor.completion_count > 1) ++duplicate_completion_count;
                descriptor.view.state = DescriptorState::DONE;
                descriptor.view.current_core = worker_id;
                running[static_cast<size_t>(worker_id)].reset();
                running_estimated_finish_us[static_cast<size_t>(worker_id)] = 0.0;
                ++terminal_count;
                completed_outcome = outcome_for(descriptor);
                callback = completion_callback;
                terminal_cv.notify_all();
            }
            if (callback) callback(completed_outcome);
        }
    }

    void start_threads() {
        for (int worker = 0; worker < config.worker_count; ++worker)
            workers.emplace_back([this, worker] { worker_loop(worker); });
        {
            std::unique_lock<std::mutex> lock(mutex);
            ready_cv.wait(lock, [&] {
                return ready_workers == static_cast<size_t>(config.worker_count);
            });
            const bool affinity_failed = std::any_of(
                worker_affinity_ok.begin(), worker_affinity_ok.end(),
                [](bool ok) { return !ok; });
            if (config.strict_affinity && affinity_failed) {
                stopping.store(true, std::memory_order_release);
                for (auto& cv : worker_cvs) cv->notify_all();
                lock.unlock();
                for (auto& worker : workers) worker.join();
                workers.clear();
                throw std::runtime_error("strict worker affinity setup failed");
            }
        }
        if (config.policy != PolicyKind::L0_RANDOM_CORE)
            scheduler = std::thread([this] { scheduler_loop(); });
    }

    void stop_threads() {
        stopping.store(true, std::memory_order_release);
        for (auto& cv : worker_cvs) cv->notify_all();
        scheduler_wait_cv.notify_all();
        if (scheduler.joinable()) scheduler.join();
        for (auto& worker : workers) {
            if (worker.joinable()) worker.join();
        }
        workers.clear();
    }

    RuntimeResult build_result() {
        RuntimeResult result;
        std::lock_guard<std::mutex> lock(mutex);
        result.worker_cpu_ids = worker_cpu_ids;
        result.worker_affinity_ok = worker_affinity_ok;
        result.decisions = decisions;
        result.migrations = migrations;
        result.requests.reserve(descriptors.size());
        std::vector<double> latencies;
        double first_measurement_arrival = std::numeric_limits<double>::infinity();
        double last_measurement_finish = 0.0;
        uint64_t successful_measurement = 0;
        uint64_t migrated_measurement = 0;

        result.summary.total_requests = descriptors.size();
        result.summary.duplicate_execution_count = duplicate_execution_count;
        result.summary.duplicate_completion_count = duplicate_completion_count;
        result.summary.max_submit_lag_us = max_submit_lag_us;
        result.summary.mean_submit_lag_us = submitted_count > 0
            ? submit_lag_sum_us / static_cast<double>(submitted_count) : 0.0;
        result.summary.affinity_failure_count = static_cast<uint64_t>(std::count(
            worker_affinity_ok.begin(), worker_affinity_ok.end(), false));

        for (const Descriptor& descriptor : descriptors) {
            RequestOutcome outcome = outcome_for(descriptor);
            result.requests.push_back(outcome);
            if (outcome.state == DescriptorState::DONE) ++result.summary.completed_requests;
            if (outcome.state == DescriptorState::CANCELLED)
                ++result.summary.cancelled_requests;
            if (outcome.state != DescriptorState::DONE
                && outcome.state != DescriptorState::CANCELLED)
                ++result.summary.lost_descriptor_count;
            if (!outcome.measurement_eligible) continue;
            ++result.summary.measurement_requests;
            if (outcome.state != DescriptorState::DONE) continue;
            latencies.push_back(outcome.server_completion_us);
            first_measurement_arrival = std::min(
                first_measurement_arrival, outcome.planned_arrival_us);
            last_measurement_finish = std::max(last_measurement_finish, outcome.finish_us);
            if (outcome.deadline_violation) ++result.summary.deadline_violations;
            else ++successful_measurement;
            if (outcome.migration_count > 0) ++migrated_measurement;
            result.summary.migration_count += outcome.migration_count;
        }
        result.summary.migrated_requests = migrated_measurement;
        result.summary.nonzero_reservation_count = static_cast<uint64_t>(std::count_if(
            reservations_us.begin(), reservations_us.end(),
            [](double value) { return value > 1e-9; }));
        if (!latencies.empty()) {
            result.summary.deadline_violation_rate =
                static_cast<double>(result.summary.deadline_violations)
                / static_cast<double>(latencies.size());
            result.summary.p50_server_completion_us = percentile(latencies, 0.50);
            result.summary.p99_server_completion_us = percentile(latencies, 0.99);
            result.summary.p999_server_completion_us = percentile(latencies, 0.999);
        }
        const double duration_us = last_measurement_finish - first_measurement_arrival;
        if (std::isfinite(duration_us) && duration_us > 0.0) {
            result.summary.goodput_rps = static_cast<double>(successful_measurement)
                * 1e6 / duration_us;
        }
        result.summary.invariants_pass =
            result.summary.lost_descriptor_count == 0
            && result.summary.duplicate_execution_count == 0
            && result.summary.duplicate_completion_count == 0
            && result.summary.nonzero_reservation_count == 0
            && (!config.strict_affinity || result.summary.affinity_failure_count == 0);
        return result;
    }
};

PhysicalRuntime::PhysicalRuntime(FrozenTrace trace, RuntimeConfig config)
    : impl_(std::make_unique<Impl>(std::move(trace), std::move(config))) {}

PhysicalRuntime::~PhysicalRuntime() {
    if (impl_) impl_->stop_threads();
}

RuntimeResult PhysicalRuntime::run() {
    if (impl_->started) throw std::logic_error("physical runtime is single-use");
    if (impl_->config.arrival_mode != ArrivalMode::TRACE_REPLAY)
        throw std::logic_error("run() requires trace replay arrival mode");
    impl_->started = true;
    impl_->start_time = Clock::now();
    impl_->start_threads();

    for (size_t index = 0; index < impl_->descriptors.size(); ++index) {
        auto& descriptor = impl_->descriptors[index];
        const auto target = impl_->start_time + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double, std::micro>(
                descriptor.view.planned_arrival_us * impl_->config.time_scale));
        std::this_thread::sleep_until(target);
        const auto estimate = impl_->estimator.snapshot(descriptor.view.method);
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (descriptor.view.state == DescriptorState::CANCELLED) continue;
            const double now = impl_->now_us();
            const double lag = std::max(0.0, now - descriptor.view.planned_arrival_us);
            impl_->submit_lag_sum_us += lag;
            impl_->max_submit_lag_us = std::max(impl_->max_submit_lag_us, lag);
            ++impl_->submitted_count;
            descriptor.view.estimated_service_us = estimate.first;
            descriptor.view.estimator_prior_samples = estimate.second;
            descriptor.enqueue_us = now;
            descriptor.view.current_core = descriptor.view.initial_core;
            impl_->queues[static_cast<size_t>(descriptor.view.initial_core)].push_back(index);
            descriptor.view.state = DescriptorState::QUEUED;
            impl_->worker_cvs[static_cast<size_t>(descriptor.view.initial_core)]->notify_one();
        }
    }

    {
        std::unique_lock<std::mutex> lock(impl_->mutex);
        impl_->submission_done = true;
        impl_->terminal_cv.wait(lock, [&] {
            return impl_->terminal_count == impl_->descriptors.size();
        });
    }
    impl_->stop_threads();
    return impl_->build_result();
}

void PhysicalRuntime::start_network_ingress() {
    if (impl_->started) throw std::logic_error("physical runtime is single-use");
    if (impl_->config.arrival_mode != ArrivalMode::NETWORK_INGRESS)
        throw std::logic_error("network ingress mode is not enabled");
    if (impl_->config.time_scale != 1.0)
        throw std::logic_error("network ingress requires time_scale=1");
    impl_->started = true;
    impl_->start_time = Clock::now();
    impl_->start_threads();
}

NetworkSubmitStatus PhysicalRuntime::submit_network_request(
        uint64_t request_id, uint64_t flow_id, int ingress_core) {
    if (!impl_->started || impl_->config.arrival_mode != ArrivalMode::NETWORK_INGRESS)
        throw std::logic_error("network ingress runtime has not started");
    if (ingress_core < 0 || ingress_core >= impl_->config.worker_count)
        throw std::out_of_range("network ingress core outside worker range");

    const auto found = impl_->index_by_id.find(request_id);
    if (found == impl_->index_by_id.end()) return NetworkSubmitStatus::UNKNOWN_REQUEST;
    const size_t index = found->second;
    if (impl_->trace.entries()[index].flow_id != flow_id)
        return NetworkSubmitStatus::FLOW_MISMATCH;
    const auto estimate = impl_->estimator.snapshot(
        impl_->descriptors[index].view.method);
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto& descriptor = impl_->descriptors[index];
        if (descriptor.view.state != DescriptorState::PENDING)
            return NetworkSubmitStatus::DUPLICATE_OR_TERMINAL;
        const double now = impl_->now_us();
        const double deadline_budget = impl_->trace.entries()[index].deadline_budget_us;
        descriptor.view.planned_arrival_us = now;
        descriptor.view.deadline_abs_us = now + deadline_budget;
        descriptor.view.estimated_service_us = estimate.first;
        descriptor.view.estimator_prior_samples = estimate.second;
        descriptor.view.initial_core = ingress_core;
        descriptor.view.current_core = ingress_core;
        descriptor.enqueue_us = now;
        ++impl_->submitted_count;
        impl_->queues[static_cast<size_t>(ingress_core)].push_back(index);
        descriptor.view.state = DescriptorState::QUEUED;
        impl_->worker_cvs[static_cast<size_t>(ingress_core)]->notify_one();
    }
    return NetworkSubmitStatus::ACCEPTED;
}

RuntimeResult PhysicalRuntime::finish_network_ingress(bool cancel_unreceived) {
    if (!impl_->started || impl_->config.arrival_mode != ArrivalMode::NETWORK_INGRESS)
        throw std::logic_error("network ingress runtime has not started");

    std::vector<std::pair<CompletionCallback, RequestOutcome>> callbacks;
    {
        std::unique_lock<std::mutex> lock(impl_->mutex);
        if (cancel_unreceived) {
            for (auto& descriptor : impl_->descriptors) {
                if (descriptor.view.state != DescriptorState::PENDING) continue;
                descriptor.view.state = DescriptorState::CANCELLED;
                descriptor.finish_us = impl_->now_us();
                ++impl_->terminal_count;
                if (impl_->completion_callback) {
                    callbacks.emplace_back(
                        impl_->completion_callback, impl_->outcome_for(descriptor));
                }
            }
        }
        impl_->submission_done = true;
        impl_->terminal_cv.wait(lock, [&] {
            return impl_->terminal_count == impl_->descriptors.size();
        });
    }
    for (auto& item : callbacks) item.first(item.second);
    impl_->stop_threads();
    return impl_->build_result();
}

bool PhysicalRuntime::request_cancel(uint64_t request_id) {
    CompletionCallback callback;
    RequestOutcome outcome;
    bool terminal_now = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        auto found = impl_->index_by_id.find(request_id);
        if (found == impl_->index_by_id.end()) return false;
        auto& descriptor = impl_->descriptors[found->second];
        switch (descriptor.view.state) {
            case DescriptorState::PENDING:
                descriptor.view.state = DescriptorState::CANCELLED;
                descriptor.finish_us = impl_->now_us();
                ++impl_->terminal_count;
                terminal_now = true;
                break;
            case DescriptorState::QUEUED:
                if (!impl_->remove_from_queue(descriptor.view.current_core, found->second))
                    return false;
                descriptor.view.state = DescriptorState::CANCELLED;
                descriptor.finish_us = impl_->now_us();
                ++impl_->terminal_count;
                terminal_now = true;
                break;
            case DescriptorState::IN_FLIGHT:
                descriptor.cancel_pending = true;
                return true;
            case DescriptorState::RUNNING:
                descriptor.cancel_requested_after_start = true;
                return true;
            case DescriptorState::DONE:
            case DescriptorState::CANCELLED:
                return false;
        }
        if (terminal_now) {
            outcome = impl_->outcome_for(descriptor);
            callback = impl_->completion_callback;
            impl_->terminal_cv.notify_all();
        }
    }
    if (callback) callback(outcome);
    return terminal_now;
}

DescriptorState PhysicalRuntime::request_state(uint64_t request_id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto found = impl_->index_by_id.find(request_id);
    if (found == impl_->index_by_id.end())
        throw std::out_of_range("unknown physical runtime request ID");
    return impl_->descriptors[found->second].view.state;
}

void PhysicalRuntime::set_completion_callback(CompletionCallback callback) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->completion_callback = std::move(callback);
}

void PhysicalRuntime::write_outputs(const RuntimeResult& result) const {
    namespace fs = std::filesystem;
    if (impl_->config.output_dir.empty())
        throw std::invalid_argument("runtime output_dir is empty");
    fs::create_directories(impl_->config.output_dir);
    const fs::path root(impl_->config.output_dir);

    {
        std::ofstream csv(root / "requests.csv");
        csv << "request_id,state,method,measurement_eligible,planned_arrival_us,"
               "trace_arrival_us,"
               "enqueue_us,service_start_us,finish_us,deadline_abs_us,"
               "server_completion_us,client_rtt_us,client_rtt_status,initial_core,"
               "final_core,migration_count,execution_count,completion_count,"
               "synthetic_service_us,estimated_service_us,estimator_prior_samples,"
               "deadline_violation,cancel_requested_after_start\n";
        csv << std::setprecision(17);
        for (const auto& request : result.requests) {
            const auto found = impl_->index_by_id.find(request.id);
            const auto method = found == impl_->index_by_id.end()
                ? sim::RpcMethod::SHORT_RPC
                : impl_->descriptors[found->second].view.method;
            csv << request.id << ',' << descriptor_state_name(request.state) << ','
                << method_name(method) << ',' << (request.measurement_eligible ? 1 : 0) << ','
                << request.planned_arrival_us << ',' << request.trace_arrival_us << ','
                << request.enqueue_us << ','
                << request.start_us << ',' << request.finish_us << ','
                << request.deadline_abs_us << ',' << request.server_completion_us
                << ",,unavailable_server_log," << request.initial_core << ','
                << request.final_core << ',' << request.migration_count << ','
                << request.execution_count << ',' << request.completion_count << ','
                << request.synthetic_service_us << ',' << request.estimated_service_us << ','
                << request.estimator_prior_samples << ','
                << (request.deadline_violation ? 1 : 0) << ','
                << (request.cancel_requested_after_start ? 1 : 0) << '\n';
        }
    }

    {
        std::ofstream csv(root / "decisions.csv");
        csv << "check_id,timestamp_us,policy,request_id,source_core,target_core,"
               "scanned_entries,evaluated_targets,predicted_local_completion_us,"
               "predicted_remote_completion_us,deadline_abs_us,reason,"
               "decision_cycles,decision_duration_ns\n";
        csv << std::setprecision(17);
        for (const auto& decision : result.decisions) {
            csv << decision.check_id << ',' << decision.timestamp_us << ','
                << policy_name(impl_->config.policy) << ',' << decision.request_id << ','
                << decision.source_core << ',' << decision.target_core << ','
                << decision.scanned_entries << ',' << decision.evaluated_targets << ','
                << decision.predicted_local_completion_us << ','
                << decision.predicted_remote_completion_us << ','
                << decision.deadline_abs_us << ',' << decision.reason << ','
                << decision.decision_cycles << ',' << decision.decision_duration_ns << '\n';
        }
    }

    {
        std::ofstream csv(root / "migrations.csv");
        csv << "request_id,source_core,target_core,handoff_start_us,handoff_end_us,"
               "handoff_duration_ns,target_insert_policy,outcome\n";
        csv << std::setprecision(17);
        for (const auto& migration : result.migrations) {
            csv << migration.request_id << ',' << migration.source_core << ','
                << migration.target_core << ',' << migration.start_us << ','
                << migration.end_us << ',' << migration.handoff_duration_ns
                << ",append_tail," << migration.outcome << '\n';
        }
    }

    {
        std::ofstream csv(root / "summary.csv");
        csv << "evidence_scope,trace_embedded_sha256,trace_input_file_sha256,"
               "workload,rho,seed,repetition,policy,"
               "total_requests,measurement_requests,"
               "completed_requests,cancelled_requests,deadline_violations,"
               "deadline_violation_rate,goodput_rps,P50_server_completion_us,"
               "P99_server_completion_us,P999_server_completion_us,migrated_requests,"
               "migration_count,duplicate_execution_count,duplicate_completion_count,"
               "lost_descriptor_count,nonzero_reservation_count,affinity_failure_count,"
               "max_submit_lag_us,mean_submit_lag_us,invariants_pass\n";
        const auto& summary = result.summary;
        csv << std::setprecision(17)
            << (impl_->config.arrival_mode == ArrivalMode::NETWORK_INGRESS
                    ? "physical_network_rpc_server" :
                      "local_synthetic_runtime_implementation_validation") << ','
            << impl_->trace.embedded_sha256() << ','
            << impl_->trace.input_file_sha256() << ','
            << impl_->config.workload_label << ',' << impl_->config.rho_label << ','
            << impl_->config.seed_label << ',' << impl_->config.repetition << ','
            << policy_name(impl_->config.policy) << ',' << summary.total_requests << ','
            << summary.measurement_requests << ',' << summary.completed_requests << ','
            << summary.cancelled_requests << ',' << summary.deadline_violations << ','
            << summary.deadline_violation_rate << ',' << summary.goodput_rps << ','
            << summary.p50_server_completion_us << ','
            << summary.p99_server_completion_us << ','
            << summary.p999_server_completion_us << ','
            << summary.migrated_requests << ',' << summary.migration_count << ','
            << summary.duplicate_execution_count << ','
            << summary.duplicate_completion_count << ','
            << summary.lost_descriptor_count << ','
            << summary.nonzero_reservation_count << ','
            << summary.affinity_failure_count << ',' << summary.max_submit_lag_us << ','
            << summary.mean_submit_lag_us << ','
            << (summary.invariants_pass ? 1 : 0) << '\n';
    }

    {
        std::ofstream manifest(root / "manifest.env");
        const bool network = impl_->config.arrival_mode == ArrivalMode::NETWORK_INGRESS;
        manifest << std::setprecision(17)
                 << "evidence_scope="
                 << (network ? "physical_network_rpc_server"
                             : "local_synthetic_runtime_implementation_validation") << '\n'
                 << "physical_rpc_runtime_present=" << (network ? "YES" : "NO") << '\n'
                 << "client_rtt_available=NO\n"
                 << "workload=" << impl_->config.workload_label << '\n'
                 << "rho=" << impl_->config.rho_label << '\n'
                 << "seed=" << impl_->config.seed_label << '\n'
                 << "repetition=" << impl_->config.repetition << '\n'
                 << "policy=" << policy_name(impl_->config.policy) << '\n'
                 << "trace_path=" << impl_->trace.path() << '\n'
                 << "trace_version=" << impl_->trace.version() << '\n'
                 << "trace_embedded_sha256=" << impl_->trace.embedded_sha256() << '\n'
                 << "trace_input_file_sha256=" << impl_->trace.input_file_sha256() << '\n'
                 << "trace_placement_mode=" << impl_->trace.placement_mode() << '\n'
                 << "worker_count=" << impl_->config.worker_count << '\n'
                 << "worker_cpu_ids=" << join_ints(result.worker_cpu_ids) << '\n'
                 << "strict_affinity=" << (impl_->config.strict_affinity ? 1 : 0) << '\n'
                 << "time_scale=" << impl_->config.time_scale << '\n'
                 << "warmup_requests=" << impl_->config.warmup_requests << '\n'
                 << "check_period_us=" << impl_->config.check_period_us << '\n'
                 << "scan_depth=" << impl_->config.scan_depth << '\n'
                 << "max_candidates=" << impl_->config.max_candidates << '\n'
                 << "target_count=" << impl_->config.target_count << '\n'
                 << "moves_per_check=" << impl_->config.moves_per_check << '\n'
                 << "epsilon_us=" << impl_->config.epsilon_us << '\n'
                 << "handoff_estimate_us=" << impl_->config.handoff_estimate_us << '\n'
                 << "host_overhead_us=" << impl_->config.host_overhead_us << '\n'
                 << "ewma_alpha=" << impl_->config.ewma_alpha << '\n'
                 << "estimator_scope=shared_global_method_keyed_completion_updated\n"
                 << "target_insert_policy=append_tail\n"
                 << "arrival_model="
                 << (network ? "external_network_ingress" : "open_loop_monotonic_clock")
                 << '\n'
                 << "trace_driven_cpu_service=YES\n";
    }

    std::ofstream status(root / "RUNTIME_STATUS.txt");
    const bool network = impl_->config.arrival_mode == ArrivalMode::NETWORK_INGRESS;
    status << "status=" << (result.summary.invariants_pass ? "PASS" : "FAIL") << '\n'
           << "scope=" << (network ? "physical_network_rpc_server"
                                   : "local_synthetic_runtime_implementation_validation")
           << '\n'
           << "physical_rpc_runtime=" << (network ? "IMPLEMENTED" : "NOT_ACTIVE")
           << '\n';
}

} // namespace physical
