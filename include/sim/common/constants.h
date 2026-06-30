#pragma once

namespace sim {

// Frozen physical constants (us).
inline constexpr double T_host_us       = 2.1;
inline constexpr double T_net_oneway_us = 3.15;
inline constexpr double T_rpc_us        = 6.3;   // 2 * T_net_oneway_us

// Frozen cluster topology.
inline constexpr int NUM_HOSTS       = 64;
inline constexpr int CORES_PER_HOST  = 16;
inline constexpr int TOTAL_CORES     = NUM_HOSTS * CORES_PER_HOST;

// Frozen SLO thresholds (us).
inline constexpr double SLO_SHORT_US = 40.0;
inline constexpr double SLO_LONG_US  = 200.0;
inline constexpr double SLO_SHORT_SERVICE_THRESHOLD_US = 20.0;

// Frozen statistical protocol.
inline constexpr int WARMUP_REQUESTS     = 200000;
inline constexpr int MEASUREMENT_REQUESTS = 1000000;

// Frozen B2 parameters.
inline constexpr int    B2_N_MOVE        = 1;
inline constexpr int    B2_K_DST         = 2;
inline constexpr double B2_T_COOLDOWN_US = 2.0;
inline constexpr int    B2_H_Q           = 2;
inline constexpr double B2_R_BAD         = 0.3;
inline constexpr double B2_BUDGET        = 0.05;

// Frozen M0 parameters.
inline constexpr double M0_T_CHECK_US        = 1.0;
inline constexpr double M0_ALPHA             = 0.8;
inline constexpr double M0_T_MARGIN_US       = 1.5;
inline constexpr int    M0_K_DST             = 4;
inline constexpr int    M0_N_MOVE_PER_CHECK  = 1;
inline constexpr double M0_BUDGET            = 0.05;

// AQB-PM v1 parameters: bounded queue-prefix scan + batched migration.
inline constexpr int    AQB_SCAN_DEPTH        = 4;
inline constexpr int    AQB_MAX_BATCH_PER_HOST = 4;
inline constexpr int    AQB_MAX_PER_CORE       = 1;
inline constexpr int    AQB_MAX_PER_DST        = 2;
inline constexpr double AQB_EFFECTIVE_BUDGET   = 0.045;
inline constexpr double AQB_SATURATION_P25_US  = 250.0;
inline constexpr double AQB_PRESSURE_WEIGHT    = 0.01;
inline constexpr double AQB_SATURATION_MARGIN_MULT = 2.0;

// DQB-PM parameters: distribution-aware queue-batch control.
inline constexpr int    DQB_SUMMARY_SCAN_LIMIT     = 256;
inline constexpr int    DQB_MAX_BATCHES_PER_HOST   = 8;
inline constexpr int    DQB_MAX_TASKS_PER_BATCH    = 64;
inline constexpr int    DQB_MIN_TASKS_PER_BATCH    = 8;
inline constexpr int    DQB_SEGMENT_TARGET_TASKS   = 16;
inline constexpr int    DQB_W3_MIN_TASKS_PER_BATCH = 4;
inline constexpr int    DQB_W3_SEGMENT_TARGET_TASKS = 8;
inline constexpr int    DQB_W3_MIN_FRAGMENT_TASKS = 1;
inline constexpr int    DQB_W3_HOST_MIN_TASKS = 8;
inline constexpr int    DQB_W3_HOST_TARGET_TASKS = 16;
inline constexpr int    DQB_W3_HOST_MAX_FRAGMENTS = 12;
inline constexpr int    DQB_SEGMENT_EXPAND_LIMIT   = 4;
inline constexpr int    DQB_MAX_PER_DST_BATCHES    = 2;
inline constexpr double DQB_MAX_BATCH_WORK_US      = 2400.0;
inline constexpr double DQB_MIN_BATCH_WORK_US      = 80.0;
inline constexpr double DQB_W3_MIN_BATCH_WORK_US   = 40.0;
inline constexpr double DQB_W3_HOST_AGE_SPREAD_US  = 160.0;
inline constexpr double DQB_EFFECTIVE_BUDGET       = 0.045;
inline constexpr double DQB_RESERVATION_LIMIT_US   = 600.0;
inline constexpr double DQB_TARGET_HARM_LIMIT_US   = 250.0;
inline constexpr double DQB_REMOTE_DEADLINE_MULT   = 1.5;
inline constexpr double DQB_SATURATION_P25_US      = 250.0;
inline constexpr double DQB_SATURATION_MARGIN_MULT = 2.0;
inline constexpr double DQB_PRESSURE_WEIGHT        = 0.02;
inline constexpr double DQB_ELEPHANT_SERVICE_US    = 80.0;
inline constexpr double DQB_SLOW_CAPACITY_THRESHOLD = 0.5;
inline constexpr double DQB_SHORT_DOMINANT_RATIO   = 0.65;
inline constexpr double DQB_MICE_DOMINANT_RATIO    = 0.65;
inline constexpr double DQB_BLOCKING_WORK_THRESHOLD_US = 80.0;
inline constexpr double DQB_W3_MICE_DOMINANT_RATIO = 0.55;
inline constexpr double DQB_W3_BLOCKING_WORK_THRESHOLD_US = 40.0;

// Intra-host core-scheduling parameters.
inline constexpr int    INTRA_PROACTIVE_SCAN_DEPTH = 32;
inline constexpr int    INTRA_MAX_MOVES_PER_CHECK  = 1;

// Frozen W2 MMPP parameters.
inline constexpr double W2_LAMBDA_BURST_FACTOR = 1.5;
inline constexpr double W2_NORMAL_STAY_US      = 5000.0;
inline constexpr double W2_BURST_STAY_US       = 500.0;

// Bimodal service time (W1/W2).
inline constexpr double BIMODAL_SHORT_US   = 5.0;
inline constexpr double BIMODAL_LONG_US    = 100.0;
inline constexpr double BIMODAL_SHORT_PROB = 0.80;
// E[S] = 0.8*5 + 0.2*100 = 24 us

// SYNC_LOAD default period.
inline constexpr double SYNC_LOAD_PERIOD_US = 10.0;

// Frozen seeds.
inline constexpr int SEED_COUNT = 5;
inline constexpr unsigned SEEDS[SEED_COUNT] = {11, 23, 37, 47, 59};

// M0 runtime-overridable config (defaults match frozen constants).
struct M0Config {
    double alpha      = M0_ALPHA;
    double margin_us  = M0_T_MARGIN_US;
    double t_check_us = M0_T_CHECK_US;
    int    k_dst      = M0_K_DST;
    int    aqb_max_batch_per_host = AQB_MAX_BATCH_PER_HOST;
    int    dqb_max_batches_per_host = DQB_MAX_BATCHES_PER_HOST;
    int    dqb_max_tasks_per_batch = DQB_MAX_TASKS_PER_BATCH;
};

// Heterogeneous cluster constants.
inline constexpr int    HETERO_FAST_NODES   = 48;
inline constexpr int    HETERO_SLOW_NODES   = 16;
inline constexpr double HETERO_SLOW_CAPACITY = 0.2;

// W2 representative rho points.
inline constexpr double W2_RHO_POINTS[] = {0.50, 0.70, 0.85, 0.92};
inline constexpr int    W2_RHO_COUNT    = 4;

// W3 Lognormal parameters: E[S]=24us, sigma=1.0, mu=ln(24)-0.5*sigma^2
inline constexpr double W3_LOGNORMAL_SIGMA = 1.0;
inline constexpr double W3_LOGNORMAL_MU    = 2.6782363; // ln(24) - 0.5*1.0^2 ≈ 2.678
inline constexpr double W3_MEAN_SERVICE_US = 24.0;       // E[S] = exp(mu + sigma^2/2)

// W3 representative rho points (same as W2).
inline constexpr double W3_RHO_POINTS[] = {0.50, 0.70, 0.85, 0.92};
inline constexpr int    W3_RHO_COUNT    = 4;

} // namespace sim
