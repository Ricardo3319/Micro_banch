#include "qbr/model.h"
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        qbr::Config c;
        std::string trace, method = "qbr", outcomes;
        for (int i = 1; i < argc; ++i) {
            std::string k = argv[i];
            if (k == "--help") {
                std::cout << "batchsched --trace FILE --method p2c|jsw|jbsq|steal|threshold|single|qbr\n"
                    "  --hosts N --workers N --seed N --outcomes FILE\n"
                    "  --network-us U --bandwidth-gbps G --report-us U --check-us U\n"
                    "  --decision-cpu-us U --item-cpu-us U --max-batch N --max-bytes N\n"
                    "  --max-work-us U --admit-us U --pressure-us U --lease-us U\n"
                    "  --jbsq-depth N --reserve 0|1 --fixed-batch 0|1 --peers N\n";
                return 0;
            }
            if (i + 1 == argc) throw std::runtime_error("missing value for " + k);
            std::string v = argv[++i];
            if (k == "--trace") trace = v;
            else if (k == "--method") method = v;
            else if (k == "--outcomes") outcomes = v;
            else if (k == "--hosts") c.hosts = std::stoi(v);
            else if (k == "--workers") c.workers = std::stoi(v);
            else if (k == "--peers") c.peers = std::stoi(v);
            else if (k == "--seed") c.seed = std::stoull(v);
            else if (k == "--network-us") c.network_us = std::stod(v);
            else if (k == "--bandwidth-gbps") c.bandwidth_gbps = std::stod(v);
            else if (k == "--report-us") c.report_us = std::stod(v);
            else if (k == "--check-us") c.check_us = std::stod(v);
            else if (k == "--credit-batch-us") c.credit_batch_us = std::stod(v);
            else if (k == "--ingress-cpu-us") c.ingress_cpu_us = std::stod(v);
            else if (k == "--decision-cpu-us") c.decision_cpu_us = std::stod(v);
            else if (k == "--item-cpu-us") c.item_cpu_us = std::stod(v);
            else if (k == "--max-batch") c.max_batch = std::stoi(v);
            else if (k == "--max-bytes") c.max_bytes = std::stoi(v);
            else if (k == "--max-work-us") c.max_work_us = std::stod(v);
            else if (k == "--admit-us") c.admit_us = std::stod(v);
            else if (k == "--pressure-us") c.pressure_us = std::stod(v);
            else if (k == "--lease-us") c.lease_us = std::stod(v);
            else if (k == "--jbsq-depth") c.jbsq_depth = std::stoi(v);
            else if (k == "--reserve") c.reserve = std::stoi(v) != 0;
            else if (k == "--fixed-batch") c.fixed_batch = std::stoi(v) != 0;
            else if (k == "--host-us") c.host_us = std::stod(v);
            else if (k == "--mean-short-us") c.class_mean_us[0] = std::stod(v);
            else if (k == "--mean-long-us") c.class_mean_us[1] = std::stod(v);
            else if (k == "--slo-short-us") c.slo_us[0] = std::stod(v);
            else if (k == "--slo-long-us") c.slo_us[1] = std::stod(v);
            else throw std::runtime_error("unknown option " + k);
        }
        if (trace.empty()) throw std::runtime_error("--trace is required");
        qbr::validate_config(c);
        std::cout << qbr::simulate(qbr::read_trace(trace), c, method, outcomes) << '\n';
    } catch (const std::exception& e) {
        std::cerr << "batchsched: " << e.what() << '\n';
        return 1;
    }
}
