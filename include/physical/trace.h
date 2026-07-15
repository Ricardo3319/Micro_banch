#pragma once

#include "sim/common/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace physical {

struct TraceEntry {
    uint64_t id = 0;
    double arrival_us = 0.0;
    sim::RpcMethod method = sim::RpcMethod::SHORT_RPC;
    double synthetic_service_us = 0.0;
    double deadline_budget_us = 0.0;
    int initial_core = -1;
    uint64_t flow_id = 0;
    bool burst = false;
};

class FrozenTrace {
public:
    static FrozenTrace load_csv(const std::string& path, int worker_count);

    const std::string& version() const { return version_; }
    const std::string& embedded_sha256() const { return embedded_sha256_; }
    const std::string& input_file_sha256() const { return input_file_sha256_; }
    const std::string& placement_mode() const { return placement_mode_; }
    const std::string& path() const { return path_; }
    const std::vector<TraceEntry>& entries() const { return entries_; }

private:
    std::string version_;
    std::string embedded_sha256_;
    std::string input_file_sha256_;
    std::string placement_mode_;
    std::string path_;
    std::vector<TraceEntry> entries_;
};

} // namespace physical
