#include "physical/rpc_protocol.h"
#include "physical/trace.h"
#include "sim/workloads/trace.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
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
    std::string server_address;
    uint16_t port = 9000;
    int worker_count = 16;
    int flow_sockets = 256;
    int response_timeout_seconds = 15;
    double arrival_scale = 1.0;
    int client_index = 0;
    int client_count = 1;
    uint16_t source_port_base = 20000;
    std::string bind_address = "0.0.0.0";
    int warmup_requests = 0;
    std::string workload_label = "UNSPECIFIED";
    std::string rho_label = "UNSPECIFIED";
    std::string seed_label = "UNSPECIFIED";
    int repetition = 0;
    uint64_t start_at_unix_ns = 0;
    bool help = false;
};

struct Record {
    uint64_t request_id = 0;
    uint64_t flow_id = 0;
    double trace_arrival_us = 0.0;
    double planned_send_us = 0.0;
    uint64_t actual_send_ns = 0;
    uint64_t receive_ns = 0;
    uint64_t server_receive_ns = 0;
    uint64_t server_start_ns = 0;
    uint64_t server_finish_ns = 0;
    uint32_t ingress_shard = 0;
    uint32_t final_worker = 0;
    uint32_t migration_count = 0;
    bool server_deadline_violation = false;
    bool sent = false;
    bool received = false;
    bool measurement_eligible = false;
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

void usage(const char* executable) {
    std::cout
        << "Usage: " << executable
        << " --trace FILE --server IPv4 --out-dir DIR [options]\n\n"
        << "Replays a frozen trace as real open-loop UDP requests and records RTT.\n"
        << "Request packets contain identity and timing only, never service demand.\n\n"
        << "Required:\n"
        << "  --trace FILE              Frozen v3 flow-affine trace\n"
        << "  --server IPv4             Server experiment-network address\n"
        << "  --out-dir DIR             New or empty client result directory\n\n"
        << "Options:\n"
        << "  --port N                  Server UDP port (default: 9000)\n"
        << "  --workers N               Trace loader worker count (default: 16)\n"
        << "  --flow-sockets N          Stable client flows (default: 256)\n"
        << "  --arrival-scale X         Multiply trace interarrival time (default: 1)\n"
        << "  --client-index N          This client's flow partition (default: 0)\n"
        << "  --client-count N          Number of disjoint clients (default: 1)\n"
        << "  --source-port-base N      Deterministic flow port base (default: 20000)\n"
        << "  --bind ADDRESS            Client experiment IPv4 address\n"
        << "  --start-at-unix-ns N      Shared absolute start time; 0 uses local delay\n"
        << "  --response-timeout-seconds N (default: 15)\n"
        << "  --warmup-requests N       Prefix excluded from metrics\n"
        << "  --workload-label TEXT --rho-label TEXT --seed-label TEXT\n"
        << "  --repetition N\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string arg = argv[index];
        auto take = [&](const std::string& key) {
            return value_for(index, argc, argv, arg, key);
        };
        if (arg == "-h" || arg == "--help") options.help = true;
        else if (arg == "--trace" || arg.rfind("--trace=", 0) == 0)
            options.trace_path = take("--trace");
        else if (arg == "--server" || arg.rfind("--server=", 0) == 0)
            options.server_address = take("--server");
        else if (arg == "--out-dir" || arg.rfind("--out-dir=", 0) == 0)
            options.output_dir = take("--out-dir");
        else if (arg == "--port" || arg.rfind("--port=", 0) == 0)
            options.port = static_cast<uint16_t>(std::stoul(take("--port")));
        else if (arg == "--workers" || arg.rfind("--workers=", 0) == 0)
            options.worker_count = std::stoi(take("--workers"));
        else if (arg == "--flow-sockets" || arg.rfind("--flow-sockets=", 0) == 0)
            options.flow_sockets = std::stoi(take("--flow-sockets"));
        else if (arg == "--arrival-scale" || arg.rfind("--arrival-scale=", 0) == 0)
            options.arrival_scale = std::stod(take("--arrival-scale"));
        else if (arg == "--client-index" || arg.rfind("--client-index=", 0) == 0)
            options.client_index = std::stoi(take("--client-index"));
        else if (arg == "--client-count" || arg.rfind("--client-count=", 0) == 0)
            options.client_count = std::stoi(take("--client-count"));
        else if (arg == "--source-port-base" || arg.rfind("--source-port-base=", 0) == 0)
            options.source_port_base = static_cast<uint16_t>(
                std::stoul(take("--source-port-base")));
        else if (arg == "--bind" || arg.rfind("--bind=", 0) == 0)
            options.bind_address = take("--bind");
        else if (arg == "--start-at-unix-ns" || arg.rfind("--start-at-unix-ns=", 0) == 0)
            options.start_at_unix_ns = std::stoull(take("--start-at-unix-ns"));
        else if (arg == "--response-timeout-seconds"
                 || arg.rfind("--response-timeout-seconds=", 0) == 0)
            options.response_timeout_seconds = std::stoi(take("--response-timeout-seconds"));
        else if (arg == "--warmup-requests" || arg.rfind("--warmup-requests=", 0) == 0)
            options.warmup_requests = std::stoi(take("--warmup-requests"));
        else if (arg == "--workload-label" || arg.rfind("--workload-label=", 0) == 0)
            options.workload_label = take("--workload-label");
        else if (arg == "--rho-label" || arg.rfind("--rho-label=", 0) == 0)
            options.rho_label = take("--rho-label");
        else if (arg == "--seed-label" || arg.rfind("--seed-label=", 0) == 0)
            options.seed_label = take("--seed-label");
        else if (arg == "--repetition" || arg.rfind("--repetition=", 0) == 0)
            options.repetition = std::stoi(take("--repetition"));
        else throw std::runtime_error("unknown option: " + arg);
    }
    if (!options.help && (options.trace_path.empty() || options.server_address.empty()
                          || options.output_dir.empty()))
        throw std::runtime_error("--trace, --server, and --out-dir are required");
    if (options.port == 0 || options.worker_count <= 0 || options.flow_sockets <= 0
        || options.response_timeout_seconds <= 0 || !(options.arrival_scale > 0.0)
        || !std::isfinite(options.arrival_scale) || options.warmup_requests < 0
        || options.repetition < 0 || options.client_count <= 0
        || options.client_index < 0 || options.client_index >= options.client_count
        || options.source_port_base == 0
        || static_cast<uint64_t>(options.source_port_base)
               + static_cast<uint64_t>(options.client_count * options.flow_sockets) > 65535ULL)
        throw std::runtime_error("invalid client option");
    return options;
}

double percentile(std::vector<double> values, double quantile) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const size_t index = static_cast<size_t>(
        std::max(1.0, std::ceil(quantile * values.size()))) - 1;
    return values[std::min(index, values.size() - 1)];
}

int make_connected_socket(const sockaddr_in& server, const std::string& bind_address,
                          uint16_t source_port) {
    const int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) throw std::runtime_error("socket failed: " + std::string(std::strerror(errno)));
    sockaddr_in local{};
    local.sin_family = AF_INET;
    if (inet_pton(AF_INET, bind_address.c_str(), &local.sin_addr) != 1) {
        close(fd);
        throw std::runtime_error("invalid client IPv4 bind address");
    }
    local.sin_port = htons(source_port);
    if (bind(fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0
        || connect(fd, reinterpret_cast<const sockaddr*>(&server), sizeof(server)) != 0) {
        const std::string message = std::strerror(errno);
        close(fd);
        throw std::runtime_error("UDP bind/connect failed: " + message);
    }
    return fd;
}

void wait_until_precise(const Clock::time_point& target) {
    constexpr auto spin_window = std::chrono::microseconds(100);
    const auto sleep_target = target - spin_window;
    if (Clock::now() < sleep_target) std::this_thread::sleep_until(sleep_target);
    while (Clock::now() < target) std::atomic_signal_fence(std::memory_order_seq_cst);
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

        const auto trace = physical::FrozenTrace::load_csv(
            options.trace_path, options.worker_count);
        if (trace.version() != sim::RESCUE_FLOW_TRACE_VERSION)
            throw std::runtime_error("physical RPC client requires v3 flow-affine trace");
        if (options.warmup_requests >= static_cast<int>(trace.entries().size()))
            throw std::runtime_error("warmup prefix covers the complete trace");

        sockaddr_in server{};
        server.sin_family = AF_INET;
        server.sin_port = htons(options.port);
        if (inet_pton(AF_INET, options.server_address.c_str(), &server.sin_addr) != 1)
            throw std::runtime_error("--server must be an IPv4 address");

        std::vector<int> sockets;
        sockets.reserve(static_cast<size_t>(options.flow_sockets));
        const int epoll_fd = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd < 0) throw std::runtime_error("epoll_create1 failed");
        for (int flow = 0; flow < options.flow_sockets; ++flow) {
            const uint16_t source_port = static_cast<uint16_t>(
                options.source_port_base + options.client_index * options.flow_sockets + flow);
            const int fd = make_connected_socket(
                server, options.bind_address, source_port);
            epoll_event event{};
            event.events = EPOLLIN;
            event.data.u32 = static_cast<uint32_t>(flow);
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) != 0)
                throw std::runtime_error("epoll_ctl failed");
            sockets.push_back(fd);
        }

        std::vector<Record> records;
        records.reserve(trace.entries().size());
        std::unordered_map<uint64_t, size_t> index_by_id;
        for (size_t index = 0; index < trace.entries().size(); ++index) {
            const auto& entry = trace.entries()[index];
            if (entry.flow_id % static_cast<uint64_t>(options.client_count)
                != static_cast<uint64_t>(options.client_index)) continue;
            Record record;
            record.request_id = entry.id;
            record.flow_id = entry.flow_id;
            record.trace_arrival_us = entry.arrival_us;
            record.planned_send_us = entry.arrival_us * options.arrival_scale;
            record.measurement_eligible =
                index >= static_cast<size_t>(options.warmup_requests);
            records.push_back(record);
            index_by_id.emplace(entry.id, records.size() - 1);
        }
        if (records.empty()) throw std::runtime_error("client flow partition is empty");

        Clock::time_point origin;
        if (options.start_at_unix_ns > 0) {
            const auto wall_target = std::chrono::system_clock::time_point(
                std::chrono::nanoseconds(options.start_at_unix_ns));
            const auto wall_now = std::chrono::system_clock::now();
            if (wall_target <= wall_now)
                throw std::runtime_error("--start-at-unix-ns is in the past");
            origin = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                wall_target - wall_now);
        } else {
            origin = Clock::now() + std::chrono::milliseconds(250);
        }
        std::mutex records_mutex;
        std::atomic<uint64_t> responses{0};
        std::atomic<uint64_t> invalid_responses{0};
        std::atomic<uint64_t> duplicate_responses{0};
        std::atomic<bool> sender_done{false};
        std::thread receiver([&] {
            std::vector<epoll_event> events(64);
            Clock::time_point sender_done_seen{};
            while (!sender_done.load(std::memory_order_acquire)
                   || responses.load(std::memory_order_acquire) < records.size()) {
                const int count = epoll_wait(epoll_fd, events.data(),
                                             static_cast<int>(events.size()), 100);
                if (count < 0) {
                    if (errno == EINTR) continue;
                    invalid_responses.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                for (int event_index = 0; event_index < count; ++event_index) {
                    const int flow = static_cast<int>(events[event_index].data.u32);
                    while (true) {
                        physical::rpc::ResponseWire wire{};
                        const ssize_t bytes = recv(sockets[static_cast<size_t>(flow)],
                                                   &wire, sizeof(wire), 0);
                        if (bytes < 0) {
                            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                            invalid_responses.fetch_add(1, std::memory_order_relaxed);
                            break;
                        }
                        physical::rpc::ResponseWire response{};
                        if (bytes != static_cast<ssize_t>(sizeof(wire))
                            || !physical::rpc::decode_response(wire, &response)) {
                            invalid_responses.fetch_add(1, std::memory_order_relaxed);
                            continue;
                        }
                        const uint64_t receive_ns = steady_ns(origin);
                        std::lock_guard<std::mutex> lock(records_mutex);
                        const auto found = index_by_id.find(response.request_id);
                        if (found == index_by_id.end()) {
                            invalid_responses.fetch_add(1, std::memory_order_relaxed);
                            continue;
                        }
                        auto& record = records[found->second];
                        if (!record.sent || response.flow_id != record.flow_id
                            || response.client_send_ns != record.actual_send_ns
                            || static_cast<int>(response.flow_id
                                % static_cast<uint64_t>(options.flow_sockets)) != flow) {
                            invalid_responses.fetch_add(1, std::memory_order_relaxed);
                            continue;
                        }
                        if (record.received) {
                            duplicate_responses.fetch_add(1, std::memory_order_relaxed);
                            continue;
                        }
                        record.received = true;
                        record.receive_ns = receive_ns;
                        record.server_receive_ns = response.server_receive_ns;
                        record.server_start_ns = response.server_start_ns;
                        record.server_finish_ns = response.server_finish_ns;
                        record.ingress_shard = response.ingress_shard;
                        record.final_worker = response.final_worker;
                        record.migration_count = response.migration_count;
                        record.server_deadline_violation = response.deadline_violation != 0;
                        responses.fetch_add(1, std::memory_order_release);
                    }
                }
                if (sender_done.load(std::memory_order_acquire)) {
                    if (sender_done_seen == Clock::time_point{})
                        sender_done_seen = Clock::now();
                    if (Clock::now() - sender_done_seen
                        > std::chrono::seconds(options.response_timeout_seconds)) break;
                }
            }
        });

        uint64_t send_failures = 0;
        double max_send_lag_us = 0.0;
        double sum_send_lag_us = 0.0;
        for (size_t index = 0; index < records.size(); ++index) {
            auto& record = records[index];
            const auto target = origin + std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double, std::micro>(
                    record.trace_arrival_us * options.arrival_scale));
            wait_until_precise(target);
            const uint64_t send_ns = steady_ns(origin);
            const int flow = static_cast<int>(record.flow_id
                % static_cast<uint64_t>(options.flow_sockets));
            const auto request = physical::rpc::make_request(
                record.request_id, record.flow_id, send_ns);
            {
                std::lock_guard<std::mutex> lock(records_mutex);
                record.actual_send_ns = send_ns;
                record.sent = true;
            }
            const ssize_t bytes = send(sockets[static_cast<size_t>(flow)],
                                       &request, sizeof(request), 0);
            if (bytes != static_cast<ssize_t>(sizeof(request))) {
                std::lock_guard<std::mutex> lock(records_mutex);
                record.sent = false;
                ++send_failures;
            }
            const double lag_us = std::max(0.0,
                static_cast<double>(send_ns) / 1000.0 - records[index].planned_send_us);
            max_send_lag_us = std::max(max_send_lag_us, lag_us);
            sum_send_lag_us += lag_us;
        }
        sender_done.store(true, std::memory_order_release);
        receiver.join();

        std::vector<double> rtts;
        uint64_t received_measurement = 0;
        uint64_t server_deadline_violations = 0;
        uint64_t migrated_requests = 0;
        std::ofstream csv(fs::path(options.output_dir) / "client_requests.csv");
        csv << "request_id,flow_id,trace_arrival_us,planned_send_us,actual_send_us,"
               "client_receive_us,client_rtt_us,response_status,server_receive_us,"
               "server_start_us,server_finish_us,ingress_shard,final_worker,"
               "migration_count,server_deadline_violation,measurement_eligible\n";
        csv << std::setprecision(17);
        for (size_t index = 0; index < records.size(); ++index) {
            const auto& record = records[index];
            const bool measurement = record.measurement_eligible;
            const double rtt_us = record.received
                ? static_cast<double>(record.receive_ns - record.actual_send_ns) / 1000.0 : 0.0;
            if (measurement && record.received) {
                ++received_measurement;
                rtts.push_back(rtt_us);
                if (record.server_deadline_violation) ++server_deadline_violations;
                if (record.migration_count > 0) ++migrated_requests;
            }
            csv << record.request_id << ',' << record.flow_id << ','
                << record.trace_arrival_us << ',' << record.planned_send_us << ','
                << static_cast<double>(record.actual_send_ns) / 1000.0 << ','
                << static_cast<double>(record.receive_ns) / 1000.0 << ',' << rtt_us << ','
                << (record.received ? "received" : "timeout") << ','
                << static_cast<double>(record.server_receive_ns) / 1000.0 << ','
                << static_cast<double>(record.server_start_ns) / 1000.0 << ','
                << static_cast<double>(record.server_finish_ns) / 1000.0 << ','
                << record.ingress_shard << ',' << record.final_worker << ','
                << record.migration_count << ','
                << (record.server_deadline_violation ? 1 : 0) << ','
                << (measurement ? 1 : 0) << '\n';
        }

        const uint64_t expected_measurement = static_cast<uint64_t>(std::count_if(
            records.begin(), records.end(), [](const Record& record) {
                return record.measurement_eligible;
            }));
        const bool pass = send_failures == 0 && responses.load() == records.size()
            && invalid_responses.load() == 0 && duplicate_responses.load() == 0;
        std::ofstream summary(fs::path(options.output_dir) / "client_summary.csv");
        summary << "evidence_scope,trace_embedded_sha256,trace_input_file_sha256,"
                   "workload,rho,seed,repetition,total_requests,measurement_requests,"
                   "responses,measurement_responses,timeouts,send_failures,"
                   "invalid_responses,duplicate_responses,P50_client_rtt_us,"
                   "P99_client_rtt_us,P999_client_rtt_us,server_deadline_violations,"
                   "migrated_requests,max_send_lag_us,mean_send_lag_us,"
                   "client_index,client_count,source_port_base,bind_address,"
                   "start_at_unix_ns,status\n";
        summary << std::setprecision(17)
                << "physical_network_rpc_client," << trace.embedded_sha256() << ','
                << trace.input_file_sha256() << ',' << options.workload_label << ','
                << options.rho_label << ',' << options.seed_label << ','
                << options.repetition << ',' << records.size() << ','
                << expected_measurement << ',' << responses.load() << ','
                << received_measurement << ',' << (records.size() - responses.load()) << ','
                << send_failures << ',' << invalid_responses.load() << ','
                << duplicate_responses.load() << ',' << percentile(rtts, 0.50) << ','
                << percentile(rtts, 0.99) << ',' << percentile(rtts, 0.999) << ','
                << server_deadline_violations << ',' << migrated_requests << ','
                << max_send_lag_us << ','
                << sum_send_lag_us / static_cast<double>(records.size()) << ','
                << options.client_index << ',' << options.client_count << ','
                << options.source_port_base << ',' << options.bind_address << ','
                << options.start_at_unix_ns << ','
                << (pass ? "PASS" : "FAIL") << '\n';
        std::ofstream status(fs::path(options.output_dir) / "RPC_CLIENT_STATUS.txt");
        status << "status=" << (pass ? "PASS" : "FAIL") << '\n'
               << "total_requests=" << records.size() << '\n'
               << "responses=" << responses.load() << '\n'
               << "timeouts=" << (records.size() - responses.load()) << '\n'
               << "send_failures=" << send_failures << '\n'
               << "invalid_responses=" << invalid_responses.load() << '\n'
               << "duplicate_responses=" << duplicate_responses.load() << '\n';

        for (int fd : sockets) close(fd);
        close(epoll_fd);
        std::cout << "RPC client " << (pass ? "PASS" : "FAIL")
                  << " responses=" << responses.load() << '/' << records.size()
                  << " output=" << options.output_dir << '\n';
        return pass ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "RPC client error: " << error.what() << '\n';
        return 2;
    }
}
