#include "physical/runtime.h"
#include "physical/trace.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Options {
    std::string trace_path;
    std::string output_dir;
    physical::RuntimeConfig runtime;
    bool help = false;
};

std::string option_value(int& index, int argc, char** argv,
                         const std::string& argument, const std::string& key) {
    const std::string prefix = key + "=";
    if (argument.rfind(prefix, 0) == 0) return argument.substr(prefix.size());
    if (index + 1 >= argc) throw std::runtime_error("missing value for " + key);
    return argv[++index];
}

std::vector<int> parse_cpu_list(const std::string& value) {
    std::vector<int> cpus;
    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (item.empty()) throw std::runtime_error("empty CPU ID");
        size_t consumed = 0;
        int cpu = std::stoi(item, &consumed);
        if (consumed != item.size() || cpu < 0)
            throw std::runtime_error("invalid CPU ID: " + item);
        cpus.push_back(cpu);
    }
    if (cpus.empty()) throw std::runtime_error("CPU list is empty");
    return cpus;
}

void print_usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " --trace FILE.csv --out-dir DIR [options]\n\n"
        << "Runs an in-process pinned-worker synthetic request runtime. It is not an\n"
        << "RPC server and its output is not CloudLab or paper physical evidence.\n"
        << "Evidence scope: local implementation validation only.\n\n"
        << "Required:\n"
        << "  --trace FILE.csv          Frozen simulator trace v2/v3\n"
        << "  --out-dir DIR             New or empty result directory\n\n"
        << "Runtime:\n"
        << "  --policy NAME             L0_RandomCore, L1_WorkStealingPolling,\n"
        << "                            M0_AltoThreshold, or M1_RescueSched\n"
        << "  --workers N               Worker count (default: 16)\n"
        << "  --cpus A,B,...            Explicit unique CPU affinity list\n"
        << "  --allow-affinity-failure  Continue if pinning is unavailable\n"
        << "  --warmup-requests N       Excluded prefix (default: 0)\n"
        << "  --time-scale X            Wall/logical time multiplier (default: 1)\n"
        << "  --check-period-us X       Scheduler period (default: 100)\n"
        << "  --scan-depth N            Bounded queue prefix (default: 64)\n"
        << "  --k-candidates N          Candidate bound (default: 16)\n"
        << "  --h-targets N             Target bound (default: 4)\n"
        << "  --moves-per-check N       Commit bound (default: 1)\n"
        << "  --epsilon-us X            Rescue feasibility margin (default: 2)\n"
        << "  --handoff-estimate-us X   Policy estimate only (default: 0.5)\n"
        << "  --host-overhead-us X      Synthetic execution overhead (default: 2.1)\n"
        << "  --ewma-alpha X            Completion-updated EWMA alpha (default: 0.05)\n"
        << "  --alto-threshold-us X     ALTO source work threshold (default: 40)\n"
        << "  --alto-min-gain-us X      ALTO minimum predicted gain (default: 0)\n"
        << "\nAudit labels:\n"
        << "  --workload-label TEXT     Workload identifier recorded verbatim\n"
        << "  --rho-label TEXT          Offered-load label recorded verbatim\n"
        << "  --seed-label TEXT         Trace/seed label recorded verbatim\n"
        << "  --repetition N            Paired repetition index (default: 0)\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    options.runtime.policy = physical::PolicyKind::M1_RESCUE_SCHED;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            options.help = true;
        } else if (argument == "--trace" || argument.rfind("--trace=", 0) == 0) {
            options.trace_path = option_value(index, argc, argv, argument, "--trace");
        } else if (argument == "--out-dir" || argument.rfind("--out-dir=", 0) == 0) {
            options.output_dir = option_value(index, argc, argv, argument, "--out-dir");
        } else if (argument == "--policy" || argument.rfind("--policy=", 0) == 0) {
            options.runtime.policy = physical::parse_policy(
                option_value(index, argc, argv, argument, "--policy"));
        } else if (argument == "--workers" || argument.rfind("--workers=", 0) == 0) {
            options.runtime.worker_count = std::stoi(
                option_value(index, argc, argv, argument, "--workers"));
        } else if (argument == "--cpus" || argument.rfind("--cpus=", 0) == 0) {
            options.runtime.cpu_ids = parse_cpu_list(
                option_value(index, argc, argv, argument, "--cpus"));
        } else if (argument == "--allow-affinity-failure") {
            options.runtime.strict_affinity = false;
        } else if (argument == "--warmup-requests"
                   || argument.rfind("--warmup-requests=", 0) == 0) {
            options.runtime.warmup_requests = std::stoi(option_value(
                index, argc, argv, argument, "--warmup-requests"));
        } else if (argument == "--time-scale"
                   || argument.rfind("--time-scale=", 0) == 0) {
            options.runtime.time_scale = std::stod(option_value(
                index, argc, argv, argument, "--time-scale"));
        } else if (argument == "--check-period-us"
                   || argument.rfind("--check-period-us=", 0) == 0) {
            options.runtime.check_period_us = std::stod(option_value(
                index, argc, argv, argument, "--check-period-us"));
        } else if (argument == "--scan-depth"
                   || argument.rfind("--scan-depth=", 0) == 0) {
            options.runtime.scan_depth = std::stoi(option_value(
                index, argc, argv, argument, "--scan-depth"));
        } else if (argument == "--k-candidates"
                   || argument.rfind("--k-candidates=", 0) == 0) {
            options.runtime.max_candidates = std::stoi(option_value(
                index, argc, argv, argument, "--k-candidates"));
        } else if (argument == "--h-targets"
                   || argument.rfind("--h-targets=", 0) == 0) {
            options.runtime.target_count = std::stoi(option_value(
                index, argc, argv, argument, "--h-targets"));
        } else if (argument == "--moves-per-check"
                   || argument.rfind("--moves-per-check=", 0) == 0) {
            options.runtime.moves_per_check = std::stoi(option_value(
                index, argc, argv, argument, "--moves-per-check"));
        } else if (argument == "--epsilon-us"
                   || argument.rfind("--epsilon-us=", 0) == 0) {
            options.runtime.epsilon_us = std::stod(option_value(
                index, argc, argv, argument, "--epsilon-us"));
        } else if (argument == "--handoff-estimate-us"
                   || argument.rfind("--handoff-estimate-us=", 0) == 0) {
            options.runtime.handoff_estimate_us = std::stod(option_value(
                index, argc, argv, argument, "--handoff-estimate-us"));
        } else if (argument == "--host-overhead-us"
                   || argument.rfind("--host-overhead-us=", 0) == 0) {
            options.runtime.host_overhead_us = std::stod(option_value(
                index, argc, argv, argument, "--host-overhead-us"));
        } else if (argument == "--ewma-alpha"
                   || argument.rfind("--ewma-alpha=", 0) == 0) {
            options.runtime.ewma_alpha = std::stod(option_value(
                index, argc, argv, argument, "--ewma-alpha"));
        } else if (argument == "--alto-threshold-us"
                   || argument.rfind("--alto-threshold-us=", 0) == 0) {
            options.runtime.alto_queue_threshold_us = std::stod(option_value(
                index, argc, argv, argument, "--alto-threshold-us"));
        } else if (argument == "--alto-min-gain-us"
                   || argument.rfind("--alto-min-gain-us=", 0) == 0) {
            options.runtime.alto_min_gain_us = std::stod(option_value(
                index, argc, argv, argument, "--alto-min-gain-us"));
        } else if (argument == "--workload-label"
                   || argument.rfind("--workload-label=", 0) == 0) {
            options.runtime.workload_label = option_value(
                index, argc, argv, argument, "--workload-label");
        } else if (argument == "--rho-label"
                   || argument.rfind("--rho-label=", 0) == 0) {
            options.runtime.rho_label = option_value(
                index, argc, argv, argument, "--rho-label");
        } else if (argument == "--seed-label"
                   || argument.rfind("--seed-label=", 0) == 0) {
            options.runtime.seed_label = option_value(
                index, argc, argv, argument, "--seed-label");
        } else if (argument == "--repetition"
                   || argument.rfind("--repetition=", 0) == 0) {
            options.runtime.repetition = std::stoi(option_value(
                index, argc, argv, argument, "--repetition"));
        } else {
            throw std::runtime_error("unknown option: " + argument);
        }
    }
    if (!options.help && options.trace_path.empty())
        throw std::runtime_error("--trace is required");
    if (!options.help && options.output_dir.empty())
        throw std::runtime_error("--out-dir is required");
    options.runtime.output_dir = options.output_dir;
    return options;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Options options = parse_options(argc, argv);
        if (options.help) {
            print_usage(argv[0]);
            return 0;
        }
        const std::filesystem::path output(options.output_dir);
        if (std::filesystem::exists(output) && !std::filesystem::is_empty(output))
            throw std::runtime_error("output directory must be new or empty");

        physical::FrozenTrace trace = physical::FrozenTrace::load_csv(
            options.trace_path, options.runtime.worker_count);
        physical::PhysicalRuntime runtime(std::move(trace), options.runtime);
        physical::RuntimeResult result = runtime.run();
        runtime.write_outputs(result);

        std::cout << "Local synthetic runtime "
                  << (result.summary.invariants_pass ? "PASS" : "FAIL")
                  << ": policy=" << physical::policy_name(options.runtime.policy)
                  << " requests=" << result.summary.total_requests
                  << " completed=" << result.summary.completed_requests
                  << " migrations=" << result.summary.migration_count
                  << " output=" << options.output_dir << '\n'
                  << "Evidence scope: local implementation validation only; "
                     "not RPC, CloudLab, or paper physical evidence.\n";
        return result.summary.invariants_pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "physical runtime error: " << error.what() << '\n';
        return 2;
    }
}
