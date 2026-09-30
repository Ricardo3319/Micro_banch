#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace qbr {
struct Config {
    int hosts = 16, workers = 8, peers = 4;
    double host_us = 2.1, network_us = 3.15, bandwidth_gbps = 25.0;
    double report_us = 20.0, check_us = 5.0;
    double report_cpu_us = 0.08, receive_cpu_us = 0.03;
    double check_cpu_us = 0.05, decision_cpu_us = 0.25, item_cpu_us = 0.015;
    double ingress_cpu_us = 0.02;
    double credit_batch_us = 2.0;
    double pressure_us = 20.0, gain_margin_us = 3.0, admit_us = 40.0;
    double lease_us = 60.0, max_work_us = 800.0;
    int max_batch = 32, max_bytes = 16384, jbsq_depth = 2;
    bool reserve = true, fixed_batch = false;
    uint64_t seed = 11;
    std::array<double, 2> class_mean_us{5.0, 100.0};
    std::array<double, 2> slo_us{40.0, 200.0};
};

struct InputRequest {
    double arrival_us = 0, service_us = 0;
    uint8_t type = 0;
    uint32_t bytes = 64;
};
struct CapacityChange { double time_us; int host; double speed; };
struct Trace {
    double warmup_us = 0, end_us = 0;
    std::vector<InputRequest> requests;
    std::vector<CapacityChange> changes;
    std::string fingerprint;
};

// Only observable/estimated fields cross the policy boundary. Execution engines
// keep actual service demands and residual work out of this type.
struct Summary {
    double work_us = 0, speed = 1, observed_us = 0, input_work_us = 0;
    int count = 0;
    uint64_t input_count = 0;
};

double batch_work_limit(double source_work_us, double source_rate,
                        double target_work_us, double target_rate,
                        double path_us, double margin_us, double cap_us);
Trace read_trace(const std::string& path);
std::string simulate(const Trace& trace, const Config& cfg,
                     const std::string& method, const std::string& outcomes_path = "");
void validate_config(const Config& cfg);
} // namespace qbr
