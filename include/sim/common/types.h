#pragma once

namespace sim {

enum class RpcMethod : int {
    SHORT_RPC = 0,
    LONG_RPC = 1
};

enum class WorkloadType {
    W2_MMPP_BIMODAL,
    W3_POISSON_LOGNORMAL
};

enum class PlacementMode {
    REQUEST_RANDOM,
    FLOW_AFFINE
};

} // namespace sim
