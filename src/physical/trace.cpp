#include "physical/trace.h"

#include "sim/workloads/trace.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace physical {
namespace {

const std::vector<std::string> kV2Header = {
    "trace_version", "trace_sha256", "id", "generate_time_us",
    "rpc_method", "service_time_us", "deadline_budget_us", "initial_core",
    "burst"
};

const std::vector<std::string> kV3Header = {
    "trace_version", "trace_sha256", "placement_mode", "id", "flow_id",
    "generate_time_us", "rpc_method", "service_time_us", "deadline_budget_us",
    "initial_core", "burst"
};

std::vector<std::string> parse_csv_row(const std::string& input, size_t line_number) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t index = 0; index < input.size(); ++index) {
        const char ch = input[index];
        if (quoted) {
            if (ch == '"') {
                if (index + 1 < input.size() && input[index + 1] == '"') {
                    field.push_back('"');
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                field.push_back(ch);
            }
        } else if (ch == ',' ) {
            fields.push_back(field);
            field.clear();
        } else if (ch == '"' && field.empty()) {
            quoted = true;
        } else {
            field.push_back(ch);
        }
    }
    if (quoted) {
        throw std::runtime_error(
            "unterminated quoted CSV field at line " + std::to_string(line_number));
    }
    fields.push_back(field);
    return fields;
}

uint64_t parse_uint64(const std::string& value, const char* field, size_t line) {
    if (value.empty() || value.front() == '-') {
        throw std::runtime_error(std::string("invalid ") + field
            + " at line " + std::to_string(line));
    }
    size_t consumed = 0;
    uint64_t parsed = 0;
    try {
        parsed = std::stoull(value, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid ") + field
            + " at line " + std::to_string(line));
    }
    if (consumed != value.size()) {
        throw std::runtime_error(std::string("invalid ") + field
            + " at line " + std::to_string(line));
    }
    return parsed;
}

int parse_int(const std::string& value, const char* field, size_t line) {
    size_t consumed = 0;
    int parsed = 0;
    try {
        parsed = std::stoi(value, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid ") + field
            + " at line " + std::to_string(line));
    }
    if (consumed != value.size()) {
        throw std::runtime_error(std::string("invalid ") + field
            + " at line " + std::to_string(line));
    }
    return parsed;
}

double parse_double(const std::string& value, const char* field, size_t line) {
    size_t consumed = 0;
    double parsed = 0.0;
    try {
        parsed = std::stod(value, &consumed);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid ") + field
            + " at line " + std::to_string(line));
    }
    if (consumed != value.size() || !std::isfinite(parsed)) {
        throw std::runtime_error(std::string("non-finite or malformed ") + field
            + " at line " + std::to_string(line));
    }
    return parsed;
}

bool valid_sha256(const std::string& value) {
    return value.size() == 64
        && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        });
}

std::string read_file_bytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) throw std::runtime_error("cannot open trace: " + path);
    std::ostringstream bytes;
    bytes << input.rdbuf();
    if (!input.good() && !input.eof())
        throw std::runtime_error("cannot read trace: " + path);
    return bytes.str();
}

} // namespace

FrozenTrace FrozenTrace::load_csv(const std::string& path, int worker_count) {
    if (worker_count <= 0)
        throw std::invalid_argument("worker_count must be positive");

    const std::string bytes = read_file_bytes(path);
    std::istringstream input(bytes);
    std::string line;
    if (!std::getline(input, line)) throw std::runtime_error("trace is empty: " + path);
    if (!line.empty() && line.back() == '\r') line.pop_back();

    const auto header = parse_csv_row(line, 1);
    const bool is_v2 = header == kV2Header;
    const bool is_v3 = header == kV3Header;
    if (!is_v2 && !is_v3)
        throw std::runtime_error("unsupported or reordered trace header: " + path);

    FrozenTrace trace;
    trace.path_ = path;
    trace.input_file_sha256_ = sim::sha256_hex(bytes);
    trace.version_ = is_v3 ? sim::RESCUE_FLOW_TRACE_VERSION : sim::RESCUE_TRACE_VERSION;
    trace.placement_mode_ = is_v3 ? "flow_affine" : "request_random";

    uint64_t previous_id = 0;
    double previous_arrival = -1.0;
    size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty())
            throw std::runtime_error("blank trace row at line " + std::to_string(line_number));
        const auto fields = parse_csv_row(line, line_number);
        if (fields.size() != header.size())
            throw std::runtime_error("wrong trace field count at line "
                + std::to_string(line_number));
        if (fields[0] != trace.version_)
            throw std::runtime_error("mixed or unexpected trace version at line "
                + std::to_string(line_number));
        if (!valid_sha256(fields[1]))
            throw std::runtime_error("invalid embedded trace SHA-256 at line "
                + std::to_string(line_number));
        if (trace.embedded_sha256_.empty()) trace.embedded_sha256_ = fields[1];
        if (fields[1] != trace.embedded_sha256_)
            throw std::runtime_error("inconsistent embedded trace SHA-256 at line "
                + std::to_string(line_number));

        size_t offset = 2;
        if (is_v3) {
            if (fields[offset] != "flow_affine")
                throw std::runtime_error("invalid placement_mode at line "
                    + std::to_string(line_number));
            ++offset;
        }

        TraceEntry entry;
        entry.id = parse_uint64(fields[offset++], "id", line_number);
        if (is_v3) entry.flow_id = parse_uint64(fields[offset++], "flow_id", line_number);
        entry.arrival_us = parse_double(fields[offset++], "generate_time_us", line_number);
        if (fields[offset] == "short") {
            entry.method = sim::RpcMethod::SHORT_RPC;
        } else if (fields[offset] == "long") {
            entry.method = sim::RpcMethod::LONG_RPC;
        } else {
            throw std::runtime_error("unknown rpc_method at line "
                + std::to_string(line_number));
        }
        ++offset;
        entry.synthetic_service_us = parse_double(
            fields[offset++], "service_time_us", line_number);
        entry.deadline_budget_us = parse_double(
            fields[offset++], "deadline_budget_us", line_number);
        entry.initial_core = parse_int(fields[offset++], "initial_core", line_number);
        if (fields[offset] == "0") entry.burst = false;
        else if (fields[offset] == "1") entry.burst = true;
        else throw std::runtime_error("invalid burst flag at line "
            + std::to_string(line_number));

        if (entry.id == 0 || entry.id <= previous_id)
            throw std::runtime_error("trace IDs must be unique and increasing at line "
                + std::to_string(line_number));
        if (entry.arrival_us < 0.0 || entry.arrival_us < previous_arrival)
            throw std::runtime_error("trace arrivals must be non-negative and monotonic at line "
                + std::to_string(line_number));
        if (!(entry.synthetic_service_us > 0.0) || !(entry.deadline_budget_us > 0.0))
            throw std::runtime_error("service and deadline must be positive at line "
                + std::to_string(line_number));
        if (entry.initial_core < 0 || entry.initial_core >= worker_count)
            throw std::runtime_error("initial_core outside worker range at line "
                + std::to_string(line_number));

        previous_id = entry.id;
        previous_arrival = entry.arrival_us;
        trace.entries_.push_back(entry);
    }

    if (trace.entries_.empty()) throw std::runtime_error("trace has no request rows: " + path);
    return trace;
}

} // namespace physical
