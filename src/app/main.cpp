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
#include <direct.h>

static const char* method_name(sim::MethodType m) {
    switch (m) {
        case sim::MethodType::B0_IDEAL_CFCFS:          return "B0_IdealCFCFS";
        case sim::MethodType::B1_POWER_OF_K:           return "B1_PowerOf2";
        case sim::MethodType::B2_REACTIVE_MIGRATION:   return "B2_Reactive";
        case sim::MethodType::M0_PROACTIVE_MIGRATION:  return "M0_Proactive";
        case sim::MethodType::M1_AQB_PROACTIVE_MIGRATION: return "M1_AQB_PM";
        case sim::MethodType::M2_DQB_PROACTIVE_MIGRATION: return "M2_DQB_PM";
    }
    return "Unknown";
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
        std::cout << pt.wl_name << " rho=" << pt.rho << " "
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
           "target_plan_reject_count\n";

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
                    << m.target_plan_reject_count << "\n";

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
           "target_plan_reject_count\n";

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
                    << m.target_plan_reject_count << "\n";

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
           "target_plan_reject_count,saturation_guard_count\n";

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
            << m.target_plan_reject_count << "," << m.saturation_guard_count << "\n";

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

int main(int argc, char** argv) {
    std::string mode = "all";
    if (argc > 1) mode = argv[1];

    ensure_dir("artifacts");
    ensure_dir("artifacts/step-04b-sensitivity");
    ensure_dir("artifacts/step-04c-heterogeneous");
    ensure_dir("artifacts/step-06-aqb");
    ensure_dir("artifacts/step-08-dqb-batch");

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
