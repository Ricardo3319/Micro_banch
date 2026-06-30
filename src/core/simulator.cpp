#include "sim/core/simulator.h"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <cmath>
#include <limits>

namespace sim {

Simulator::Simulator() = default;

bool Simulator::is_intra_host_method() const {
    return method_type_ == MethodType::L0_RANDOM_CORE
        || method_type_ == MethodType::L1_WORK_STEALING
        || method_type_ == MethodType::M0_INTRA_HOST_PROACTIVE;
}

Task* Simulator::alloc_task() {
    task_pool_.push_back(std::make_unique<Task>());
    Task* t = task_pool_.back().get();
    t->id = ++task_id_counter_;
    return t;
}

void Simulator::configure(MethodType method, double rho, unsigned seed,
                          WorkloadType wl, ClusterProfile profile,
                          const M0Config& m0cfg) {
    method_type_ = method;
    workload_type_ = wl;
    cluster_profile_ = profile;
    m0_config_ = m0cfg;
    rng_.seed(seed);

    // Init nodes with capacity based on cluster profile.
    active_host_count_ =
        (method == MethodType::L0_RANDOM_CORE
         || method == MethodType::L1_WORK_STEALING
         || method == MethodType::M0_INTRA_HOST_PROACTIVE)
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
        case MethodType::M0_INTRA_HOST_PROACTIVE:
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

    // Workload.
    // lambda_global = rho * effective_capacity / E[S]
    double mean_service_us = 24.0; // same for W1/W2 bimodal and W3 lognormal
    double lambda_global = rho * effective_capacity_ / mean_service_us; // requests/us
    mmpp_.reset();
    poisson_.reset();
    bimodal_.reset();
    lognormal_.reset();
    if (wl == WorkloadType::W3_POISSON_LOGNORMAL) {
        poisson_ = std::make_unique<PoissonArrival>(lambda_global, rng_);
        lognormal_ = std::make_unique<LognormalService>(
            W3_LOGNORMAL_MU, W3_LOGNORMAL_SIGMA, rng_);
    } else {
        bimodal_ = std::make_unique<BimodalService>(rng_);
        if (wl == WorkloadType::W1_POISSON_BIMODAL) {
            poisson_ = std::make_unique<PoissonArrival>(lambda_global, rng_);
        } else {
            mmpp_ = std::make_unique<MMPPArrival>(lambda_global, rng_);
        }
    }

    // Metrics.
    measurement_target_ = MEASUREMENT_REQUESTS;
    metrics_.init(WARMUP_REQUESTS);
    task_id_counter_ = 0;
    total_generated_work_us_ = 0.0;
    task_pool_.clear();
    now_us_ = 0.0;
    b2_thresholds_set_ = false;
    migration_decisions_ = 0;
    migration_batch_id_counter_ = 0;

    // B0 global queue.
    global_queue_.clear();

    // W2 localized burst init.
    hot_nodes_.clear();
    last_burst_state_ = false;
    if (active_host_count_ == NUM_HOSTS && wl == WorkloadType::W2_MMPP_BIMODAL)
        refresh_hot_nodes();

    // Clear event queue.
    while (!event_queue_.empty()) event_queue_.pop();

    // Seed initial events.
    Event gen;
    gen.timestamp_us = 0.0;
    gen.type = EventType::TASK_GENERATE;
    schedule_event(gen);

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
    } else if (method == MethodType::M0_INTRA_HOST_PROACTIVE) {
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
        case EventType::TASK_EXECUTE:     /* not queued directly */ break;
        case EventType::TASK_FINISH:      handle_task_finish(e);   break;
        case EventType::SYNC_LOAD:        handle_sync_load(e);     break;
        case EventType::CHECK_MIGRATION:  handle_check_migration(e); break;
    }
}

void Simulator::handle_task_generate(const Event& /*e*/) {
    // Schedule next generation.
    double gap;
    if (workload_type_ == WorkloadType::W1_POISSON_BIMODAL
        || workload_type_ == WorkloadType::W3_POISSON_LOGNORMAL)
        gap = poisson_->next_interarrival();
    else
        gap = mmpp_->next_interarrival(now_us_);
    Event next_gen;
    next_gen.timestamp_us = now_us_ + gap;
    next_gen.type = EventType::TASK_GENERATE;
    schedule_event(next_gen);

    // Generate a task.
    Task* t = alloc_task();
    t->generate_time_us = now_us_;
    if (workload_type_ == WorkloadType::W3_POISSON_LOGNORMAL) {
        t->base_service_time_us = lognormal_->next();
    } else {
        t->base_service_time_us = bimodal_->next();
    }
    t->expected_service_time_us = t->base_service_time_us;
    total_generated_work_us_ += t->expected_service_time_us;
    t->slo_target_us = t->slo_for_service();

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
        && mmpp_ && mmpp_->is_burst() && !hot_nodes_.empty()) {
        if (!last_burst_state_) {
            refresh_hot_nodes();
            last_burst_state_ = true;
        }
        std::uniform_real_distribution<double> u(0.0, 1.0);
        if (u(rng_) < HOT_DISPATCH_PROB) {
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
    std::uniform_int_distribution<int> core_dist(0, CORES_PER_HOST - 1);
    enqueue_task_on_core(task, 0, core_dist(rng_), now_us_);
}

void Simulator::refresh_hot_nodes() {
    // Select HOT_NODE_COUNT random nodes as burst targets.
    hot_nodes_.resize(HOT_NODE_COUNT);
    std::vector<int> all(NUM_HOSTS);
    std::iota(all.begin(), all.end(), 0);
    std::shuffle(all.begin(), all.end(), rng_);
    std::copy_n(all.begin(), HOT_NODE_COUNT, hot_nodes_.begin());
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

void Simulator::handle_task_execute(int host, int core_id) {
    // Not used as a queued event; execution is started inline.
    (void)host; (void)core_id;
}

void Simulator::start_execution(Core& c, double now) {
    Task* t = c.pop_waiting_front();
    if (!t) { c.idle = true; return; }
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
                                             bool proactive) {
    if (!task || host < 0 || host >= static_cast<int>(nodes_.size())) return false;
    if (src_core_id < 0 || src_core_id >= CORES_PER_HOST) return false;
    if (dst_core_id < 0 || dst_core_id >= CORES_PER_HOST) return false;
    if (src_core_id == dst_core_id) return false;

    Node& node = nodes_[host];
    Core& src_core = node.cores[src_core_id];
    Core& dst_core = node.cores[dst_core_id];

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
        task->proactive_intra_recorded = metrics_.recording;
        metrics_.on_proactive_intra_success(moved_work_us);
    } else {
        metrics_.on_steal_success(moved_work_us);
    }

    dst_core.push_waiting(task);
    if (dst_core.idle) start_execution(dst_core, now_us_);
    return true;
}

bool Simulator::steal_one_task(int host, int idle_core_id) {
    metrics_.on_steal_attempt();
    if (host < 0 || host >= static_cast<int>(nodes_.size())) return false;
    if (idle_core_id < 0 || idle_core_id >= CORES_PER_HOST) return false;

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
        host, src_core_id, idle_core_id, task, 0.0, false);
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
                && local_latency_us > cur->slo_target_us * m0_config_.alpha) {
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

void Simulator::handle_task_finish(const Event& e) {
    Node& node = nodes_[e.host_id];
    Core& core = node.cores[e.core_id];
    Task* t = core.running;
    if (!t || t->id != e.task_id) return; // stale finish event after migration

    double latency_us = now_us_ - t->generate_time_us;
    metrics_.on_task_finish(latency_us, t->slo_target_us, t->base_service_time_us);

    // Check if migration was invalid: actual latency > estimated local latency.
    if (t->migrated) {
        bool invalid = (latency_us > t->estimated_local_latency_us);
        metrics_.on_migration(invalid);
    }
    if (t->proactive_intra_moved && t->proactive_intra_recorded) {
        bool invalid = (latency_us > t->estimated_local_latency_us);
        metrics_.on_proactive_intra_finish(invalid);
    }

    core.running = nullptr;
    core.idle = true;

    // Pull next from queue.
    if (!core.wait_queue.empty()) {
        start_execution(core, now_us_);
    } else if (method_type_ == MethodType::L1_WORK_STEALING) {
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

    if (method_type_ == MethodType::M0_INTRA_HOST_PROACTIVE) {
        int host_idx = e.host_id;
        if (host_idx < 0 || host_idx >= static_cast<int>(nodes_.size())) return;

        int moved = 0;
        while (moved < INTRA_MAX_MOVES_PER_CHECK) {
            if (!run_intra_proactive_check(host_idx)) break;
            ++moved;
        }

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
