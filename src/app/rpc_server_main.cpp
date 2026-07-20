#include "physical/bounded_queue.h"
#include "physical/rpc_protocol.h"
#include "physical/runtime.h"
#include "physical/runtime_support.h"
#include "physical/trace.h"
#include "sim/workloads/trace.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
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
    std::vector<int> receiver_cpus;
    std::vector<int> response_sender_cpus;
    int scheduler_cpu = -1;
    int server_main_cpu = -1;
    std::size_t response_queue_capacity = 65536;
    uint64_t inject_response_queue_failure_after = 0;
    physical::RuntimeConfig runtime;
    bool help = false;
};

struct Peer {
    sockaddr_storage address{};
    socklen_t length = 0;
    uint64_t client_send_ns = 0;
    uint64_t flow_id = 0;
    uint64_t server_receive_ns = 0;
    uint16_t source_port = 0;
    std::string source_address;
    int ingress_shard = -1;
};

struct ResponseTask {
    Peer peer;
    physical::RequestOutcome outcome;
};

struct MappingRow {
    uint64_t request_id = 0;
    uint64_t flow_id = 0;
    std::string source_address;
    uint16_t source_port = 0;
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

std::string join_ints(const std::vector<int>& values) {
    std::ostringstream output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index) output << ';';
        output << values[index];
    }
    return output.str();
}

void usage(const char* executable) {
    std::cout
        << "Usage: " << executable << " --trace FILE --out-dir DIR [options]\n\n"
        << "Runs a real UDP request/response server with one SO_REUSEPORT socket per\n"
        << "kernel ingress shard, two epoll receivers, and two response senders.\n\n"
        << "Required:\n"
        << "  --trace FILE --out-dir DIR\n\n"
        << "Network:\n"
        << "  --bind ADDRESS --port N --idle-timeout-seconds N\n"
        << "  --receiver-cpus A,B       Exactly two fixed receiver CPUs\n"
        << "  --response-sender-cpus A,B Exactly two fixed sender CPUs\n"
        << "  --scheduler-cpu N         Dedicated scheduler CPU for M0/M1\n"
        << "  --server-main-cpu N       Pin the server main/control thread\n"
        << "  --response-queue-capacity N (default: 65536)\n\n"
        << "Runtime:\n"
        << "  --policy NAME --workers N --cpus A,B,...\n"
        << "  --irq-cpus A,B,... --allow-control-irq-smt-siblings\n"
        << "  --allow-affinity-failure --warmup-requests N\n"
        << "  --check-period-us X --scan-depth N --k-candidates N --h-targets N\n"
        << "  --moves-per-check N --epsilon-us X --handoff-estimate-us X\n"
        << "  --host-overhead-us X --ewma-alpha X --alto-threshold-us X\n"
        << "  --alto-min-gain-us X --decision-sample-cap N --decision-bucket-us X\n"
        << "  --workload-label TEXT --rho-label TEXT --seed-label TEXT --repetition N\n";
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
        else if (arg == "--receiver-cpus" || arg.rfind("--receiver-cpus=", 0) == 0)
            options.receiver_cpus = parse_cpu_list(take("--receiver-cpus"));
        else if (arg == "--response-sender-cpus"
                 || arg.rfind("--response-sender-cpus=", 0) == 0)
            options.response_sender_cpus = parse_cpu_list(take("--response-sender-cpus"));
        else if (arg == "--scheduler-cpu" || arg.rfind("--scheduler-cpu=", 0) == 0)
            options.scheduler_cpu = std::stoi(take("--scheduler-cpu"));
        else if (arg == "--server-main-cpu" || arg.rfind("--server-main-cpu=", 0) == 0)
            options.server_main_cpu = std::stoi(take("--server-main-cpu"));
        else if (arg == "--response-queue-capacity"
                 || arg.rfind("--response-queue-capacity=", 0) == 0)
            options.response_queue_capacity = std::stoull(take("--response-queue-capacity"));
        else if (arg == "--inject-response-queue-failure-after"
                 || arg.rfind("--inject-response-queue-failure-after=", 0) == 0)
            options.inject_response_queue_failure_after = std::stoull(
                take("--inject-response-queue-failure-after"));
        else if (arg == "--policy" || arg.rfind("--policy=", 0) == 0)
            options.runtime.policy = physical::parse_policy(take("--policy"));
        else if (arg == "--workers" || arg.rfind("--workers=", 0) == 0)
            options.runtime.worker_count = std::stoi(take("--workers"));
        else if (arg == "--cpus" || arg.rfind("--cpus=", 0) == 0)
            options.runtime.cpu_ids = parse_cpu_list(take("--cpus"));
        else if (arg == "--irq-cpus" || arg.rfind("--irq-cpus=", 0) == 0)
            options.runtime.irq_cpu_ids = parse_cpu_list(take("--irq-cpus"));
        else if (arg == "--allow-control-irq-smt-siblings")
            options.runtime.allow_control_irq_smt_siblings = true;
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
        else if (arg == "--decision-sample-cap"
                 || arg.rfind("--decision-sample-cap=", 0) == 0)
            options.runtime.decision_sample_cap = std::stoull(take("--decision-sample-cap"));
        else if (arg == "--decision-bucket-us"
                 || arg.rfind("--decision-bucket-us=", 0) == 0)
            options.runtime.decision_bucket_us = std::stod(take("--decision-bucket-us"));
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
    if (options.port == 0 || options.idle_timeout_seconds <= 0
        || options.response_queue_capacity == 0)
        throw std::runtime_error("port, timeout, and response queue capacity must be positive");
    if (!options.receiver_cpus.empty() && options.receiver_cpus.size() != 2)
        throw std::runtime_error("--receiver-cpus must contain exactly two CPUs");
    if (!options.response_sender_cpus.empty()
        && options.response_sender_cpus.size() != 2)
        throw std::runtime_error("--response-sender-cpus must contain exactly two CPUs");
    if (options.scheduler_cpu < -1 || options.server_main_cpu < -1)
        throw std::runtime_error("scheduler and server-main CPU IDs must be non-negative");

    auto allowed = physical::process_allowed_cpu_ids();
    if (options.runtime.cpu_ids.empty()) {
        if (allowed.size() < static_cast<std::size_t>(options.runtime.worker_count))
            throw std::runtime_error("not enough process-allowed worker CPUs");
        options.runtime.cpu_ids.assign(
            allowed.begin(), allowed.begin() + options.runtime.worker_count);
    }
    const auto contains_cpu = [](const std::vector<int>& cpus, int cpu) {
        return std::find(cpus.begin(), cpus.end(), cpu) != cpus.end();
    };
    std::vector<int> remaining;
    for (int cpu : allowed) {
        if (!contains_cpu(options.runtime.cpu_ids, cpu)
            && !contains_cpu(options.receiver_cpus, cpu)
            && !contains_cpu(options.response_sender_cpus, cpu))
            remaining.push_back(cpu);
    }
    if (options.receiver_cpus.empty()) {
        if (remaining.size() < 2)
            throw std::runtime_error("two dedicated receiver CPUs are unavailable");
        options.receiver_cpus.assign(remaining.begin(), remaining.begin() + 2);
        remaining.erase(remaining.begin(), remaining.begin() + 2);
    }
    if (options.response_sender_cpus.empty()) {
        if (remaining.size() < 2)
            throw std::runtime_error("two dedicated response sender CPUs are unavailable");
        options.response_sender_cpus.assign(remaining.begin(), remaining.begin() + 2);
    }
    options.runtime.control_cpu_ids = options.receiver_cpus;
    options.runtime.control_cpu_ids.insert(options.runtime.control_cpu_ids.end(),
        options.response_sender_cpus.begin(), options.response_sender_cpus.end());
    if (options.scheduler_cpu >= 0)
        options.runtime.control_cpu_ids.push_back(options.scheduler_cpu);
    if (options.server_main_cpu >= 0)
        options.runtime.control_cpu_ids.push_back(options.server_main_cpu);
    options.runtime.scheduler_cpu_id = options.scheduler_cpu;
    options.runtime.output_dir = options.output_dir;
    return options;
}

int make_reuseport_socket(const Options& options) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) throw std::runtime_error("socket failed: " + std::string(std::strerror(errno)));
    int enabled = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled)) != 0) {
        ::close(fd);
        throw std::runtime_error("SO_REUSEPORT failed: " + std::string(std::strerror(errno)));
    }
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

std::string ipv4_peer(const sockaddr_storage& address, uint16_t* source_port) {
    const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(&address);
    char text[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &ipv4->sin_addr, text, sizeof(text)))
        return "INVALID";
    *source_port = ntohs(ipv4->sin_port);
    return text;
}

} // namespace

int main(int argc, char** argv) {
    std::string output_dir_hint;
    bool output_directory_initialized = false;
    try {
        const Options options = parse_options(argc, argv);
        output_dir_hint = options.output_dir;
        if (options.help) {
            usage(argv[0]);
            return 0;
        }
        namespace fs = std::filesystem;
        if (fs::exists(options.output_dir) && !fs::is_empty(options.output_dir))
            throw std::runtime_error("output directory must be new or empty");
        fs::create_directories(options.output_dir);
        output_directory_initialized = true;

        const bool server_main_affinity_ok = options.server_main_cpu < 0
            || physical::pin_current_thread_to_cpu(options.server_main_cpu);
        if (options.runtime.strict_affinity && !server_main_affinity_ok) {
            std::ofstream status(fs::path(options.output_dir) / "RPC_SERVER_STATUS.txt");
            status << "status=FAIL\n"
                   << "classification=INFRASTRUCTURE_FAILURE\n"
                   << "server_main_cpu=" << options.server_main_cpu << '\n'
                   << "server_main_affinity_pass=0\n";
            return 1;
        }

        auto trace = physical::FrozenTrace::load_csv(
            options.trace_path, options.runtime.worker_count);
        if (trace.version() != sim::RESCUE_FLOW_TRACE_VERSION)
            throw std::runtime_error("physical RPC server requires v3 flow-affine trace");
        const uint64_t expected_requests = trace.entries().size();
        physical::PhysicalRuntime runtime(std::move(trace), options.runtime);

        std::vector<int> sockets;
        sockets.reserve(static_cast<std::size_t>(options.runtime.worker_count));
        for (int shard = 0; shard < options.runtime.worker_count; ++shard)
            sockets.push_back(make_reuseport_socket(options));

        const auto process_origin = Clock::now();
        std::mutex peer_mutex;
        std::unordered_map<uint64_t, Peer> peers;
        std::vector<MappingRow> mappings;
        physical::BoundedQueue<ResponseTask> response_queue(
            options.response_queue_capacity);
        std::atomic<uint64_t> accepted{0};
        std::atomic<uint64_t> invalid_packets{0};
        std::atomic<uint64_t> duplicate_packets{0};
        std::atomic<uint64_t> unknown_requests{0};
        std::atomic<uint64_t> flow_mismatch_requests{0};
        std::atomic<uint64_t> responses_enqueued{0};
        std::atomic<uint64_t> response_enqueue_failures{0};
        std::atomic<uint64_t> responses_sent{0};
        std::atomic<uint64_t> response_send_failures{0};
        std::atomic<uint64_t> last_receive_ns{steady_ns(process_origin)};
        std::atomic<uint64_t> receive_generation{0};
        std::atomic<uint64_t> receiver_internal_failures{0};
        std::atomic<bool> stop_receivers{false};
        std::condition_variable receive_cv;
        std::mutex receive_mutex;
        std::condition_variable thread_ready_cv;
        std::mutex thread_ready_mutex;
        int ready_threads = 0;
        std::vector<bool> receiver_affinity_ok(2, false);
        std::vector<bool> sender_affinity_ok(2, false);

        runtime.set_completion_callback([&](const physical::RequestOutcome& outcome) {
            Peer peer;
            {
                std::lock_guard<std::mutex> lock(peer_mutex);
                const auto found = peers.find(outcome.id);
                if (found == peers.end()) return;
                peer = found->second;
                peers.erase(found);
            }
            const uint64_t already = responses_enqueued.load(std::memory_order_relaxed);
            const bool injected = options.inject_response_queue_failure_after != 0
                && already >= options.inject_response_queue_failure_after;
            if (injected || !response_queue.try_push(ResponseTask{peer, outcome})) {
                response_enqueue_failures.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            responses_enqueued.fetch_add(1, std::memory_order_relaxed);
        });
        runtime.start_network_ingress();

        std::vector<std::thread> senders;
        for (int sender_id = 0; sender_id < 2; ++sender_id) {
            senders.emplace_back([&, sender_id] {
                const bool affinity = physical::pin_current_thread_to_cpu(
                    options.response_sender_cpus[static_cast<std::size_t>(sender_id)]);
                {
                    std::lock_guard<std::mutex> lock(thread_ready_mutex);
                    sender_affinity_ok[static_cast<std::size_t>(sender_id)] = affinity;
                    ++ready_threads;
                    thread_ready_cv.notify_all();
                }
                ResponseTask task;
                while (response_queue.wait_pop(&task)) {
                    const auto response = physical::rpc::make_response(
                        task.outcome.id, task.peer.flow_id, task.peer.client_send_ns,
                        task.peer.server_receive_ns,
                        static_cast<uint64_t>(std::max(0.0, task.outcome.start_us) * 1000.0),
                        static_cast<uint64_t>(std::max(0.0, task.outcome.finish_us) * 1000.0),
                        static_cast<uint32_t>(task.peer.ingress_shard),
                        static_cast<uint32_t>(std::max(0, task.outcome.final_core)),
                        task.outcome.migration_count, task.outcome.deadline_violation);
                    const int socket_fd = sockets[static_cast<std::size_t>(
                        task.peer.ingress_shard)];
                    const ssize_t sent = sendto(socket_fd, &response, sizeof(response), 0,
                        reinterpret_cast<const sockaddr*>(&task.peer.address),
                        task.peer.length);
                    if (sent == static_cast<ssize_t>(sizeof(response)))
                        responses_sent.fetch_add(1, std::memory_order_relaxed);
                    else
                        response_send_failures.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        std::vector<std::thread> receivers;
        for (int receiver_id = 0; receiver_id < 2; ++receiver_id) {
            receivers.emplace_back([&, receiver_id] {
                const bool affinity = physical::pin_current_thread_to_cpu(
                    options.receiver_cpus[static_cast<std::size_t>(receiver_id)]);
                const int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
                bool setup_ok = epoll_fd >= 0;
                if (setup_ok) {
                    for (int shard = receiver_id; shard < options.runtime.worker_count;
                         shard += 2) {
                        epoll_event event{};
                        event.events = EPOLLIN;
                        event.data.u32 = static_cast<uint32_t>(shard);
                        if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD,
                                sockets[static_cast<std::size_t>(shard)], &event) != 0) {
                            setup_ok = false;
                            break;
                        }
                    }
                }
                {
                    std::lock_guard<std::mutex> lock(thread_ready_mutex);
                    receiver_affinity_ok[static_cast<std::size_t>(receiver_id)] = affinity;
                    ++ready_threads;
                    thread_ready_cv.notify_all();
                }
                if (!setup_ok) {
                    receiver_internal_failures.fetch_add(1, std::memory_order_relaxed);
                    if (epoll_fd >= 0) close(epoll_fd);
                    receive_cv.notify_all();
                    return;
                }
                std::vector<epoll_event> events(static_cast<std::size_t>(
                    std::max(1, (options.runtime.worker_count + 1) / 2)));
                while (!stop_receivers.load(std::memory_order_acquire)) {
                    const int event_count = epoll_wait(epoll_fd, events.data(),
                        static_cast<int>(events.size()), 100);
                    if (event_count < 0) {
                        if (errno == EINTR) continue;
                        receiver_internal_failures.fetch_add(1, std::memory_order_relaxed);
                        receive_cv.notify_all();
                        break;
                    }
                    for (int event_index = 0; event_index < event_count; ++event_index) {
                        const int shard = static_cast<int>(events[static_cast<std::size_t>(
                            event_index)].data.u32);
                        while (true) {
                            physical::rpc::RequestWire request{};
                            Peer peer;
                            peer.length = sizeof(peer.address);
                            const ssize_t bytes = recvfrom(
                                sockets[static_cast<std::size_t>(shard)],
                                &request, sizeof(request), 0,
                                reinterpret_cast<sockaddr*>(&peer.address), &peer.length);
                            if (bytes < 0) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                                if (errno == EINTR) continue;
                                invalid_packets.fetch_add(1, std::memory_order_relaxed);
                                break;
                            }
                            uint64_t request_id = 0;
                            uint64_t flow_id = 0;
                            if (bytes != static_cast<ssize_t>(sizeof(request))
                                || !physical::rpc::decode_request(
                                    request, &request_id, &flow_id,
                                    &peer.client_send_ns)) {
                                invalid_packets.fetch_add(1, std::memory_order_relaxed);
                                continue;
                            }
                            peer.flow_id = flow_id;
                            peer.ingress_shard = shard;
                            peer.server_receive_ns = steady_ns(process_origin);
                            peer.source_address = ipv4_peer(
                                peer.address, &peer.source_port);
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
                                else if (submit == physical::NetworkSubmitStatus::UNKNOWN_REQUEST)
                                    unknown_requests.fetch_add(1, std::memory_order_relaxed);
                                else if (submit == physical::NetworkSubmitStatus::FLOW_MISMATCH)
                                    flow_mismatch_requests.fetch_add(1, std::memory_order_relaxed);
                                else
                                    invalid_packets.fetch_add(1, std::memory_order_relaxed);
                                continue;
                            }
                            {
                                std::lock_guard<std::mutex> lock(peer_mutex);
                                mappings.push_back(MappingRow{
                                    request_id, flow_id, peer.source_address,
                                    peer.source_port, shard});
                            }
                            accepted.fetch_add(1, std::memory_order_release);
                            last_receive_ns.store(
                                peer.server_receive_ns, std::memory_order_release);
                            receive_generation.fetch_add(1, std::memory_order_acq_rel);
                            receive_cv.notify_all();
                        }
                    }
                }
                close(epoll_fd);
            });
        }

        {
            std::unique_lock<std::mutex> lock(thread_ready_mutex);
            thread_ready_cv.wait(lock, [&] { return ready_threads == 4; });
        }
        const bool receiver_affinity_pass = std::all_of(
            receiver_affinity_ok.begin(), receiver_affinity_ok.end(), [](bool value) {
                return value;
            });
        const bool sender_affinity_pass = std::all_of(
            sender_affinity_ok.begin(), sender_affinity_ok.end(), [](bool value) {
                return value;
            });
        const bool startup_infrastructure_pass = receiver_affinity_pass
            && sender_affinity_pass
            && receiver_internal_failures.load(std::memory_order_acquire) == 0;
        if (!startup_infrastructure_pass) {
            stop_receivers.store(true, std::memory_order_release);
            receive_cv.notify_all();
            for (auto& thread : receivers) thread.join();
            const auto failed_result = runtime.finish_network_ingress(true);
            response_queue.close();
            for (auto& thread : senders) thread.join();
            runtime.write_outputs(failed_result);
            for (int fd : sockets) ::close(fd);

            std::ofstream status(fs::path(options.output_dir) / "RPC_SERVER_STATUS.txt");
            status << "status=FAIL\n"
                   << "classification=INFRASTRUCTURE_FAILURE\n"
                   << "expected_requests=" << expected_requests << '\n'
                   << "accepted_requests=" << accepted.load() << '\n'
                   << "responses_enqueued=" << responses_enqueued.load() << '\n'
                   << "responses_sent=" << responses_sent.load() << '\n'
                   << "invalid_packets=" << invalid_packets.load() << '\n'
                   << "duplicate_packets=" << duplicate_packets.load() << '\n'
                   << "unknown_requests=" << unknown_requests.load() << '\n'
                   << "flow_mismatch_requests=" << flow_mismatch_requests.load() << '\n'
                   << "response_enqueue_failures=" << response_enqueue_failures.load() << '\n'
                   << "response_send_failures=" << response_send_failures.load() << '\n'
                   << "receiver_affinity_pass=" << (receiver_affinity_pass ? 1 : 0) << '\n'
                   << "response_sender_affinity_pass=" << (sender_affinity_pass ? 1 : 0) << '\n'
                   << "scheduler_affinity_pass="
                   << (failed_result.summary.scheduler_affinity_ok ? 1 : 0) << '\n'
                   << "server_main_affinity_pass=" << (server_main_affinity_ok ? 1 : 0) << '\n'
                   << "receiver_internal_failures=" << receiver_internal_failures.load() << '\n'
                   << "idle_termination=0\n"
                   << "runtime_invariants_pass="
                   << (failed_result.summary.invariants_pass ? 1 : 0) << '\n';
            std::cerr << "RPC server startup infrastructure gate failed before readiness\n";
            return 1;
        }

        std::cout << "RPC_SERVER_READY port=" << options.port
                  << " workers=" << options.runtime.worker_count
                  << " epoll_receivers=2 response_senders=2"
                  << " expected_requests=" << expected_requests << std::endl;

        uint64_t idle_wait_calls = 0;
        uint64_t idle_predicate_wakeups = 0;
        uint64_t idle_timeout_wakeups = 0;
        uint64_t saturating_underflow_avoided = 0;
        bool idle_termination = false;
        {
            std::unique_lock<std::mutex> lock(receive_mutex);
            while (accepted.load(std::memory_order_acquire) < expected_requests
                   && receiver_internal_failures.load(std::memory_order_acquire) == 0) {
                ++idle_wait_calls;
                const uint64_t observed_generation = receive_generation.load(
                    std::memory_order_acquire);
                const bool predicate = receive_cv.wait_for(
                    lock, std::chrono::milliseconds(200), [&] {
                        return accepted.load(std::memory_order_acquire) >= expected_requests
                            || receive_generation.load(std::memory_order_acquire)
                                != observed_generation
                            || receiver_internal_failures.load(std::memory_order_acquire) != 0;
                    });
                if (predicate) ++idle_predicate_wakeups;
                else ++idle_timeout_wakeups;
                const uint64_t now_ns = steady_ns(process_origin);
                const uint64_t last_ns = last_receive_ns.load(std::memory_order_acquire);
                if (now_ns < last_ns) ++saturating_underflow_avoided;
                const uint64_t idle_ns = physical::saturating_elapsed_ns(now_ns, last_ns);
                if (idle_ns > static_cast<uint64_t>(options.idle_timeout_seconds)
                        * 1000000000ULL) {
                    idle_termination = true;
                    break;
                }
            }
        }
        stop_receivers.store(true, std::memory_order_release);
        for (auto& thread : receivers) thread.join();

        const auto result = runtime.finish_network_ingress(true);
        response_queue.close();
        for (auto& thread : senders) thread.join();
        runtime.write_outputs(result);
        for (int fd : sockets) ::close(fd);

        {
            std::sort(mappings.begin(), mappings.end(), [](const auto& lhs, const auto& rhs) {
                if (lhs.flow_id != rhs.flow_id) return lhs.flow_id < rhs.flow_id;
                if (lhs.source_port != rhs.source_port) return lhs.source_port < rhs.source_port;
                return lhs.request_id < rhs.request_id;
            });
            std::ofstream mapping(fs::path(options.output_dir) / "ingress_mapping.csv");
            mapping << "request_id,flow_id,source_address,source_port,"
                       "kernel_reuseport_ingress_shard\n";
            for (const auto& row : mappings) {
                mapping << row.request_id << ',' << row.flow_id << ','
                        << row.source_address << ',' << row.source_port << ','
                        << row.ingress_shard << '\n';
            }
        }

        const bool pass = result.summary.invariants_pass
            && accepted.load() == expected_requests
            && responses_enqueued.load() == expected_requests
            && responses_sent.load() == expected_requests
            && invalid_packets.load() == 0
            && duplicate_packets.load() == 0
            && unknown_requests.load() == 0
            && flow_mismatch_requests.load() == 0
            && response_enqueue_failures.load() == 0
            && response_send_failures.load() == 0
            && receiver_internal_failures.load() == 0
            && receiver_affinity_pass && sender_affinity_pass
            && server_main_affinity_ok && result.summary.scheduler_affinity_ok;
        const bool infrastructure_failure = !receiver_affinity_pass
            || !sender_affinity_pass || !server_main_affinity_ok
            || !result.summary.scheduler_affinity_ok
            || receiver_internal_failures.load() != 0
            || result.summary.infrastructure_failure;
        std::ofstream status(fs::path(options.output_dir) / "RPC_SERVER_STATUS.txt");
        status << "status=" << (pass ? "PASS" : "FAIL") << '\n'
               << "classification=" << (infrastructure_failure
                    ? "INFRASTRUCTURE_FAILURE"
                    : (pass ? "VALID_COMPLETE" : "INCOMPLETE")) << '\n'
               << "expected_requests=" << expected_requests << '\n'
               << "accepted_requests=" << accepted.load() << '\n'
               << "responses_enqueued=" << responses_enqueued.load() << '\n'
               << "responses_sent=" << responses_sent.load() << '\n'
               << "invalid_packets=" << invalid_packets.load() << '\n'
               << "duplicate_packets=" << duplicate_packets.load() << '\n'
               << "unknown_requests=" << unknown_requests.load() << '\n'
               << "flow_mismatch_requests=" << flow_mismatch_requests.load() << '\n'
               << "response_enqueue_failures=" << response_enqueue_failures.load() << '\n'
               << "response_send_failures=" << response_send_failures.load() << '\n'
               << "response_queue_capacity=" << response_queue.capacity() << '\n'
               << "response_queue_high_watermark=" << response_queue.high_watermark() << '\n'
               << "epoll_receiver_count=2\nresponse_sender_count=2\n"
               << "receiver_cpus=" << join_ints(options.receiver_cpus) << '\n'
               << "response_sender_cpus=" << join_ints(options.response_sender_cpus) << '\n'
               << "scheduler_cpu=" << options.scheduler_cpu << '\n'
               << "server_main_cpu=" << options.server_main_cpu << '\n'
               << "receiver_affinity_pass=" << (receiver_affinity_pass ? 1 : 0) << '\n'
               << "response_sender_affinity_pass=" << (sender_affinity_pass ? 1 : 0) << '\n'
               << "scheduler_affinity_pass="
               << (result.summary.scheduler_affinity_ok ? 1 : 0) << '\n'
               << "server_main_affinity_pass=" << (server_main_affinity_ok ? 1 : 0) << '\n'
               << "receiver_internal_failures=" << receiver_internal_failures.load() << '\n'
               << "idle_wait_calls=" << idle_wait_calls << '\n'
               << "idle_predicate_wakeups=" << idle_predicate_wakeups << '\n'
               << "idle_timeout_wakeups=" << idle_timeout_wakeups << '\n'
               << "idle_termination=" << (idle_termination ? 1 : 0) << '\n'
               << "saturating_underflow_avoided="
               << saturating_underflow_avoided << '\n'
               << "runtime_invariants_pass=" << (result.summary.invariants_pass ? 1 : 0)
               << '\n';
        std::cout << "RPC server " << (pass ? "PASS" : "FAIL")
                  << " accepted=" << accepted.load()
                  << " responses=" << responses_sent.load()
                  << " output=" << options.output_dir << '\n';
        return pass ? 0 : 1;
    } catch (const std::exception& error) {
        if (output_directory_initialized && !output_dir_hint.empty()) {
            const std::filesystem::path status_path =
                std::filesystem::path(output_dir_hint) / "RPC_SERVER_STATUS.txt";
            if (!std::filesystem::exists(status_path)) {
                std::ofstream status(status_path);
                status << "status=FAIL\n"
                       << "classification=INFRASTRUCTURE_FAILURE\n"
                       << "failure_stage=EXCEPTION_BEFORE_COMPLETION\n"
                       << "error=" << error.what() << '\n';
            }
        }
        std::cerr << "RPC server error: " << error.what() << '\n';
        return 2;
    }
}
