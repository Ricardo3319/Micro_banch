#include "sim/workloads/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::string output;
    sim::TraceConfig trace;
    bool help = false;
};

std::string value_for(int& index, int argc, char** argv,
                      const std::string& argument, const std::string& key) {
    const std::string prefix = key + "=";
    if (argument.rfind(prefix, 0) == 0) return argument.substr(prefix.size());
    if (index + 1 >= argc) throw std::runtime_error("missing value for " + key);
    return argv[++index];
}

sim::WorkloadType parse_workload(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    if (value == "W2") return sim::WorkloadType::W2_MMPP_BIMODAL;
    if (value == "W3") return sim::WorkloadType::W3_POISSON_LOGNORMAL;
    throw std::runtime_error("--workload must be W2 or W3");
}

void usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " --out FILE.csv [options]\n\n"
        << "Generates the frozen flow-affine trace consumed by the physical RPC runtime.\n"
        << "The generator preserves the simulation workload, rho, seed, service-demand,\n"
        << "deadline, flow-affinity, and canonical SHA-256 semantics without shipping the\n"
        << "historical discrete-event simulator.\n\n"
        << "Options:\n"
        << "  --out FILE.csv          Output trace (required)\n"
        << "  --workload W2|W3        Workload model (default: W3)\n"
        << "  --rho X                 Source load label (default: 0.85)\n"
        << "  --seed N                Deterministic seed (default: 11)\n"
        << "  --workers N             Ingress shard count (default: 16)\n"
        << "  --warmup N              Warmup requests (default: 200000)\n"
        << "  --requests N            Measurement requests (default: 1000000)\n"
        << "  --flow-count N          Stable flow population (default: 4096)\n"
        << "  --flow-zipf-alpha X     Flow popularity skew (default: 0)\n"
        << "  --flow-hash-seed N      RSS-like mapping seed (default: 0x52535331)\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    options.trace.placement_mode = sim::PlacementMode::FLOW_AFFINE;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto take = [&](const std::string& key) {
            return value_for(index, argc, argv, arg, key);
        };
        if (arg == "-h" || arg == "--help") options.help = true;
        else if (arg == "--out" || arg.rfind("--out=", 0) == 0)
            options.output = take("--out");
        else if (arg == "--workload" || arg.rfind("--workload=", 0) == 0)
            options.trace.workload = parse_workload(take("--workload"));
        else if (arg == "--rho" || arg.rfind("--rho=", 0) == 0)
            options.trace.rho = std::stod(take("--rho"));
        else if (arg == "--seed" || arg.rfind("--seed=", 0) == 0)
            options.trace.seed = static_cast<unsigned>(std::stoul(take("--seed")));
        else if (arg == "--workers" || arg.rfind("--workers=", 0) == 0)
            options.trace.core_count = std::stoi(take("--workers"));
        else if (arg == "--warmup" || arg.rfind("--warmup=", 0) == 0)
            options.trace.warmup_requests = std::stoi(take("--warmup"));
        else if (arg == "--requests" || arg.rfind("--requests=", 0) == 0)
            options.trace.measurement_requests = std::stoi(take("--requests"));
        else if (arg == "--flow-count" || arg.rfind("--flow-count=", 0) == 0)
            options.trace.flow_count = std::stoi(take("--flow-count"));
        else if (arg == "--flow-zipf-alpha"
                 || arg.rfind("--flow-zipf-alpha=", 0) == 0)
            options.trace.flow_zipf_alpha = std::stod(take("--flow-zipf-alpha"));
        else if (arg == "--flow-hash-seed"
                 || arg.rfind("--flow-hash-seed=", 0) == 0)
            options.trace.flow_hash_seed = static_cast<unsigned>(
                std::stoul(take("--flow-hash-seed"), nullptr, 0));
        else throw std::runtime_error("unknown option: " + arg);
    }
    if (!options.help && options.output.empty())
        throw std::runtime_error("--out is required");
    if (!std::isfinite(options.trace.rho) || !(options.trace.rho > 0.0)
        || options.trace.core_count <= 0 || options.trace.warmup_requests < 0
        || options.trace.measurement_requests <= 0 || options.trace.flow_count <= 0)
        throw std::runtime_error("invalid trace option");
    options.trace.effective_core_capacity = options.trace.core_count;
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        if (options.help) {
            usage(argv[0]);
            return 0;
        }
        const sim::WorkloadTrace trace = sim::WorkloadTrace::generate(options.trace);
        if (!trace.write_csv(options.output))
            throw std::runtime_error("cannot write trace: " + options.output);
        std::cout << "Trace " << trace.version() << " sha256=" << trace.sha256()
                  << " requests=" << trace.entries().size()
                  << " output=" << options.output << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "trace generator error: " << error.what() << '\n';
        return 2;
    }
}
