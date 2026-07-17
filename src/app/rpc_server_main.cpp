#include "physical/rpc_protocol.h"
#include "physical/runtime.h"
#include "physical/trace.h"
#include "sim/workloads/trace.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Options {
    std::string trace_path;
    std::string output_dir;
    std::string bind_address = "0.0.0.0";
    uint16_t port = 9000;
    int idle_timeout_seconds = 10;
    physical::RuntimeConfig runtime;
    bool help = false;
};

struct Peer {
    sockaddr_storage address{};
    socklen_t length = 0;
    uint64_t client_send_ns = 0;
    uint64_t flow_id = 0;
    int ingress_shard = -1;
};

uint64_t steady_ns(const Clock::time_point& origin) {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - origin).count());
}

std::string value_for(int& index, int argc, char** argv,
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
    while (std::getline(stream, item, ',')) cpus.push_back(std::stoi(item));
    if (cpus.empty()) throw std::runtime_error("CPU list is empty");
    return cpus;
}

void usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " --trace FILE --out-dir DIR [options]\n\n"
        << "Runs a real UDP request/response server with SO_REUSEPORT ingress shards.\n"
        << "Service work is trace-driven CPU execution; scheduling sees only method EWMA.\n\n"
        << "Required:\n"
        << "  --trace FILE              Frozen v2/v3 trace shared with client\n"
        << "  --out-dir DIR             New or empty server result directory\n\n"
        << "Network:\n"
        << "  --bind ADDRESS            IPv4 bind address (default: 0.0.0.0)\n"
        << "  --port N                  UDP port (default: 9000)\n"
        << "  --idle-timeout-seconds N  Stop after no new request (default: 10)\n\n"
        << "Runtime:\n"
        << "  --policy NAME             L0_RandomCore, L1_WorkStealingPolling,\n"
        << "                            M0_AltoThreshold, M1_RescueSched\n"
        << "  --workers N               Ingress shard and worker count (default: 16)\n"
        << "  --cpus A,B,...            Explicit worker CPU list\n"
        << "  --allow-affinity-failure  Continue if worker pinning fails\n"
        << "  --warmup-requests N       Trace prefix excluded from metrics\n"
        << "  --check-period-us X       Scheduler period (default: 100)\n"
        << "  --scan-depth N            Queue scan bound (default: 64)\n"
        << "  --k-candidates N          Candidate bound (default: 16)\n"
        << "  --h-targets N             Target bound (default: 4)\n"
        << "  --moves-per-check N       Migration commit bound (default: 1)\n"
        << "  --epsilon-us X            Remote-feasibility margin (default: 2)\n"
        << "  --handoff-estimate-us X   Decision estimate (default: 0.5)\n"
        << "  --host-overhead-us X      Additional real CPU work (default: 2.1)\n"
        << "  --ewma-alpha X            Completion-updated EWMA alpha (default: 0.05)\n"
        << "  --alto-threshold-us X     ALTO queue threshold (default: 40)\n"
        << "  --alto-min-gain-us X      ALTO minimum gain (default: 0)\n"
        << "  --workload-label TEXT --rho-label TEXT --seed-label TEXT\n"
        << "  --repetition N\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    options.runtime.arrival_mode = physical::ArrivalMode::NETWORK_INGRESS;
    options.runtime.policy = physical::PolicyKind::M1_RESCUE_SCHED;
    options.runtime.time_scale = 1.0;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto take = [&](const std::string& key) {
            return value_for(index, argc, argv, arg, key);
        };
        if (arg == "-h" || arg == "--help") options.help = true;
        else if (arg == "--trace" || arg.rfind("--trace=", 0) == 0)
            options.trace_path = take("--trace");
        else if (arg == "--out-dir" || arg.rfind("--out-dir=", 0) == 0)
            options.output_dir = take("--out-dir");
        else if (arg == "--bind" || arg.rfind("--bind=", 0) == 0)
            options.bind_address = take("--bind");
        else if (arg == "--port" || arg.rfind("--port=", 0) == 0)
            options.port = static_cast<uint16_t>(std::stoul(take("--port")));
        else if (arg == "--idle-timeout-seconds"
                 || arg.rfind("--idle-timeout-seconds=", 0) == 0)
            options.idle_timeout_seconds = std::stoi(take("--idle-timeout-seconds"));
        else if (arg == "--policy" || arg.rfind("--policy=", 0) == 0)
            options.runtime.policy = physical::parse_policy(take("--policy"));
        else if (arg == "--workers" || arg.rfind("--workers=", 0) == 0)
            options.runtime.worker_count = std::stoi(take("--workers"));
        else if (arg == "--cpus" || arg.rfind("--cpus=", 0) == 0)
            options.runtime.cpu_ids = parse_cpu_list(take("--cpus"));
        else if (arg == "--allow-affinity-failure") options.runtime.strict_affinity = false;
        else if (arg == "--warmup-requests" || arg.rfind("--warmup-requests=", 0) == 0)
            options.runtime.warmup_requests = std::stoi(take("--warmup-requests"));
        else if (arg == "--check-period-us" || arg.rfind("--check-period-us=", 0) == 0)
            options.runtime.check_period_us = std::stod(take("--check-period-us"));
        else if (arg == "--scan-depth" || arg.rfind("--scan-depth=", 0) == 0)
            options.runtime.scan_depth = std::stoi(take("--scan-depth"));
        else if (arg == "--k-candidates" || arg.rfind("--k-candidates=", 0) == 0)
            options.runtime.max_candidates = std::stoi(take("--k-candidates"));
        else if (arg == "--h-targets" || arg.rfind("--h-targets=", 0) == 0)
            options.runtime.target_count = std::stoi(take("--h-targets"));
        else if (arg == "--moves-per-check" || arg.rfind("--moves-per-check=", 0) == 0)
            options.runtime.moves_per_check = std::stoi(take("--moves-per-check"));
        else if (arg == "--epsilon-us" || arg.rfind("--epsilon-us=", 0) == 0)
            options.runtime.epsilon_us = std::stod(take("--epsilon-us"));
        else if (arg == "--handoff-estimate-us"
                 || arg.rfind("--handoff-estimate-us=", 0) == 0)
            options.runtime.handoff_estimate_us = std::stod(take("--handoff-estimate-us"));
        else if (arg == "--host-overhead-us" || arg.rfind("--host-overhead-us=", 0) == 0)
            options.runtime.host_overhead_us = std::stod(take("--host-overhead-us"));
        else if (arg == "--ewma-alpha" || arg.rfind("--ewma-alpha=", 0) == 0)
            options.runtime.ewma_alpha = std::stod(take("--ewma-alpha"));
        else if (arg == "--alto-threshold-us"
                 || arg.rfind("--alto-threshold-us=", 0) == 0)
            options.runtime.alto_queue_threshold_us = std::stod(take("--alto-threshold-us"));
        else if (arg == "--alto-min-gain-us"
                 || arg.rfind("--alto-min-gain-us=", 0) == 0)
            options.runtime.alto_min_gain_us = std::stod(take("--alto-min-gain-us"));
        else if (arg == "--workload-label" || arg.rfind("--workload-label=", 0) == 0)
            options.runtime.workload_label = take("--workload-label");
        else if (arg == "--rho-label" || arg.rfind("--rho-label=", 0) == 0)
            options.runtime.rho_label = take("--rho-label");
        else if (arg == "--seed-label" || arg.rfind("--seed-label=", 0) == 0)
            options.runtime.seed_label = take("--seed-label");
        else if (arg == "--repetition" || arg.rfind("--repetition=", 0) == 0)
            options.runtime.repetition = std::stoi(take("--repetition"));
        else throw std::runtime_error("unknown option: " + arg);
    }
    if (!options.help && (options.trace_path.empty() || options.output_dir.empty()))
        throw std::runtime_error("--trace and --out-dir are required");
    if (options.port == 0 || options.idle_timeout_seconds <= 0)
        throw std::runtime_error("port and timeout must be positive");
    options.runtime.output_dir = options.output_dir;
    return options;
}

int make_reuseport_socket(const Options& options) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) throw std::runtime_error("socket failed: " + std::string(std::strerror(errno)));
    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled)) != 0) {
        ::close(fd);
        throw std::runtime_error("SO_REUSEPORT failed: " + std::string(std::strerror(errno)));
    }
    timeval timeout{0, 200000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    if (inet_pton(AF_INET, options.bind_address.c_str(), &address.sin_addr) != 1) {
        ::close(fd);
        throw std::runtime_error("invalid IPv4 bind address");
    }
    if (bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        const std::string message = std::strerror(errno);
        ::close(fd);
        throw std::runtime_error("bind failed: " + message);
    }
    return fd;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parse_options(argc, argv);
        if (options.help) {
            usage(argv[0]);
            return 0;
        }
        namespace fs = std::filesystem;
        if (fs::exists(options.output_dir) && !fs::is_empty(options.output_dir))
            throw std::runtime_error("output directory must be new or empty");
        fs::create_directories(options.output_dir);

        auto trace = physical::FrozenTrace::load_csv(
            options.trace_path, options.runtime.worker_count);
        if (trace.version() != sim::RESCUE_FLOW_TRACE_VERSION)
            throw std::runtime_error("physical RPC server requires v3 flow-affine trace");
        const uint64_t expected_requests = trace.entries().size();
        physical::PhysicalRuntime runtime(std::move(trace), options.runtime);

        std::vector<int> sockets;
        sockets.reserve(static_cast<size_t>(options.runtime.worker_count));
        for (int shard = 0; shard < options.runtime.worker_count; ++shard)
            sockets.push_back(make_reuseport_socket(options));

        const auto process_origin = Clock::now();
        std::mutex peer_mutex;
        std::unordered_map<uint64_t, Peer> peers;
        std::atomic<uint64_t> accepted{0};
        std::atomic<uint64_t> invalid_packets{0};
        std::atomic<uint64_t> duplicate_packets{0};
        std::atomic<uint64_t> responses_sent{0};
        std::atomic<uint64_t> response_send_failures{0};
        std::atomic<uint64_t> last_receive_ns{steady_ns(process_origin)};
        std::atomic<bool> stop_receivers{false};
        std::condition_variable receive_cv;
        std::mutex receive_mutex;

        runtime.set_completion_callback([&](const physical::RequestOutcome& outcome) {
            Peer peer;
            {
                std::lock_guard<std::mutex> lock(peer_mutex);
                const auto found = peers.find(outcome.id);
                if (found == peers.end()) return;
                peer = found->second;
                peers.erase(found);
            }
            const auto response = physical::rpc::make_response(
                outcome.id, peer.flow_id, peer.client_send_ns,
                static_cast<uint64_t>(outcome.planned_arrival_us * 1000.0),
                static_cast<uint64_t>(outcome.start_us * 1000.0),
                static_cast<uint64_t>(outcome.finish_us * 1000.0),
                static_cast<uint32_t>(peer.ingress_shard),
                static_cast<uint32_t>(outcome.final_core), outcome.migration_count,
                outcome.deadline_violation);
            const ssize_t sent = sendto(sockets[static_cast<size_t>(peer.ingress_shard)],
                &response, sizeof(response), 0,
                reinterpret_cast<const sockaddr*>(&peer.address), peer.length);
            if (sent == static_cast<ssize_t>(sizeof(response))) {
                responses_sent.fetch_add(1, std::memory_order_relaxed);
            } else {
                response_send_failures.fetch_add(1, std::memory_order_relaxed);
            }
        });

        runtime.start_network_ingress();
        std::vector<std::thread> receivers;
        for (int shard = 0; shard < options.runtime.worker_count; ++shard) {
            receivers.emplace_back([&, shard] {
                while (!stop_receivers.load(std::memory_order_acquire)) {
                    physical::rpc::RequestWire request{};
                    Peer peer;
                    peer.length = sizeof(peer.address);
                    const ssize_t bytes = recvfrom(sockets[static_cast<size_t>(shard)],
                        &request, sizeof(request), 0,
                        reinterpret_cast<sockaddr*>(&peer.address), &peer.length);
                    if (bytes < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                            continue;
                        invalid_packets.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    uint64_t request_id = 0;
                    uint64_t flow_id = 0;
                    if (bytes != static_cast<ssize_t>(sizeof(request))
                        || !physical::rpc::decode_request(
                            request, &request_id, &flow_id, &peer.client_send_ns)) {
                        invalid_packets.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    peer.flow_id = flow_id;
                    peer.ingress_shard = shard;
                    {
                        std::lock_guard<std::mutex> lock(peer_mutex);
                        if (!peers.emplace(request_id, peer).second) {
                            duplicate_packets.fetch_add(1, std::memory_order_relaxed);
                            continue;
                        }
                    }
                    const auto submit = runtime.submit_network_request(
                        request_id, flow_id, shard);
                    if (submit != physical::NetworkSubmitStatus::ACCEPTED) {
                        std::lock_guard<std::mutex> lock(peer_mutex);
                        peers.erase(request_id);
                        if (submit == physical::NetworkSubmitStatus::DUPLICATE_OR_TERMINAL)
                            duplicate_packets.fetch_add(1, std::memory_order_relaxed);
                        else
                            invalid_packets.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }
                    accepted.fetch_add(1, std::memory_order_relaxed);
                    last_receive_ns.store(
                        steady_ns(process_origin), std::memory_order_release);
                    receive_cv.notify_all();
                }
            });
        }

        std::cout << "RPC_SERVER_READY port=" << options.port
                  << " workers=" << options.runtime.worker_count
                  << " expected_requests=" << expected_requests << std::endl;

        {
            std::unique_lock<std::mutex> lock(receive_mutex);
            while (accepted.load(std::memory_order_acquire) < expected_requests) {
                receive_cv.wait_for(lock, std::chrono::milliseconds(200));
                const uint64_t idle_ns = steady_ns(process_origin)
                    - last_receive_ns.load(std::memory_order_acquire);
                if (idle_ns > static_cast<uint64_t>(options.idle_timeout_seconds)
                        * 1000000000ULL)
                    break;
            }
        }
        stop_receivers.store(true, std::memory_order_release);
        for (auto& thread : receivers) thread.join();
        const auto result = runtime.finish_network_ingress(true);
        runtime.write_outputs(result);
        for (int fd : sockets) ::close(fd);

        const bool pass = result.summary.invariants_pass
            && accepted.load() == expected_requests
            && responses_sent.load() == expected_requests
            && invalid_packets.load() == 0
            && duplicate_packets.load() == 0
            && response_send_failures.load() == 0;
        std::ofstream status(fs::path(options.output_dir) / "RPC_SERVER_STATUS.txt");
        status << "status=" << (pass ? "PASS" : "FAIL") << '\n'
               << "expected_requests=" << expected_requests << '\n'
               << "accepted_requests=" << accepted.load() << '\n'
               << "responses_sent=" << responses_sent.load() << '\n'
               << "invalid_packets=" << invalid_packets.load() << '\n'
               << "duplicate_packets=" << duplicate_packets.load() << '\n'
               << "response_send_failures=" << response_send_failures.load() << '\n'
               << "runtime_invariants_pass=" << (result.summary.invariants_pass ? 1 : 0)
               << '\n';
        std::cout << "RPC server " << (pass ? "PASS" : "FAIL")
                  << " accepted=" << accepted.load()
                  << " responses=" << responses_sent.load()
                  << " output=" << options.output_dir << '\n';
        return pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "RPC server error: " << error.what() << '\n';
        return 2;
    }
}
