#include "physical/runtime_support.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#include <time.h>
#endif

namespace physical {
namespace {

uint64_t timespec_ns(const timespec& value) {
    return static_cast<uint64_t>(value.tv_sec) * 1000000000ULL
         + static_cast<uint64_t>(value.tv_nsec);
}

int read_int_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    int value = -1;
    if (!(input >> value)) throw std::runtime_error("cannot read " + path.string());
    return value;
}

std::string core_name(const CpuTopologyEntry& entry) {
    return "socket=" + std::to_string(entry.socket_id)
         + ",core=" + std::to_string(entry.core_id);
}

} // namespace

uint64_t saturating_elapsed_ns(uint64_t now_ns, uint64_t then_ns) noexcept {
    return now_ns >= then_ns ? now_ns - then_ns : 0;
}

AbsoluteEpochAdvance advance_absolute_epoch(
        uint64_t now_ns, uint64_t next_deadline_ns, uint64_t period_ns) {
    if (period_ns == 0) throw std::invalid_argument("absolute epoch period must be positive");
    AbsoluteEpochAdvance result;
    result.next_deadline_ns = next_deadline_ns;
    if (now_ns < next_deadline_ns) return result;
    result.lag_ns = saturating_elapsed_ns(now_ns, next_deadline_ns);
    result.missed = result.lag_ns / period_ns;
    result.scheduled = result.missed + 1;
    if (result.scheduled > (std::numeric_limits<uint64_t>::max() - next_deadline_ns)
            / period_ns) {
        result.next_deadline_ns = std::numeric_limits<uint64_t>::max();
    } else {
        result.next_deadline_ns = next_deadline_ns + result.scheduled * period_ns;
    }
    return result;
}

CpuWorkMeasurement execute_thread_cpu_work(double target_cpu_us) {
    if (!std::isfinite(target_cpu_us) || target_cpu_us < 0.0)
        throw std::invalid_argument("thread CPU work target must be finite and non-negative");
    CpuWorkMeasurement result;
    result.target_cpu_us = target_cpu_us;
    if (target_cpu_us == 0.0) return result;
#if defined(__linux__)
    timespec cpu_start{};
    timespec cpu_now{};
    const auto wall_start = std::chrono::steady_clock::now();
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_start) != 0)
        throw std::runtime_error("CLOCK_THREAD_CPUTIME_ID start failed");
    const uint64_t start_ns = timespec_ns(cpu_start);
    const uint64_t target_ns = static_cast<uint64_t>(std::ceil(target_cpu_us * 1000.0));
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    do {
        state ^= state << 7U;
        state ^= state >> 9U;
        state *= 0xbf58476d1ce4e5b9ULL;
        std::atomic_signal_fence(std::memory_order_seq_cst);
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &cpu_now) != 0)
            throw std::runtime_error("CLOCK_THREAD_CPUTIME_ID read failed");
    } while (saturating_elapsed_ns(timespec_ns(cpu_now), start_ns) < target_ns);
    const auto wall_end = std::chrono::steady_clock::now();
    result.actual_cpu_us = static_cast<double>(
        saturating_elapsed_ns(timespec_ns(cpu_now), start_ns)) / 1000.0;
    result.wall_us = std::chrono::duration<double, std::micro>(
        wall_end - wall_start).count();
    if (state == 0) std::atomic_signal_fence(std::memory_order_seq_cst);
#else
    const auto wall_start = std::chrono::steady_clock::now();
    const auto deadline = wall_start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double, std::micro>(target_cpu_us));
    while (std::chrono::steady_clock::now() < deadline)
        std::atomic_signal_fence(std::memory_order_seq_cst);
    result.wall_us = std::chrono::duration<double, std::micro>(
        std::chrono::steady_clock::now() - wall_start).count();
    result.actual_cpu_us = result.wall_us;
#endif
    return result;
}

bool pin_current_thread_to_cpu(int cpu_id) noexcept {
#if defined(__linux__)
    if (cpu_id < 0 || cpu_id >= CPU_SETSIZE) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu_id, &set);
    return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
    (void)cpu_id;
    return false;
#endif
}

std::vector<int> process_allowed_cpu_ids() {
    std::vector<int> cpus;
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    if (sched_getaffinity(0, sizeof(set), &set) == 0) {
        for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            if (CPU_ISSET(cpu, &set)) cpus.push_back(cpu);
    }
#endif
    if (cpus.empty()) {
        const unsigned count = std::max(1U, std::thread::hardware_concurrency());
        for (unsigned cpu = 0; cpu < count; ++cpu) cpus.push_back(static_cast<int>(cpu));
    }
    return cpus;
}

std::vector<CpuTopologyEntry> load_linux_cpu_topology() {
    std::vector<CpuTopologyEntry> result;
#if defined(__linux__)
    const std::filesystem::path root("/sys/devices/system/cpu");
    for (const auto& item : std::filesystem::directory_iterator(root)) {
        if (!item.is_directory()) continue;
        const std::string name = item.path().filename().string();
        if (name.size() <= 3 || name.rfind("cpu", 0) != 0
            || !std::all_of(name.begin() + 3, name.end(), ::isdigit)) continue;
        CpuTopologyEntry entry;
        entry.logical_cpu = std::stoi(name.substr(3));
        entry.socket_id = read_int_file(item.path() / "topology/physical_package_id");
        entry.core_id = read_int_file(item.path() / "topology/core_id");
        const auto online_path = item.path() / "online";
        entry.online = !std::filesystem::exists(online_path)
            || read_int_file(online_path) == 1;
        result.push_back(entry);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.logical_cpu < rhs.logical_cpu;
    });
#endif
    if (result.empty()) throw std::runtime_error("CPU topology unavailable");
    return result;
}

TopologyValidationResult validate_cpu_topology(
        const std::vector<CpuTopologyEntry>& topology,
        const TopologyValidationRequest& request) {
    TopologyValidationResult result;
    std::unordered_map<int, CpuTopologyEntry> by_cpu;
    for (const auto& entry : topology) by_cpu.emplace(entry.logical_cpu, entry);

    std::set<int> worker_logical;
    std::set<std::pair<int, int>> worker_cores;
    for (int cpu : request.worker_cpus) {
        if (!worker_logical.insert(cpu).second) {
            result.errors.push_back("duplicate worker logical CPU " + std::to_string(cpu));
            continue;
        }
        const auto found = by_cpu.find(cpu);
        if (found == by_cpu.end()) {
            result.errors.push_back("worker CPU " + std::to_string(cpu) + " is absent");
            continue;
        }
        if (!found->second.online)
            result.errors.push_back("worker CPU " + std::to_string(cpu) + " is offline");
        const auto core = std::make_pair(found->second.socket_id, found->second.core_id);
        if (!worker_cores.insert(core).second)
            result.errors.push_back("worker CPU " + std::to_string(cpu)
                + " duplicates physical " + core_name(found->second));
    }

    auto validate_nonworker = [&](const std::vector<int>& cpus, const char* role) {
        std::set<int> seen;
        for (int cpu : cpus) {
            if (!seen.insert(cpu).second) {
                result.errors.push_back(std::string("duplicate ") + role + " CPU "
                    + std::to_string(cpu));
                continue;
            }
            const auto found = by_cpu.find(cpu);
            if (found == by_cpu.end()) {
                result.errors.push_back(std::string(role) + " CPU "
                    + std::to_string(cpu) + " is absent");
                continue;
            }
            if (!found->second.online)
                result.errors.push_back(std::string(role) + " CPU "
                    + std::to_string(cpu) + " is offline");
            if (worker_logical.count(cpu) != 0) {
                result.errors.push_back(std::string(role) + " CPU "
                    + std::to_string(cpu) + " overlaps a worker logical CPU");
                continue;
            }
            const auto core = std::make_pair(found->second.socket_id, found->second.core_id);
            if (worker_cores.count(core) != 0
                && !request.allow_control_irq_smt_siblings) {
                result.errors.push_back(std::string(role) + " CPU "
                    + std::to_string(cpu) + " is an SMT sibling of a worker on "
                    + core_name(found->second));
            }
        }
    };
    validate_nonworker(request.control_cpus, "control");
    validate_nonworker(request.irq_cpus, "IRQ");

    std::set<int> control_logical(
        request.control_cpus.begin(), request.control_cpus.end());
    for (int cpu : request.irq_cpus) {
        if (control_logical.count(cpu) != 0) {
            result.errors.push_back("IRQ CPU " + std::to_string(cpu)
                + " overlaps a control logical CPU");
        }
    }

    if (!request.allow_control_irq_smt_siblings) {
        std::set<std::pair<int, int>> control_cores;
        for (int cpu : request.control_cpus) {
            const auto found = by_cpu.find(cpu);
            if (found != by_cpu.end()) {
                control_cores.emplace(found->second.socket_id, found->second.core_id);
            }
        }
        for (int cpu : request.irq_cpus) {
            const auto found = by_cpu.find(cpu);
            if (found == by_cpu.end() || control_logical.count(cpu) != 0) continue;
            const auto core = std::make_pair(
                found->second.socket_id, found->second.core_id);
            if (control_cores.count(core) != 0) {
                result.errors.push_back("IRQ CPU " + std::to_string(cpu)
                    + " is an SMT sibling of a control CPU on "
                    + core_name(found->second));
            }
        }
    }

    result.pass = result.errors.empty();
    result.classification = result.pass ? "PASS" : "INFRASTRUCTURE_FAILURE";
    return result;
}

} // namespace physical
