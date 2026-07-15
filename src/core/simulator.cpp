#include "sim/core/simulator.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sim {

namespace {
uint64_t splitmix_seed(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}
}

Simulator::Simulator() = default;

bool Simulator::is_intra_host_method() const {
    return method_type_ == MethodType::L0_RANDOM_CORE
        || method_type_ == MethodType::L1_WORK_STEALING
        || method_type_ == MethodType::L1_WORK_STEALING_POLLING
        || method_type_ == MethodType::M0_INTRA_HOST_PROACTIVE
        || method_type_ == MethodType::M0_ALTO_THRESHOLD
        || method_type_ == MethodType::M1_RESCUE_SCHED
        || method_type_ == MethodType::M1_RESCUE_NO_TARGET_SAFETY
        || method_type_ == MethodType::M1_RESCUE_NO_RESCUABLE
        || method_type_ == MethodType::M1_RESCUE_HYBRID;
}

Task* Simulator::alloc_task() {
    task_pool_.push_back(std::make_unique<Task>());
    Task* t = task_pool_.back().get();
    t->id = ++task_id_counter_;
    return t;
}

void Simulator::configure(MethodType method, double rho, unsigned seed,
                          WorkloadType wl, ClusterProfile profile,
                          const M0Config& m0cfg,
                          std::shared_ptr<const WorkloadTrace> supplied_trace) {
    method_type_ = method;
    workload_type_ = wl;
    cluster_profile_ = profile;
    m0_config_ = m0cfg;
    rng_.seed(splitmix_seed(static_cast<uint64_t>(seed) ^ 0x434f4e54524f4cULL));
    estimator_rng_.seed(splitmix_seed(static_cast<uint64_t>(seed) ^ 0x455354494d415445ULL));

    // Init nodes with capacity based on cluster profile.
    active_host_count_ =
        (method == MethodType::L0_RANDOM_CORE
         || method == MethodType::L1_WORK_STEALING
         || method == MethodType::L1_WORK_STEALING_POLLING
         || method == MethodType::M0_INTRA_HOST_PROACTIVE
         || method == MethodType::M0_ALTO_THRESHOLD
         || method == MethodType::M1_RESCUE_SCHED
         || method == MethodType::M1_RESCUE_NO_TARGET_SAFETY
         || method == MethodType::M1_RESCUE_NO_RESCUABLE
         || method == MethodType::M1_RESCUE_HYBRID)
            ? 1 : NUM_HOSTS;
    nodes_.resize(active_host_count_);
    node_capacities_.resize(active_host_count_);
    for (int i = 0; i < active_host_count_; ++i) {
        double cap = 1.0;
        if (active_host_count_ == NUM_HOSTS
            && profile == ClusterProfile::HETERO_25PCT
            && i >= HETERO_FAST_NODES)
            cap = HETERO_SLOW_CAPACITY;
        nodes_[i].init(i, CORES_PER_HOST, cap);
        node_capacities_[i] = cap;
    }
    stale_view_.assign(active_host_count_, 0);
    stale_workload_view_.assign(active_host_count_, 0.0);
    incoming_reservation_.assign(active_host_count_, 0.0);
    incoming_core_reservation_.assign(
        active_host_count_, std::vector<double>(CORES_PER_HOST, 0.0));
    effective_capacity_ = (active_host_count_ == 1)
        ? static_cast<double>(CORES_PER_HOST)
        : compute_effective_capacity(profile);

    // Scheduler (B0 uses global queue, no IScheduler).
    scheduler_.reset();
    switch (method) {
        case MethodType::B0_IDEAL_CFCFS:
            break; // no scheduler needed
        case MethodType::L0_RANDOM_CORE:
        case MethodType::L1_WORK_STEALING:
        case MethodType::L1_WORK_STEALING_POLLING:
        case MethodType::M0_INTRA_HOST_PROACTIVE:
        case MethodType::M0_ALTO_THRESHOLD:
        case MethodType::M1_RESCUE_SCHED:
        case MethodType::M1_RESCUE_NO_TARGET_SAFETY:
        case MethodType::M1_RESCUE_NO_RESCUABLE:
        case MethodType::M1_RESCUE_HYBRID:
            break; // local core scheduling only
        case MethodType::B1_POWER_OF_K:
            scheduler_ = std::make_unique<PowerOfKScheduler>(2);
            break;
        case MethodType::B2_REACTIVE_MIGRATION:
            scheduler_ = std::make_unique<ReactiveMigrationScheduler>();
            break;
        case MethodType::M0_PROACTIVE_MIGRATION:
            scheduler_ = std::make_unique<ProactiveMigrationScheduler>();
            break;
        case MethodType::M1_AQB_PROACTIVE_MIGRATION:
            scheduler_ = std::make_unique<ProactiveMigrationScheduler>();
            break;
        case MethodType::M2_DQB_PROACTIVE_MIGRATION:
            scheduler_ = std::make_unique<DqbProactiveMigrationScheduler>();
            break;
    }

    // Pass M0 runtime config to proactive scheduler.
    proactive_sched_.set_config(m0cfg);
    proactive_sched_.reset();
    dqb_sched_.set_config(m0cfg);
    dqb_sched_.reset();

    // Policy-independent workload trace. Legacy callers receive the same
    // deterministic trace that paper runners can explicitly share.
    if (supplied_trace) {
        if (supplied_trace->config().workload != wl
            || std::abs(supplied_trace->config().rho - rho) > 1e-12
            || supplied_trace->config().seed != seed) {
            throw std::invalid_argument("supplied trace does not match workload/rho/seed");
        }
        trace_ = std::move(supplied_trace);
    } else {
        TraceConfig trace_config;
        trace_config.workload = wl;
        trace_config.rho = rho;
        trace_config.seed = seed;
        trace_config.core_count = CORES_PER_HOST;
        trace_config.effective_core_capacity = effective_capacity_;
        trace_config.w2_hot_core_count = m0cfg.w2_hot_core_count;
        trace_config.w2_hot_dispatch_prob = m0cfg.w2_hot_dispatch_prob;
        trace_config.placement_mode = m0cfg.placement_mode;
        trace_config.flow_count = m0cfg.flow_count;
        trace_config.flow_zipf_alpha = m0cfg.flow_zipf_alpha;
        trace_config.flow_hash_seed = m0cfg.flow_hash_seed;
        trace_ = std::make_shared<WorkloadTrace>(WorkloadTrace::generate(trace_config));
    }
    trace_index_ = 0;

    // Metrics.
    measurement_target_ = trace_->config().measurement_requests;
    metrics_.init(measurement_target_);
    task_id_counter_ = 0;
    total_generated_work_us_ = 0.0;
    task_pool_.clear();
    now_us_ = 0.0;
    b2_thresholds_set_ = false;
    migration_decisions_ = 0;
    migration_batch_id_counter_ = 0;
    ewma_short_service_us_ = class_mean_service_estimate(RpcMethod::SHORT_RPC);
    ewma_long_service_us_ = class_mean_service_estimate(RpcMethod::LONG_RPC);
    estimator_short_samples_ = 0;
    estimator_long_samples_ = 0;
    quantile_short_service_us_ = std::max(
        SLO_SHORT_SERVICE_THRESHOLD_US, ewma_short_service_us_ * 1.5);
    quantile_long_service_us_ = std::max(BIMODAL_LONG_US, ewma_long_service_us_ * 1.5);

    // B0 global queue.
    global_queue_.clear();

    // W2 localized burst init.
    hot_nodes_.clear();
    hot_cores_.clear();
    last_burst_state_ = false;
    if (active_host_count_ == NUM_HOSTS && wl == WorkloadType::W2_MMPP_BIMODAL)
        refresh_hot_nodes();
    if (is_intra_host_method() && wl == WorkloadType::W2_MMPP_BIMODAL)
        refresh_hot_cores();

    // Clear event queue.
    while (!event_queue_.empty()) event_queue_.pop();

    // Seed initial events.
    if (trace_ && !trace_->entries().empty()) {
        Event gen;
        gen.timestamp_us = trace_->entries().front().generate_time_us;
        gen.type = EventType::TASK_GENERATE;
        schedule_event(gen);
    }

    // Periodic SYNC_LOAD (not needed for B0).
    if (method != MethodType::B0_IDEAL_CFCFS && !is_intra_host_method()) {
        Event sync;
        sync.timestamp_us = SYNC_LOAD_PERIOD_US;
        sync.type = EventType::SYNC_LOAD;
        schedule_event(sync);
    }

    // Periodic CHECK_MIGRATION.
    if (method == MethodType::B2_REACTIVE_MIGRATION) {
        Event chk;
        chk.timestamp_us = M0_T_CHECK_US;
        chk.type = EventType::CHECK_MIGRATION;
        chk.host_id = -1;
        schedule_event(chk);
    } else if (method == MethodType::L1_WORK_STEALING_POLLING) {
        Event chk;
        chk.timestamp_us = m0_config_.work_steal_poll_us;
        chk.type = EventType::CHECK_MIGRATION;
        chk.host_id = 0;
        schedule_event(chk);
    } else if (method == MethodType::M0_INTRA_HOST_PROACTIVE
               || method == MethodType::M0_ALTO_THRESHOLD) {
        Event chk;
        chk.timestamp_us = m0_config_.t_check_us;
        chk.type = EventType::CHECK_MIGRATION;
        chk.host_id = 0;
        schedule_event(chk);
    } else if (method == MethodType::M1_RESCUE_SCHED
               || method == MethodType::M1_RESCUE_NO_TARGET_SAFETY
               || method == MethodType::M1_RESCUE_NO_RESCUABLE
               || method == MethodType::M1_RESCUE_HYBRID) {
        Event chk;
        chk.timestamp_us = m0_config_.t_check_us;
        chk.type = EventType::CHECK_MIGRATION;
        chk.host_id = 0;
        schedule_event(chk);
    } else if (method == MethodType::M0_PROACTIVE_MIGRATION
               || method == MethodType::M1_AQB_PROACTIVE_MIGRATION
               || method == MethodType::M2_DQB_PROACTIVE_MIGRATION) {
        double t_check = m0_config_.t_check_us;
        std::uniform_real_distribution<double> offset_dist(0.0, t_check);
        for (int i = 0; i < active_host_count_; ++i) {
            Event chk;
            chk.timestamp_us = t_check + offset_dist(rng_);
            chk.type = EventType::CHECK_MIGRATION;
            chk.host_id = i;
            schedule_event(chk);
        }
    }
}

int Simulator::run() {
    while (!event_queue_.empty()) {
        Event e = event_queue_.top();
        event_queue_.pop();
        now_us_ = e.timestamp_us;
        process_event(e);

        // Stop after measurement window.
        if (metrics_.recording &&
            metrics_.total_finished >= static_cast<uint64_t>(measurement_target_))
            break;
    }
    return 0;
}

void Simulator::schedule_event(Event e) {
    event_queue_.push(e);
}

void Simulator::process_event(const Event& e) {
    switch (e.type) {
        case EventType::TASK_GENERATE:    handle_task_generate(e); break;
        case EventType::TASK_ARRIVE:      handle_task_arrive(e);   break;
        case EventType::TASK_MIGRATION_ARRIVE: handle_task_migration_arrive(e); break;
        case EventType::TASK_EXECUTE:     /* not queued directly */ break;
        case EventType::TASK_FINISH:      handle_task_finish(e);   break;
        case EventType::SYNC_LOAD:        handle_sync_load(e);     break;
        case EventType::CHECK_MIGRATION:  handle_check_migration(e); break;
    }
}

double Simulator::estimate_service_time(RpcMethod method, double base_service_us) {
    switch (m0_config_.service_estimate_mode) {
        case SERVICE_ESTIMATE_MEAN:
            return W3_MEAN_SERVICE_US;
        case SERVICE_ESTIMATE_NOISY_ORACLE: {
            double cv = std::max(0.0, m0_config_.service_estimate_noise_cv);
            if (cv <= 0.0) return base_service_us;
            double sigma = std::sqrt(std::log(1.0 + cv * cv));
            double mu = -0.5 * sigma * sigma;
            std::normal_distribution<double> noise(mu, sigma);
            return std::max(0.1, base_service_us * std::exp(noise(estimator_rng_)));
        }
        case SERVICE_ESTIMATE_CLASS_MEAN:
            return class_mean_service_estimate(method);
        case SERVICE_ESTIMATE_EWMA:
            return (method == RpcMethod::SHORT_RPC)
                ? ewma_short_service_us_ : ewma_long_service_us_;
        case SERVICE_ESTIMATE_QUANTILE_GUARD:
            return (method == RpcMethod::SHORT_RPC)
                ? quantile_short_service_us_ : quantile_long_service_us_;
        case SERVICE_ESTIMATE_ORACLE:
        default:
            return base_service_us;
    }
}

double Simulator::class_mean_service_estimate(RpcMethod method) const {
    bool short_class = method == RpcMethod::SHORT_RPC;
    if (workload_type_ == WorkloadType::W3_POISSON_LOGNORMAL) {
        return short_class ? 10.0 : 42.0;
    }
    return short_class ? BIMODAL_SHORT_US : BIMODAL_LONG_US;
}

void Simulator::update_service_estimator(RpcMethod method, double base_service_us) {
    double alpha = std::min(1.0, std::max(0.001, m0_config_.service_estimate_ewma_alpha));
    bool short_class = method == RpcMethod::SHORT_RPC;
    double& ewma = short_class ? ewma_short_service_us_ : ewma_long_service_us_;
    double& guard = short_class ? quantile_short_service_us_ : quantile_long_service_us_;
    double floor_est = class_mean_service_estimate(method);

    ewma = (1.0 - alpha) * ewma + alpha * base_service_us;

    if (base_service_us > guard) {
        guard = (1.0 - alpha) * guard + alpha * base_service_us;
    } else {
        double decay_alpha = alpha * 0.10;
        guard = (1.0 - decay_alpha) * guard + decay_alpha * base_service_us;
    }
    guard = std::max(floor_est, guard);
    if (short_class) ++estimator_short_samples_;
    else ++estimator_long_samples_;
}

void Simulator::handle_task_generate(const Event& /*e*/) {
    if (!trace_ || trace_index_ >= trace_->entries().size()) return;
    const WorkloadTraceEntry& entry = trace_->entries()[trace_index_++];
    if (trace_index_ < trace_->entries().size()) {
        Event next_gen;
        next_gen.timestamp_us = trace_->entries()[trace_index_].generate_time_us;
        next_gen.type = EventType::TASK_GENERATE;
        schedule_event(next_gen);
    }

    Task* t = alloc_task();
    if (t->id != entry.id) throw std::logic_error("trace/task id mismatch");
    t->generate_time_us = entry.generate_time_us;
    t->rpc_method = entry.rpc_method;
    t->base_service_time_us = entry.service_time_us;
    t->deadline_budget_us = entry.deadline_budget_us;
    t->initial_core = entry.initial_core;
    t->flow_id = entry.flow_id;
    t->arrival_burst = entry.burst;
    t->measurement_eligible =
        entry.id > static_cast<uint64_t>(trace_->config().warmup_requests);
    if (t->measurement_eligible && !metrics_.recording) metrics_.start_recording();
    t->estimator_prior_samples = t->rpc_method == RpcMethod::SHORT_RPC
        ? estimator_short_samples_ : estimator_long_samples_;
    t->expected_service_time_us = estimate_service_time(
        t->rpc_method, t->base_service_time_us);
    if (t->measurement_eligible)
        total_generated_work_us_ += t->base_service_time_us + T_host_us;

    if (is_intra_host_method()) {
        enqueue_task_on_random_core(t);
        return;
    }

    if (method_type_ == MethodType::B0_IDEAL_CFCFS) {
        // B0: push to global queue, try to dispatch to idle core.
        global_queue_.push_back(t);
        try_b0_pull();
        return;
    }

    // B1/B2/M0: dispatch to a host.
    int dst;
    if (workload_type_ == WorkloadType::W2_MMPP_BIMODAL
        && t->arrival_burst && !hot_nodes_.empty()) {
        if (!last_burst_state_) {
            refresh_hot_nodes();
            last_burst_state_ = true;
        }
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (u(rng_) < m0_config_.w2_hot_dispatch_prob) {
            std::uniform_int_distribution<int> hd(0, static_cast<int>(hot_nodes_.size()) - 1);
            int a = hot_nodes_[hd(rng_)], b = hot_nodes_[hd(rng_)];
            dst = (stale_view_[a] <= stale_view_[b]) ? a : b;
        } else {
            dst = dispatch_task(t->expected_service_time_us);
        }
    } else {
        if (last_burst_state_) last_burst_state_ = false;
        dst = dispatch_task(t->expected_service_time_us);
    }
    t->assigned_host = dst;

    Event arr;
    arr.timestamp_us = now_us_ + T_net_oneway_us;
    arr.type = EventType::TASK_ARRIVE;
    arr.host_id = dst;
    arr.task_id = t->id;
    schedule_event(arr);
}

int Simulator::dispatch_task(double service_est_us) {
    return scheduler_->on_task_dispatch(nodes_, stale_view_, service_est_us, rng_);
}

void Simulator::enqueue_task_on_core(Task* task, int host, int core_id, double now_us) {
    if (!task || host < 0 || host >= static_cast<int>(nodes_.size())) return;
    Node& node = nodes_[host];
    if (core_id < 0 || core_id >= static_cast<int>(node.cores.size())) return;

    task->arrive_time_us = now_us;
    task->assigned_host = host;
    task->assigned_core = core_id;

    Core& core = node.cores[core_id];
    core.push_waiting(task);
    if (core.idle) start_execution(core, now_us);
}

void Simulator::enqueue_task_on_random_core(Task* task) {
    int core_id = task ? task->initial_core : -1;
    if (core_id < 0 || core_id >= CORES_PER_HOST)
        throw std::logic_error("trace initial core is out of range");
    enqueue_task_on_core(task, 0, core_id, now_us_);
}

void Simulator::refresh_hot_nodes() {
    // Select HOT_NODE_COUNT random nodes as burst targets.
    hot_nodes_.resize(HOT_NODE_COUNT);
    std::vector<int> all(NUM_HOSTS);
    std::iota(all.begin(), all.end(), 0);
    std::shuffle(all.begin(), all.end(), rng_);
    std::copy_n(all.begin(), HOT_NODE_COUNT, hot_nodes_.begin());
}

void Simulator::refresh_hot_cores() {
    int hot_core_count = std::max(1, std::min(CORES_PER_HOST, m0_config_.w2_hot_core_count));
    hot_cores_.resize(hot_core_count);
    std::vector<int> all(CORES_PER_HOST);
    std::iota(all.begin(), all.end(), 0);
    std::shuffle(all.begin(), all.end(), rng_);
    std::copy_n(all.begin(), hot_core_count, hot_cores_.begin());
}

void Simulator::handle_task_arrive(const Event& e) {
    int host = e.host_id;
    Task* t = nullptr;
    // Find task by id in pool.
    if (e.task_id > 0 && e.task_id <= task_pool_.size())
        t = task_pool_[e.task_id - 1].get();
    if (!t) return;

    t->arrive_time_us = now_us_;
    if (t->pending_reserved_work_us > 0.0
        && t->reserved_dst_host == host
        && host >= 0 && host < static_cast<int>(incoming_reservation_.size())) {
        incoming_reservation_[host] =
            std::max(0.0, incoming_reservation_[host] - t->pending_reserved_work_us);
        if (t->reserved_dst_core >= 0
            && t->reserved_dst_core < CORES_PER_HOST
            && host < static_cast<int>(incoming_core_reservation_.size())) {
            incoming_core_reservation_[host][t->reserved_dst_core] =
                std::max(0.0,
                         incoming_core_reservation_[host][t->reserved_dst_core]
                         - t->pending_reserved_work_us);
        }
        t->pending_reserved_work_us = 0.0;
        t->reserved_dst_host = -1;
        t->reserved_dst_core = -1;
    }

    Node& node = nodes_[host];
    t->assigned_host = host;

    // Migrated tasks can carry a pre-planned destination core from the
    // batch-aware placement step. Fresh arrivals still use shortest-queue core.
    int cid = node.shortest_queue_core();
    if (t->reserved_dst_core >= 0 && t->reserved_dst_core < CORES_PER_HOST)
        cid = t->reserved_dst_core;
    t->assigned_core = cid;
    Core& core = node.cores[cid];

    if (core.idle) {
        core.push_waiting(t);
        start_execution(core, now_us_);
    } else {
        core.push_waiting(t);
    }
}

void Simulator::handle_task_migration_arrive(const Event& e) {
    Task* task = nullptr;
    if (e.task_id > 0 && e.task_id <= task_pool_.size())
        task = task_pool_[e.task_id - 1].get();
    if (!task || !task->migration_in_flight
        || task->assigned_host != e.host_id || task->assigned_core != e.core_id)
        return;
    if (task->pending_reserved_work_us > 0.0) {
        incoming_reservation_[e.host_id] = std::max(
            0.0, incoming_reservation_[e.host_id] - task->pending_reserved_work_us);
        incoming_core_reservation_[e.host_id][e.core_id] = std::max(
            0.0, incoming_core_reservation_[e.host_id][e.core_id]
                   - task->pending_reserved_work_us);
    }
    task->pending_reserved_work_us = 0.0;
    task->reserved_dst_host = -1;
    task->reserved_dst_core = -1;
    task->migration_in_flight = false;
    const double handoff_us = now_us_ - task->migration_start_us;
    if (task->descriptor_handoff_paid)
        metrics_.on_descriptor_handoff(handoff_us, task->measurement_eligible);
    if (task->rescue_intra_moved)
        metrics_.on_rescue_handoff(handoff_us, task->measurement_eligible);
    task->descriptor_handoff_paid = false;
    task->arrive_time_us = now_us_;
    Core& destination = nodes_[e.host_id].cores[e.core_id];
    if (task->rescue_intra_moved
        && m0_config_.rescue_target_insert_policy == RESCUE_TARGET_INSERT_HEAD_STRESS)
        destination.push_waiting_front(task);
    else
        destination.push_waiting(task);
    if (destination.idle) start_execution(destination, now_us_);
}

void Simulator::handle_task_execute(int host, int core_id) {
    // Not used as a queued event; execution is started inline.
    (void)host; (void)core_id;
}

void Simulator::start_execution(Core& c, double now) {
    Task* t = c.pop_waiting_front();
    if (!t) { c.idle = true; return; }
    if (t->migration_in_flight)
        throw std::logic_error("in-flight descriptor reached execution");
    if (t->execution_started || t->completed)
        throw std::logic_error("request executed more than once");
    t->execution_started = true;
    c.idle = false;
    c.running = t;
    double exec_us = compute_exec_time(t->base_service_time_us, c.capacity);
    c.finish_time_us = now + exec_us;

    Event fin;
    fin.timestamp_us = c.finish_time_us;
    fin.type = EventType::TASK_FINISH;
    fin.host_id = c.host_id;
    fin.core_id = c.core_id;
    fin.task_id = t->id;
    schedule_event(fin);
}

double Simulator::compute_exec_time(double base_service_us, double capacity) const {
    return base_service_us / capacity + T_host_us;
}

bool Simulator::move_waiting_task_intra_host(int host, int src_core_id, int dst_core_id,
                                             Task* task,
                                             double estimated_local_latency_us,
                                             bool proactive,
                                             bool paid_handoff) {
    if (!task || task->migration_in_flight
        || host < 0 || host >= static_cast<int>(nodes_.size())) return false;
    if (src_core_id < 0 || src_core_id >= CORES_PER_HOST) return false;
    if (dst_core_id < 0 || dst_core_id >= CORES_PER_HOST) return false;
    if (src_core_id == dst_core_id) return false;

    Node& node = nodes_[host];
    Core& src_core = node.cores[src_core_id];
    Core& dst_core = node.cores[dst_core_id];
    if (!src_core.wait_queue.contains(task)) return false;

    src_core.remove_waiting(task);
    task->intra_moved = true;
    task->src_host = host;
    task->src_core = src_core_id;
    task->assigned_host = host;
    task->assigned_core = dst_core_id;

    double moved_work_us =
        task->expected_service_time_us / dst_core.capacity + T_host_us;
    if (proactive) {
        task->estimated_local_latency_us = estimated_local_latency_us;
        task->proactive_intra_moved = true;
        task->proactive_intra_recorded = task->measurement_eligible;
        metrics_.on_proactive_intra_success(moved_work_us, task->measurement_eligible);
    } else {
        metrics_.on_steal_success(moved_work_us, task->measurement_eligible);
    }

    if (paid_handoff) {
        task->migration_in_flight = true;
        task->descriptor_handoff_paid = true;
        task->migration_start_us = now_us_;
        task->pending_reserved_work_us = moved_work_us;
        task->reserved_dst_host = host;
        task->reserved_dst_core = dst_core_id;
        incoming_reservation_[host] += moved_work_us;
        incoming_core_reservation_[host][dst_core_id] += moved_work_us;
        metrics_.on_target_reservation(
            incoming_core_reservation_[host][dst_core_id], task->measurement_eligible);
        Event arrival;
        arrival.timestamp_us = now_us_ + std::max(
            0.0, m0_config_.rescue_migration_cost_us);
        arrival.type = EventType::TASK_MIGRATION_ARRIVE;
        arrival.host_id = host;
        arrival.core_id = dst_core_id;
        arrival.task_id = task->id;
        schedule_event(arrival);
    } else {
        dst_core.push_waiting(task);
        if (dst_core.idle) start_execution(dst_core, now_us_);
    }
    return true;
}

bool Simulator::move_rescue_task_intra_host(int host, int src_core_id, int dst_core_id,
                                            Task* task,
                                            double estimated_local_latency_us,
                                            double estimated_remote_latency_us,
                                            int predicted_target_delta_risk,
                                            bool predicted_harmful,
                                            bool relief) {
    if (!task || task->migration_in_flight
        || host < 0 || host >= static_cast<int>(nodes_.size())) return false;
    if (src_core_id < 0 || src_core_id >= CORES_PER_HOST) return false;
    if (dst_core_id < 0 || dst_core_id >= CORES_PER_HOST) return false;
    if (src_core_id == dst_core_id) return false;

    Node& node = nodes_[host];
    Core& src_core = node.cores[src_core_id];
    Core& dst_core = node.cores[dst_core_id];
    if (!src_core.wait_queue.contains(task)) return false;
    const double source_work_at_commit_us = src_core.local_workload_us(now_us_);
    const double destination_work_at_commit_us = dst_core.local_workload_us(now_us_)
        + incoming_core_reservation_[host][dst_core_id];
    uint64_t rescue_migration_id = ++migration_batch_id_counter_;

    auto deadline_abs = [](const Task* t) {
        return t ? t->deadline_abs_us() : 0.0;
    };

    uint64_t watched_target_tasks = 0;
    double target_prefix_work_us = 0.0;
    if (!dst_core.idle && dst_core.running)
        target_prefix_work_us = std::max(0.0, dst_core.finish_time_us - now_us_);
    for (Task* cur = dst_core.wait_queue.begin(); cur; cur = cur->next) {
        double work_us = cur->expected_service_time_us / dst_core.capacity + T_host_us;
        double completion_abs_us = now_us_ + target_prefix_work_us + work_us;
        if (completion_abs_us <= deadline_abs(cur)) {
            cur->target_harm_watch_active = true;
            cur->target_harm_watch_recorded = cur->measurement_eligible;
            cur->target_harm_watch_migration_id = rescue_migration_id;
            cur->target_harm_counterfactual_latency_us =
                completion_abs_us - cur->generate_time_us;
            ++watched_target_tasks;
        }
        target_prefix_work_us += work_us;
    }
    metrics_.on_rescue_target_harm_watch(watched_target_tasks);

    src_core.remove_waiting(task);
    task->target_harm_watch_active = false;
    task->target_harm_watch_recorded = false;
    task->target_harm_watch_migration_id = 0;
    task->target_harm_counterfactual_latency_us = 0.0;
    task->intra_moved = true;
    task->rescue_intra_moved = true;
    task->rescue_intra_recorded = task->measurement_eligible;
    task->rescue_relief_moved = relief;
    task->rescue_predicted_harmful = predicted_harmful;
    task->rescue_migration_id = rescue_migration_id;
    task->src_host = host;
    task->src_core = src_core_id;
    task->assigned_host = host;
    task->assigned_core = dst_core_id;
    task->migration_in_flight = true;
    task->descriptor_handoff_paid = true;
    task->migration_start_us = now_us_;
    task->estimated_local_latency_us = estimated_local_latency_us;
    task->rescue_predicted_remote_latency_us = estimated_remote_latency_us;
    task->rescue_predicted_target_delta_risk = predicted_target_delta_risk;

    double moved_work_us =
        task->expected_service_time_us / dst_core.capacity + T_host_us;
    task->pending_reserved_work_us = moved_work_us;
    task->reserved_dst_host = host;
    task->reserved_dst_core = dst_core_id;
    incoming_reservation_[host] += moved_work_us;
    incoming_core_reservation_[host][dst_core_id] += moved_work_us;
    metrics_.on_target_reservation(
        incoming_core_reservation_[host][dst_core_id], task->measurement_eligible);
    metrics_.on_rescue_success(
        moved_work_us, predicted_harmful, relief, task->measurement_eligible,
        task->rpc_method, task->arrival_burst, source_work_at_commit_us,
        destination_work_at_commit_us);

    Event arrival;
    arrival.timestamp_us = now_us_ + std::max(0.0, m0_config_.rescue_migration_cost_us);
    arrival.type = EventType::TASK_MIGRATION_ARRIVE;
    arrival.host_id = host;
    arrival.core_id = dst_core_id;
    arrival.task_id = task->id;
    schedule_event(arrival);
    return true;
}

bool Simulator::steal_one_task(int host, int idle_core_id) {
    metrics_.on_steal_attempt();
    if (host < 0 || host >= static_cast<int>(nodes_.size())) return false;
    if (idle_core_id < 0 || idle_core_id >= CORES_PER_HOST) return false;
    if (incoming_core_reservation_[host][idle_core_id] > 0.0) return false;

    Node& node = nodes_[host];
    int src_core_id = -1;
    double best_work_us = -1.0;
    for (const auto& core : node.cores) {
        if (core.core_id == idle_core_id || core.wait_queue.empty()) continue;
        double work_us = core.local_workload_us(now_us_);
        if (work_us > best_work_us) {
            best_work_us = work_us;
            src_core_id = core.core_id;
        }
    }

    if (src_core_id < 0) return false;
    Task* task = node.cores[src_core_id].wait_queue.front();
    if (!task) return false;
    return move_waiting_task_intra_host(
        host, src_core_id, idle_core_id, task, 0.0, false,
        method_type_ == MethodType::L1_WORK_STEALING_POLLING);
}

int Simulator::run_work_stealing_poll(int host) {
    if (host < 0 || host >= static_cast<int>(nodes_.size())) return 0;
    Node& node = nodes_[host];
    uint64_t idle_cores = 0;
    for (const auto& core : node.cores)
        if (core.idle && incoming_core_reservation_[host][core.core_id] <= 0.0)
            ++idle_cores;
    metrics_.on_steal_poll(idle_cores);
    metrics_.on_control_poll(m0_config_.control_poll_cost_us);

    int moved = 0;
    const int limit = std::max(1, m0_config_.work_steal_max_per_poll);
    for (auto& core : node.cores) {
        if (moved >= limit) break;
        if (!core.idle || incoming_core_reservation_[host][core.core_id] > 0.0)
            continue;
        if (steal_one_task(host, core.core_id)) ++moved;
    }
    return moved;
}

bool Simulator::run_alto_threshold_check(int host) {
    metrics_.on_proactive_intra_attempt();
    metrics_.on_control_check(m0_config_.control_check_cost_us);
    if (host < 0 || host >= static_cast<int>(nodes_.size())) return false;
    Node& node = nodes_[host];
    const int scan_depth = std::max(1, m0_config_.rescue_scan_depth);
    const int max_candidates = std::max(1, m0_config_.rescue_k_candidates);
    const int target_count = std::max(1, m0_config_.rescue_h_targets);

    struct Target { int core; double work_us; };
    std::vector<Target> targets;
    for (const auto& core : node.cores) {
        targets.push_back(Target{
            core.core_id,
            core.local_workload_us(now_us_)
                + incoming_core_reservation_[host][core.core_id]
        });
    }
    std::sort(targets.begin(), targets.end(),
              [](const Target& a, const Target& b) { return a.work_us < b.work_us; });

    Task* best_task = nullptr;
    int best_src = -1;
    int best_dst = -1;
    double best_local_latency = 0.0;
    double best_score = -std::numeric_limits<double>::infinity();

    for (auto& source : node.cores) {
        const double source_work = source.local_workload_us(now_us_);
        if (source_work < m0_config_.alto_queue_threshold_us) continue;
        double prefix_work_us = (!source.idle && source.running)
            ? std::max(0.0, source.finish_time_us - now_us_) : 0.0;
        int depth = 0;
        int candidates = 0;
        for (Task* task = source.wait_queue.begin();
             task && depth < scan_depth && candidates < max_candidates;
             task = task->next, ++depth) {
            metrics_.on_control_queue_entry(
                m0_config_.control_queue_entry_cost_us);
            const double source_task_work =
                task->expected_service_time_us / source.capacity + T_host_us;
            const double local_completion_abs = now_us_ + prefix_work_us
                                              + source_task_work;
            if (!task->intra_moved && local_completion_abs > task->deadline_abs_us()) {
                ++candidates;
                metrics_.on_control_candidate(m0_config_.control_candidate_cost_us);
                int tried = 0;
                for (const auto& target : targets) {
                    if (target.core == source.core_id) continue;
                    if (tried++ >= target_count) break;
                    metrics_.on_control_target(m0_config_.control_target_cost_us);
                    const Core& destination = node.cores[target.core];
                    const double target_task_work =
                        task->expected_service_time_us / destination.capacity + T_host_us;
                    const double remote_completion_abs = now_us_
                        + m0_config_.rescue_migration_cost_us
                        + target.work_us + target_task_work;
                    const double gain_us = local_completion_abs - remote_completion_abs;
                    if (gain_us + 1e-9 < m0_config_.alto_min_gain_us) continue;
                    const double lateness_us = std::max(
                        0.0, local_completion_abs - task->deadline_abs_us());
                    const double score = gain_us + 0.10 * lateness_us;
                    if (score > best_score) {
                        best_score = score;
                        best_task = task;
                        best_src = source.core_id;
                        best_dst = target.core;
                        best_local_latency =
                            local_completion_abs - task->generate_time_us;
                    }
                }
            }
            prefix_work_us += source_task_work;
        }
    }

    if (!best_task) return false;
    return move_waiting_task_intra_host(
        host, best_src, best_dst, best_task, best_local_latency, true, true);
}

bool Simulator::run_intra_proactive_check(int host) {
    metrics_.on_proactive_intra_attempt();
    if (host < 0 || host >= static_cast<int>(nodes_.size())) return false;

    struct Candidate {
        Task* task = nullptr;
        int src_core = -1;
        int dst_core = -1;
        double local_latency_us = 0.0;
        double gain_us = 0.0;
    };

    Node& node = nodes_[host];
    Candidate best;
    double best_score = -std::numeric_limits<double>::infinity();

    for (auto& src_core : node.cores) {
        double prefix_work_us = 0.0;
        if (!src_core.idle && src_core.running) {
            prefix_work_us = std::max(0.0, src_core.finish_time_us - now_us_);
        }

        Task* cur = src_core.wait_queue.begin();
        int depth = 0;
        while (cur && depth < INTRA_PROACTIVE_SCAN_DEPTH) {
            Task* next = cur->next;
            double exec_src_us =
                cur->expected_service_time_us / src_core.capacity + T_host_us;
            double age_us = now_us_ - cur->generate_time_us;
            double local_latency_us = age_us + prefix_work_us + exec_src_us;

            if (!cur->proactive_intra_moved
                && local_latency_us > cur->deadline_budget_us * m0_config_.alpha) {
                int dst_core_id = -1;
                double dst_work_us = std::numeric_limits<double>::infinity();
                for (const auto& dst_core : node.cores) {
                    if (dst_core.core_id == src_core.core_id) continue;
                    double work_us = dst_core.local_workload_us(now_us_);
                    if (work_us < dst_work_us) {
                        dst_work_us = work_us;
                        dst_core_id = dst_core.core_id;
                    }
                }

                if (dst_core_id >= 0) {
                    const Core& dst_core = node.cores[dst_core_id];
                    double exec_dst_us =
                        cur->expected_service_time_us / dst_core.capacity + T_host_us;
                    double moved_latency_us = age_us + dst_work_us + exec_dst_us;
                    double gain_us = local_latency_us - moved_latency_us;
                    if (gain_us > m0_config_.margin_us && gain_us > best_score) {
                        best.task = cur;
                        best.src_core = src_core.core_id;
                        best.dst_core = dst_core_id;
                        best.local_latency_us = local_latency_us;
                        best.gain_us = gain_us;
                        best_score = gain_us;
                    }
                }
            }

            prefix_work_us += exec_src_us;
            cur = next;
            ++depth;
        }
    }

    if (!best.task) return false;
    return move_waiting_task_intra_host(
        host, best.src_core, best.dst_core, best.task,
        best.local_latency_us, true);
}

bool Simulator::run_rescue_sched_check(int host) {
    metrics_.on_rescue_attempt();
    metrics_.on_control_check(m0_config_.control_check_cost_us);
    if (host < 0 || host >= static_cast<int>(nodes_.size())) return false;

    const bool no_target_safety =
        method_type_ == MethodType::M1_RESCUE_NO_TARGET_SAFETY;
    const bool no_rescuable =
        method_type_ == MethodType::M1_RESCUE_NO_RESCUABLE;
    const bool hybrid =
        method_type_ == MethodType::M1_RESCUE_HYBRID;
    const int scan_depth = std::max(1, m0_config_.rescue_scan_depth);
    const int max_candidates = std::max(1, m0_config_.rescue_k_candidates);
    const int target_count = std::max(1, m0_config_.rescue_h_targets);
    const int budget = std::max(1, m0_config_.rescue_budget_per_check);

    struct TargetSummary {
        int core = -1;
        double workload_us = 0.0;
        int risk_before = 0;
    };

    struct Candidate {
        Task* task = nullptr;
        int src_core = -1;
        double local_latency_us = 0.0;
        double local_completion_abs_us = 0.0;
        double source_work_us = 0.0;
    };

    struct ScoredMove {
        Candidate cand;
        int dst_core = -1;
        double score = 0.0;
        double remote_latency_us = 0.0;
        int target_delta_risk = 0;
        bool predicted_harmful = false;
    };

    Node& node = nodes_[host];

    auto estimated_task_work = [](const Core& core, const Task* task) {
        return task ? task->expected_service_time_us / core.capacity + T_host_us : 0.0;
    };

    auto deadline_abs = [](const Task* task) {
        return task->deadline_abs_us();
    };

    auto estimate_risk_before = [&](const Core& core) {
        int risk = 0;
        double prefix_work_us = 0.0;
        if (!core.idle && core.running)
            prefix_work_us = std::max(0.0, core.finish_time_us - now_us_);

        int depth = 0;
        for (Task* cur = core.wait_queue.begin();
             cur && depth < scan_depth;
             cur = cur->next, ++depth) {
            metrics_.on_rescue_queue_entry();
            metrics_.on_control_queue_entry(
                m0_config_.control_queue_entry_cost_us);
            double work_us = estimated_task_work(core, cur);
            double completion_abs_us = now_us_ + prefix_work_us + work_us;
            if (completion_abs_us > deadline_abs(cur)) ++risk;
            prefix_work_us += work_us;
        }
        return risk;
    };

    std::vector<TargetSummary> targets;
    targets.reserve(node.cores.size());
    double total_work_us = 0.0;
    for (const auto& core : node.cores) {
        double work_us = core.local_workload_us(now_us_)
                       + incoming_core_reservation_[host][core.core_id];
        total_work_us += work_us;
        targets.push_back(TargetSummary{
            core.core_id,
            work_us,
            estimate_risk_before(core)
        });
    }
    std::sort(targets.begin(), targets.end(),
              [](const TargetSummary& a, const TargetSummary& b) {
                  if (a.risk_before != b.risk_before)
                      return a.risk_before < b.risk_before;
                  return a.workload_us < b.workload_us;
              });

    double avg_work_us =
        node.cores.empty() ? 0.0 : total_work_us / static_cast<double>(node.cores.size());

    std::vector<Candidate> candidates;
    candidates.reserve(CORES_PER_HOST * max_candidates);

    for (auto& src_core : node.cores) {
        double source_work_us = src_core.local_workload_us(now_us_);
        if (no_rescuable && source_work_us <= avg_work_us) continue;

        double prefix_work_us = 0.0;
        if (!src_core.idle && src_core.running)
            prefix_work_us = std::max(0.0, src_core.finish_time_us - now_us_);

        int depth = 0;
        int accepted_from_source = 0;
        for (Task* cur = src_core.wait_queue.begin();
             cur && depth < scan_depth && accepted_from_source < max_candidates;
             cur = cur->next, ++depth) {
            metrics_.on_rescue_candidate();
            metrics_.on_rescue_queue_entry();
            metrics_.on_control_queue_entry(
                m0_config_.control_queue_entry_cost_us);
            if (cur->intra_moved) {
                prefix_work_us += estimated_task_work(src_core, cur);
                continue;
            }

            double work_us = estimated_task_work(src_core, cur);
            double local_completion_abs_us = now_us_ + prefix_work_us + work_us;
            double local_latency_us = local_completion_abs_us - cur->generate_time_us;
            bool locally_doomed = local_completion_abs_us > deadline_abs(cur);
            if (locally_doomed) metrics_.on_rescue_locally_doomed();

            if (no_rescuable || locally_doomed) {
                candidates.push_back(Candidate{
                    cur,
                    src_core.core_id,
                    local_latency_us,
                    local_completion_abs_us,
                    source_work_us
                });
                ++accepted_from_source;
                metrics_.on_rescue_accepted_candidate();
                metrics_.on_control_candidate(
                    m0_config_.control_candidate_cost_us);
            }

            prefix_work_us += work_us;
        }
    }

    std::vector<ScoredMove> scored;
    scored.reserve(candidates.size());

    for (const auto& cand : candidates) {
        if (!cand.task) continue;
        int tried_targets = 0;
        for (const auto& target : targets) {
            if (target.core == cand.src_core) continue;
            if (tried_targets >= target_count) break;
            ++tried_targets;
            metrics_.on_rescue_target_evaluation();
            metrics_.on_control_target(m0_config_.control_target_cost_us);

            const Core& dst_core = node.cores[target.core];
            double remote_work_us = estimated_task_work(dst_core, cand.task);
            double remote_completion_abs_us =
                now_us_ + m0_config_.rescue_migration_cost_us
                + target.workload_us + remote_work_us;
            double remote_latency_us =
                remote_completion_abs_us - cand.task->generate_time_us;
            bool remote_feasible =
                remote_completion_abs_us + m0_config_.rescue_epsilon_us
                <= deadline_abs(cand.task);

            if (!no_rescuable && !remote_feasible) {
                metrics_.on_rescue_remote_infeasible_reject();
                continue;
            }
            if (remote_feasible) metrics_.on_rescue_remote_feasible();

            int delta_risk = no_rescuable
                ? 0
                : ((remote_completion_abs_us > deadline_abs(cand.task)) ? 1 : 0);
            bool strict_target_safe =
                target.risk_before == 0 && delta_risk <= m0_config_.rescue_theta;
            if (strict_target_safe) {
                metrics_.on_rescue_target_safe();
            } else if (!no_target_safety) {
                metrics_.on_rescue_target_unsafe_reject();
                continue;
            }

            double local_lateness_us =
                std::max(0.0, cand.local_completion_abs_us - deadline_abs(cand.task));
            double remote_slack_us = deadline_abs(cand.task) - remote_completion_abs_us;
            double score = remote_slack_us + 0.10 * local_lateness_us
                         - m0_config_.rescue_migration_cost_us;
            if (no_rescuable) {
                double pressure_gain_us =
                    cand.source_work_us - target.workload_us - remote_work_us;
                if (pressure_gain_us <= 0.0) continue;
                score = pressure_gain_us + 0.05 * local_lateness_us
                      - 0.01 * std::max(0.0, remote_latency_us);
            }

            scored.push_back(ScoredMove{
                cand,
                target.core,
                score,
                remote_latency_us,
                delta_risk,
                !strict_target_safe
            });
        }
    }

    if (scored.empty()) {
        return hybrid ? run_hybrid_relief_check(host, budget) : false;
    }

    std::sort(scored.begin(), scored.end(),
              [](const ScoredMove& a, const ScoredMove& b) {
                  return a.score > b.score;
              });

    auto task_is_waiting_on_source = [&](const ScoredMove& move) {
        const Core& src_core = node.cores[move.cand.src_core];
        for (Task* cur = src_core.wait_queue.begin(); cur; cur = cur->next) {
            if (cur == move.cand.task) return true;
        }
        return false;
    };

    std::vector<bool> source_used(CORES_PER_HOST, false);
    int moved = 0;
    for (const auto& move : scored) {
        if (moved >= budget) break;
        if (move.cand.src_core < 0 || move.cand.src_core >= CORES_PER_HOST) continue;
        if (move.dst_core < 0 || move.dst_core >= CORES_PER_HOST) continue;
        if (source_used[move.cand.src_core]) continue;
        if (!task_is_waiting_on_source(move)) {
            metrics_.on_rescue_source_revalidation_reject();
            continue;
        }

        const Core& destination = node.cores[move.dst_core];
        double remote_work_us = estimated_task_work(destination, move.cand.task);
        double current_target_work_us = destination.local_workload_us(now_us_)
            + incoming_core_reservation_[host][move.dst_core];
        double current_remote_completion_abs_us = now_us_
            + m0_config_.rescue_migration_cost_us
            + current_target_work_us + remote_work_us;
        if (!no_rescuable
            && current_remote_completion_abs_us + m0_config_.rescue_epsilon_us
                > deadline_abs(move.cand.task)) {
            metrics_.on_rescue_remote_infeasible_reject();
            metrics_.on_rescue_remote_revalidation_reject();
            continue;
        }
        double current_remote_latency_us =
            current_remote_completion_abs_us - move.cand.task->generate_time_us;

        if (move_rescue_task_intra_host(
                host, move.cand.src_core, move.dst_core, move.cand.task,
                move.cand.local_latency_us, current_remote_latency_us,
                move.target_delta_risk, move.predicted_harmful, false)) {
            source_used[move.cand.src_core] = true;
            ++moved;
        }
    }

    metrics_.on_rescue_check_commits(static_cast<uint64_t>(moved));
    if (moved > 0) return true;
    return hybrid ? run_hybrid_relief_check(host, budget) : false;
}

bool Simulator::run_hybrid_relief_check(int host, int budget) {
    metrics_.on_relief_attempt();
    if (host < 0 || host >= static_cast<int>(nodes_.size())) return false;
    if (workload_type_ != WorkloadType::W2_MMPP_BIMODAL) return false;
    if (budget <= 0 || m0_config_.rescue_hybrid_max_relief_per_check <= 0) return false;

    Node& node = nodes_[host];
    double total_work_us = 0.0;
    for (const auto& core : node.cores)
        total_work_us += core.local_workload_us(now_us_)
                       + incoming_core_reservation_[host][core.core_id];
    double avg_work_us =
        node.cores.empty() ? 0.0 : total_work_us / static_cast<double>(node.cores.size());
    if (avg_work_us <= 0.0) return false;

    auto deadline_abs = [](const Task* task) {
        return task->deadline_abs_us();
    };
    auto estimated_task_work = [](const Core& core, const Task* task) {
        return task ? task->expected_service_time_us / core.capacity + T_host_us : 0.0;
    };

    struct ReliefMove {
        Task* task = nullptr;
        int src_core = -1;
        int dst_core = -1;
        double local_latency_us = 0.0;
        double remote_latency_us = 0.0;
        double score = 0.0;
        bool predicted_harmful = false;
    };

    ReliefMove best;
    double best_score = -std::numeric_limits<double>::infinity();
    double pressure_ratio = std::max(1.0, m0_config_.rescue_hybrid_pressure_ratio);
    int scan_depth = std::max(1, m0_config_.rescue_scan_depth / 2);

    std::vector<double> core_work_us(CORES_PER_HOST, 0.0);
    for (const auto& core : node.cores)
        core_work_us[core.core_id] = core.local_workload_us(now_us_)
            + incoming_core_reservation_[host][core.core_id];

    for (auto& src_core : node.cores) {
        double src_work = core_work_us[src_core.core_id];
        if (src_work < avg_work_us * pressure_ratio) continue;

        double prefix_work_us = 0.0;
        if (!src_core.idle && src_core.running)
            prefix_work_us = std::max(0.0, src_core.finish_time_us - now_us_);

        int depth = 0;
        for (Task* cur = src_core.wait_queue.begin();
             cur && depth < scan_depth;
             cur = cur->next, ++depth) {
            if (cur->intra_moved) {
                prefix_work_us += estimated_task_work(src_core, cur);
                continue;
            }

            double task_work_src = estimated_task_work(src_core, cur);
            double local_completion_abs_us = now_us_ + prefix_work_us + task_work_src;
            double local_latency_us = local_completion_abs_us - cur->generate_time_us;

            for (const auto& dst_core : node.cores) {
                if (dst_core.core_id == src_core.core_id) continue;
                double dst_work = core_work_us[dst_core.core_id];
                if (dst_work >= avg_work_us) continue;

                double task_work_dst = estimated_task_work(dst_core, cur);
                double remote_completion_abs_us =
                    now_us_ + m0_config_.rescue_migration_cost_us
                    + dst_work + task_work_dst;
                if (remote_completion_abs_us + m0_config_.rescue_epsilon_us
                    > deadline_abs(cur)) {
                    continue;
                }

                double remote_latency_us =
                    remote_completion_abs_us - cur->generate_time_us;
                double latency_gain_us = local_latency_us - remote_latency_us;
                double pressure_gain_us = src_work - dst_work - task_work_dst;
                if (pressure_gain_us <= 0.0
                    || latency_gain_us < m0_config_.rescue_hybrid_min_gain_us) {
                    continue;
                }

                bool predicted_harmful = dst_work > avg_work_us * 0.80;
                double score = latency_gain_us + 0.10 * pressure_gain_us;
                if (score > best_score) {
                    best_score = score;
                    best.task = cur;
                    best.src_core = src_core.core_id;
                    best.dst_core = dst_core.core_id;
                    best.local_latency_us = local_latency_us;
                    best.remote_latency_us = remote_latency_us;
                    best.score = score;
                    best.predicted_harmful = predicted_harmful;
                }
            }

            prefix_work_us += task_work_src;
        }
    }

    if (!best.task) return false;
    int relief_budget = std::min(budget, m0_config_.rescue_hybrid_max_relief_per_check);
    if (relief_budget <= 0) return false;
    return move_rescue_task_intra_host(
        host, best.src_core, best.dst_core, best.task,
        best.local_latency_us, best.remote_latency_us,
        0, best.predicted_harmful, true);
}

void Simulator::handle_task_finish(const Event& e) {
    Node& node = nodes_[e.host_id];
    Core& core = node.cores[e.core_id];
    Task* t = core.running;
    if (!t || t->id != e.task_id) return;

    double latency_us = now_us_ - t->generate_time_us;
    metrics_.on_task_finish(latency_us, t->deadline_budget_us,
                            t->base_service_time_us, t->rpc_method,
                            t->measurement_eligible, t->rescue_intra_moved);
    metrics_.on_estimator_observation(
        t->expected_service_time_us, t->base_service_time_us, t->rpc_method,
        t->estimator_prior_samples, t->measurement_eligible);
    metrics_.on_control_estimator_update(
        m0_config_.control_estimator_update_cost_us);
    update_service_estimator(t->rpc_method, t->base_service_time_us);

    // Check if migration was invalid: actual latency > estimated local latency.
    if (t->migrated) {
        bool invalid = (latency_us > t->estimated_local_latency_us);
        metrics_.on_migration(invalid);
    }
    if (t->proactive_intra_moved && t->proactive_intra_recorded) {
        bool invalid = (latency_us > t->estimated_local_latency_us);
        metrics_.on_proactive_intra_finish(invalid);
    }
    if (t->rescue_intra_moved && t->rescue_intra_recorded) {
        metrics_.on_rescue_finish(
            t->estimated_local_latency_us, latency_us, t->deadline_budget_us,
            t->rescue_relief_moved);
    }
    if (t->target_harm_watch_active && t->target_harm_watch_recorded
        && t->target_harm_counterfactual_latency_us <= t->deadline_budget_us
        && latency_us > t->deadline_budget_us) {
        metrics_.on_rescue_target_induced_miss(
            t->target_harm_watch_migration_id);
    }
    t->target_harm_watch_active = false;
    t->target_harm_watch_recorded = false;
    t->target_harm_watch_migration_id = 0;
    t->completed = true;

    core.running = nullptr;
    core.idle = true;

    // Pull next from queue.
    if (!core.wait_queue.empty()) {
        start_execution(core, now_us_);
    } else if (method_type_ == MethodType::L1_WORK_STEALING
               || method_type_ == MethodType::L1_WORK_STEALING_POLLING) {
        steal_one_task(e.host_id, e.core_id);
    } else if (method_type_ == MethodType::B0_IDEAL_CFCFS) {
        try_b0_pull(e.host_id);
    }
}

void Simulator::handle_sync_load(const Event& /*e*/) {
    // Update stale global view.
    for (int i = 0; i < active_host_count_; ++i) {
        stale_view_[i] = nodes_[i].local_total_queue_len();
        stale_workload_view_[i] = nodes_[i].local_total_workload_us(now_us_);
    }

    // Compute B2 thresholds (p25/p75) once after warmup.
    if (!b2_thresholds_set_ && metrics_.recording) {
        std::vector<int> qlens = stale_view_;
        std::sort(qlens.begin(), qlens.end());
        int n = static_cast<int>(qlens.size());
        if (n == 0) return;
        b2_q_lo_ = qlens[n / 4];
        b2_q_hi_ = qlens[3 * n / 4];
        if (b2_q_hi_ <= b2_q_lo_) b2_q_hi_ = b2_q_lo_ + 1;
        b2_thresholds_set_ = true;
    }

    // Schedule next sync.
    Event next_sync;
    next_sync.timestamp_us = now_us_ + SYNC_LOAD_PERIOD_US;
    next_sync.type = EventType::SYNC_LOAD;
    schedule_event(next_sync);
}

void Simulator::handle_check_migration(const Event& e) {
    // Budget against migrations when they are scheduled, not when they finish.
    auto budget_remaining = [&](double budget) -> int {
        if (!metrics_.recording) return std::numeric_limits<int>::max();
        double limit = std::floor(budget * static_cast<double>(task_id_counter_));
        if (limit <= static_cast<double>(migration_decisions_)) return 0;
        double remaining = limit - static_cast<double>(migration_decisions_);
        if (remaining > static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(remaining);
    };

    if (method_type_ == MethodType::L1_WORK_STEALING_POLLING) {
        int host_idx = e.host_id;
        if (host_idx < 0 || host_idx >= static_cast<int>(nodes_.size())) return;
        run_work_stealing_poll(host_idx);

        Event next_chk;
        next_chk.timestamp_us = now_us_ + std::max(
            0.1, m0_config_.work_steal_poll_us);
        next_chk.type = EventType::CHECK_MIGRATION;
        next_chk.host_id = host_idx;
        schedule_event(next_chk);

    } else if (method_type_ == MethodType::M0_INTRA_HOST_PROACTIVE
               || method_type_ == MethodType::M0_ALTO_THRESHOLD) {
        int host_idx = e.host_id;
        if (host_idx < 0 || host_idx >= static_cast<int>(nodes_.size())) return;

        int moved = 0;
        const int move_limit = method_type_ == MethodType::M0_ALTO_THRESHOLD
            ? std::max(1, m0_config_.rescue_budget_per_check)
            : INTRA_MAX_MOVES_PER_CHECK;
        while (moved < move_limit) {
            bool success = method_type_ == MethodType::M0_ALTO_THRESHOLD
                ? run_alto_threshold_check(host_idx)
                : run_intra_proactive_check(host_idx);
            if (!success) break;
            ++moved;
        }

        Event next_chk;
        next_chk.timestamp_us = now_us_ + m0_config_.t_check_us;
        next_chk.type = EventType::CHECK_MIGRATION;
        next_chk.host_id = host_idx;
        schedule_event(next_chk);

    } else if (method_type_ == MethodType::M1_RESCUE_SCHED
               || method_type_ == MethodType::M1_RESCUE_NO_TARGET_SAFETY
               || method_type_ == MethodType::M1_RESCUE_NO_RESCUABLE
               || method_type_ == MethodType::M1_RESCUE_HYBRID) {
        int host_idx = e.host_id;
        if (host_idx < 0 || host_idx >= static_cast<int>(nodes_.size())) return;

        run_rescue_sched_check(host_idx);

        Event next_chk;
        next_chk.timestamp_us = now_us_ + m0_config_.t_check_us;
        next_chk.type = EventType::CHECK_MIGRATION;
        next_chk.host_id = host_idx;
        schedule_event(next_chk);

    } else if (method_type_ == MethodType::B2_REACTIVE_MIGRATION) {
        // B2: centralized CHECK — scan all hosts (random start), migrate from first overloaded.
        bool budget_ok = (budget_remaining(B2_BUDGET) > 0);
        if (budget_ok) {
            std::uniform_int_distribution<int> hdist(0, NUM_HOSTS - 1);
            int start = hdist(rng_);
            for (int i = 0; i < NUM_HOSTS; ++i) {
                int idx = (start + i) % NUM_HOSTS;
                auto dec = reactive_sched_.check_migration(
                    nodes_[idx], stale_view_, b2_q_hi_, b2_q_lo_, now_us_, rng_);
                if (dec.task) {
                    dec.task->migrated = true;
                    dec.task->src_host = nodes_[idx].node_id;
                    Event arr;
                    arr.timestamp_us = now_us_ + T_net_oneway_us;
                    arr.type = EventType::TASK_ARRIVE;
                    arr.host_id = dec.dst_host;
                    arr.task_id = dec.task->id;
                    schedule_event(arr);
                    if (metrics_.recording) ++migration_decisions_;
                    break;
                }
            }
        }
        // Reschedule centralized.
        Event next_chk;
        next_chk.timestamp_us = now_us_ + M0_T_CHECK_US;
        next_chk.type = EventType::CHECK_MIGRATION;
        next_chk.host_id = -1;
        schedule_event(next_chk);

    } else if (method_type_ == MethodType::M0_PROACTIVE_MIGRATION) {
        // M0: per-host CHECK — this host scans its own cores.
        int host_idx = e.host_id;
        if (host_idx < 0 || host_idx >= NUM_HOSTS) return;
        // Tighter effective budget (0.045) to absorb parallel overshoot from 64 hosts.
        bool budget_ok = (budget_remaining(M0_BUDGET * 0.90) > 0);
        if (budget_ok) {
            for (auto& core : nodes_[host_idx].cores) {
                auto dec = proactive_sched_.check_core(
                    core, nodes_[host_idx], stale_view_, node_capacities_, now_us_, rng_);
                if (dec.task) {
                    dec.task->migrated = true;
                    dec.task->src_host = nodes_[host_idx].node_id;
                    Event arr;
                    arr.timestamp_us = now_us_ + T_net_oneway_us;
                    arr.type = EventType::TASK_ARRIVE;
                    arr.host_id = dec.dst_host;
                    arr.task_id = dec.task->id;
                    schedule_event(arr);
                    if (metrics_.recording) ++migration_decisions_;
                    break; // 1 migration per host per check
                }
            }
        }
        // Reschedule for this host (runtime t_check_us).
        Event next_chk;
        next_chk.timestamp_us = now_us_ + m0_config_.t_check_us;
        next_chk.type = EventType::CHECK_MIGRATION;
        next_chk.host_id = host_idx;
        schedule_event(next_chk);
    } else if (method_type_ == MethodType::M1_AQB_PROACTIVE_MIGRATION) {
        // M1/AQB-PM: queueing-pressure candidates with bounded batch selection.
        int host_idx = e.host_id;
        if (host_idx < 0 || host_idx >= NUM_HOSTS) return;

        std::vector<ProactiveMigrationScheduler::AqbCandidate> candidates;
        candidates.reserve(CORES_PER_HOST * AQB_SCAN_DEPTH);
        for (auto& core : nodes_[host_idx].cores) {
            proactive_sched_.collect_aqb_candidates(
                core, nodes_[host_idx], now_us_, candidates);
        }

        std::vector<double> host_pressure = stale_workload_view_;
        std::sort(host_pressure.begin(), host_pressure.end());
        double p25_pressure = host_pressure[NUM_HOSTS / 4] / CORES_PER_HOST;
        bool saturated = (p25_pressure > AQB_SATURATION_P25_US);
        double effective_margin = saturated
            ? m0_config_.margin_us * AQB_SATURATION_MARGIN_MULT
            : m0_config_.margin_us;
        int batch_cap = saturated ? 1 : m0_config_.aqb_max_batch_per_host;
        batch_cap = std::min(batch_cap, budget_remaining(AQB_EFFECTIVE_BUDGET));
        bool budget_ok = (batch_cap > 0);

        struct ScoredCandidate {
            ProactiveMigrationScheduler::AqbCandidate cand;
            int dst_host = -1;
            double score = 0.0;
            double dst_work_us = 0.0;
        };

        std::vector<ScoredCandidate> scored;
        scored.reserve(candidates.size());
        std::vector<double> reservation(NUM_HOSTS, 0.0);
        std::uniform_int_distribution<int> host_dist(0, NUM_HOSTS - 1);

        for (const auto& cand : candidates) {
            ScoredCandidate best;
            best.cand = cand;
            double best_score = -std::numeric_limits<double>::infinity();

            for (int k = 0; k < m0_config_.k_dst; ++k) {
                int dst = host_dist(rng_);
                if (dst == cand.src_host) continue;

                double dst_cap = node_capacities_[dst];
                double remote_wait_us =
                    (stale_workload_view_[dst] + reservation[dst]) / CORES_PER_HOST;
                double remote_exec_us =
                    cand.task->expected_service_time_us / dst_cap + T_host_us;
                double remote_total_us =
                    (now_us_ - cand.task->generate_time_us)
                    + T_net_oneway_us + T_host_us
                    + remote_wait_us + remote_exec_us;
                double net_gain = cand.local_latency_us
                                  - remote_total_us - effective_margin;
                if (net_gain <= 0.0) continue;

                double src_pressure =
                    nodes_[cand.src_host].local_total_workload_us(now_us_) / CORES_PER_HOST;
                double dst_pressure =
                    (stale_workload_view_[dst] + reservation[dst]) / CORES_PER_HOST;
                double pressure_gain = std::max(0.0, src_pressure - dst_pressure);
                double score = cand.urgency * net_gain
                             + AQB_PRESSURE_WEIGHT * pressure_gain;

                if (score > best_score) {
                    best_score = score;
                    best.dst_host = dst;
                    best.score = score;
                    best.dst_work_us = remote_exec_us;
                }
            }

            if (best.dst_host >= 0 && best.score > 0.0) scored.push_back(best);
        }

        std::sort(scored.begin(), scored.end(),
                  [](const ScoredCandidate& a, const ScoredCandidate& b) {
                      return a.score > b.score;
                  });

        std::vector<int> per_core(CORES_PER_HOST, 0);
        std::vector<int> per_dst(NUM_HOSTS, 0);
        int moved = 0;
        if (budget_ok) {
            for (const auto& s : scored) {
                if (moved >= batch_cap) break;
                if (per_core[s.cand.src_core] >= AQB_MAX_PER_CORE) continue;
                if (per_dst[s.dst_host] >= AQB_MAX_PER_DST) continue;

                Core& src_core = nodes_[host_idx].cores[s.cand.src_core];
                src_core.remove_waiting(s.cand.task);
                s.cand.task->migrated = true;
                s.cand.task->src_host = host_idx;
                s.cand.task->estimated_local_latency_us = s.cand.local_latency_us;

                reservation[s.dst_host] += s.dst_work_us;
                ++per_core[s.cand.src_core];
                ++per_dst[s.dst_host];
                ++moved;

                Event arr;
                arr.timestamp_us = now_us_ + T_net_oneway_us;
                arr.type = EventType::TASK_ARRIVE;
                arr.host_id = s.dst_host;
                arr.task_id = s.cand.task->id;
                schedule_event(arr);
                if (metrics_.recording) ++migration_decisions_;
            }
            if (moved > 0)
                proactive_sched_.mark_host_migrated(host_idx, now_us_, moved);
        }

        Event next_chk;
        next_chk.timestamp_us = now_us_ + m0_config_.t_check_us;
        next_chk.type = EventType::CHECK_MIGRATION;
        next_chk.host_id = host_idx;
        schedule_event(next_chk);
    } else if (method_type_ == MethodType::M2_DQB_PROACTIVE_MIGRATION) {
        // M2/DQB-PM: distribution-aware queue-batch migration.
        int host_idx = e.host_id;
        if (host_idx < 0 || host_idx >= NUM_HOSTS) return;

        std::vector<double> host_pressure = stale_workload_view_;
        std::sort(host_pressure.begin(), host_pressure.end());
        double p25_pressure = host_pressure[NUM_HOSTS / 4] / CORES_PER_HOST;
        double saturation_threshold_us = DQB_SATURATION_P25_US;
        if (workload_type_ == WorkloadType::W2_MMPP_BIMODAL) {
            saturation_threshold_us = DQB_SATURATION_P25_US * 4.0;
        } else if (workload_type_ == WorkloadType::W1_POISSON_BIMODAL) {
            saturation_threshold_us = DQB_SATURATION_P25_US * 0.60;
        }
        bool saturated = (p25_pressure > saturation_threshold_us);
        if (saturated) metrics_.on_saturation_guard();
        double effective_margin = saturated
            ? m0_config_.margin_us * DQB_SATURATION_MARGIN_MULT
            : m0_config_.margin_us;
        int batch_cap = m0_config_.dqb_max_batches_per_host;
        if (saturated) {
            batch_cap =
                (workload_type_ == WorkloadType::W2_MMPP_BIMODAL) ? 1 : 0;
        }
        int remaining_budget = budget_remaining(DQB_EFFECTIVE_BUDGET);
        bool budget_ok = (batch_cap > 0 && remaining_budget > 0);
        if (!budget_ok) {
            if (remaining_budget <= 0) {
                metrics_.on_no_migrate(NoMigrateReason::BUDGET_EXHAUSTED);
            }
            if (batch_cap <= 0) {
                metrics_.on_no_migrate(NoMigrateReason::SATURATION_GUARD);
            }
            Event next_chk;
            next_chk.timestamp_us = now_us_ + m0_config_.t_check_us;
            next_chk.type = EventType::CHECK_MIGRATION;
            next_chk.host_id = host_idx;
            schedule_event(next_chk);
            return;
        }

        std::vector<QueueBatchCandidate> candidates;
        candidates.reserve(CORES_PER_HOST);
        uint64_t summaries_built = 0;
        if (workload_type_ == WorkloadType::W3_POISSON_LOGNORMAL) {
            dqb_sched_.collect_host_batch_candidate(
                nodes_[host_idx], workload_type_, now_us_, candidates);
            summaries_built = CORES_PER_HOST;
        } else {
            for (auto& core : nodes_[host_idx].cores) {
                dqb_sched_.collect_batch_candidate(
                    core, nodes_[host_idx], workload_type_, now_us_, candidates);
                ++summaries_built;
            }
        }
        metrics_.on_batch_candidates(candidates.size(), summaries_built);
        if (candidates.empty()) {
            metrics_.on_no_migrate(
                workload_type_ == WorkloadType::W3_POISSON_LOGNORMAL
                    ? NoMigrateReason::SPARSE_BLOCKING_NOT_BATCHABLE
                    : NoMigrateReason::NO_BATCH_FORMED);
        } else {
            for (const auto& cand : candidates) {
                if (cand.src_host >= 0 && cand.src_host < NUM_HOSTS) {
                    metrics_.on_source_queue_diag(
                        static_cast<uint64_t>(nodes_[cand.src_host].local_total_queue_len()),
                        nodes_[cand.src_host].local_total_workload_us(now_us_));
                }
                if (cand.estimate_confidence < 0.50)
                    metrics_.on_no_migrate(NoMigrateReason::LOW_CONFIDENCE);
            }
        }

        struct ScoredBatch {
            QueueBatchCandidate cand;
            int dst_host = -1;
            double score = 0.0;
            double remote_tail_us = 0.0;
            double remote_batch_work_us = 0.0;
            std::vector<int> dst_core_for_task;
            std::vector<double> work_for_task_us;
            std::vector<double> core_reservation_delta_us;
        };

        struct BatchPlacementPlan {
            double remote_tail_us = 0.0;
            double remote_batch_work_us = 0.0;
            double improvement_mass_us = 0.0;
            double target_harm_est_us = 0.0;
            int improved_tasks = 0;
            int harmful_tasks = 0;
            std::vector<int> dst_core_for_task;
            std::vector<double> work_for_task_us;
            std::vector<double> core_reservation_delta_us;
        };

        auto batch_type_id = [](DqbBatchType type) -> int {
            switch (type) {
                case DqbBatchType::GenericPressure: return 0;
                case DqbBatchType::ShortBehindLong: return 1;
                case DqbBatchType::MiceBehindElephant: return 2;
                case DqbBatchType::SlowNodeBatchPressure: return 3;
                case DqbBatchType::DistributionWindow: return 4;
            }
            return -1;
        };

        auto build_batch_plan = [&](const QueueBatchCandidate& cand,
                                    int dst,
                                    double gain_margin_us,
                                    BatchPlacementPlan& plan) -> bool {
            if (dst < 0 || dst >= NUM_HOSTS || cand.tasks.empty()) return false;

            double reserved_us = incoming_reservation_[dst];
            if (reserved_us / CORES_PER_HOST > DQB_RESERVATION_LIMIT_US) {
                metrics_.on_reservation_reject();
                return false;
            }

            double avg_remote_wait_us =
                (stale_workload_view_[dst] + reserved_us) / CORES_PER_HOST;
            metrics_.on_destination_virtual_occupancy(avg_remote_wait_us);
            if (avg_remote_wait_us > DQB_TARGET_HARM_LIMIT_US) {
                metrics_.on_no_migrate(NoMigrateReason::DST_TAIL_HARM);
                return false;
            }

            double dst_cap = node_capacities_[dst];
            std::vector<double> slots(CORES_PER_HOST, 0.0);
            double base_slot_us = stale_workload_view_[dst] / CORES_PER_HOST;
            for (int c = 0; c < CORES_PER_HOST; ++c) {
                double reserved_core_us = 0.0;
                if (dst < static_cast<int>(incoming_core_reservation_.size())
                    && c < static_cast<int>(incoming_core_reservation_[dst].size()))
                    reserved_core_us = incoming_core_reservation_[dst][c];
                slots[c] = base_slot_us + reserved_core_us;
            }

            plan = BatchPlacementPlan{};
            plan.dst_core_for_task.reserve(cand.tasks.size());
            plan.work_for_task_us.reserve(cand.tasks.size());
            plan.core_reservation_delta_us.assign(CORES_PER_HOST, 0.0);

            for (size_t ti = 0; ti < cand.tasks.size(); ++ti) {
                Task* task = cand.tasks[ti];
                auto it = std::min_element(slots.begin(), slots.end());
                int core_id = static_cast<int>(std::distance(slots.begin(), it));
                double task_work_us =
                    task->expected_service_time_us / dst_cap + T_host_us;
                double completion_after_arrival_us = slots[core_id] + task_work_us;
                double remote_latency_us =
                    (now_us_ - task->generate_time_us)
                    + T_net_oneway_us + T_host_us
                    + completion_after_arrival_us;
                double local_finish_us = cand.local_finish_us_per_task[ti];
                double task_gain_us = local_finish_us - remote_latency_us;
                if (task_gain_us > gain_margin_us) {
                    ++plan.improved_tasks;
                    plan.improvement_mass_us += task_gain_us;
                } else if (remote_latency_us > local_finish_us + gain_margin_us) {
                    ++plan.harmful_tasks;
                    plan.target_harm_est_us +=
                        remote_latency_us - local_finish_us - gain_margin_us;
                }

                slots[core_id] += task_work_us;
                plan.remote_tail_us =
                    std::max(plan.remote_tail_us, remote_latency_us);
                plan.remote_batch_work_us += task_work_us;
                plan.dst_core_for_task.push_back(core_id);
                plan.work_for_task_us.push_back(task_work_us);
                plan.core_reservation_delta_us[core_id] += task_work_us;
            }

            int min_improved = std::max(
                DQB_MIN_TASKS_PER_BATCH / 2,
                static_cast<int>(std::ceil(0.60 * static_cast<double>(cand.move_count))));
            int max_harmful = std::max(1, cand.move_count / 5);
            if (plan.improved_tasks < min_improved) {
                metrics_.on_no_migrate(NoMigrateReason::LOW_EXPECTED_GAIN);
                return false;
            }
            if (plan.harmful_tasks > max_harmful) {
                metrics_.on_target_harm_est(plan.target_harm_est_us);
                metrics_.on_no_migrate(NoMigrateReason::DST_TAIL_HARM);
                return false;
            }
            return true;
        };

        std::vector<ScoredBatch> scored;
        scored.reserve(candidates.size());
        std::uniform_int_distribution<int> host_dist(0, NUM_HOSTS - 1);

        for (const auto& cand : candidates) {
            ScoredBatch best;
            best.cand = cand;
            double best_score = -std::numeric_limits<double>::infinity();

            for (int k = 0; k < m0_config_.k_dst; ++k) {
                int dst = host_dist(rng_);
                if (dst == cand.src_host) continue;

                BatchPlacementPlan plan;
                if (!build_batch_plan(cand, dst, effective_margin, plan)) {
                    metrics_.on_target_plan_reject();
                    continue;
                }
                double gain_us = cand.estimated_local_tail_us
                               - plan.remote_tail_us - effective_margin;
                if (gain_us <= 0.0) {
                    metrics_.on_no_migrate(NoMigrateReason::LOW_EXPECTED_GAIN);
                    continue;
                }

                double reserved_us = incoming_reservation_[dst];
                double src_pressure =
                    nodes_[cand.src_host].local_total_workload_us(now_us_) / CORES_PER_HOST;
                double dst_pressure =
                    (stale_workload_view_[dst] + reserved_us) / CORES_PER_HOST;
                double pressure_gain = std::max(0.0, src_pressure - dst_pressure);
                double score = cand.risk_mass
                             + cand.blocking_score * 0.05
                             + gain_us
                             + 0.10 * plan.improvement_mass_us
                             + DQB_PRESSURE_WEIGHT * pressure_gain;

                if (score > best_score) {
                    best_score = score;
                    best.dst_host = dst;
                    best.score = score;
                    best.remote_tail_us = plan.remote_tail_us;
                    best.remote_batch_work_us = plan.remote_batch_work_us;
                    best.dst_core_for_task = plan.dst_core_for_task;
                    best.work_for_task_us = plan.work_for_task_us;
                    best.core_reservation_delta_us = plan.core_reservation_delta_us;
                }
            }

            if (best.dst_host >= 0 && best.score > 0.0)
                scored.push_back(best);
        }

        std::sort(scored.begin(), scored.end(),
                  [](const ScoredBatch& a, const ScoredBatch& b) {
                      return a.score > b.score;
                  });

        std::vector<int> per_core(CORES_PER_HOST, 0);
        std::vector<int> per_dst_batches(NUM_HOSTS, 0);
        int selected_batches = 0;
        int moved_tasks = 0;
        for (const auto& s : scored) {
            if (selected_batches >= batch_cap) break;
            if (remaining_budget <= 0) break;
            bool src_core_busy = false;
            for (int src_core : s.cand.src_cores) {
                if (src_core >= 0 && src_core < CORES_PER_HOST && per_core[src_core] > 0) {
                    src_core_busy = true;
                    break;
                }
            }
            if (src_core_busy) continue;
            if (per_dst_batches[s.dst_host] >= DQB_MAX_PER_DST_BATCHES) continue;
            if (remaining_budget < s.cand.move_count) continue;

            uint64_t batch_id = dqb_sched_.next_batch_id();
            int moved_in_batch = 0;
            double reserved_batch_work_us = 0.0;

            for (size_t ti = 0; ti < s.cand.tasks.size(); ++ti) {
                Task* task = s.cand.tasks[ti];
                double task_reserved_work_us = s.work_for_task_us[ti];
                int src_core_id = s.cand.src_core;
                if (ti < s.cand.src_core_for_task.size()) {
                    src_core_id = s.cand.src_core_for_task[ti];
                }
                if (src_core_id < 0 || src_core_id >= CORES_PER_HOST) continue;
                Core& src_core = nodes_[host_idx].cores[src_core_id];
                src_core.remove_waiting(task);
                task->migrated = true;
                task->src_host = host_idx;
                task->estimated_local_latency_us = s.cand.estimated_local_tail_us;
                task->migration_batch_id = batch_id;
                task->reserved_dst_host = s.dst_host;
                task->reserved_dst_core = s.dst_core_for_task[ti];
                task->pending_reserved_work_us = task_reserved_work_us;

                Event arr;
                arr.timestamp_us = now_us_ + T_net_oneway_us;
                arr.type = EventType::TASK_ARRIVE;
                arr.host_id = s.dst_host;
                arr.task_id = task->id;
                schedule_event(arr);

                reserved_batch_work_us += task_reserved_work_us;
                metrics_.on_migration_scheduled_work(task_reserved_work_us);
                ++moved_in_batch;
                if (metrics_.recording) ++migration_decisions_;
            }

            if (moved_in_batch > 0) {
                incoming_reservation_[s.dst_host] += reserved_batch_work_us;
                for (int c = 0; c < CORES_PER_HOST; ++c) {
                    incoming_core_reservation_[s.dst_host][c] +=
                        s.core_reservation_delta_us[c];
                }
                ++selected_batches;
                moved_tasks += moved_in_batch;
                remaining_budget -= moved_in_batch;
                for (int src_core : s.cand.src_cores) {
                    if (src_core >= 0 && src_core < CORES_PER_HOST) ++per_core[src_core];
                }
                ++per_dst_batches[s.dst_host];
                metrics_.on_batch_selected_detail(
                    moved_in_batch, batch_type_id(s.cand.type));
            }
        }

        if (moved_tasks > 0) {
            dqb_sched_.mark_host_migrated(host_idx, now_us_, moved_tasks);
            metrics_.on_batch_selected(selected_batches, moved_tasks);
        }

        Event next_chk;
        next_chk.timestamp_us = now_us_ + m0_config_.t_check_us;
        next_chk.type = EventType::CHECK_MIGRATION;
        next_chk.host_id = host_idx;
        schedule_event(next_chk);
    }
}

void Simulator::try_b0_pull(int prefer_host) {
    if (global_queue_.empty()) return;
    int host = -1;
    if (prefer_host >= 0) {
        for (auto& c : nodes_[prefer_host].cores) {
            if (c.idle && c.wait_queue.empty()) { host = prefer_host; break; }
        }
    }
    if (host < 0) host = find_b0_idle_host();
    if (host < 0) return; // all busy, task stays in global queue
    Task* t = global_queue_.pop_front();
    if (!t) return;
    t->assigned_host = host;
    Event arr;
    arr.timestamp_us = now_us_ + T_net_oneway_us;
    arr.type = EventType::TASK_ARRIVE;
    arr.host_id = host;
    arr.task_id = t->id;
    schedule_event(arr);
}

int Simulator::find_b0_idle_host() {
    std::uniform_int_distribution<int> dist(0, NUM_HOSTS - 1);
    int start = dist(rng_);
    for (int i = 0; i < NUM_HOSTS; ++i) {
        int idx = (start + i) % NUM_HOSTS;
        for (auto& c : nodes_[idx].cores) {
            if (c.idle && c.wait_queue.empty()) return idx;
        }
    }
    return -1;
}

double Simulator::compute_effective_capacity(ClusterProfile profile) {
    switch (profile) {
        case ClusterProfile::HETERO_25PCT:
            return HETERO_FAST_NODES * CORES_PER_HOST * 1.0
                 + HETERO_SLOW_NODES * CORES_PER_HOST * HETERO_SLOW_CAPACITY;
        case ClusterProfile::HOMOGENEOUS:
        default:
            return static_cast<double>(NUM_HOSTS * CORES_PER_HOST);
    }
}

} // namespace sim
