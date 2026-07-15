#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#include <sys/resource.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::string scenario = "same-core";
    std::string output_path;
    std::string samples_path;
    int source_cpu = -1;
    int target_cpu = -1;
    int contention_cpu = -1;
    int iterations = 20000;
    int warmup = 2000;
    bool contended = false;
    bool allow_affinity_failure = false;
    bool help = false;
};

struct ContextSwitches {
    int64_t voluntary = 0;
    int64_t involuntary = 0;
};

uint64_t read_cycles() {
#if defined(__x86_64__) || defined(_M_X64)
    unsigned low = 0;
    unsigned high = 0;
    __asm__ volatile("rdtscp" : "=a"(low), "=d"(high) :: "rcx");
    return (static_cast<uint64_t>(high) << 32U) | low;
#else
    return 0;
#endif
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

ContextSwitches context_switches() {
    ContextSwitches result;
#if defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        result.voluntary = usage.ru_nvcsw;
        result.involuntary = usage.ru_nivcsw;
    }
#endif
    return result;
}

double percentile(std::vector<uint64_t> values, double quantile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const double rank = std::ceil(quantile * static_cast<double>(values.size()));
    const size_t index = static_cast<size_t>(std::max(1.0, rank)) - 1;
    return static_cast<double>(values[std::min(index, values.size() - 1)]);
}

std::string option_value(int& index, int argc, char** argv,
                         const std::string& argument, const std::string& key) {
    const std::string prefix = key + "=";
    if (argument.rfind(prefix, 0) == 0) return argument.substr(prefix.size());
    if (index + 1 >= argc) throw std::runtime_error("missing value for " + key);
    return argv[++index];
}

void print_usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " --output FILE.csv --source-cpu N [options]\n\n"
        << "Runs a pinned descriptor handoff microbenchmark. This is host-local\n"
        << "preflight evidence, not RPC, NIC, or CloudLab application evidence.\n\n"
        << "  --scenario NAME          same-core, same-socket, or cross-numa\n"
        << "  --source-cpu N           Source CPU ID\n"
        << "  --target-cpu N           Target CPU ID for cross-thread scenarios\n"
        << "  --contention-cpu N       CPU for the mutex contender\n"
        << "  --contended              Add a concurrent queue-lock contender\n"
        << "  --iterations N           Recorded operations (default: 20000)\n"
        << "  --warmup N               Discarded operations (default: 2000)\n"
        << "  --samples FILE.csv       Optional per-operation samples\n"
        << "  --allow-affinity-failure Record affinity failure instead of aborting\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            options.help = true;
        } else if (argument == "--scenario" || argument.rfind("--scenario=", 0) == 0) {
            options.scenario = option_value(index, argc, argv, argument, "--scenario");
        } else if (argument == "--output" || argument.rfind("--output=", 0) == 0) {
            options.output_path = option_value(index, argc, argv, argument, "--output");
        } else if (argument == "--samples" || argument.rfind("--samples=", 0) == 0) {
            options.samples_path = option_value(index, argc, argv, argument, "--samples");
        } else if (argument == "--source-cpu"
                   || argument.rfind("--source-cpu=", 0) == 0) {
            options.source_cpu = std::stoi(option_value(
                index, argc, argv, argument, "--source-cpu"));
        } else if (argument == "--target-cpu"
                   || argument.rfind("--target-cpu=", 0) == 0) {
            options.target_cpu = std::stoi(option_value(
                index, argc, argv, argument, "--target-cpu"));
        } else if (argument == "--contention-cpu"
                   || argument.rfind("--contention-cpu=", 0) == 0) {
            options.contention_cpu = std::stoi(option_value(
                index, argc, argv, argument, "--contention-cpu"));
        } else if (argument == "--iterations"
                   || argument.rfind("--iterations=", 0) == 0) {
            options.iterations = std::stoi(option_value(
                index, argc, argv, argument, "--iterations"));
        } else if (argument == "--warmup" || argument.rfind("--warmup=", 0) == 0) {
            options.warmup = std::stoi(option_value(
                index, argc, argv, argument, "--warmup"));
        } else if (argument == "--contended") {
            options.contended = true;
        } else if (argument == "--allow-affinity-failure") {
            options.allow_affinity_failure = true;
        } else {
            throw std::runtime_error("unknown option: " + argument);
        }
    }
    if (options.help) return options;
    if (options.output_path.empty()) throw std::runtime_error("--output is required");
    if (options.source_cpu < 0) throw std::runtime_error("--source-cpu is required");
    if (options.scenario != "same-core" && options.scenario != "same-socket"
        && options.scenario != "cross-numa")
        throw std::runtime_error("unknown scenario: " + options.scenario);
    if (options.scenario != "same-core" && options.target_cpu < 0)
        throw std::runtime_error("cross-thread scenarios require --target-cpu");
    if (options.scenario != "same-core" && options.target_cpu == options.source_cpu)
        throw std::runtime_error("source and target CPUs must differ");
    if (options.contended && options.contention_cpu < 0)
        throw std::runtime_error("--contended requires --contention-cpu");
    if (options.iterations < 1 || options.warmup < 0)
        throw std::runtime_error("iterations must be positive and warmup non-negative");
    return options;
}

struct Measurements {
    std::vector<uint64_t> duration_ns;
    std::vector<uint64_t> cycles;
    bool source_affinity_ok = false;
    bool target_affinity_ok = true;
    bool contention_affinity_ok = true;
    ContextSwitches context_delta;
};

Measurements run_same_core(const Options& options) {
    Measurements result;
    result.source_affinity_ok = pin_current_thread(options.source_cpu);
    std::deque<uint64_t> queue;
    const ContextSwitches before = context_switches();
    const int total = options.warmup + options.iterations;
    result.duration_ns.reserve(static_cast<size_t>(options.iterations));
    result.cycles.reserve(static_cast<size_t>(options.iterations));
    for (int iteration = 0; iteration < total; ++iteration) {
        const auto start = Clock::now();
        const uint64_t cycles_start = read_cycles();
        queue.push_back(static_cast<uint64_t>(iteration));
        const uint64_t value = queue.front();
        queue.pop_front();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        const uint64_t cycles_end = read_cycles();
        const auto end = Clock::now();
        if (value != static_cast<uint64_t>(iteration))
            throw std::runtime_error("same-core queue integrity failure");
        if (iteration >= options.warmup) {
            result.duration_ns.push_back(static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
            result.cycles.push_back(cycles_end >= cycles_start
                ? cycles_end - cycles_start : 0);
        }
    }
    const ContextSwitches after = context_switches();
    result.context_delta.voluntary = after.voluntary - before.voluntary;
    result.context_delta.involuntary = after.involuntary - before.involuntary;
    return result;
}

Measurements run_cross_thread(const Options& options) {
    struct Token {
        uint64_t sequence = 0;
        Clock::time_point enqueue_time;
        uint64_t enqueue_cycles = 0;
    };
    struct Shared {
        std::mutex mutex;
        std::condition_variable has_item;
        std::condition_variable acknowledged;
        std::deque<Token> queue;
        uint64_t last_ack = 0;
        bool ready = false;
        bool stop_contender = false;
    } shared;

    Measurements result;
    result.source_affinity_ok = pin_current_thread(options.source_cpu);
    const int total = options.warmup + options.iterations;
    result.duration_ns.reserve(static_cast<size_t>(options.iterations));
    result.cycles.reserve(static_cast<size_t>(options.iterations));

    std::thread target([&] {
        result.target_affinity_ok = pin_current_thread(options.target_cpu);
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.ready = true;
            shared.acknowledged.notify_all();
        }
        for (int iteration = 0; iteration < total; ++iteration) {
            Token token;
            {
                std::unique_lock<std::mutex> lock(shared.mutex);
                shared.has_item.wait(lock, [&] { return !shared.queue.empty(); });
                token = shared.queue.front();
                shared.queue.pop_front();
            }
            const uint64_t cycles_end = read_cycles();
            const auto end = Clock::now();
            if (iteration >= options.warmup) {
                result.duration_ns.push_back(static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        end - token.enqueue_time).count()));
                result.cycles.push_back(cycles_end >= token.enqueue_cycles
                    ? cycles_end - token.enqueue_cycles : 0);
            }
            {
                std::lock_guard<std::mutex> lock(shared.mutex);
                shared.last_ack = token.sequence;
                shared.acknowledged.notify_one();
            }
        }
    });

    {
        std::unique_lock<std::mutex> lock(shared.mutex);
        shared.acknowledged.wait(lock, [&] { return shared.ready; });
    }

    std::thread contender;
    if (options.contended) {
        contender = std::thread([&] {
            result.contention_affinity_ok = pin_current_thread(options.contention_cpu);
            while (true) {
                {
                    std::lock_guard<std::mutex> lock(shared.mutex);
                    if (shared.stop_contender) return;
                    std::atomic_signal_fence(std::memory_order_seq_cst);
                }
                std::this_thread::yield();
            }
        });
    }

    const ContextSwitches before = context_switches();
    for (int iteration = 0; iteration < total; ++iteration) {
        Token token;
        token.sequence = static_cast<uint64_t>(iteration + 1);
        token.enqueue_time = Clock::now();
        token.enqueue_cycles = read_cycles();
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.queue.push_back(token);
            shared.has_item.notify_one();
        }
        std::unique_lock<std::mutex> lock(shared.mutex);
        shared.acknowledged.wait(lock, [&] {
            return shared.last_ack == token.sequence;
        });
    }
    const ContextSwitches after = context_switches();
    result.context_delta.voluntary = after.voluntary - before.voluntary;
    result.context_delta.involuntary = after.involuntary - before.involuntary;

    target.join();
    if (contender.joinable()) {
        {
            std::lock_guard<std::mutex> lock(shared.mutex);
            shared.stop_contender = true;
        }
        contender.join();
    }
    return result;
}

void write_results(const Options& options, const Measurements& measurements) {
    const std::filesystem::path output(options.output_path);
    if (output.has_parent_path()) std::filesystem::create_directories(output.parent_path());
    const bool affinity_ok = measurements.source_affinity_ok
        && measurements.target_affinity_ok && measurements.contention_affinity_ok;
    const double mean_ns = measurements.duration_ns.empty() ? 0.0
        : static_cast<double>(std::accumulate(
            measurements.duration_ns.begin(), measurements.duration_ns.end(), uint64_t{0}))
          / static_cast<double>(measurements.duration_ns.size());

    std::ofstream summary(output);
    summary << "evidence_scope,scenario,contention,metric_semantics,source_cpu,"
               "target_cpu,contention_cpu,iterations,affinity_success,mean_us,"
               "P50_us,P95_us,P99_us,P999_us,P50_cycles,P99_cycles,"
               "voluntary_context_switches,involuntary_context_switches,"
               "cache_misses_status\n";
    summary << std::setprecision(17)
            << "local_host_pinned_handoff_microbenchmark_not_rpc_or_network,"
            << options.scenario << ',' << (options.contended ? "contended" : "unloaded")
            << ",source_enqueue_to_target_dequeue_or_local_push_pop,"
            << options.source_cpu << ',' << options.target_cpu << ','
            << options.contention_cpu << ',' << options.iterations << ','
            << (affinity_ok ? 1 : 0) << ',' << mean_ns / 1000.0 << ','
            << percentile(measurements.duration_ns, 0.50) / 1000.0 << ','
            << percentile(measurements.duration_ns, 0.95) / 1000.0 << ','
            << percentile(measurements.duration_ns, 0.99) / 1000.0 << ','
            << percentile(measurements.duration_ns, 0.999) / 1000.0 << ','
            << percentile(measurements.cycles, 0.50) << ','
            << percentile(measurements.cycles, 0.99) << ','
            << measurements.context_delta.voluntary << ','
            << measurements.context_delta.involuntary
            << ",external_perf_stat_required\n";

    if (!options.samples_path.empty()) {
        const std::filesystem::path samples(options.samples_path);
        if (samples.has_parent_path())
            std::filesystem::create_directories(samples.parent_path());
        std::ofstream csv(samples);
        csv << "sample,duration_ns,cycles\n";
        for (size_t index = 0; index < measurements.duration_ns.size(); ++index)
            csv << index + 1 << ',' << measurements.duration_ns[index] << ','
                << measurements.cycles[index] << '\n';
    }

    if (!affinity_ok && !options.allow_affinity_failure)
        throw std::runtime_error("strict CPU affinity setup failed");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        if (options.help) {
            print_usage(argv[0]);
            return 0;
        }
        const Measurements measurements = options.scenario == "same-core"
            ? run_same_core(options) : run_cross_thread(options);
        write_results(options, measurements);
        std::cout << "Pinned handoff microbenchmark: PASS scenario="
                  << options.scenario << " contention="
                  << (options.contended ? "contended" : "unloaded")
                  << " output=" << options.output_path << '\n'
                  << "Scope: host-local primitive timing, not RPC, NIC, or paper physical "
                     "performance evidence.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "handoff microbenchmark error: " << error.what() << '\n';
        return 2;
    }
}
