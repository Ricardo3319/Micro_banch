#include "physical/rpc_protocol.h"

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_request_round_trip() {
    const auto wire = physical::rpc::make_request(0x0102030405060708ULL,
                                                   0x1112131415161718ULL,
                                                   0x2122232425262728ULL);
    uint64_t request_id = 0;
    uint64_t flow_id = 0;
    uint64_t client_send_ns = 0;
    require(physical::rpc::decode_request(
                wire, &request_id, &flow_id, &client_send_ns),
            "request decode failed");
    require(request_id == 0x0102030405060708ULL, "request ID changed");
    require(flow_id == 0x1112131415161718ULL, "flow ID changed");
    require(client_send_ns == 0x2122232425262728ULL, "send time changed");
}

void test_response_round_trip() {
    const auto wire = physical::rpc::make_response(
        7, 9, 11, 13, 15, 17, 19, 21, 23, true);
    physical::rpc::ResponseWire host{};
    require(physical::rpc::decode_response(wire, &host), "response decode failed");
    require(host.request_id == 7 && host.flow_id == 9 && host.client_send_ns == 11,
            "response identity changed");
    require(host.server_receive_ns == 13 && host.server_start_ns == 15
                && host.server_finish_ns == 17,
            "response timestamps changed");
    require(host.ingress_shard == 19 && host.final_worker == 21
                && host.migration_count == 23 && host.deadline_violation == 1,
            "response outcome changed");
}

void test_invalid_header_rejected() {
    auto request = physical::rpc::make_request(1, 2, 3);
    request.magic = 0;
    uint64_t request_id = 0;
    uint64_t flow_id = 0;
    uint64_t client_send_ns = 0;
    require(!physical::rpc::decode_request(
                request, &request_id, &flow_id, &client_send_ns),
            "invalid request header was accepted");
}

} // namespace

int main() {
    try {
        test_request_round_trip();
        test_response_round_trip();
        test_invalid_header_rejected();
        std::cout << "RPC protocol tests: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "RPC protocol tests: FAIL: " << error.what() << '\n';
        return 1;
    }
}
