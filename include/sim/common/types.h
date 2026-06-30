#pragma once
#include <cstdint>

namespace sim {

using TaskId = uint64_t;

enum class EventType : int {
    TASK_FINISH    = 0,   // highest priority at same ts
    TASK_ARRIVE    = 1,
    TASK_EXECUTE   = 2,
    TASK_GENERATE  = 3,   // lowest priority at same ts
    SYNC_LOAD      = 4,
    CHECK_MIGRATION = 5
};

enum class MethodType {
    B0_IDEAL_CFCFS,
    L0_RANDOM_CORE,
    L1_WORK_STEALING,
    B1_POWER_OF_K,
    B2_REACTIVE_MIGRATION,
    M0_INTRA_HOST_PROACTIVE,
    M0_PROACTIVE_MIGRATION,
    M1_AQB_PROACTIVE_MIGRATION,
    M2_DQB_PROACTIVE_MIGRATION
};

enum class WorkloadType {
    W1_POISSON_BIMODAL,
    W2_MMPP_BIMODAL,
    W3_POISSON_LOGNORMAL
};

enum class ClusterProfile {
    HOMOGENEOUS,       // 64×C=1.0
    HETERO_25PCT       // 48×C=1.0 + 16×C=0.2
};

} // namespace sim
