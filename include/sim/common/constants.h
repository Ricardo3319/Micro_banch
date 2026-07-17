#pragma once

#include "sim/common/types.h"

namespace sim {

inline constexpr double T_host_us = 2.1;
inline constexpr int CORES_PER_HOST = 16;
inline constexpr double SLO_SHORT_US = 40.0;
inline constexpr double SLO_LONG_US = 200.0;
inline constexpr double SLO_SHORT_SERVICE_THRESHOLD_US = 20.0;

inline constexpr int WARMUP_REQUESTS = 200000;
inline constexpr int MEASUREMENT_REQUESTS = 1000000;

inline constexpr double W2_LAMBDA_BURST_FACTOR = 1.5;
inline constexpr double W2_NORMAL_STAY_US = 5000.0;
inline constexpr double W2_BURST_STAY_US = 500.0;

inline constexpr double BIMODAL_SHORT_US = 5.0;
inline constexpr double BIMODAL_LONG_US = 100.0;
inline constexpr double BIMODAL_SHORT_PROB = 0.80;

inline constexpr double W3_LOGNORMAL_SIGMA = 1.0;
inline constexpr double W3_LOGNORMAL_MU = 2.6782363;
inline constexpr double W3_MEAN_SERVICE_US = 24.0;

} // namespace sim
