// Linux UDP measurement tool. This is a transport probe, not the QBR runtime.
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
double clock_us() {
    timespec t{}; clock_gettime(CLOCK_MONOTONIC_RAW, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1000.0;
}
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(std::string(message) + ": " + std::strerror(errno));
}
sockaddr_in address(const std::string& ip, int port) {
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) throw std::runtime_error("IPv4 literal required");
    return a;
}
double quantile(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(std::ceil(p * v.size())) - 1];
}
struct Socket { int fd; ~Socket() { if (fd >= 0) close(fd); } };
}
int main(int argc, char** argv) {
    try {
        if (argc < 2) throw std::runtime_error("usage: net_probe server|client [--bind IP --peer IP --port 19000 --cpu N --bytes 128 --train 1 --iterations 10000 --warmup 1000 --max-packets N --service-us 0]");
        std::string mode = argv[1], bind_ip = "127.0.0.1", peer_ip = "127.0.0.1";
        int port = 19000, cpu = -1, bytes = 128, train = 1, iterations = 10000, warmup = 1000;
        uint64_t max_packets = 0; double service_us = 0;
        for (int i = 2; i < argc; ++i) {
            std::string k = argv[i]; if (i + 1 == argc) throw std::runtime_error("missing argument");
            std::string v = argv[++i];
            if (k == "--bind") bind_ip = v;
            else if (k == "--peer") peer_ip = v;
            else if (k == "--port") port = std::stoi(v);
            else if (k == "--cpu") cpu = std::stoi(v);
            else if (k == "--bytes") bytes = std::stoi(v);
            else if (k == "--train") train = std::stoi(v);
            else if (k == "--iterations") iterations = std::stoi(v);
            else if (k == "--warmup") warmup = std::stoi(v);
            else if (k == "--max-packets") max_packets = std::stoull(v);
            else if (k == "--service-us") service_us = std::stod(v);
            else throw std::runtime_error("unknown option: " + k);
        }
        if (port < 1 || port > 65535 || bytes < 16 || bytes > 1400 || train < 1 || train > 32 ||
            iterations < 1 || warmup < 0 || !std::isfinite(service_us) || service_us < 0 || cpu >= CPU_SETSIZE || cpu < -1)
            throw std::runtime_error("invalid configuration; payload limited to 1400 bytes");
        if (mode != "server" && mode != "client") throw std::runtime_error("invalid mode");
        if (cpu >= 0) { cpu_set_t set; CPU_ZERO(&set); CPU_SET(cpu, &set);
            check(sched_setaffinity(0, sizeof(set), &set) == 0, "CPU affinity"); }
        Socket sock{socket(AF_INET, SOCK_DGRAM, 0)}; check(sock.fd >= 0, "socket");
        timeval timeout{0, 250000};
        check(setsockopt(sock.fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0, "timeout");
        auto bind_addr = address(bind_ip, mode == "server" ? port : 0);
        check(bind(sock.fd, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) == 0, "bind");
        std::vector<char> packet(1400), response(1400);
        if (mode == "server") {
            uint64_t count = 0; double last = clock_us();
            std::cerr << "UDP probe ready at " << bind_ip << ':' << port << '\n';
            while (!max_packets || count < max_packets) {
                sockaddr_in peer{}; socklen_t length = sizeof(peer);
                ssize_t n = recvfrom(sock.fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&peer), &length);
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
                    if (clock_us() - last > 30e6) throw std::runtime_error("server idle timeout after 30 seconds");
                    continue;
                }
                check(n >= 0, "receive"); last = clock_us();
                double until = last + service_us;
                while (clock_us() < until) { /* Optional controlled service; not a real RPC application. */ }
                check(sendto(sock.fd, packet.data(), n, 0, reinterpret_cast<sockaddr*>(&peer), length) == n, "echo");
                ++count;
            }
            std::cout << "{\"mode\":\"server\",\"received\":" << count << "}\n"; return 0;
        }
        auto peer = address(peer_ip, port);
        check(connect(sock.fd, reinterpret_cast<sockaddr*>(&peer), sizeof(peer)) == 0, "connect");
        std::vector<double> rtts, batch_rtts, starts(train);
        std::vector<bool> seen(train);
        uint64_t sequence = 0, lost = 0, stale = 0;
        double total_begin = 0, total_end = 0;
        for (int round = -warmup; round < iterations; ++round) {
            if (round == 0) total_begin = clock_us();
            uint64_t first = sequence; double begin = clock_us();
            std::fill(seen.begin(), seen.end(), false);
            for (int j = 0; j < train; ++j) {
                std::memcpy(packet.data(), &sequence, sizeof(sequence)); ++sequence;
                starts[j] = clock_us(); check(send(sock.fd, packet.data(), bytes, 0) == bytes, "send");
            }
            int received = 0;
            while (received < train) {
                ssize_t n = recv(sock.fd, response.data(), response.size(), 0);
                if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
                if (n < 0 && errno == EINTR) continue;
                check(n >= 0, "receive");
                if (n != bytes) throw std::runtime_error("incorrect response size");
                uint64_t id; std::memcpy(&id, response.data(), sizeof(id));
                if (id < first || id >= first + train || seen[id - first]) { if (round >= 0) ++stale; continue; }
                seen[id - first] = true; ++received;
                if (round >= 0) rtts.push_back(clock_us() - starts[id - first]);
            }
            if (round >= 0) {
                lost += train - received;
                if (received == train) batch_rtts.push_back(clock_us() - begin);
            }
            total_end = clock_us();
        }
        std::cout << std::setprecision(12) << "{\"mode\":\"client\",\"transport\":\"Linux UDP sockets\",\"peer\":\"" << peer_ip
          << "\",\"bytes\":" << bytes << ",\"train\":" << train << ",\"cpu\":" << cpu << ",\"warmup_trains\":" << warmup
          << ",\"iterations\":" << iterations << ",\"received\":" << rtts.size() << ",\"lost\":" << lost << ",\"stale\":" << stale
          << ",\"rtt_p50_us\":" << quantile(rtts, .5) << ",\"rtt_p99_us\":" << quantile(rtts, .99)
          << ",\"rtt_p999_us\":" << quantile(rtts, .999) << ",\"train_p50_us\":" << quantile(batch_rtts, .5)
          << ",\"train_p99_us\":" << quantile(batch_rtts, .99)
          << ",\"elapsed_us\":" << total_end - total_begin
          << ",\"closed_loop_mrps\":" << rtts.size() / (total_end - total_begin) << "}\n";
        return lost ? 2 : 0;
    } catch (const std::exception& e) { std::cerr << "net_probe: " << e.what() << '\n'; return 1; }
}
