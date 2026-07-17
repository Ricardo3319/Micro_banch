#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <type_traits>

#if defined(__linux__)
#include <arpa/inet.h>
#endif

namespace physical::rpc {

inline constexpr uint32_t MAGIC = 0x52534348U; // RSCH
inline constexpr uint16_t VERSION = 1;

enum class MessageType : uint16_t {
    REQUEST = 1,
    RESPONSE = 2
};

inline uint64_t byte_swap64(uint64_t value) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(value);
#else
    return ((value & 0x00000000000000ffULL) << 56U)
         | ((value & 0x000000000000ff00ULL) << 40U)
         | ((value & 0x0000000000ff0000ULL) << 24U)
         | ((value & 0x00000000ff000000ULL) << 8U)
         | ((value & 0x000000ff00000000ULL) >> 8U)
         | ((value & 0x0000ff0000000000ULL) >> 24U)
         | ((value & 0x00ff000000000000ULL) >> 40U)
         | ((value & 0xff00000000000000ULL) >> 56U);
#endif
}

inline uint64_t host_to_network64(uint64_t value) {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return byte_swap64(value);
#else
    return value;
#endif
}

inline uint64_t network_to_host64(uint64_t value) {
    return host_to_network64(value);
}

#pragma pack(push, 1)
struct RequestWire {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint64_t request_id;
    uint64_t flow_id;
    uint64_t client_send_ns;
};

struct ResponseWire {
    uint32_t magic;
    uint16_t version;
    uint16_t type;
    uint64_t request_id;
    uint64_t flow_id;
    uint64_t client_send_ns;
    uint64_t server_receive_ns;
    uint64_t server_start_ns;
    uint64_t server_finish_ns;
    uint32_t ingress_shard;
    uint32_t final_worker;
    uint32_t migration_count;
    uint32_t deadline_violation;
};
#pragma pack(pop)

static_assert(std::is_trivially_copyable<RequestWire>::value,
              "request wire message must be trivially copyable");
static_assert(std::is_trivially_copyable<ResponseWire>::value,
              "response wire message must be trivially copyable");

inline RequestWire make_request(uint64_t request_id, uint64_t flow_id,
                                uint64_t client_send_ns) {
    return RequestWire{
        htonl(MAGIC), htons(VERSION), htons(static_cast<uint16_t>(MessageType::REQUEST)),
        host_to_network64(request_id), host_to_network64(flow_id),
        host_to_network64(client_send_ns)
    };
}

inline bool decode_request(const RequestWire& wire, uint64_t* request_id,
                           uint64_t* flow_id, uint64_t* client_send_ns) {
    if (ntohl(wire.magic) != MAGIC || ntohs(wire.version) != VERSION
        || ntohs(wire.type) != static_cast<uint16_t>(MessageType::REQUEST)) {
        return false;
    }
    *request_id = network_to_host64(wire.request_id);
    *flow_id = network_to_host64(wire.flow_id);
    *client_send_ns = network_to_host64(wire.client_send_ns);
    return true;
}

inline ResponseWire make_response(uint64_t request_id, uint64_t flow_id,
                                  uint64_t client_send_ns,
                                  uint64_t server_receive_ns,
                                  uint64_t server_start_ns,
                                  uint64_t server_finish_ns,
                                  uint32_t ingress_shard,
                                  uint32_t final_worker,
                                  uint32_t migration_count,
                                  bool deadline_violation) {
    return ResponseWire{
        htonl(MAGIC), htons(VERSION), htons(static_cast<uint16_t>(MessageType::RESPONSE)),
        host_to_network64(request_id), host_to_network64(flow_id),
        host_to_network64(client_send_ns),
        host_to_network64(server_receive_ns), host_to_network64(server_start_ns),
        host_to_network64(server_finish_ns), htonl(ingress_shard),
        htonl(final_worker), htonl(migration_count),
        htonl(deadline_violation ? 1U : 0U)
    };
}

inline bool decode_response(const ResponseWire& wire, ResponseWire* host) {
    if (ntohl(wire.magic) != MAGIC || ntohs(wire.version) != VERSION
        || ntohs(wire.type) != static_cast<uint16_t>(MessageType::RESPONSE)) {
        return false;
    }
    *host = wire;
    host->magic = ntohl(wire.magic);
    host->version = ntohs(wire.version);
    host->type = ntohs(wire.type);
    host->request_id = network_to_host64(wire.request_id);
    host->flow_id = network_to_host64(wire.flow_id);
    host->client_send_ns = network_to_host64(wire.client_send_ns);
    host->server_receive_ns = network_to_host64(wire.server_receive_ns);
    host->server_start_ns = network_to_host64(wire.server_start_ns);
    host->server_finish_ns = network_to_host64(wire.server_finish_ns);
    host->ingress_shard = ntohl(wire.ingress_shard);
    host->final_worker = ntohl(wire.final_worker);
    host->migration_count = ntohl(wire.migration_count);
    host->deadline_violation = ntohl(wire.deadline_violation);
    return true;
}

} // namespace physical::rpc
