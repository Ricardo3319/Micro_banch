#include "sim/core/simulator.h"
#include "sim/common/constants.h"
#include "sim/common/types.h"

#include <iostream>
#include <fstream>
#include <string>
#include <iomanip>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <direct.h>

static const char* method_name(sim::MethodType m) {
    switch (m) {
        case sim::MethodType::B0_IDEAL_CFCFS:          return "B0_IdealCFCFS";
        case sim::MethodType::L0_RANDOM_CORE:          return "L0_RandomCore";
        case sim::MethodType::L1_WORK_STEALING:        return "L1_WorkStealing";
        case sim::MethodType::B1_POWER_OF_K:           return "B1_PowerOf2";
        case sim::MethodType::B2_REACTIVE_MIGRATION:   return "B2_Reactive";
        case sim::MethodType::M0_INTRA_HOST_PROACTIVE: return "M0_IntraHostProactive";
        case sim::MethodType::M1_RESCUE_SCHED:         return "M1_RescueSched";
        case sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY: return "M1_RescueSched_NoTargetSafety";
        case sim::MethodType::M1_RESCUE_NO_RESCUABLE:  return "M1_RescueSched_NoRescuable";
        case sim::MethodType::M1_RESCUE_HYBRID:        return "M1_RescueSched_Hybrid";
        case sim::MethodType::M0_PROACTIVE_MIGRATION:  return "M0_Proactive";
        case sim::MethodType::M1_AQB_PROACTIVE_MIGRATION: return "M1_AQB_PM";
        case sim::MethodType::M2_DQB_PROACTIVE_MIGRATION: return "M2_DQB_PM";
    }
    return "Unknown";
}

static void write_diag_header_suffix(std::ofstream& csv) {
    csv << ",short_slo_violation_rate,long_slo_violation_rate,"
           "mice_slo_violation_rate,elephant_slo_violation_rate,"
           "migration_work_rate,migrated_work_us,target_harm_est_us,"
           "avg_destination_virtual_occupancy_us,avg_source_queue_depth,"
           "avg_source_queue_work_us,exact_batch_size_histogram,"
           "no_batch_formed_count,low_confidence_count,low_expected_gain_count,"
           "dst_reservation_high_count,dst_tail_harm_count,budget_exhausted_count,"
           "sparse_blocking_not_batchable_count,"
           "summary_update_cost_est_us,batch_estimation_cost_est_us,"
           "target_selection_cost_est_us";
}

static void write_diag_value_suffix(std::ofstream& csv,
                                    const sim::MetricsCollector& m,
                                    double total_generated_work_us) {
    csv << "," << m.short_slo_violation_rate()
        << "," << m.long_slo_violation_rate()
        << "," << m.mice_slo_violation_rate()
        << "," << m.elephant_slo_violation_rate()
        << "," << m.migration_work_rate(total_generated_work_us)
        << "," << m.migrated_work_us
        << "," << m.target_harm_est_us
        << "," << m.avg_destination_virtual_occupancy_us()
        << "," << m.avg_source_queue_depth()
        << "," << m.avg_source_queue_work_us()
        << ",\"" << m.exact_batch_size_histogram() << "\""
        << "," << m.no_batch_formed_count
        << "," << m.low_confidence_count
        << "," << m.low_expected_gain_count
        << "," << m.dst_reservation_high_count
        << "," << m.dst_tail_harm_count
        << "," << m.budget_exhausted_count
        << "," << m.sparse_blocking_not_batchable_count
        << "," << m.summary_update_cost_est_us()
        << "," << m.batch_estimation_cost_est_us()
        << "," << m.target_selection_cost_est_us();
}

static void ensure_dir(const std::string& path) {
    _mkdir(path.c_str());
}

// ========== Experiment B: Parameter Sensitivity Scan ==========
static int run_sensitivity(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }

    csv << "param_name,param_value,method,rho,seed,"
           "P99_us,P999_us,slo_violation_rate,"
           "migration_rate,invalid_migration_ratio,total_finished,total_generated\n";

    const double rho = 0.85;
    const auto wl = sim::WorkloadType::W2_MMPP_BIMODAL;
    int run_count = 0;

    // Helper to run one config and write CSV row.
    auto run_one = [&](const char* pname, double pval_display,
                       sim::MethodType method, const sim::M0Config& cfg, unsigned seed) {
        sim::Simulator eng;
        eng.configure(method, rho, seed, wl, sim::ClusterProfile::HOMOGENEOUS, cfg);
        eng.run();
        auto& m = eng.metrics();
        uint64_t gen = eng.total_generated();
        csv << pname << "," << pval_display << "," << method_name(method) << ","
            << rho << "," << seed << ","
            << std::setprecision(3) << m.p99() << ","
            << m.p999() << ","
            << std::setprecision(6) << m.slo_violation_rate() << ","
            << m.migration_rate(gen) << ","
            << m.invalid_migration_ratio() << ","
            << m.total_finished << "," << gen << "\n";
        ++run_count;
        std::cout << "[" << run_count << "/120] " << pname << "=" << pval_display
                  << " " << method_name(method) << " seed=" << seed
                  << "  P99=" << std::setprecision(1) << m.p99()
                  << " P999=" << m.p999()
                  << " mr=" << std::setprecision(4) << m.migration_rate(gen)
                  << " imr=" << m.invalid_migration_ratio() << "\n";
    };

    // ---- B2 baseline (5 seeds) ----
    std::cout << "=== Sensitivity: B2 baseline (W2 rho=0.85) ===\n";
    for (int si = 0; si < sim::SEED_COUNT; ++si) {
        sim::M0Config dummy;
        run_one("baseline", 0.0, sim::MethodType::B2_REACTIVE_MIGRATION,
                dummy, sim::SEEDS[si]);
    }

    // ---- Alpha scan ----
    std::cout << "\n=== Sensitivity: alpha scan ===\n";
    double alphas[] = {0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    for (double a : alphas) {
        sim::M0Config cfg;
        cfg.alpha = a;
        for (int si = 0; si < sim::SEED_COUNT; ++si)
            run_one("alpha", a, sim::MethodType::M0_PROACTIVE_MIGRATION,
                    cfg, sim::SEEDS[si]);
    }

    // ---- T_margin scan ----
    std::cout << "\n=== Sensitivity: T_margin scan ===\n";
    double margins[] = {0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 5.0};
    for (double mg : margins) {
        sim::M0Config cfg;
        cfg.margin_us = mg;
        for (int si = 0; si < sim::SEED_COUNT; ++si)
            run_one("T_margin", mg, sim::MethodType::M0_PROACTIVE_MIGRATION,
                    cfg, sim::SEEDS[si]);
    }

    // ---- K_DST scan ----
    std::cout << "\n=== Sensitivity: K_DST scan ===\n";
    int kdsts[] = {1, 2, 4, 8, 16};
    for (int k : kdsts) {
        sim::M0Config cfg;
        cfg.k_dst = k;
        for (int si = 0; si < sim::SEED_COUNT; ++si)
            run_one("K_DST", static_cast<double>(k),
                    sim::MethodType::M0_PROACTIVE_MIGRATION, cfg, sim::SEEDS[si]);
    }

    // ---- T_CHECK scan ----
    std::cout << "\n=== Sensitivity: T_CHECK scan ===\n";
    double tchecks[] = {0.5, 1.0, 2.0, 5.0, 10.0};
    for (double tc : tchecks) {
        sim::M0Config cfg;
        cfg.t_check_us = tc;
        for (int si = 0; si < sim::SEED_COUNT; ++si)
            run_one("T_CHECK", tc, sim::MethodType::M0_PROACTIVE_MIGRATION,
                    cfg, sim::SEEDS[si]);
    }

    csv.close();
    std::cout << "\nSensitivity done: " << run_count << " runs -> " << csv_path << "\n";
    return 0;
}

// ========== Experiment A: Heterogeneous Cluster ==========
static int run_heterogeneous(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }

    csv << "cluster,workload,method,rho,seed,"
           "P99_us,P999_us,slo_violation_rate,"
           "migration_rate,invalid_migration_ratio,total_finished,total_generated\n";

    const auto wl = sim::WorkloadType::W2_MMPP_BIMODAL;
    const auto profile = sim::ClusterProfile::HETERO_25PCT;
    double rhos[] = {0.50, 0.70, 0.85, 0.92};
    sim::MethodType methods[] = {
        sim::MethodType::B1_POWER_OF_K,
        sim::MethodType::B2_REACTIVE_MIGRATION,
        sim::MethodType::M0_PROACTIVE_MIGRATION,
        sim::MethodType::M1_AQB_PROACTIVE_MIGRATION,
        sim::MethodType::M2_DQB_PROACTIVE_MIGRATION
    };
    const int total_runs = static_cast<int>(sizeof(rhos) / sizeof(rhos[0]))
                         * static_cast<int>(sizeof(methods) / sizeof(methods[0]))
                         * sim::SEED_COUNT;
    int run_count = 0;

    std::cout << "=== Experiment A: Heterogeneous (48 fast + 16 slow) W2 ===\n";
    std::cout << "Effective capacity = "
              << sim::Simulator::compute_effective_capacity(profile) << "\n";

    for (double rho : rhos) {
        for (auto method : methods) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg; // defaults
                eng.configure(method, rho, seed, wl, profile, cfg);
                eng.run();
                auto& m = eng.metrics();
                uint64_t gen = eng.total_generated();

                csv << "HETERO_25PCT," << "W2," << method_name(method) << ","
                    << rho << "," << seed << ","
                    << std::setprecision(3) << m.p99() << ","
                    << m.p999() << ","
                    << std::setprecision(6) << m.slo_violation_rate() << ","
                    << m.migration_rate(gen) << ","
                    << m.invalid_migration_ratio() << ","
                    << m.total_finished << "," << gen << "\n";

                ++run_count;
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << method_name(method)
                          << " rho=" << std::setprecision(2) << rho
                          << " seed=" << seed
                          << "  P99=" << std::setprecision(1) << m.p99()
                          << " P999=" << m.p999()
                          << " mr=" << std::setprecision(4) << m.migration_rate(gen)
                          << " imr=" << m.invalid_migration_ratio() << "\n";
            }
        }
    }

    csv.close();
    std::cout << "\nHeterogeneous done: " << run_count << " runs -> " << csv_path << "\n";
    return 0;
}

// ========== Regression Check ==========
static int run_regression() {
    std::cout << "=== Regression Check: Homogeneous default params ===\n";

    struct RegPoint {
        sim::WorkloadType wl;
        const char* wl_name;
        double rho;
        sim::MethodType method;
    };
    RegPoint points[] = {
        {sim::WorkloadType::W2_MMPP_BIMODAL,    "W2", 0.85, sim::MethodType::M0_PROACTIVE_MIGRATION},
        {sim::WorkloadType::W2_MMPP_BIMODAL,    "W2", 0.85, sim::MethodType::B2_REACTIVE_MIGRATION},
        {sim::WorkloadType::W3_POISSON_LOGNORMAL,"W3", 0.85, sim::MethodType::M0_PROACTIVE_MIGRATION},
        {sim::WorkloadType::W3_POISSON_LOGNORMAL,"W3", 0.85, sim::MethodType::B2_REACTIVE_MIGRATION},
    };

    for (auto& pt : points) {
        std::vector<double> p99s;
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            sim::Simulator eng;
            eng.configure(pt.method, pt.rho, sim::SEEDS[si], pt.wl);
            eng.run();
            p99s.push_back(eng.metrics().p99());
        }
        std::sort(p99s.begin(), p99s.end());
        double median = p99s[2]; // 5 seeds -> median is index 2
        std::cout << pt.wl_name << " rho=" << std::setprecision(2) << pt.rho << " "
                  << method_name(pt.method)
                  << "  P99 median=" << std::setprecision(1) << median
                  << "  [";
        for (size_t i = 0; i < p99s.size(); ++i)
            std::cout << (i ? "," : "") << p99s[i];
        std::cout << "]\n";
    }

    std::cout << "\nExpected from Step-04 freeze:\n"
              << "  W2 rho=0.85 M0 P99 median ~ 964\n"
              << "  W2 rho=0.85 B2 P99 median ~ 1610\n"
              << "  W3 rho=0.85 M0 P99 median ~ 180\n"
              << "  W3 rho=0.85 B2 P99 median ~ 200\n";
    return 0;
}

// ========== AQB-PM Smoke/Invariant Check ==========
static int run_aqb_smoke() {
    std::cout << "=== AQB-PM Smoke Check: W2 rho=0.85 seed=11 ===\n";

    sim::MethodType methods[] = {
        sim::MethodType::B2_REACTIVE_MIGRATION,
        sim::MethodType::M0_PROACTIVE_MIGRATION,
        sim::MethodType::M1_AQB_PROACTIVE_MIGRATION,
        sim::MethodType::M2_DQB_PROACTIVE_MIGRATION
    };

    bool ok = true;
    for (auto method : methods) {
        sim::Simulator eng;
        eng.configure(method, 0.85, 11, sim::WorkloadType::W2_MMPP_BIMODAL);
        eng.run();

        const auto& m = eng.metrics();
        uint64_t gen = eng.total_generated();
        double mr = m.migration_rate(gen);
        double imr = m.invalid_migration_ratio();
        std::cout << method_name(method)
                  << " P99=" << std::setprecision(3) << m.p99()
                  << " P999=" << m.p999()
                  << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                  << " mr=" << mr
                  << " imr=" << imr
                  << " batches=" << m.batch_selected_count
                  << " moved_by_batch=" << m.batch_move_count
                  << " size1=" << m.batch_size_1_count
                  << " size2_7=" << m.batch_size_2_7_count
                  << " size8_31=" << m.batch_size_8_31_count
                  << " size32p=" << m.batch_size_32_plus_count
                  << " type_short=" << m.batch_type_short_count
                  << " type_mice=" << m.batch_type_mice_count
                  << " type_dist=" << m.batch_type_distribution_count
                  << " finished=" << m.total_finished
                  << " generated=" << gen << "\n";

        if (m.total_finished != sim::MEASUREMENT_REQUESTS) ok = false;
        if (mr < 0.0 || mr > 0.055) ok = false;
        if (imr < 0.0 || imr > 1.0) ok = false;
    }

    std::cout << (ok ? "AQB smoke status: PASS\n" : "AQB smoke status: FAIL\n");
    return ok ? 0 : 2;
}

// ========== AQB-PM Representative Evaluation ==========
static int run_aqb_eval(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }

    struct Scenario {
        const char* name;
        sim::WorkloadType wl;
        sim::ClusterProfile profile;
        double rho;
    };

    Scenario scenarios[] = {
        {"W2_burst_homo", sim::WorkloadType::W2_MMPP_BIMODAL,
         sim::ClusterProfile::HOMOGENEOUS, 0.85},
        {"W3_heavytail_homo", sim::WorkloadType::W3_POISSON_LOGNORMAL,
         sim::ClusterProfile::HOMOGENEOUS, 0.85},
        {"W1_saturation_homo", sim::WorkloadType::W1_POISSON_BIMODAL,
         sim::ClusterProfile::HOMOGENEOUS, 0.95},
    };
    sim::MethodType methods[] = {
        sim::MethodType::B2_REACTIVE_MIGRATION,
        sim::MethodType::M0_PROACTIVE_MIGRATION,
        sim::MethodType::M1_AQB_PROACTIVE_MIGRATION,
        sim::MethodType::M2_DQB_PROACTIVE_MIGRATION
    };

    csv << "scenario,workload,cluster,method,rho,seed,"
           "P99_us,P999_us,slo_violation_rate,"
           "migration_rate,invalid_migration_ratio,total_finished,total_generated,"
           "batch_candidate_count,batch_selected_count,batch_move_count,"
           "summary_update_count,reservation_reject_count,saturation_guard_count,"
           "batch_size_1_count,batch_size_2_7_count,batch_size_8_31_count,batch_size_32_plus_count,"
           "batch_type_generic_count,batch_type_short_count,batch_type_mice_count,"
           "batch_type_slow_count,batch_type_distribution_count,"
           "target_plan_reject_count";
    write_diag_header_suffix(csv);
    csv << "\n";

    const int total_runs =
        static_cast<int>(sizeof(scenarios) / sizeof(scenarios[0]))
        * static_cast<int>(sizeof(methods) / sizeof(methods[0]))
        * sim::SEED_COUNT;
    int run_count = 0;

    auto workload_name = [](sim::WorkloadType wl) {
        switch (wl) {
            case sim::WorkloadType::W1_POISSON_BIMODAL: return "W1";
            case sim::WorkloadType::W2_MMPP_BIMODAL: return "W2";
            case sim::WorkloadType::W3_POISSON_LOGNORMAL: return "W3";
        }
        return "Unknown";
    };
    auto cluster_name = [](sim::ClusterProfile profile) {
        switch (profile) {
            case sim::ClusterProfile::HOMOGENEOUS: return "HOMOGENEOUS";
            case sim::ClusterProfile::HETERO_25PCT: return "HETERO_25PCT";
        }
        return "Unknown";
    };

    std::cout << "=== AQB-PM Representative Evaluation ===\n";
    for (const auto& sc : scenarios) {
        for (auto method : methods) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                eng.configure(method, sc.rho, seed, sc.wl, sc.profile, cfg);
                eng.run();
                const auto& m = eng.metrics();
                uint64_t gen = eng.total_generated();

                csv << sc.name << "," << workload_name(sc.wl) << ","
                    << cluster_name(sc.profile) << "," << method_name(method) << ","
                    << sc.rho << "," << seed << ","
                    << std::setprecision(3) << m.p99() << "," << m.p999() << ","
                    << std::setprecision(6) << m.slo_violation_rate() << ","
                    << m.migration_rate(gen) << "," << m.invalid_migration_ratio() << ","
                    << m.total_finished << "," << gen << ","
                    << m.batch_candidate_count << "," << m.batch_selected_count << ","
                    << m.batch_move_count << "," << m.summary_update_count << ","
                    << m.reservation_reject_count << "," << m.saturation_guard_count << ","
                    << m.batch_size_1_count << "," << m.batch_size_2_7_count << ","
                    << m.batch_size_8_31_count << "," << m.batch_size_32_plus_count << ","
                    << m.batch_type_generic_count << "," << m.batch_type_short_count << ","
                    << m.batch_type_mice_count << "," << m.batch_type_slow_count << ","
                    << m.batch_type_distribution_count << ","
                    << m.target_plan_reject_count;
                write_diag_value_suffix(csv, m, eng.total_generated_work_us());
                csv << "\n";

                ++run_count;
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << sc.name << " " << method_name(method)
                          << " seed=" << seed
                          << " P99=" << std::setprecision(1) << m.p99()
                          << " P999=" << m.p999()
                          << " mr=" << std::setprecision(4) << m.migration_rate(gen)
                          << " imr=" << m.invalid_migration_ratio() << "\n";
            }
        }
    }

    csv.close();
    std::cout << "\nAQB eval done: " << run_count << " runs -> " << csv_path << "\n";
    return 0;
}

// ========== AQB-PM Heterogeneous Evaluation ==========
static int run_aqb_heterogeneous(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }

    csv << "cluster,workload,method,rho,seed,"
           "P99_us,P999_us,slo_violation_rate,"
           "migration_rate,invalid_migration_ratio,total_finished,total_generated,"
           "batch_candidate_count,batch_selected_count,batch_move_count,"
           "summary_update_count,reservation_reject_count,saturation_guard_count,"
           "batch_size_1_count,batch_size_2_7_count,batch_size_8_31_count,batch_size_32_plus_count,"
           "batch_type_generic_count,batch_type_short_count,batch_type_mice_count,"
           "batch_type_slow_count,batch_type_distribution_count,"
           "target_plan_reject_count";
    write_diag_header_suffix(csv);
    csv << "\n";

    const auto wl = sim::WorkloadType::W2_MMPP_BIMODAL;
    const auto profile = sim::ClusterProfile::HETERO_25PCT;
    double rhos[] = {0.50, 0.70, 0.85, 0.92};
    sim::MethodType methods[] = {
        sim::MethodType::B1_POWER_OF_K,
        sim::MethodType::B2_REACTIVE_MIGRATION,
        sim::MethodType::M0_PROACTIVE_MIGRATION,
        sim::MethodType::M1_AQB_PROACTIVE_MIGRATION,
        sim::MethodType::M2_DQB_PROACTIVE_MIGRATION
    };

    const int total_runs =
        static_cast<int>(sizeof(rhos) / sizeof(rhos[0]))
        * static_cast<int>(sizeof(methods) / sizeof(methods[0]))
        * sim::SEED_COUNT;
    int run_count = 0;

    std::cout << "=== AQB-PM Heterogeneous Evaluation ===\n";
    for (double rho : rhos) {
        for (auto method : methods) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                eng.configure(method, rho, seed, wl, profile, cfg);
                eng.run();
                const auto& m = eng.metrics();
                uint64_t gen = eng.total_generated();

                csv << "HETERO_25PCT,W2," << method_name(method) << ","
                    << rho << "," << seed << ","
                    << std::setprecision(3) << m.p99() << "," << m.p999() << ","
                    << std::setprecision(6) << m.slo_violation_rate() << ","
                    << m.migration_rate(gen) << "," << m.invalid_migration_ratio() << ","
                    << m.total_finished << "," << gen << ","
                    << m.batch_candidate_count << "," << m.batch_selected_count << ","
                    << m.batch_move_count << "," << m.summary_update_count << ","
                    << m.reservation_reject_count << "," << m.saturation_guard_count << ","
                    << m.batch_size_1_count << "," << m.batch_size_2_7_count << ","
                    << m.batch_size_8_31_count << "," << m.batch_size_32_plus_count << ","
                    << m.batch_type_generic_count << "," << m.batch_type_short_count << ","
                    << m.batch_type_mice_count << "," << m.batch_type_slow_count << ","
                    << m.batch_type_distribution_count << ","
                    << m.target_plan_reject_count;
                write_diag_value_suffix(csv, m, eng.total_generated_work_us());
                csv << "\n";

                ++run_count;
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << method_name(method)
                          << " rho=" << std::setprecision(2) << rho
                          << " seed=" << seed
                          << " P99=" << std::setprecision(1) << m.p99()
                          << " P999=" << m.p999()
                          << " mr=" << std::setprecision(4) << m.migration_rate(gen)
                          << " imr=" << m.invalid_migration_ratio() << "\n";
            }
        }
    }

    csv.close();
    std::cout << "\nAQB hetero done: " << run_count << " runs -> " << csv_path << "\n";
    return 0;
}

// ========== AQB-PM Batch Size Sweep ==========
static int run_aqb_batch_sweep(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }

    csv << "scenario,batch_cap,method,rho,seed,"
           "P99_us,P999_us,slo_violation_rate,"
           "migration_rate,invalid_migration_ratio,total_finished,total_generated\n";

    int batch_caps[] = {1, 2, 4, 8};
    const double rho = 0.85;
    const auto wl = sim::WorkloadType::W2_MMPP_BIMODAL;
    const int total_runs =
        static_cast<int>(sizeof(batch_caps) / sizeof(batch_caps[0])) * sim::SEED_COUNT;
    int run_count = 0;

    std::cout << "=== AQB-PM Batch Size Sweep: W2 rho=0.85 ===\n";
    for (int cap : batch_caps) {
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            unsigned seed = sim::SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            cfg.aqb_max_batch_per_host = cap;
            eng.configure(sim::MethodType::M1_AQB_PROACTIVE_MIGRATION,
                          rho, seed, wl, sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            const auto& m = eng.metrics();
            uint64_t gen = eng.total_generated();

            csv << "W2_burst_homo," << cap << "," << method_name(sim::MethodType::M1_AQB_PROACTIVE_MIGRATION)
                << "," << rho << "," << seed << ","
                << std::setprecision(3) << m.p99() << "," << m.p999() << ","
                << std::setprecision(6) << m.slo_violation_rate() << ","
                << m.migration_rate(gen) << "," << m.invalid_migration_ratio() << ","
                << m.total_finished << "," << gen << "\n";

            ++run_count;
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << "batch=" << cap << " seed=" << seed
                      << " P99=" << std::setprecision(1) << m.p99()
                      << " P999=" << m.p999()
                      << " mr=" << std::setprecision(4) << m.migration_rate(gen)
                      << " imr=" << m.invalid_migration_ratio() << "\n";
        }
    }

    csv.close();
    std::cout << "\nAQB batch sweep done: " << run_count << " runs -> " << csv_path << "\n";
    return 0;
}

// ========== DQB-PM Focused Scenario Check ==========
static int run_dqb_focus(const std::string& title,
                         const std::string& scenario_name,
                         sim::WorkloadType wl,
                         double rho,
                         const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }

    csv << "scenario,method,rho,seed,"
           "P99_us,P999_us,slo_violation_rate,"
           "migration_rate,invalid_migration_ratio,total_finished,total_generated,"
           "batch_candidate_count,batch_selected_count,batch_move_count,"
           "batch_size_1_count,batch_size_2_7_count,batch_size_8_31_count,batch_size_32_plus_count,"
           "batch_type_generic_count,batch_type_short_count,batch_type_mice_count,"
           "batch_type_slow_count,batch_type_distribution_count,"
           "target_plan_reject_count,saturation_guard_count";
    write_diag_header_suffix(csv);
    csv << "\n";

    int run_count = 0;
    std::cout << "=== " << title << " ===\n";
    for (int si = 0; si < sim::SEED_COUNT; ++si) {
        unsigned seed = sim::SEEDS[si];
        sim::Simulator eng;
        sim::M0Config cfg;
        eng.configure(sim::MethodType::M2_DQB_PROACTIVE_MIGRATION,
                      rho, seed, wl, sim::ClusterProfile::HOMOGENEOUS, cfg);
        eng.run();
        const auto& m = eng.metrics();
        uint64_t gen = eng.total_generated();

        csv << scenario_name << "," << method_name(sim::MethodType::M2_DQB_PROACTIVE_MIGRATION)
            << "," << rho << "," << seed << ","
            << std::setprecision(3) << m.p99() << "," << m.p999() << ","
            << std::setprecision(6) << m.slo_violation_rate() << ","
            << m.migration_rate(gen) << "," << m.invalid_migration_ratio() << ","
            << m.total_finished << "," << gen << ","
            << m.batch_candidate_count << "," << m.batch_selected_count << ","
            << m.batch_move_count << ","
            << m.batch_size_1_count << "," << m.batch_size_2_7_count << ","
            << m.batch_size_8_31_count << "," << m.batch_size_32_plus_count << ","
            << m.batch_type_generic_count << "," << m.batch_type_short_count << ","
            << m.batch_type_mice_count << "," << m.batch_type_slow_count << ","
            << m.batch_type_distribution_count << ","
            << m.target_plan_reject_count << "," << m.saturation_guard_count;
        write_diag_value_suffix(csv, m, eng.total_generated_work_us());
        csv << "\n";

        ++run_count;
        std::cout << "[" << run_count << "/" << sim::SEED_COUNT << "] seed=" << seed
                  << " P99=" << std::setprecision(1) << m.p99()
                  << " P999=" << m.p999()
                  << " mr=" << std::setprecision(4) << m.migration_rate(gen)
                  << " imr=" << m.invalid_migration_ratio()
                  << " batches=" << m.batch_selected_count
                  << " moved=" << m.batch_move_count << "\n";
    }

    csv.close();
    std::cout << "\nFocused DQB check done -> " << csv_path << "\n";
    return 0;
}

static const char* workload_name(sim::WorkloadType wl) {
    switch (wl) {
        case sim::WorkloadType::W1_POISSON_BIMODAL: return "W1";
        case sim::WorkloadType::W2_MMPP_BIMODAL: return "W2";
        case sim::WorkloadType::W3_POISSON_LOGNORMAL: return "W3";
    }
    return "Unknown";
}

static constexpr int RESCUE_10_SEED_COUNT = 10;
static constexpr unsigned RESCUE_10_SEEDS[RESCUE_10_SEED_COUNT] = {
    11, 23, 37, 47, 59, 71, 83, 97, 109, 131
};

static const char* service_estimate_name(int mode) {
    switch (mode) {
        case sim::SERVICE_ESTIMATE_ORACLE: return "oracle";
        case sim::SERVICE_ESTIMATE_MEAN: return "mean";
        case sim::SERVICE_ESTIMATE_NOISY_ORACLE: return "noisy_oracle";
        case sim::SERVICE_ESTIMATE_CLASS_MEAN: return "class_mean";
        case sim::SERVICE_ESTIMATE_EWMA: return "ewma";
        case sim::SERVICE_ESTIMATE_QUANTILE_GUARD: return "quantile_guard";
    }
    return "unknown";
}

static const char* target_insert_policy_name(int mode) {
    switch (mode) {
        case sim::RESCUE_TARGET_INSERT_APPEND_TAIL: return "append_tail";
        case sim::RESCUE_TARGET_INSERT_HEAD_STRESS: return "head_insert_stress";
    }
    return "unknown";
}

static void write_intra_header(std::ofstream& csv) {
    csv << "scenario,workload,method,rho,seed,"
           "P99_us,P999_us,slo_violation_rate,total_finished,total_generated,"
           "migration_rate,invalid_migration_ratio,"
           "intra_move_rate,intra_move_count,intra_moved_work_us,"
           "steal_attempt_count,steal_success_count,stolen_task_count,"
           "proactive_intra_attempt_count,proactive_intra_success_count,"
           "invalid_intra_move_ratio\n";
}

static void write_intra_row(std::ofstream& csv,
                            const char* scenario,
                            sim::WorkloadType wl,
                            sim::MethodType method,
                            double rho,
                            unsigned seed,
                            const sim::Simulator& eng) {
    const auto& m = eng.metrics();
    uint64_t gen = eng.total_generated();
    csv << scenario << "," << workload_name(wl) << "," << method_name(method)
        << "," << rho << "," << seed << ","
        << std::setprecision(3) << m.p99() << "," << m.p999() << ","
        << std::setprecision(6) << m.slo_violation_rate() << ","
        << m.total_finished << "," << gen << ","
        << m.migration_rate(gen) << "," << m.invalid_migration_ratio() << ","
        << m.intra_move_rate(gen) << "," << m.intra_move_count << ","
        << m.intra_moved_work_us << ","
        << m.steal_attempt_count << "," << m.steal_success_count << ","
        << m.stolen_task_count << ","
        << m.proactive_intra_attempt_count << ","
        << m.proactive_intra_success_count << ","
        << m.invalid_intra_move_ratio() << "\n";
}

static void write_intra_tuning_header(std::ofstream& csv) {
    csv << "scenario,workload,method,rho,seed,check_period_us,"
           "P99_us,P999_us,slo_violation_rate,total_finished,total_generated,"
           "migration_rate,invalid_migration_ratio,"
           "intra_move_rate,intra_move_count,intra_moved_work_us,"
           "steal_attempt_count,steal_success_count,stolen_task_count,"
           "proactive_intra_attempt_count,proactive_intra_success_count,"
           "invalid_intra_move_ratio\n";
}

static void write_intra_tuning_row(std::ofstream& csv,
                                   const char* scenario,
                                   sim::WorkloadType wl,
                                   sim::MethodType method,
                                   double rho,
                                   unsigned seed,
                                   double check_period_us,
                                   const sim::Simulator& eng) {
    const auto& m = eng.metrics();
    uint64_t gen = eng.total_generated();
    csv << scenario << "," << workload_name(wl) << "," << method_name(method)
        << "," << rho << "," << seed << "," << check_period_us << ","
        << std::setprecision(3) << m.p99() << "," << m.p999() << ","
        << std::setprecision(6) << m.slo_violation_rate() << ","
        << m.total_finished << "," << gen << ","
        << m.migration_rate(gen) << "," << m.invalid_migration_ratio() << ","
        << m.intra_move_rate(gen) << "," << m.intra_move_count << ","
        << m.intra_moved_work_us << ","
        << m.steal_attempt_count << "," << m.steal_success_count << ","
        << m.stolen_task_count << ","
        << m.proactive_intra_attempt_count << ","
        << m.proactive_intra_success_count << ","
        << m.invalid_intra_move_ratio() << "\n";
}

static bool method_has_target_safety(sim::MethodType method) {
    return method != sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY;
}

static bool method_has_rescuable_filter(sim::MethodType method) {
    return method != sim::MethodType::M1_RESCUE_NO_RESCUABLE;
}

static void write_rescue_header(std::ofstream& csv) {
    csv << "scenario,workload,method,rho,seed,"
           "check_period_us,epsilon_us,budget_per_check,k_candidates,h_targets,"
           "migration_cost_us,service_estimate_mode,service_estimate_noise_cv,"
           "service_estimate_ewma_alpha,w2_hot_core_count,w2_hot_dispatch_prob,"
           "target_insert_policy,hybrid_pressure_ratio,hybrid_min_gain_us,"
           "target_safety_enabled,rescuable_filter_enabled,"
           "P99_us,P999_us,slo_violation_rate,total_finished,total_generated,"
           "migration_rate,invalid_migration_ratio,"
           "intra_move_rate,intra_move_count,intra_moved_work_us,"
           "steal_attempt_count,steal_success_count,stolen_task_count,"
           "proactive_intra_attempt_count,proactive_intra_success_count,"
           "invalid_intra_move_ratio,"
           "rescue_attempt_count,rescue_candidate_count,locally_doomed_count,"
           "remote_feasible_count,target_safe_count,rescue_success_count,"
           "rescue_moved_work_us,target_unsafe_reject_count,"
           "remote_infeasible_reject_count,needless_migration_count,"
           "unsaved_migration_count,beneficial_migration_count,"
           "harmful_migration_count,predicted_target_unsafe_accept_count,"
           "target_harm_watch_count,harmful_actual_count,harmful_actual_ratio,"
           "target_induced_miss_actual,beneficial_migration_ratio,"
           "useless_migration_ratio,rescue_per_migration,"
           "relief_attempt_count,relief_success_count,relief_beneficial_count,"
           "relief_useless_count,relief_moved_work_us,"
           "relief_beneficial_migration_ratio,relief_useless_migration_ratio\n";
}

static void write_rescue_row(std::ofstream& csv,
                             const char* scenario,
                             sim::WorkloadType wl,
                             sim::MethodType method,
                             double rho,
                             unsigned seed,
                             const sim::M0Config& cfg,
                             const sim::Simulator& eng) {
    const auto& m = eng.metrics();
    uint64_t gen = eng.total_generated();
    csv << scenario << "," << workload_name(wl) << "," << method_name(method)
        << "," << rho << "," << seed << ","
        << cfg.t_check_us << "," << cfg.rescue_epsilon_us << ","
        << cfg.rescue_budget_per_check << "," << cfg.rescue_k_candidates << ","
        << cfg.rescue_h_targets << ","
        << cfg.rescue_migration_cost_us << ","
        << service_estimate_name(cfg.service_estimate_mode) << ","
        << cfg.service_estimate_noise_cv << ","
        << cfg.service_estimate_ewma_alpha << ","
        << cfg.w2_hot_core_count << "," << cfg.w2_hot_dispatch_prob << ","
        << target_insert_policy_name(cfg.rescue_target_insert_policy) << ","
        << cfg.rescue_hybrid_pressure_ratio << ","
        << cfg.rescue_hybrid_min_gain_us << ","
        << (method_has_target_safety(method) ? 1 : 0) << ","
        << (method_has_rescuable_filter(method) ? 1 : 0) << ","
        << std::setprecision(3) << m.p99() << "," << m.p999() << ","
        << std::setprecision(6) << m.slo_violation_rate() << ","
        << m.total_finished << "," << gen << ","
        << m.migration_rate(gen) << "," << m.invalid_migration_ratio() << ","
        << m.intra_move_rate(gen) << "," << m.intra_move_count << ","
        << m.intra_moved_work_us << ","
        << m.steal_attempt_count << "," << m.steal_success_count << ","
        << m.stolen_task_count << ","
        << m.proactive_intra_attempt_count << ","
        << m.proactive_intra_success_count << ","
        << m.invalid_intra_move_ratio() << ","
        << m.rescue_attempt_count << "," << m.rescue_candidate_count << ","
        << m.locally_doomed_count << "," << m.remote_feasible_count << ","
        << m.target_safe_count << "," << m.rescue_success_count << ","
        << m.rescue_moved_work_us << "," << m.target_unsafe_reject_count << ","
        << m.remote_infeasible_reject_count << ","
        << m.needless_migration_count << "," << m.unsaved_migration_count << ","
        << m.beneficial_migration_count << "," << m.harmful_migration_count << ","
        << m.predicted_target_unsafe_accept_count << ","
        << m.target_harm_watch_count << ","
        << m.harmful_actual_count << "," << m.harmful_actual_ratio() << ","
        << m.target_induced_miss_actual << ","
        << m.beneficial_migration_ratio() << ","
        << m.useless_migration_ratio() << ","
        << m.rescue_per_migration() << ","
        << m.relief_attempt_count << "," << m.relief_success_count << ","
        << m.relief_beneficial_count << "," << m.relief_useless_count << ","
        << m.relief_moved_work_us << ","
        << m.relief_beneficial_migration_ratio() << ","
        << m.relief_useless_migration_ratio() << "\n";
}

static int run_intra_smoke() {
    std::cout << "=== Intra-host Smoke: W3 rho=0.85 seed=11 ===\n";
    sim::MethodType methods[] = {
        sim::MethodType::L0_RANDOM_CORE,
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE
    };

    bool ok = true;
    for (auto method : methods) {
        sim::Simulator eng;
        sim::M0Config cfg;
        eng.configure(method, 0.85, 11, sim::WorkloadType::W3_POISSON_LOGNORMAL,
                      sim::ClusterProfile::HOMOGENEOUS, cfg);
        eng.run();
        const auto& m = eng.metrics();
        uint64_t gen = eng.total_generated();

        std::cout << method_name(method)
                  << " P99=" << std::setprecision(3) << m.p99()
                  << " P999=" << m.p999()
                  << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                  << " intra_rate=" << m.intra_move_rate(gen)
                  << " moves=" << m.intra_move_count
                  << " steal_attempts=" << m.steal_attempt_count
                  << " steal_success=" << m.steal_success_count
                  << " proactive_attempts=" << m.proactive_intra_attempt_count
                  << " proactive_success=" << m.proactive_intra_success_count
                  << " invalid_intra=" << m.invalid_intra_move_ratio()
                  << " cross_mr=" << m.migration_rate(gen)
                  << " finished=" << m.total_finished
                  << " generated=" << gen << "\n";

        if (m.total_finished != sim::MEASUREMENT_REQUESTS) ok = false;
        if (m.p99() <= 0.0 || m.p999() <= 0.0) ok = false;
        if (m.migration_rate(gen) != 0.0) ok = false;
        if (method == sim::MethodType::L0_RANDOM_CORE && m.intra_move_count != 0) ok = false;
        if (method == sim::MethodType::L1_WORK_STEALING
            && (m.steal_attempt_count == 0 || m.proactive_intra_attempt_count != 0)) ok = false;
        if (method == sim::MethodType::M0_INTRA_HOST_PROACTIVE
            && (m.proactive_intra_attempt_count == 0 || m.steal_attempt_count != 0)) ok = false;
    }

    std::cout << (ok ? "Intra-host smoke status: PASS\n"
                     : "Intra-host smoke status: FAIL\n");
    return ok ? 0 : 2;
}

static int run_intra_w3_only(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_intra_header(csv);

    sim::MethodType methods[] = {
        sim::MethodType::L0_RANDOM_CORE,
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE
    };

    int run_count = 0;
    const int total_runs = 3 * sim::SEED_COUNT;
    std::cout << "=== Intra-host W3 Focus: rho=0.85 ===\n";
    for (auto method : methods) {
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            unsigned seed = sim::SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            eng.configure(method, 0.85, seed, sim::WorkloadType::W3_POISSON_LOGNORMAL,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_intra_row(csv, "intra_w3_heavytail", sim::WorkloadType::W3_POISSON_LOGNORMAL,
                            method, 0.85, seed, eng);
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << method_name(method) << " seed=" << seed
                      << " P99=" << std::setprecision(1) << m.p99()
                      << " P999=" << m.p999()
                      << " moves=" << m.intra_move_count << "\n";
        }
    }
    std::cout << "\nIntra-host W3 focus done -> " << csv_path << "\n";
    return 0;
}

static int run_intra_focus(const std::string& title,
                           const char* scenario,
                           sim::WorkloadType wl,
                           double rho,
                           const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_intra_header(csv);

    sim::MethodType methods[] = {
        sim::MethodType::L0_RANDOM_CORE,
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE
    };

    int run_count = 0;
    const int total_runs = 3 * sim::SEED_COUNT;
    std::cout << "=== " << title << " ===\n";
    for (auto method : methods) {
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            unsigned seed = sim::SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            eng.configure(method, rho, seed, wl,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_intra_row(csv, scenario, wl, method, rho, seed, eng);
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << method_name(method) << " seed=" << seed
                      << " P99=" << std::setprecision(1) << m.p99()
                      << " P999=" << m.p999()
                      << " moves=" << m.intra_move_count << "\n";
        }
    }
    std::cout << "\n" << title << " done -> " << csv_path << "\n";
    return 0;
}

static int run_intra_check_period_sweep(const std::string& title,
                                        const char* scenario,
                                        sim::WorkloadType wl,
                                        double rho,
                                        const std::vector<double>& check_periods,
                                        const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_intra_tuning_header(csv);
    csv.flush();

    const int baseline_runs = sim::SEED_COUNT;
    const int proactive_runs =
        static_cast<int>(check_periods.size()) * sim::SEED_COUNT;
    const int total_runs = baseline_runs + proactive_runs;
    int run_count = 0;

    std::cout << "=== " << title << " ===\n";
    for (int si = 0; si < sim::SEED_COUNT; ++si) {
        unsigned seed = sim::SEEDS[si];
        sim::Simulator eng;
        sim::M0Config cfg;
        eng.configure(sim::MethodType::L1_WORK_STEALING, rho, seed, wl,
                      sim::ClusterProfile::HOMOGENEOUS, cfg);
        eng.run();
        write_intra_tuning_row(csv, scenario, wl, sim::MethodType::L1_WORK_STEALING,
                               rho, seed, 0.0, eng);
        csv.flush();
        ++run_count;
        const auto& m = eng.metrics();
        std::cout << "[" << run_count << "/" << total_runs << "] "
                  << method_name(sim::MethodType::L1_WORK_STEALING)
                  << " seed=" << seed
                  << " P99=" << std::setprecision(1) << m.p99()
                  << " P999=" << m.p999()
                  << " moves=" << m.intra_move_count << "\n";
        std::cout.flush();
    }

    for (double check_period_us : check_periods) {
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            unsigned seed = sim::SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            cfg.t_check_us = check_period_us;
            eng.configure(sim::MethodType::M0_INTRA_HOST_PROACTIVE, rho, seed, wl,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_intra_tuning_row(csv, scenario, wl,
                                   sim::MethodType::M0_INTRA_HOST_PROACTIVE,
                                   rho, seed, check_period_us, eng);
            csv.flush();
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << method_name(sim::MethodType::M0_INTRA_HOST_PROACTIVE)
                      << " check=" << std::setprecision(3) << check_period_us
                      << " seed=" << seed
                      << " P99=" << std::setprecision(1) << m.p99()
                      << " P999=" << m.p999()
                      << " moves=" << m.intra_move_count
                      << " proactive_attempts=" << m.proactive_intra_attempt_count
                      << "\n";
            std::cout.flush();
        }
    }

    std::cout << "\n" << title << " done -> " << csv_path << "\n";
    return 0;
}

static int run_intra_main(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_intra_header(csv);

    double rhos[] = {0.50, 0.70, 0.85, 0.92};
    sim::MethodType methods[] = {
        sim::MethodType::L0_RANDOM_CORE,
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE
    };

    const int total_runs =
        static_cast<int>(sizeof(rhos) / sizeof(rhos[0])) * 3 * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== Intra-host Main: W3 rho sweep ===\n";
    for (double rho : rhos) {
        for (auto method : methods) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                eng.configure(method, rho, seed, sim::WorkloadType::W3_POISSON_LOGNORMAL,
                              sim::ClusterProfile::HOMOGENEOUS, cfg);
                eng.run();
                write_intra_row(csv, "intra_w3_heavytail",
                                sim::WorkloadType::W3_POISSON_LOGNORMAL,
                                method, rho, seed, eng);
                ++run_count;
                const auto& m = eng.metrics();
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << method_name(method)
                          << " rho=" << std::setprecision(2) << rho
                          << " seed=" << seed
                          << " P99=" << std::setprecision(1) << m.p99()
                          << " P999=" << m.p999()
                          << " moves=" << m.intra_move_count << "\n";
            }
        }
    }
    std::cout << "\nIntra-host main done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_smoke() {
    std::cout << "=== RescueSched Smoke: W3 rho=0.85 seed=11 ===\n";
    sim::MethodType methods[] = {
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE,
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY
    };

    bool ok = true;
    for (auto method : methods) {
        sim::Simulator eng;
        sim::M0Config cfg;
        eng.configure(method, 0.85, 11, sim::WorkloadType::W3_POISSON_LOGNORMAL,
                      sim::ClusterProfile::HOMOGENEOUS, cfg);
        eng.run();
        const auto& m = eng.metrics();
        uint64_t gen = eng.total_generated();

        std::cout << method_name(method)
                  << " P99=" << std::setprecision(3) << m.p99()
                  << " P999=" << m.p999()
                  << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                  << " intra_rate=" << m.intra_move_rate(gen)
                  << " moves=" << m.intra_move_count
                  << " rescue_moves=" << m.rescue_success_count
                  << " BMR=" << m.beneficial_migration_ratio()
                  << " UMR=" << m.useless_migration_ratio()
                  << " RPM=" << m.rescue_per_migration()
                  << " cross_mr=" << m.migration_rate(gen)
                  << " finished=" << m.total_finished
                  << " generated=" << gen << "\n";

        if (m.total_finished != sim::MEASUREMENT_REQUESTS) ok = false;
        if (m.p99() <= 0.0 || m.p999() <= 0.0) ok = false;
        if (m.migration_rate(gen) != 0.0) ok = false;
        if (method == sim::MethodType::M1_RESCUE_SCHED
            && (m.rescue_attempt_count == 0 || m.rescue_success_count == 0)) ok = false;
    }

    std::cout << (ok ? "RescueSched smoke status: PASS\n"
                     : "RescueSched smoke status: FAIL\n");
    return ok ? 0 : 2;
}

static int run_rescue_w3_only(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    sim::MethodType methods[] = {
        sim::MethodType::L0_RANDOM_CORE,
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE,
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY
    };

    const int total_runs = 5 * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched W3 Focus: rho=0.85 ===\n";
    for (auto method : methods) {
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            unsigned seed = sim::SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            eng.configure(method, 0.85, seed, sim::WorkloadType::W3_POISSON_LOGNORMAL,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_rescue_row(csv, "rescue_w3_heavytail_focus",
                             sim::WorkloadType::W3_POISSON_LOGNORMAL,
                             method, 0.85, seed, cfg, eng);
            csv.flush();
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << method_name(method) << " seed=" << seed
                      << " P99=" << std::setprecision(1) << m.p99()
                      << " P999=" << m.p999()
                      << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                      << " rescue_moves=" << m.rescue_success_count
                      << " BMR=" << m.beneficial_migration_ratio()
                      << "\n";
        }
    }
    std::cout << "\nRescueSched W3 focus done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_main(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    double rhos[] = {0.50, 0.70, 0.85, 0.92};
    sim::MethodType methods[] = {
        sim::MethodType::L0_RANDOM_CORE,
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE,
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY
    };

    const int total_runs = 4 * 5 * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Main: W3 rho sweep ===\n";
    for (double rho : rhos) {
        for (auto method : methods) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                eng.configure(method, rho, seed, sim::WorkloadType::W3_POISSON_LOGNORMAL,
                              sim::ClusterProfile::HOMOGENEOUS, cfg);
                eng.run();
                write_rescue_row(csv, "rescue_w3_heavytail_main",
                                 sim::WorkloadType::W3_POISSON_LOGNORMAL,
                                 method, rho, seed, cfg, eng);
                csv.flush();
                ++run_count;
                const auto& m = eng.metrics();
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << method_name(method)
                          << " rho=" << std::setprecision(2) << rho
                          << " seed=" << seed
                          << " P99=" << std::setprecision(1) << m.p99()
                          << " P999=" << m.p999()
                          << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                          << " rescue_moves=" << m.rescue_success_count
                          << " BMR=" << m.beneficial_migration_ratio()
                          << "\n";
                std::cout.flush();
            }
        }
    }
    std::cout << "\nRescueSched main done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_ablation(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    sim::MethodType methods[] = {
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY,
        sim::MethodType::M1_RESCUE_NO_RESCUABLE
    };

    const int total_runs = 3 * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Ablation: W3 rho=0.85 ===\n";
    for (auto method : methods) {
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            unsigned seed = sim::SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            eng.configure(method, 0.85, seed, sim::WorkloadType::W3_POISSON_LOGNORMAL,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_rescue_row(csv, "rescue_w3_ablation",
                             sim::WorkloadType::W3_POISSON_LOGNORMAL,
                             method, 0.85, seed, cfg, eng);
            csv.flush();
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << method_name(method) << " seed=" << seed
                      << " P99=" << std::setprecision(1) << m.p99()
                      << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                      << " rescue_moves=" << m.rescue_success_count
                      << " needless=" << m.needless_migration_count
                      << " unsaved=" << m.unsaved_migration_count
                      << " pred_unsafe=" << m.predicted_target_unsafe_accept_count
                      << " harmful_actual=" << m.harmful_actual_count
                      << "\n";
        }
    }
    std::cout << "\nRescueSched ablation done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_check_sweep(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    struct SweepPoint {
        const char* scenario;
        double check_period_us;
        double epsilon_us;
        int budget;
    };
    std::vector<SweepPoint> points;
    for (double check : std::vector<double>{1.0, 2.0, 5.0})
        points.push_back({"rescue_check_period_sweep", check, sim::RESCUE_EPSILON_US,
                          sim::RESCUE_BUDGET_PER_CHECK});
    for (double eps : std::vector<double>{0.0, 2.0, 5.0})
        points.push_back({"rescue_epsilon_sweep", sim::M0_T_CHECK_US, eps,
                          sim::RESCUE_BUDGET_PER_CHECK});
    for (int budget : std::vector<int>{1, 2, 4})
        points.push_back({"rescue_budget_sweep", sim::M0_T_CHECK_US,
                          sim::RESCUE_EPSILON_US, budget});

    const int total_runs = static_cast<int>(points.size()) * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Parameter Sweep: W3 rho=0.85 ===\n";
    for (const auto& point : points) {
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            unsigned seed = sim::SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            cfg.t_check_us = point.check_period_us;
            cfg.rescue_epsilon_us = point.epsilon_us;
            cfg.rescue_budget_per_check = point.budget;
            eng.configure(sim::MethodType::M1_RESCUE_SCHED, 0.85, seed,
                          sim::WorkloadType::W3_POISSON_LOGNORMAL,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_rescue_row(csv, point.scenario,
                             sim::WorkloadType::W3_POISSON_LOGNORMAL,
                             sim::MethodType::M1_RESCUE_SCHED,
                             0.85, seed, cfg, eng);
            csv.flush();
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << point.scenario
                      << " check=" << std::setprecision(3) << point.check_period_us
                      << " eps=" << point.epsilon_us
                      << " B=" << point.budget
                      << " seed=" << seed
                      << " P99=" << std::setprecision(1) << m.p99()
                      << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                      << " rescue_moves=" << m.rescue_success_count
                      << " BMR=" << m.beneficial_migration_ratio()
                      << "\n";
            std::cout.flush();
        }
    }
    std::cout << "\nRescueSched parameter sweep done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_overload_sanity(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    struct Scenario {
        const char* name;
        sim::WorkloadType wl;
        double rho;
    };
    Scenario scenarios[] = {
        {"rescue_w1_sanity", sim::WorkloadType::W1_POISSON_BIMODAL, 0.85},
        {"rescue_w1_overload_boundary", sim::WorkloadType::W1_POISSON_BIMODAL, 0.95}
    };
    sim::MethodType methods[] = {
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE,
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY
    };

    const int total_runs = 2 * 4 * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched W1 Sanity and Overload Boundary ===\n";
    for (const auto& sc : scenarios) {
        for (auto method : methods) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                eng.configure(method, sc.rho, seed, sc.wl,
                              sim::ClusterProfile::HOMOGENEOUS, cfg);
                eng.run();
                write_rescue_row(csv, sc.name, sc.wl, method, sc.rho, seed, cfg, eng);
                csv.flush();
                ++run_count;
                const auto& m = eng.metrics();
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << sc.name << " " << method_name(method)
                          << " seed=" << seed
                          << " P99=" << std::setprecision(1) << m.p99()
                          << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                          << " rescue_moves=" << m.rescue_success_count
                          << " BMR=" << m.beneficial_migration_ratio()
                          << "\n";
                std::cout.flush();
            }
        }
    }
    std::cout << "\nRescueSched overload sanity done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_w2_burst(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    double rhos[] = {0.70, 0.85, 0.92};
    sim::MethodType methods[] = {
        sim::MethodType::L0_RANDOM_CORE,
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE,
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY,
        sim::MethodType::M1_RESCUE_NO_RESCUABLE
    };

    const int total_runs = 3 * 6 * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched W2 Burst-Skew: rho sweep ===\n";
    for (double rho : rhos) {
        for (auto method : methods) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                eng.configure(method, rho, seed, sim::WorkloadType::W2_MMPP_BIMODAL,
                              sim::ClusterProfile::HOMOGENEOUS, cfg);
                eng.run();
                write_rescue_row(csv, "rescue_w2_burst_skew",
                                 sim::WorkloadType::W2_MMPP_BIMODAL,
                                 method, rho, seed, cfg, eng);
                csv.flush();
                ++run_count;
                const auto& m = eng.metrics();
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << method_name(method)
                          << " rho=" << std::setprecision(2) << rho
                          << " seed=" << seed
                          << " P99=" << std::setprecision(1) << m.p99()
                          << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                          << " rescue_moves=" << m.rescue_success_count
                          << " BMR=" << m.beneficial_migration_ratio()
                          << "\n";
                std::cout.flush();
            }
        }
    }
    std::cout << "\nRescueSched W2 burst-skew done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_robustness_10seed(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    struct Scenario {
        const char* name;
        sim::WorkloadType wl;
        double rho;
    };
    Scenario scenarios[] = {
        {"rescue_w3_robustness_10seed", sim::WorkloadType::W3_POISSON_LOGNORMAL, 0.85},
        {"rescue_w2_robustness_10seed", sim::WorkloadType::W2_MMPP_BIMODAL, 0.85}
    };
    sim::MethodType methods[] = {
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE,
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY,
        sim::MethodType::M1_RESCUE_NO_RESCUABLE
    };

    const int total_runs = 2 * 5 * RESCUE_10_SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Robustness: 10 seeds ===\n";
    for (const auto& sc : scenarios) {
        for (auto method : methods) {
            for (int si = 0; si < RESCUE_10_SEED_COUNT; ++si) {
                unsigned seed = RESCUE_10_SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                eng.configure(method, sc.rho, seed, sc.wl,
                              sim::ClusterProfile::HOMOGENEOUS, cfg);
                eng.run();
                write_rescue_row(csv, sc.name, sc.wl, method, sc.rho, seed, cfg, eng);
                csv.flush();
                ++run_count;
                const auto& m = eng.metrics();
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << sc.name << " " << method_name(method)
                          << " seed=" << seed
                          << " P99=" << std::setprecision(1) << m.p99()
                          << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                          << " rescue_moves=" << m.rescue_success_count
                          << " BMR=" << m.beneficial_migration_ratio()
                          << "\n";
                std::cout.flush();
            }
        }
    }
    std::cout << "\nRescueSched robustness 10-seed done -> " << csv_path << "\n";
    return 0;
}

struct MigrationCostBenchResult {
    double local_queue_op_us = 0.0;
    double cross_thread_handoff_us = 0.0;
};

static MigrationCostBenchResult write_migration_cost_microbench(std::ofstream& csv) {
    MigrationCostBenchResult result;
    using Clock = std::chrono::steady_clock;
    constexpr int local_iters = 1000000;
    std::deque<int> q;
    auto start = Clock::now();
    for (int i = 0; i < local_iters; ++i) q.push_back(i);
    for (int i = 0; i < local_iters; ++i) {
        volatile int v = q.front();
        (void)v;
        q.pop_front();
    }
    auto end = Clock::now();
    double total_us =
        std::chrono::duration<double, std::micro>(end - start).count();
    csv << "local_deque_push_pop," << local_iters
        << "," << total_us
        << "," << total_us / (2.0 * local_iters)
        << ",single_thread_descriptor_queue_ops\n";
    result.local_queue_op_us = total_us / (2.0 * local_iters);

    constexpr int handoff_iters = 50000;
    std::mutex mu;
    std::condition_variable cv_item;
    std::condition_variable cv_space;
    bool has_item = false;
    int slot = 0;
    int consumed = 0;

    std::thread consumer([&]() {
        for (int i = 0; i < handoff_iters; ++i) {
            std::unique_lock<std::mutex> lock(mu);
            cv_item.wait(lock, [&]() { return has_item; });
            volatile int v = slot;
            (void)v;
            has_item = false;
            ++consumed;
            lock.unlock();
            cv_space.notify_one();
        }
    });

    start = Clock::now();
    for (int i = 0; i < handoff_iters; ++i) {
        std::unique_lock<std::mutex> lock(mu);
        cv_space.wait(lock, [&]() { return !has_item; });
        slot = i;
        has_item = true;
        lock.unlock();
        cv_item.notify_one();
    }
    consumer.join();
    end = Clock::now();
    total_us = std::chrono::duration<double, std::micro>(end - start).count();
    csv << "cross_thread_cv_handoff," << handoff_iters
        << "," << total_us
        << "," << total_us / handoff_iters
        << ",not_cpu_pinned_upper_bound_consumed_" << consumed << "\n";
    result.cross_thread_handoff_us = total_us / handoff_iters;

    csv << "sim_sweep_low,0,0,0,descriptor_migration_lower_bound\n";
    csv << "sim_sweep_default,0,0," << sim::RESCUE_MIGRATION_COST_US
        << ",current_rescuesched_default\n";
    csv << "sim_sweep_measured,0,0," << result.cross_thread_handoff_us
        << ",cross_thread_cv_handoff_upper_bound\n";
    csv << "sim_sweep_high,0,0,5,stress_cost_for_sensitivity\n";
    return result;
}

static int run_rescue_cost_microbench(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    csv << "benchmark,iterations,total_us,per_op_us,notes\n";
    write_migration_cost_microbench(csv);
    csv.close();
    std::cout << "RescueSched migration-cost microbench done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_calibration(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    struct CalPoint {
        const char* scenario;
        double migration_cost_us;
        int service_estimate_mode;
        double service_estimate_noise_cv;
    };
    CalPoint points[] = {
        {"rescue_cost_sweep", 0.0, sim::SERVICE_ESTIMATE_ORACLE, 0.0},
        {"rescue_cost_sweep", 0.5, sim::SERVICE_ESTIMATE_ORACLE, 0.0},
        {"rescue_cost_sweep", 1.0, sim::SERVICE_ESTIMATE_ORACLE, 0.0},
        {"rescue_cost_sweep", 2.0, sim::SERVICE_ESTIMATE_ORACLE, 0.0},
        {"rescue_cost_sweep", 5.0, sim::SERVICE_ESTIMATE_ORACLE, 0.0},
        {"rescue_service_estimate_sweep", 0.5, sim::SERVICE_ESTIMATE_MEAN, 0.0},
        {"rescue_service_estimate_sweep", 0.5, sim::SERVICE_ESTIMATE_NOISY_ORACLE, 0.25},
        {"rescue_service_estimate_sweep", 0.5, sim::SERVICE_ESTIMATE_NOISY_ORACLE, 0.50},
        {"rescue_service_estimate_sweep", 0.5, sim::SERVICE_ESTIMATE_NOISY_ORACLE, 1.00}
    };

    const int total_runs =
        static_cast<int>(sizeof(points) / sizeof(points[0])) * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Calibration: migration cost and service estimate ===\n";
    for (const auto& point : points) {
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            unsigned seed = sim::SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            cfg.rescue_migration_cost_us = point.migration_cost_us;
            cfg.service_estimate_mode = point.service_estimate_mode;
            cfg.service_estimate_noise_cv = point.service_estimate_noise_cv;
            eng.configure(sim::MethodType::M1_RESCUE_SCHED, 0.85, seed,
                          sim::WorkloadType::W3_POISSON_LOGNORMAL,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_rescue_row(csv, point.scenario,
                             sim::WorkloadType::W3_POISSON_LOGNORMAL,
                             sim::MethodType::M1_RESCUE_SCHED,
                             0.85, seed, cfg, eng);
            csv.flush();
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << point.scenario
                      << " cost=" << std::setprecision(3) << point.migration_cost_us
                      << " est=" << service_estimate_name(point.service_estimate_mode)
                      << " cv=" << point.service_estimate_noise_cv
                      << " seed=" << seed
                      << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                      << " moves=" << m.rescue_success_count
                      << " BMR=" << m.beneficial_migration_ratio()
                      << " UMR=" << m.useless_migration_ratio()
                      << "\n";
            std::cout.flush();
        }
    }
    std::cout << "\nRescueSched calibration done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_estimator_main(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    struct EstPoint {
        const char* scenario;
        int mode;
        double noise_cv;
    };
    EstPoint points[] = {
        {"rescue_estimator_oracle", sim::SERVICE_ESTIMATE_ORACLE, 0.0},
        {"rescue_estimator_class_mean", sim::SERVICE_ESTIMATE_CLASS_MEAN, 0.0},
        {"rescue_estimator_ewma", sim::SERVICE_ESTIMATE_EWMA, 0.0},
        {"rescue_estimator_quantile_guard", sim::SERVICE_ESTIMATE_QUANTILE_GUARD, 0.0},
        {"rescue_estimator_global_mean", sim::SERVICE_ESTIMATE_MEAN, 0.0},
        {"rescue_estimator_noisy_oracle_cv_0_5", sim::SERVICE_ESTIMATE_NOISY_ORACLE, 0.5}
    };

    int total_runs = (2 + static_cast<int>(sizeof(points) / sizeof(points[0])))
                   * RESCUE_10_SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Estimator Main: W3 rho=0.85 ===\n";

    for (auto method : {sim::MethodType::L1_WORK_STEALING,
                        sim::MethodType::M0_INTRA_HOST_PROACTIVE}) {
        for (int si = 0; si < RESCUE_10_SEED_COUNT; ++si) {
            unsigned seed = RESCUE_10_SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            eng.configure(method, 0.85, seed, sim::WorkloadType::W3_POISSON_LOGNORMAL,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_rescue_row(csv, "rescue_estimator_w3_baseline",
                             sim::WorkloadType::W3_POISSON_LOGNORMAL,
                             method, 0.85, seed, cfg, eng);
            csv.flush();
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << method_name(method) << " seed=" << seed
                      << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                      << "\n";
        }
    }

    for (const auto& point : points) {
        for (int si = 0; si < RESCUE_10_SEED_COUNT; ++si) {
            unsigned seed = RESCUE_10_SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            cfg.service_estimate_mode = point.mode;
            cfg.service_estimate_noise_cv = point.noise_cv;
            eng.configure(sim::MethodType::M1_RESCUE_SCHED, 0.85, seed,
                          sim::WorkloadType::W3_POISSON_LOGNORMAL,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_rescue_row(csv, point.scenario,
                             sim::WorkloadType::W3_POISSON_LOGNORMAL,
                             sim::MethodType::M1_RESCUE_SCHED,
                             0.85, seed, cfg, eng);
            csv.flush();
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << service_estimate_name(point.mode)
                      << " seed=" << seed
                      << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                      << " BMR=" << m.beneficial_migration_ratio()
                      << " UMR=" << m.useless_migration_ratio()
                      << "\n";
        }
    }
    std::cout << "\nRescueSched estimator main done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_estimator_w2(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    struct EstPoint { const char* scenario; int mode; double noise_cv; };
    EstPoint points[] = {
        {"rescue_w2_estimator_oracle", sim::SERVICE_ESTIMATE_ORACLE, 0.0},
        {"rescue_w2_estimator_class_mean", sim::SERVICE_ESTIMATE_CLASS_MEAN, 0.0},
        {"rescue_w2_estimator_ewma", sim::SERVICE_ESTIMATE_EWMA, 0.0},
        {"rescue_w2_estimator_quantile_guard", sim::SERVICE_ESTIMATE_QUANTILE_GUARD, 0.0}
    };
    double rhos[] = {0.70, 0.85};

    int total_runs = 2 * (2 + static_cast<int>(sizeof(points) / sizeof(points[0])))
                   * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Estimator W2: rho=0.70/0.85 ===\n";
    for (double rho : rhos) {
        for (auto method : {sim::MethodType::L1_WORK_STEALING,
                            sim::MethodType::M0_INTRA_HOST_PROACTIVE}) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                eng.configure(method, rho, seed, sim::WorkloadType::W2_MMPP_BIMODAL,
                              sim::ClusterProfile::HOMOGENEOUS, cfg);
                eng.run();
                write_rescue_row(csv, "rescue_w2_estimator_baseline",
                                 sim::WorkloadType::W2_MMPP_BIMODAL,
                                 method, rho, seed, cfg, eng);
                csv.flush();
                ++run_count;
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << method_name(method) << " rho=" << rho
                          << " seed=" << seed
                          << " SLO=" << std::setprecision(6)
                          << eng.metrics().slo_violation_rate() << "\n";
            }
        }
        for (const auto& point : points) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                cfg.service_estimate_mode = point.mode;
                cfg.service_estimate_noise_cv = point.noise_cv;
                eng.configure(sim::MethodType::M1_RESCUE_SCHED, rho, seed,
                              sim::WorkloadType::W2_MMPP_BIMODAL,
                              sim::ClusterProfile::HOMOGENEOUS, cfg);
                eng.run();
                write_rescue_row(csv, point.scenario,
                                 sim::WorkloadType::W2_MMPP_BIMODAL,
                                 sim::MethodType::M1_RESCUE_SCHED,
                                 rho, seed, cfg, eng);
                csv.flush();
                ++run_count;
                const auto& m = eng.metrics();
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << service_estimate_name(point.mode)
                          << " rho=" << rho << " seed=" << seed
                          << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                          << " BMR=" << m.beneficial_migration_ratio()
                          << "\n";
            }
        }
    }
    std::cout << "\nRescueSched estimator W2 done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_cost_calibration(const std::string& csv_path,
                                       const std::string& bench_path) {
    std::ofstream bench(bench_path);
    if (!bench.is_open()) { std::cerr << "Cannot open " << bench_path << "\n"; return 1; }
    bench << "benchmark,iterations,total_us,per_op_us,notes\n";
    MigrationCostBenchResult measured = write_migration_cost_microbench(bench);
    bench.close();

    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    std::vector<double> costs = {0.0, 0.5, 1.0, 2.0, 5.0,
                                 measured.cross_thread_handoff_us};
    int total_runs = static_cast<int>(costs.size()) * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Cost Calibration: W3 rho=0.85 ===\n";
    std::cout << "Measured cross-thread handoff cost ~= "
              << std::setprecision(6) << measured.cross_thread_handoff_us << " us\n";
    for (double cost : costs) {
        for (int si = 0; si < sim::SEED_COUNT; ++si) {
            unsigned seed = sim::SEEDS[si];
            sim::Simulator eng;
            sim::M0Config cfg;
            cfg.rescue_migration_cost_us = cost;
            eng.configure(sim::MethodType::M1_RESCUE_SCHED, 0.85, seed,
                          sim::WorkloadType::W3_POISSON_LOGNORMAL,
                          sim::ClusterProfile::HOMOGENEOUS, cfg);
            eng.run();
            write_rescue_row(csv,
                             cost == measured.cross_thread_handoff_us
                                 ? "rescue_cost_measured_handoff"
                                 : "rescue_cost_calibration",
                             sim::WorkloadType::W3_POISSON_LOGNORMAL,
                             sim::MethodType::M1_RESCUE_SCHED,
                             0.85, seed, cfg, eng);
            csv.flush();
            ++run_count;
            const auto& m = eng.metrics();
            std::cout << "[" << run_count << "/" << total_runs << "] "
                      << "cost=" << std::setprecision(6) << cost
                      << " seed=" << seed
                      << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                      << " moves=" << m.rescue_success_count
                      << "\n";
        }
    }
    std::cout << "\nRescueSched cost calibration done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_w2_boundary(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    int hot_counts[] = {2, 4, 8};
    double probs[] = {0.3, 0.5, 0.7};
    double rhos[] = {0.70, 0.80, 0.85, 0.90};
    sim::MethodType methods[] = {
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE,
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY,
        sim::MethodType::M1_RESCUE_NO_RESCUABLE
    };
    constexpr int boundary_seed_count = 3;
    int total_runs = 3 * 3 * 4 * 5 * boundary_seed_count;
    int run_count = 0;
    std::cout << "=== RescueSched W2 Boundary Sweep ===\n";
    for (int hot_count : hot_counts) {
        for (double prob : probs) {
            for (double rho : rhos) {
                for (auto method : methods) {
                    for (int si = 0; si < boundary_seed_count; ++si) {
                        unsigned seed = sim::SEEDS[si];
                        sim::Simulator eng;
                        sim::M0Config cfg;
                        cfg.w2_hot_core_count = hot_count;
                        cfg.w2_hot_dispatch_prob = prob;
                        eng.configure(method, rho, seed, sim::WorkloadType::W2_MMPP_BIMODAL,
                                      sim::ClusterProfile::HOMOGENEOUS, cfg);
                        eng.run();
                        write_rescue_row(csv, "rescue_w2_boundary",
                                         sim::WorkloadType::W2_MMPP_BIMODAL,
                                         method, rho, seed, cfg, eng);
                        csv.flush();
                        ++run_count;
                        const auto& m = eng.metrics();
                        std::cout << "[" << run_count << "/" << total_runs << "] "
                                  << "hot=" << hot_count << " prob=" << prob
                                  << " rho=" << rho << " " << method_name(method)
                                  << " seed=" << seed
                                  << " SLO=" << std::setprecision(6)
                                  << m.slo_violation_rate()
                                  << " BMR=" << m.beneficial_migration_ratio()
                                  << "\n";
                    }
                }
            }
        }
    }
    std::cout << "\nRescueSched W2 boundary done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_hybrid_smoke() {
    std::cout << "=== RescueSched Hybrid Smoke: W2 rho=0.85 seed=11 ===\n";
    sim::MethodType methods[] = {
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_RESCUABLE,
        sim::MethodType::M1_RESCUE_HYBRID
    };
    bool ok = true;
    for (auto method : methods) {
        sim::Simulator eng;
        sim::M0Config cfg;
        eng.configure(method, 0.85, 11, sim::WorkloadType::W2_MMPP_BIMODAL,
                      sim::ClusterProfile::HOMOGENEOUS, cfg);
        eng.run();
        const auto& m = eng.metrics();
        std::cout << method_name(method)
                  << " P99=" << std::setprecision(3) << m.p99()
                  << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                  << " moves=" << m.rescue_success_count
                  << " relief_moves=" << m.relief_success_count
                  << " BMR=" << m.beneficial_migration_ratio()
                  << " relief_BMR=" << m.relief_beneficial_migration_ratio()
                  << "\n";
        if (m.total_finished != sim::MEASUREMENT_REQUESTS) ok = false;
        if (method == sim::MethodType::M1_RESCUE_HYBRID
            && m.rescue_success_count == 0) ok = false;
    }
    std::cout << (ok ? "RescueSched hybrid smoke status: PASS\n"
                     : "RescueSched hybrid smoke status: FAIL\n");
    return ok ? 0 : 2;
}

static int run_rescue_hybrid_main(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    double rhos[] = {0.70, 0.85, 0.90};
    sim::MethodType methods[] = {
        sim::MethodType::L1_WORK_STEALING,
        sim::MethodType::M0_INTRA_HOST_PROACTIVE,
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_RESCUABLE,
        sim::MethodType::M1_RESCUE_HYBRID
    };
    int total_runs = 3 * 5 * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Hybrid Main: W2 rho sweep ===\n";
    for (double rho : rhos) {
        for (auto method : methods) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                eng.configure(method, rho, seed, sim::WorkloadType::W2_MMPP_BIMODAL,
                              sim::ClusterProfile::HOMOGENEOUS, cfg);
                eng.run();
                write_rescue_row(csv, "rescue_hybrid_w2_main",
                                 sim::WorkloadType::W2_MMPP_BIMODAL,
                                 method, rho, seed, cfg, eng);
                csv.flush();
                ++run_count;
                const auto& m = eng.metrics();
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << method_name(method) << " rho=" << rho
                          << " seed=" << seed
                          << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                          << " relief_moves=" << m.relief_success_count
                          << "\n";
            }
        }
    }
    std::cout << "\nRescueSched hybrid main done -> " << csv_path << "\n";
    return 0;
}

static int run_rescue_target_safety_stress(const std::string& csv_path) {
    std::ofstream csv(csv_path);
    if (!csv.is_open()) { std::cerr << "Cannot open " << csv_path << "\n"; return 1; }
    write_rescue_header(csv);
    csv.flush();

    int policies[] = {
        sim::RESCUE_TARGET_INSERT_APPEND_TAIL,
        sim::RESCUE_TARGET_INSERT_HEAD_STRESS
    };
    sim::MethodType methods[] = {
        sim::MethodType::M1_RESCUE_SCHED,
        sim::MethodType::M1_RESCUE_NO_TARGET_SAFETY
    };
    int total_runs = 2 * 2 * sim::SEED_COUNT;
    int run_count = 0;
    std::cout << "=== RescueSched Target Safety Stress: W3 rho=0.85 ===\n";
    for (int policy : policies) {
        for (auto method : methods) {
            for (int si = 0; si < sim::SEED_COUNT; ++si) {
                unsigned seed = sim::SEEDS[si];
                sim::Simulator eng;
                sim::M0Config cfg;
                cfg.rescue_target_insert_policy = policy;
                eng.configure(method, 0.85, seed,
                              sim::WorkloadType::W3_POISSON_LOGNORMAL,
                              sim::ClusterProfile::HOMOGENEOUS, cfg);
                eng.run();
                write_rescue_row(csv, "rescue_target_safety_stress",
                                 sim::WorkloadType::W3_POISSON_LOGNORMAL,
                                 method, 0.85, seed, cfg, eng);
                csv.flush();
                ++run_count;
                const auto& m = eng.metrics();
                std::cout << "[" << run_count << "/" << total_runs << "] "
                          << target_insert_policy_name(policy)
                          << " " << method_name(method)
                          << " seed=" << seed
                          << " SLO=" << std::setprecision(6) << m.slo_violation_rate()
                          << " pred_unsafe=" << m.predicted_target_unsafe_accept_count
                          << " harmful_actual=" << m.harmful_actual_count
                          << " induced=" << m.target_induced_miss_actual
                          << "\n";
            }
        }
    }
    std::cout << "\nRescueSched target safety stress done -> " << csv_path << "\n";
    return 0;
}

int main(int argc, char** argv) {
    std::string mode = "all";
    if (argc > 1) mode = argv[1];

    ensure_dir("artifacts");
    ensure_dir("artifacts/step-04b-sensitivity");
    ensure_dir("artifacts/step-04c-heterogeneous");
    ensure_dir("artifacts/step-06-aqb");
    ensure_dir("artifacts/step-08-dqb-batch");
    ensure_dir("artifacts/step-12-intra-host");
    ensure_dir("artifacts/step-13-intra-host-sweep");
    ensure_dir("artifacts/step-14-intra-check-period");
    ensure_dir("artifacts/step-15-rescuesched");
    ensure_dir("artifacts/step-17-rescuesched-closure");
    ensure_dir("artifacts/step-18-infocom-readiness");

    int rc = 0;

    if (mode == "all" || mode == "sensitivity") {
        rc = run_sensitivity("artifacts/step-04b-sensitivity/sensitivity_scan.csv");
        if (rc != 0) return rc;
    }

    if (mode == "all" || mode == "heterogeneous") {
        rc = run_heterogeneous("artifacts/step-04c-heterogeneous/metrics_table.csv");
        if (rc != 0) return rc;
    }

    if (mode == "all" || mode == "regression") {
        rc = run_regression();
    }

    if (mode == "aqb-smoke") {
        rc = run_aqb_smoke();
    }

    if (mode == "intra-smoke") {
        rc = run_intra_smoke();
    }

    if (mode == "intra-w3-only") {
        rc = run_intra_w3_only("artifacts/step-12-intra-host/intra_w3_only.csv");
    }

    if (mode == "intra-w3-rho-050") {
        rc = run_intra_focus(
            "Intra-host W3 Focus: rho=0.50",
            "intra_w3_heavytail",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.50,
            "artifacts/step-13-intra-host-sweep/intra_w3_rho_050.csv");
    }

    if (mode == "intra-w3-rho-070") {
        rc = run_intra_focus(
            "Intra-host W3 Focus: rho=0.70",
            "intra_w3_heavytail",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.70,
            "artifacts/step-13-intra-host-sweep/intra_w3_rho_070.csv");
    }

    if (mode == "intra-w3-rho-085") {
        rc = run_intra_focus(
            "Intra-host W3 Focus: rho=0.85",
            "intra_w3_heavytail",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.85,
            "artifacts/step-13-intra-host-sweep/intra_w3_rho_085.csv");
    }

    if (mode == "intra-w3-rho-092") {
        rc = run_intra_focus(
            "Intra-host W3 Focus: rho=0.92",
            "intra_w3_heavytail",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.92,
            "artifacts/step-13-intra-host-sweep/intra_w3_rho_092.csv");
    }

    if (mode == "intra-w1-sanity") {
        rc = run_intra_focus(
            "Intra-host W1 Sanity: rho=0.85",
            "intra_w1_bimodal_sanity",
            sim::WorkloadType::W1_POISSON_BIMODAL,
            0.85,
            "artifacts/step-13-intra-host-sweep/intra_w1_sanity.csv");
    }

    if (mode == "intra-w1-high") {
        rc = run_intra_focus(
            "Intra-host W1 High Load: rho=0.95",
            "intra_w1_high_load",
            sim::WorkloadType::W1_POISSON_BIMODAL,
            0.95,
            "artifacts/step-13-intra-host-sweep/intra_w1_high_load.csv");
    }

    if (mode == "intra-w3-high") {
        rc = run_intra_focus(
            "Intra-host W3 High Load: rho=0.95",
            "intra_w3_high_load",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.95,
            "artifacts/step-13-intra-host-sweep/intra_w3_high_load.csv");
    }

    if (mode == "intra-w3-092-check-sweep") {
        rc = run_intra_check_period_sweep(
            "Intra-host W3 rho=0.92 Check Period Sweep",
            "intra_w3_heavytail_check_period",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.92,
            std::vector<double>{1.0, 2.0, 5.0},
            "artifacts/step-14-intra-check-period/intra_w3_rho_092_check_sweep.csv");
    }

    if (mode == "intra-w3-092-check-1") {
        rc = run_intra_check_period_sweep(
            "Intra-host W3 rho=0.92 Check Period 1us",
            "intra_w3_heavytail_check_period",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.92,
            std::vector<double>{1.0},
            "artifacts/step-14-intra-check-period/intra_w3_rho_092_check_1us.csv");
    }

    if (mode == "intra-w3-092-check-2") {
        rc = run_intra_check_period_sweep(
            "Intra-host W3 rho=0.92 Check Period 2us",
            "intra_w3_heavytail_check_period",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.92,
            std::vector<double>{2.0},
            "artifacts/step-14-intra-check-period/intra_w3_rho_092_check_2us.csv");
    }

    if (mode == "intra-w3-092-check-5") {
        rc = run_intra_check_period_sweep(
            "Intra-host W3 rho=0.92 Check Period 5us",
            "intra_w3_heavytail_check_period",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.92,
            std::vector<double>{5.0},
            "artifacts/step-14-intra-check-period/intra_w3_rho_092_check_5us.csv");
    }

    if (mode == "intra-w1-095-check-sweep") {
        rc = run_intra_check_period_sweep(
            "Intra-host W1 rho=0.95 Check Period Sweep",
            "intra_w1_bimodal_check_period",
            sim::WorkloadType::W1_POISSON_BIMODAL,
            0.95,
            std::vector<double>{1.0, 2.0, 5.0},
            "artifacts/step-14-intra-check-period/intra_w1_rho_095_check_sweep.csv");
    }

    if (mode == "intra-w1-095-check-1") {
        rc = run_intra_check_period_sweep(
            "Intra-host W1 rho=0.95 Check Period 1us",
            "intra_w1_bimodal_check_period",
            sim::WorkloadType::W1_POISSON_BIMODAL,
            0.95,
            std::vector<double>{1.0},
            "artifacts/step-14-intra-check-period/intra_w1_rho_095_check_1us.csv");
    }

    if (mode == "intra-w1-095-check-2") {
        rc = run_intra_check_period_sweep(
            "Intra-host W1 rho=0.95 Check Period 2us",
            "intra_w1_bimodal_check_period",
            sim::WorkloadType::W1_POISSON_BIMODAL,
            0.95,
            std::vector<double>{2.0},
            "artifacts/step-14-intra-check-period/intra_w1_rho_095_check_2us.csv");
    }

    if (mode == "intra-w1-095-check-5") {
        rc = run_intra_check_period_sweep(
            "Intra-host W1 rho=0.95 Check Period 5us",
            "intra_w1_bimodal_check_period",
            sim::WorkloadType::W1_POISSON_BIMODAL,
            0.95,
            std::vector<double>{5.0},
            "artifacts/step-14-intra-check-period/intra_w1_rho_095_check_5us.csv");
    }

    if (mode == "intra-main") {
        rc = run_intra_main("artifacts/step-12-intra-host/intra_main.csv");
    }

    if (mode == "rescue-smoke") {
        rc = run_rescue_smoke();
    }

    if (mode == "rescue-w3-only") {
        rc = run_rescue_w3_only(
            "artifacts/step-15-rescuesched/rescue_w3_only.csv");
    }

    if (mode == "rescue-main") {
        rc = run_rescue_main(
            "artifacts/step-15-rescuesched/rescue_main.csv");
    }

    if (mode == "rescue-ablation") {
        rc = run_rescue_ablation(
            "artifacts/step-15-rescuesched/rescue_ablation.csv");
    }

    if (mode == "rescue-check-sweep") {
        rc = run_rescue_check_sweep(
            "artifacts/step-15-rescuesched/rescue_check_sweep.csv");
    }

    if (mode == "rescue-overload-sanity") {
        rc = run_rescue_overload_sanity(
            "artifacts/step-15-rescuesched/rescue_overload_sanity.csv");
    }

    if (mode == "rescue-w2-burst") {
        rc = run_rescue_w2_burst(
            "artifacts/step-17-rescuesched-closure/rescue_w2_burst.csv");
    }

    if (mode == "rescue-robustness-10seed") {
        rc = run_rescue_robustness_10seed(
            "artifacts/step-17-rescuesched-closure/rescue_robustness_10seed.csv");
    }

    if (mode == "rescue-cost-microbench") {
        rc = run_rescue_cost_microbench(
            "artifacts/step-17-rescuesched-closure/migration_cost_microbench.csv");
    }

    if (mode == "rescue-calibration") {
        rc = run_rescue_calibration(
            "artifacts/step-17-rescuesched-closure/rescue_calibration.csv");
    }

    if (mode == "rescue-estimator-main") {
        rc = run_rescue_estimator_main(
            "artifacts/step-18-infocom-readiness/rescue_estimator_main.csv");
    }

    if (mode == "rescue-estimator-w2") {
        rc = run_rescue_estimator_w2(
            "artifacts/step-18-infocom-readiness/rescue_estimator_w2.csv");
    }

    if (mode == "rescue-cost-calibration") {
        rc = run_rescue_cost_calibration(
            "artifacts/step-18-infocom-readiness/rescue_cost_calibration.csv",
            "artifacts/step-18-infocom-readiness/migration_cost_microbench.csv");
    }

    if (mode == "rescue-w2-boundary") {
        rc = run_rescue_w2_boundary(
            "artifacts/step-18-infocom-readiness/rescue_w2_boundary.csv");
    }

    if (mode == "rescue-hybrid-smoke") {
        rc = run_rescue_hybrid_smoke();
    }

    if (mode == "rescue-hybrid-main") {
        rc = run_rescue_hybrid_main(
            "artifacts/step-18-infocom-readiness/rescue_hybrid_main.csv");
    }

    if (mode == "rescue-target-safety-stress") {
        rc = run_rescue_target_safety_stress(
            "artifacts/step-18-infocom-readiness/rescue_target_safety_stress.csv");
    }

    if (mode == "aqb-eval") {
        rc = run_aqb_eval("artifacts/step-06-aqb/aqb_eval.csv");
    }

    if (mode == "dqb-eval") {
        rc = run_aqb_eval("artifacts/step-08-dqb-batch/dqb_eval.csv");
    }

    if (mode == "dqb-w2-only") {
        rc = run_dqb_focus(
            "DQB-PM Focused W2 Check",
            "W2_burst_homo",
            sim::WorkloadType::W2_MMPP_BIMODAL,
            0.85,
            "artifacts/step-08-dqb-batch/dqb_w2_only.csv");
    }

    if (mode == "dqb-w3-only") {
        rc = run_dqb_focus(
            "DQB-PM Focused W3 Check",
            "W3_heavytail_homo",
            sim::WorkloadType::W3_POISSON_LOGNORMAL,
            0.85,
            "artifacts/step-08-dqb-batch/dqb_w3_only.csv");
    }

    if (mode == "dqb-w1-only") {
        rc = run_dqb_focus(
            "DQB-PM Focused W1 Check",
            "W1_saturation_homo",
            sim::WorkloadType::W1_POISSON_BIMODAL,
            0.95,
            "artifacts/step-08-dqb-batch/dqb_w1_only.csv");
    }

    if (mode == "aqb-hetero") {
        rc = run_aqb_heterogeneous("artifacts/step-06-aqb/aqb_heterogeneous.csv");
    }

    if (mode == "aqb-batch-sweep") {
        rc = run_aqb_batch_sweep("artifacts/step-06-aqb/aqb_batch_sweep.csv");
    }

    if (mode == "aqb-extra") {
        rc = run_aqb_heterogeneous("artifacts/step-06-aqb/aqb_heterogeneous.csv");
        if (rc != 0) return rc;
        rc = run_aqb_batch_sweep("artifacts/step-06-aqb/aqb_batch_sweep.csv");
    }

    return rc;
}
