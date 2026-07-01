#pragma once

#include "sim/common/types.h"
#include "sim/common/constants.h"
#include "sim/model/event.h"
#include "sim/model/node.h"
#include "sim/model/task.h"
#include "sim/metrics/stats.h"
#include "sim/workloads/generators.h"
#include "sim/algorithms/scheduler.h"
#include "sim/algorithms/host_power_of_k.h"
#include "sim/algorithms/host_reactive_migration.h"
#include "sim/algorithms/host_proactive_migration.h"
#include "sim/algorithms/dqb_proactive_migration.h"

#include <queue>
#include <vector>
#include <string>
#include <memory>
#include <random>
#include <functional>

namespace sim {

class Simulator {
public:
    Simulator();
    void configure(MethodType method, double rho, unsigned seed,
                   WorkloadType wl = WorkloadType::W2_MMPP_BIMODAL,
                   ClusterProfile profile = ClusterProfile::HOMOGENEOUS,
                   const M0Config& m0cfg = M0Config{});
    int  run();   // returns 0 on success

    // Result accessors (valid after run()).
    const MetricsCollector& metrics() const { return metrics_; }
    uint64_t total_generated() const { return task_id_counter_; }
    double total_generated_work_us() const { return total_generated_work_us_; }

    // Effective cluster capacity (for heterogeneous rho computation).
    static double compute_effective_capacity(ClusterProfile profile);

private:
    // Event loop.
    void schedule_event(Event e);
    void process_event(const Event& e);

    // Event handlers.
    void handle_task_generate(const Event& e);
    void handle_task_arrive(const Event& e);
    void handle_task_execute(int host, int core);
    void handle_task_finish(const Event& e);
    void handle_sync_load(const Event& e);
    void handle_check_migration(const Event& e);

    // Helpers.
    void start_execution(Core& c, double now_us);
    int  dispatch_task(double service_est_us);
    double estimate_service_time(double base_service_us);
    double compute_exec_time(double base_service_us, double capacity) const;
    bool is_intra_host_method() const;
    void enqueue_task_on_core(Task* task, int host, int core, double now_us);
    void enqueue_task_on_random_core(Task* task);
    bool steal_one_task(int host, int idle_core);
    bool move_waiting_task_intra_host(int host, int src_core, int dst_core,
                                      Task* task, double estimated_local_latency_us,
                                      bool proactive);
    bool move_rescue_task_intra_host(int host, int src_core, int dst_core,
                                     Task* task,
                                     double estimated_local_latency_us,
                                     double estimated_remote_latency_us,
                                     int predicted_target_delta_risk,
                                     bool predicted_harmful);
    bool run_intra_proactive_check(int host);
    bool run_rescue_sched_check(int host);
    void try_b0_pull(int prefer_host = -1);
    int  find_b0_idle_host();

    // State.
    double now_us_ = 0.0;
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> event_queue_;

    std::vector<Node>  nodes_;
    int active_host_count_ = NUM_HOSTS;
    std::vector<int>   stale_view_;   // stale total queue lengths per host
    std::vector<double> stale_workload_view_; // stale host workload in us
    std::vector<double> incoming_reservation_; // scheduled migration work not yet visible in stale view
    std::vector<std::vector<double>> incoming_core_reservation_;

    // Task pool (simple vector-based pool).
    std::vector<std::unique_ptr<Task>> task_pool_;
    uint64_t task_id_counter_ = 0;
    Task* alloc_task();

    // Workload.
    std::mt19937_64 rng_;
    WorkloadType workload_type_ = WorkloadType::W2_MMPP_BIMODAL;
    std::unique_ptr<MMPPArrival>  mmpp_;
    std::unique_ptr<PoissonArrival> poisson_;
    std::unique_ptr<BimodalService> bimodal_;
    std::unique_ptr<LognormalService> lognormal_;

    // Scheduling.
    MethodType method_type_ = MethodType::B1_POWER_OF_K;
    std::unique_ptr<IScheduler> scheduler_;
    ReactiveMigrationScheduler  reactive_sched_;
    ProactiveMigrationScheduler proactive_sched_;
    DqbProactiveMigrationScheduler dqb_sched_;

    // Runtime M0 config and cluster profile.
    M0Config m0_config_;
    ClusterProfile cluster_profile_ = ClusterProfile::HOMOGENEOUS;
    double effective_capacity_ = TOTAL_CORES;
    std::vector<double> node_capacities_; // per-node capacity factor

    // Metrics.
    MetricsCollector metrics_;
    int measurement_target_ = 0;
    uint64_t migration_decisions_ = 0; // measured-window migrations scheduled immediately
    uint64_t migration_batch_id_counter_ = 0;
    double total_generated_work_us_ = 0.0;

    // B0 global queue (Ideal-cFCFS).
    TaskQueue global_queue_;

    // B2 thresholds (computed after warmup).
    int b2_q_hi_ = 0;
    int b2_q_lo_ = 0;
    bool b2_thresholds_set_ = false;

    // W2 localized burst: hot node subset that absorbs extra burst traffic.
    std::vector<int> hot_nodes_;
    std::vector<int> hot_cores_;
    bool last_burst_state_ = false;
    void refresh_hot_nodes();
    void refresh_hot_cores();
    static constexpr int HOT_NODE_COUNT = 16; // 25% of 64 nodes
    static constexpr int HOT_CORE_COUNT = 4;  // 25% of 16 cores
    static constexpr double HOT_DISPATCH_PROB = 0.5; // P(forced to hot node) during burst
};

} // namespace sim
