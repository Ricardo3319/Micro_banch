#include "qbr/model.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool v, const std::string& message) { if (!v) throw std::runtime_error(message); }
double field(const std::string& s, const std::string& name) {
    auto p = s.find("\"" + name + "\":");
    require(p != std::string::npos, "missing field " + name);
    return std::stod(s.substr(p + name.size() + 3));
}
double latency(const qbr::Trace& t, const qbr::Config& c, const std::string& path) {
    qbr::simulate(t, c, "jsw", path);
    std::ifstream f(path, std::ios::binary); double v;
    require(static_cast<bool>(f.read(reinterpret_cast<char*>(&v), 8)), "outcome read");
    std::remove(path.c_str()); return v;
}
}
int main() {
    try {
        require(qbr::batch_work_limit(100, 1, 20, 1, 10, 2, 100) == 34, "equal-rate balance");
        require(qbr::batch_work_limit(10, 1, 20, 1, 10, 2, 100) == 0, "negative gain");
        require(qbr::batch_work_limit(1000, 1, 0, 1, 0, 0, 10) == 10, "work cap");
        qbr::Config c; c.hosts = 1; c.workers = 1; c.peers = 0;
        qbr::Trace t; t.end_us = 100; t.requests.push_back({0, 5, 0, 64});
        double expected = c.ingress_cpu_us + 2 * c.network_us + 3 * 64 * 8 / (c.bandwidth_gbps * 1000) + 5 + c.host_us;
        require(std::abs(latency(t, c, "/tmp/qbr_test_latency.bin") - expected) < 1e-7, "network+host accounting");
        c.network_us = 0; c.bandwidth_gbps = 1e9;
        t.end_us = 200; t.requests[0].service_us = 100;
        double base = latency(t, c, "/tmp/qbr_test_base.bin");
        t.changes = {{50, 0, .5}, {150, 0, 1}};
        require(std::abs(latency(t, c, "/tmp/qbr_test_dip.bin") - base - 50) < 1e-7, "capacity residual reschedule");
        t.warmup_us = 50;
        require(field(qbr::simulate(t, c, "jsw"), "measured") == 0, "arrival-window measurement");
        c = qbr::Config{}; c.hosts = 4; c.workers = 2; c.peers = 3;
        t = qbr::Trace{}; t.end_us = 9000; t.warmup_us = 1000;
        for (int i = 0; i < 2400; ++i) t.requests.push_back({i * 3.5, i % 5 == 0 ? 100.0 : 5.0,
                                                          static_cast<uint8_t>(i % 5 == 0), 128});
        t.changes = {{1800, 0, .15}, {4000, 0, 1}, {4500, 1, .15}, {7000, 1, 1}};
        for (const auto& m : {"p2c", "jsw", "jbsq", "steal", "threshold", "single", "qbr", "work", "bwc"}) {
            auto a = qbr::simulate(t, c, m), b = qbr::simulate(t, c, m);
            require(a == b, std::string(m) + " nondeterministic");
            require(field(a, "completed") == t.requests.size(), std::string(m) + " completion conservation");
            require(field(a, "control_cpu_max") <= 1.000001, "controller resource bound");
            require(field(a, "max_batch") <= c.max_batch, "batch bound");
            require(field(a, "max_reserved_work_us") <= c.admit_us * c.workers + 1e-6, "grant admission bound");
            if (std::string(m) == "qbr") require(field(a, "migrated") > 0, "test must exercise transfer");
            if (std::string(m) == "single") require(field(a, "max_batch") <= 1, "single-task bound");
        }
        c.lease_us = 7;
        auto short_lease = qbr::simulate(t, c, "qbr");
        require(field(short_lease, "completed") == t.requests.size(), "expiry conservation");
        require(field(short_lease, "expired") > 0, "test must exercise expiry");
        c.lease_us = 8;
        c.bandwidth_gbps = 1;
        auto late_payload = qbr::simulate(t, c, "qbr");
        require(field(late_payload, "returned_batches") > 0, "test must exercise data return after expiry");
        require(field(late_payload, "completed") == t.requests.size(), "returned payload conservation");
        c.bandwidth_gbps = 25;
        c.lease_us = 60; c.max_bytes = 64;
        require(field(qbr::simulate(t, c, "qbr"), "migrated") == 0, "byte cap");
        c.max_bytes = 16384; c.reserve = false;
        require(field(qbr::simulate(t, c, "qbr"), "completed") == t.requests.size(), "no-reservation conservation");
        bool rejected = false; t.requests[0].type = 3;
        try { qbr::simulate(t, c, "jsw"); } catch (const std::exception&) { rejected = true; }
        require(rejected, "invalid trace validation");
        std::cout << "QBR tests: PASS (accounting, slowdown, conservation, determinism, grants, expiry, byte/work bounds)\n";
    } catch (const std::exception& e) { std::cerr << "QBR tests: FAIL: " << e.what() << '\n'; return 1; }
}
