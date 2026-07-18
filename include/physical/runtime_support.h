#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace physical {

uint64_t saturating_elapsed_ns(uint64_t now_ns, uint64_t then_ns) noexcept;

struct AbsoluteEpochAdvance {
    uint64_t scheduled = 0;
    uint64_t missed = 0;
    uint64_t lag_ns = 0;
    uint64_t next_deadline_ns = 0;
};

AbsoluteEpochAdvance advance_absolute_epoch(
    uint64_t now_ns, uint64_t next_deadline_ns, uint64_t period_ns);

struct CpuWorkMeasurement {
    double target_cpu_us = 0.0;
    double actual_cpu_us = 0.0;
    double wall_us = 0.0;
};

CpuWorkMeasurement execute_thread_cpu_work(double target_cpu_us);

struct CpuTopologyEntry {
    int logical_cpu = -1;
    int socket_id = -1;
    int core_id = -1;
    bool online = false;
};

struct TopologyValidationRequest {
    std::vector<int> worker_cpus;
    std::vector<int> control_cpus;
    std::vector<int> irq_cpus;
    bool allow_control_irq_smt_siblings = false;
};

struct TopologyValidationResult {
    bool pass = false;
    std::string classification = "INFRASTRUCTURE_FAILURE";
    std::vector<std::string> errors;
};

std::vector<CpuTopologyEntry> load_linux_cpu_topology();
TopologyValidationResult validate_cpu_topology(
    const std::vector<CpuTopologyEntry>& topology,
    const TopologyValidationRequest& request);

bool pin_current_thread_to_cpu(int cpu_id) noexcept;
std::vector<int> process_allowed_cpu_ids();

} // namespace physical
