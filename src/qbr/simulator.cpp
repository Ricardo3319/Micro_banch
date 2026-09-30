#include "qbr/model.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <list>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace qbr {
double batch_work_limit(double ws, double rs, double wd, double rd,
                        double path, double margin, double cap) {
    if (rs <= 0 || rd <= 0 || cap <= 0) return 0;
    return std::max(0.0, std::min(cap, (ws / rs - wd / rd - path - margin) /
                                              (1.0 / rs + 1.0 / rd)));
}
void validate_config(const Config& c) {
    if (c.hosts < 1 || c.workers < 1 || c.peers < 0 || c.max_batch < 1 ||
        c.max_bytes < 1 || c.jbsq_depth < 1) throw std::runtime_error("invalid integer configuration");
    for (double v : {c.host_us, c.network_us, c.report_cpu_us, c.receive_cpu_us,
         c.check_cpu_us, c.decision_cpu_us, c.item_cpu_us, c.ingress_cpu_us,
         c.pressure_us, c.gain_margin_us})
        if (!std::isfinite(v) || v < 0) throw std::runtime_error("invalid nonnegative configuration");
    for (double v : {c.bandwidth_gbps, c.report_us, c.check_us, c.credit_batch_us, c.admit_us,
         c.lease_us, c.max_work_us, c.class_mean_us[0], c.class_mean_us[1],
         c.slo_us[0], c.slo_us[1]})
        if (!std::isfinite(v) || v <= 0) throw std::runtime_error("invalid positive configuration");
}
Trace read_trace(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open trace: " + path);
    uint16_t endian = 1;
    if (*reinterpret_cast<uint8_t*>(&endian) != 1) throw std::runtime_error("trace requires little endian");
    uint64_t hash = 14695981039346656037ULL;
    auto read = [&](void* dst, size_t n) {
        if (!f.read(static_cast<char*>(dst), static_cast<std::streamsize>(n)))
            throw std::runtime_error("truncated trace");
        auto p = static_cast<uint8_t*>(dst);
        for (size_t i = 0; i < n; ++i) { hash ^= p[i]; hash *= 1099511628211ULL; }
    };
    char magic[8]; read(magic, 8);
    if (std::memcmp(magic, "QBRTRC1\n", 8)) throw std::runtime_error("invalid trace magic");
    uint32_t n, m; read(&n, 4); read(&m, 4);
    if (n > 100000000 || m > 1000000) throw std::runtime_error("trace too large");
    Trace t; read(&t.warmup_us, 8); read(&t.end_us, 8);
    t.requests.resize(n); t.changes.resize(m);
    for (auto& r : t.requests) {
        read(&r.arrival_us, 8); read(&r.service_us, 8); read(&r.type, 1); read(&r.bytes, 4);
    }
    for (auto& v : t.changes) { read(&v.time_us, 8); read(&v.host, 4); read(&v.speed, 8); }
    if (f.peek() != std::char_traits<char>::eof()) throw std::runtime_error("trailing bytes in trace");
    std::ostringstream s; s << std::hex << hash; t.fingerprint = s.str();
    return t;
}

namespace {
enum class State { New, Waiting, Transit, Queued, Running, Responding, Done };
struct Task {
    State state = State::New;
    bool attempted = false, moved = false;
    int host = -1;
    double latency = 0;
    std::list<int>::iterator queue_it, eligible_it;
};
struct Worker {
    int task = -1;
    uint64_t version = 0;
    double remaining = 0, predicted = 0, last = 0;
};
struct Host {
    std::list<int> queue, eligible;
    std::vector<Worker> workers;
    std::vector<Summary> views;
    std::vector<int> peers, subscribers;
    double queued_work = 0, speed = 1, input_work = 0, reserved = 0;
    double cpu_free = 0, tx_free = 0, rx_free = 0, cpu_busy = 0;
    uint64_t input_count = 0;
    int credit_count = 0;
    double credit_work = 0;
    bool credit_pending = false;
    bool pending = false;
    int pressure_ticks = 0;
};
struct Token { int dst; double work, expiry; bool closed = false; };
struct Event {
    double time;
    int rank;
    uint64_t seq;
    std::function<void()> fn;
};
struct Later {
    bool operator()(const Event& a, const Event& b) const {
        if (a.time != b.time) return a.time > b.time;
        if (a.rank != b.rank) return a.rank > b.rank;
        return a.seq > b.seq;
    }
};
struct Histogram {
    std::vector<uint64_t> bins;
    uint64_t n = 0;
    double sum = 0;
    void add(double x) {
        size_t i = static_cast<size_t>(std::floor(std::log1p(x) / std::log(1.001)));
        if (i >= bins.size()) bins.resize(i + 1);
        ++bins[i]; ++n; sum += x;
    }
    double quantile(double q) const {
        uint64_t rank = static_cast<uint64_t>(std::ceil(q * n)), seen = 0;
        if (!n) return 0;
        for (size_t i = 0; i < bins.size(); ++i) {
            seen += bins[i];
            if (seen >= rank) return std::expm1((i + 1) * std::log(1.001));
        }
        throw std::runtime_error("histogram invariant");
    }
};

class Engine {
    const Trace& trace;
    const Config& c;
    std::string method;
    bool repair;
    std::vector<Task> tasks;
    std::vector<Host> hosts;
    std::vector<Summary> ingress_views;
    std::vector<double> sent_work;
    std::vector<uint64_t> sent_count;
    std::vector<int> outstanding;
    std::vector<double> outstanding_work;
    std::deque<int> ingress_queue;
    bool ingress_busy = false;
    int rr = 0;
    std::mt19937_64 rng;
    std::priority_queue<Event, std::vector<Event>, Later> events;
    double now = 0, ingress_busy_us = 0, ingress_free = 0;
    uint64_t seq = 0, done = 0, measured = 0, misses = 0;
    uint64_t controls = 0, control_bytes = 0, payload_bytes = 0;
    uint64_t plans = 0, grants = 0, denied = 0, expired = 0, returns = 0;
    uint64_t batches = 0, migrated = 0, inspected = 0, max_observed_batch = 0;
    uint64_t credit_messages = 0, credit_items = 0;
    double moved_work = 0, max_reserved = 0;
    std::array<uint64_t, 2> class_count{}, class_miss{};
    Histogram all_hist;
    std::array<Histogram, 2> class_hist;

    bool window() const { return now >= trace.warmup_us && now < trace.end_us; }
    double overlap(double begin, double end) const {
        return std::max(0.0, std::min(end, trace.end_us) - std::max(begin, trace.warmup_us));
    }
    void at(double t, int rank, std::function<void()> fn) {
        if (t < now - 1e-7 || !std::isfinite(t)) throw std::runtime_error("invalid event time");
        events.push({std::max(t, now), rank, seq++, std::move(fn)});
    }
    void cpu(int h, double duration, std::function<void()> fn) {
        auto& host = hosts[h];
        double start = std::max(now, host.cpu_free);
        host.cpu_free = start + duration;
        host.cpu_busy += overlap(start, host.cpu_free);
        at(host.cpu_free, 3, std::move(fn));
    }
    void ingress_cpu(double duration, std::function<void()> fn) {
        double start = std::max(now, ingress_free);
        ingress_free = start + duration;
        ingress_busy_us += overlap(start, ingress_free);
        at(ingress_free, 3, std::move(fn));
    }
    void wire(int src, int dst, uint64_t bytes, bool control, std::function<void()> fn) {
        const double duration = bytes * 8.0 / (c.bandwidth_gbps * 1000.0);
        if (window()) {
            if (control) { ++controls; control_bytes += bytes; }
            else payload_bytes += bytes;
        }
        double launch = now;
        if (src >= 0) {
            auto& h = hosts[src]; launch = std::max(now, h.tx_free); h.tx_free = launch + duration;
        }
        at(launch + duration + c.network_us, 1, [this, dst, duration, fn = std::move(fn)]() mutable {
            if (dst < 0) { fn(); return; }
            // Ingress link arbitration is performed on arrival, preserving wire order.
            auto& h = hosts[dst];
            double start = std::max(now, h.rx_free);
            h.rx_free = start + duration;
            at(h.rx_free, 1, std::move(fn));
        });
    }
    double prior(int i) const { return c.class_mean_us[trace.requests[i].type] + c.host_us; }
    Summary local(int h) const {
        const auto& a = hosts[h];
        Summary s; s.work_us = a.queued_work; s.speed = a.speed; s.observed_us = now;
        s.input_work_us = a.input_work; s.input_count = a.input_count;
        s.count = static_cast<int>(a.queue.size());
        for (const auto& w : a.workers) if (w.task >= 0) {
            // A class-prior residual floor, never actual execution demand/finish time.
            s.work_us += std::max(0.25 * prior(w.task), w.predicted - (now - w.last) * a.speed);
            ++s.count;
        }
        return s;
    }
    double rate(int h) const { return c.workers * hosts[h].speed; }
    bool bounded() const { return method == "jbsq" || method == "work" || method == "bwc"; }
    bool admissible(int h, int i) const {
        if (outstanding[h] >= c.jbsq_depth * c.workers) return false;
        if (method == "jbsq" || outstanding[h] < c.workers) return true;
        return outstanding_work[h] + prior(i) <= c.admit_us * c.workers * ingress_views[h].speed;
    }
    void return_credit(int h, int count, double work) {
        ingress_cpu(c.receive_cpu_us, [this, h, count, work] {
            outstanding[h] -= count; outstanding_work[h] -= work;
            if (outstanding[h] < 0 || outstanding_work[h] < -1e-6) throw std::runtime_error("negative ingress credit");
            outstanding_work[h] = std::max(0.0, outstanding_work[h]); pump_ingress();
        });
    }
    void accumulate_credit(int h, int i) {
        auto& a = hosts[h]; ++a.credit_count; a.credit_work += prior(i);
        if (a.credit_pending) return;
        a.credit_pending = true;
        at(now + c.credit_batch_us, 3, [this, h] {
            cpu(h, c.report_cpu_us, [this, h] {
                auto& a = hosts[h]; int count = a.credit_count; double work = a.credit_work;
                a.credit_count = 0; a.credit_work = 0; a.credit_pending = false;
                if (window()) { ++credit_messages; credit_items += count; }
                wire(h, -1, 64, true, [this, h, count, work] { return_credit(h, count, work); });
            });
        });
    }
    void dequeue(int h, int i) {
        auto& a = hosts[h]; auto& t = tasks[i];
        if (t.state != State::Queued || t.host != h) throw std::runtime_error("invalid dequeue");
        a.queue.erase(t.queue_it);
        if (!t.attempted) a.eligible.erase(t.eligible_it);
        a.queued_work = std::max(0.0, a.queued_work - prior(i));
    }
    void finish_worker(int h, int k, uint64_t version) {
        auto& w = hosts[h].workers[k];
        if (version != w.version || w.task < 0) return;
        int i = w.task;
        if (tasks[i].state != State::Running) throw std::runtime_error("duplicate execution");
        w.task = -1; tasks[i].state = State::Responding;
        dispatch_workers(h);
        if (method == "bwc") accumulate_credit(h, i);
        wire(h, -1, 64, false, [this, i, h] {
            if (tasks[i].state != State::Responding) throw std::runtime_error("duplicate completion");
            tasks[i].state = State::Done; ++done;
            const auto& r = trace.requests[i]; double latency = now - r.arrival_us;
            tasks[i].latency = latency;
            if (r.arrival_us >= trace.warmup_us) {
                ++measured; all_hist.add(latency); class_hist[r.type].add(latency); ++class_count[r.type];
                if (latency > c.slo_us[r.type]) { ++misses; ++class_miss[r.type]; }
            }
            if (method == "jbsq" || method == "work") return_credit(h, 1, prior(i));
        });
    }
    void schedule_worker(int h, int k) {
        auto& w = hosts[h].workers[k];
        uint64_t version = ++w.version;
        at(now + w.remaining / hosts[h].speed, 0, [this, h, k, version] { finish_worker(h, k, version); });
    }
    void dispatch_workers(int h) {
        auto& a = hosts[h];
        for (int k = 0; k < c.workers && !a.queue.empty(); ++k) {
            auto& w = a.workers[k]; if (w.task >= 0) continue;
            int i = a.queue.front(); dequeue(h, i); tasks[i].state = State::Running;
            w.task = i; w.remaining = trace.requests[i].service_us + c.host_us;
            w.predicted = prior(i); w.last = now; schedule_worker(h, k);
        }
    }
    void enqueue(int h, int i, bool input) {
        auto& t = tasks[i]; auto& a = hosts[h];
        if (t.state != State::Transit) throw std::runtime_error("invalid enqueue state");
        t.state = State::Queued; t.host = h;
        a.queue.push_back(i); t.queue_it = std::prev(a.queue.end());
        if (!t.attempted) { a.eligible.push_back(i); t.eligible_it = std::prev(a.eligible.end()); }
        a.queued_work += prior(i);
        if (input) { a.input_work += prior(i); ++a.input_count; }
        dispatch_workers(h);
    }
    void capacity(int h, double speed) {
        auto& a = hosts[h];
        for (auto& w : a.workers) if (w.task >= 0) {
            double progress = (now - w.last) * a.speed;
            w.remaining = std::max(0.0, w.remaining - progress);
            w.predicted = std::max(0.0, w.predicted - progress); w.last = now;
        }
        a.speed = speed;
        for (int k = 0; k < c.workers; ++k) if (a.workers[k].task >= 0) schedule_worker(h, k);
    }
    double ingress_score(int h) const {
        auto s = ingress_views[h];
        double pending = std::max(0.0, sent_work[h] - s.input_work_us);
        return (std::max(0.0, s.work_us - (now - s.observed_us) * c.workers * s.speed) + pending) /
               (c.workers * s.speed);
    }
    int choose_ingress() {
        if (method == "p2c") {
            int a = static_cast<int>(rng() % c.hosts), b = static_cast<int>(rng() % c.hosts);
            if (c.hosts > 1 && a == b) b = (b + 1) % c.hosts;
            auto score = [&](int h) { return ingress_views[h].count + sent_count[h] - ingress_views[h].input_count; };
            return score(a) <= score(b) ? a : b;
        }
        int best = -1; double value = std::numeric_limits<double>::infinity();
        for (int j = 0; j < c.hosts; ++j) {
            int h = (rr + j) % c.hosts;
            double v = method == "jbsq" ? outstanding[h] :
                       (bounded() ? outstanding_work[h] / (c.workers * ingress_views[h].speed) : ingress_score(h));
            if (bounded() && !admissible(h, ingress_queue.front())) continue;
            if (v < value) { best = h; value = v; }
        }
        if (best >= 0) rr = (best + 1) % c.hosts;
        return best;
    }
    void pump_ingress() {
        if (ingress_busy || ingress_queue.empty()) return;
        // A bounded JBSQ center waits for actual completion credit.
        if (bounded()) {
            bool room = false;
            for (int h = 0; h < c.hosts; ++h) if (admissible(h, ingress_queue.front())) { room = true; break; }
            if (!room) return;
        }
        ingress_busy = true;
        ingress_cpu(c.ingress_cpu_us, [this] {
            ingress_busy = false;
            int h = choose_ingress(); if (h < 0) return;
            int i = ingress_queue.front(); ingress_queue.pop_front();
            tasks[i].state = State::Transit;
            sent_work[h] += prior(i); ++sent_count[h]; ++outstanding[h]; outstanding_work[h] += prior(i);
            wire(-1, h, trace.requests[i].bytes, false, [this, i, h] { enqueue(h, i, true); });
            pump_ingress();
        });
    }
    void report(int h) {
        if (done == tasks.size()) return;
        cpu(h, c.report_cpu_us, [this, h] {
            Summary s = local(h);
            wire(h, -1, 64, true, [this, h, s] {
                ingress_cpu(c.receive_cpu_us, [this, h, s] {
                    if (s.observed_us >= ingress_views[h].observed_us) ingress_views[h] = s;
                    if (bounded()) pump_ingress();
                });
            });
            if (repair) for (int dst : hosts[h].subscribers) {
                wire(h, dst, 64, true, [this, h, dst, s] {
                    cpu(dst, c.receive_cpu_us, [this, h, dst, s] {
                        if (s.observed_us >= hosts[dst].views[h].observed_us) hosts[dst].views[h] = s;
                    });
                });
            }
            at(now + c.report_us, 3, [this, h] { report(h); });
        });
    }
    void close_token(const std::shared_ptr<Token>& tok) {
        if (tok->closed) return;
        tok->closed = true;
        if (c.reserve) {
            hosts[tok->dst].reserved -= tok->work;
            if (hosts[tok->dst].reserved < -1e-6) throw std::runtime_error("negative reservation");
            hosts[tok->dst].reserved = std::max(0.0, hosts[tok->dst].reserved);
        }
    }
    std::shared_ptr<Token> grant(int dst, double requested) {
        double headroom = std::max(0.0, c.admit_us * rate(dst) - local(dst).work_us - hosts[dst].reserved);
        double work = std::min(requested, headroom);
        if (work < *std::min_element(c.class_mean_us.begin(), c.class_mean_us.end()) + c.host_us) {
            if (window()) ++denied;
            return {};
        }
        auto tok = std::make_shared<Token>(Token{dst, work, now + c.lease_us, false});
        if (c.reserve) hosts[dst].reserved += work;
        max_reserved = std::max(max_reserved, hosts[dst].reserved);
        if (window()) ++grants;
        at(tok->expiry, 3, [this, tok] {
            if (!tok->closed) { if (window()) ++expired; close_token(tok); }
        });
        return tok;
    }
    void release(int src, const std::shared_ptr<Token>& tok) {
        wire(src, tok->dst, 48, true, [this, tok] {
            cpu(tok->dst, c.receive_cpu_us, [this, tok] { close_token(tok); });
        });
    }
    void commit(int src, const std::shared_ptr<Token>& tok, bool pull, double target_delay) {
        // Token status is target-local: source only knows the returned expiry/work fields.
        int limit = method == "single" ? 1 : c.max_batch;
        cpu(src, c.decision_cpu_us + c.item_cpu_us * limit, [this, src, tok, pull, target_delay, limit] {
            auto& a = hosts[src];
            std::vector<int> selected;
            double work = 0; uint64_t bytes = 48; int scanned = 0;
            double source_delay = local(src).work_us / rate(src);
            bool profitable = pull || method == "threshold" ||
                              source_delay > target_delay + c.network_us + c.gain_margin_us;
            // Scan is strictly bounded, independent of total queue length.
            auto it = a.eligible.end();
            while (profitable && it != a.eligible.begin() && scanned < 2 * limit &&
                   static_cast<int>(selected.size()) < limit) {
                --it; int i = *it; ++scanned;
                double w = prior(i);
                if (work + w <= tok->work + 1e-8 && bytes + trace.requests[i].bytes <= static_cast<uint64_t>(c.max_bytes)) {
                    selected.push_back(i); work += w; bytes += trace.requests[i].bytes;
                }
            }
            if (window()) inspected += scanned;
            if (selected.empty() || now + c.network_us >= tok->expiry) {
                release(src, tok);
                if (pull) hosts[tok->dst].pending = false; else a.pending = false;
                return;
            }
            // Preserve FIFO order inside the transferred tail segment.
            std::reverse(selected.begin(), selected.end());
            for (int i : selected) { dequeue(src, i); tasks[i].attempted = true; tasks[i].state = State::Transit; }
            if (!pull) a.pending = false;
            wire(src, tok->dst, bytes, false, [this, src, tok, pull, selected = std::move(selected), work, bytes] {
                cpu(tok->dst, c.receive_cpu_us + c.item_cpu_us * selected.size(),
                    [this, src, tok, pull, selected, work, bytes] {
                    if (tok->closed || now >= tok->expiry) {
                        close_token(tok);
                        if (window()) ++returns;
                        wire(tok->dst, src, bytes, false, [this, src, selected] {
                            cpu(src, c.receive_cpu_us + c.item_cpu_us * selected.size(), [this, src, selected] {
                                for (int i : selected) enqueue(src, i, false);
                            });
                        });
                    } else {
                        if (work > tok->work + 1e-6) throw std::runtime_error("grant exceeded");
                        close_token(tok);
                        if (window()) { ++batches; migrated += selected.size(); moved_work += work;
                            max_observed_batch = std::max<uint64_t>(max_observed_batch, selected.size()); }
                        for (int i : selected) { tasks[i].moved = true; enqueue(tok->dst, i, false); }
                    }
                    if (pull) hosts[tok->dst].pending = false;
                });
            });
        });
    }
    void push_plan(int src) {
        auto& a = hosts[src]; auto s = local(src);
        double source_delay = s.work_us / rate(src);
        if (a.eligible.empty() || source_delay < c.pressure_us) { a.pressure_ticks = 0; return; }
        if (++a.pressure_ticks < 2) return;
        if (method != "threshold") {
            int head = a.queue.front(); const auto& r = trace.requests[head];
            if (now - r.arrival_us + source_delay + prior(head) / a.speed + c.network_us < 0.8 * c.slo_us[r.type]) return;
        }
        int dst = -1; double best = std::numeric_limits<double>::infinity();
        for (int p : a.peers) {
            auto v = a.views[p];
            double delay = v.work_us / (c.workers * v.speed) + 0.05 * (now - v.observed_us);
            if (delay < best) { best = delay; dst = p; }
        }
        if (dst < 0) return;
        const auto v = a.views[dst];
        double request = c.max_work_us;
        if (method != "threshold" && !c.fixed_batch) {
            request = batch_work_limit(s.work_us, rate(src), v.work_us, c.workers * v.speed,
                3 * c.network_us + 3 * c.decision_cpu_us + 0.05 * (now - v.observed_us),
                c.gain_margin_us, c.max_work_us);
        }
        if (request < c.class_mean_us[0] + c.host_us) return;
        a.pending = true; if (window()) ++plans;
        cpu(src, c.decision_cpu_us, [this, src, dst, request] {
            wire(src, dst, 48, true, [this, src, dst, request] {
                cpu(dst, c.decision_cpu_us, [this, src, dst, request] {
                    auto tok = grant(dst, request);
                    double delay = local(dst).work_us / rate(dst);
                    wire(dst, src, 48, true, [this, src, tok, delay] {
                        if (!tok) { hosts[src].pending = false; return; }
                        commit(src, tok, false, delay);
                    });
                });
            });
        });
    }
    void pull_plan(int dst) {
        auto& a = hosts[dst];
        if (!a.queue.empty() || std::none_of(a.workers.begin(), a.workers.end(),
            [](const Worker& w) { return w.task < 0; })) return;
        int src = -1; double best = 0;
        for (int p : a.peers) {
            auto v = a.views[p]; double pressure = v.work_us / (c.workers * v.speed);
            if (v.count > c.workers && pressure > best) { best = pressure; src = p; }
        }
        if (src < 0) return;
        a.pending = true; if (window()) ++plans;
        cpu(dst, c.decision_cpu_us, [this, src, dst] {
            auto tok = grant(dst, c.max_work_us);
            if (!tok) { hosts[dst].pending = false; return; }
            double delay = local(dst).work_us / rate(dst);
            wire(dst, src, 48, true, [this, src, tok, delay] { commit(src, tok, true, delay); });
        });
    }
    void check(int h) {
        if (done == tasks.size()) return;
        cpu(h, c.check_cpu_us, [this, h] {
            if (!hosts[h].pending) {
                if (method == "steal") pull_plan(h); else push_plan(h);
            }
            at(now + c.check_us, 3, [this, h] { check(h); });
        });
    }
public:
    Engine(const Trace& t, const Config& cfg, std::string m)
        : trace(t), c(cfg), method(std::move(m)),
          repair(method == "steal" || method == "threshold" || method == "single" || method == "qbr"),
          tasks(t.requests.size()), hosts(c.hosts), ingress_views(c.hosts), sent_work(c.hosts),
          sent_count(c.hosts), outstanding(c.hosts), outstanding_work(c.hosts), rng(c.seed) {
        validate_config(c);
        if (!repair && method != "p2c" && method != "jsw" && !bounded())
            throw std::runtime_error("unknown method");
        if (!std::isfinite(t.end_us) || !std::isfinite(t.warmup_us) || t.warmup_us < 0 || t.end_us <= t.warmup_us)
            throw std::runtime_error("invalid trace window");
        double prev = -1;
        for (const auto& r : t.requests) {
            if (!std::isfinite(r.arrival_us) || r.arrival_us < 0 || r.arrival_us < prev || r.arrival_us >= t.end_us ||
                !std::isfinite(r.service_us) || r.service_us <= 0 || r.type > 1 || !r.bytes)
                throw std::runtime_error("invalid trace request");
            prev = r.arrival_us;
        }
        for (const auto& v : t.changes)
            if (v.host < 0 || v.host >= c.hosts || !std::isfinite(v.speed) || v.speed <= 0 ||
                !std::isfinite(v.time_us) || v.time_us < 0 || v.time_us >= t.end_us)
                throw std::runtime_error("invalid capacity event");
        // Sparse peer graph and a separate policy RNG never alter the input trace.
        std::mt19937_64 peer_rng(c.seed ^ 0xa53a9f27ULL);
        for (int h = 0; h < c.hosts; ++h) {
            hosts[h].workers.resize(c.workers); hosts[h].views.resize(c.hosts);
            std::vector<int> p;
            for (int k = 0; k < c.hosts; ++k) if (k != h) p.push_back(k);
            std::shuffle(p.begin(), p.end(), peer_rng);
            p.resize(std::min<int>(c.peers, p.size())); hosts[h].peers = p;
            for (int k : p) hosts[k].subscribers.push_back(h);
        }
    }
    std::string run(const std::string& output) {
        for (size_t i = 0; i < tasks.size(); ++i) at(trace.requests[i].arrival_us, 2, [this, i] {
            tasks[i].state = State::Waiting; ingress_queue.push_back(static_cast<int>(i)); pump_ingress();
        });
        for (auto v : trace.changes) at(v.time_us, 1, [this, v] { capacity(v.host, v.speed); });
        for (int h = 0; h < c.hosts; ++h) {
            if (method != "jbsq") at(c.report_us * (h + 0.5) / c.hosts, 3, [this, h] { report(h); });
            if (repair) at(c.check_us * (h + 0.5) / c.hosts, 3, [this, h] { check(h); });
        }
        uint64_t count = 0;
        while (done < tasks.size() && !events.empty()) {
            auto e = events.top(); events.pop(); now = e.time; e.fn();
            if (++count > 1000000000ULL) throw std::runtime_error("event budget exceeded");
        }
        if (done != tasks.size()) throw std::runtime_error("lost request");
        for (const auto& t : tasks) if (t.state != State::Done) throw std::runtime_error("unfinished state");
        if (!output.empty()) {
            std::ofstream f(output, std::ios::binary);
            if (!f) throw std::runtime_error("cannot write outcomes");
            for (const auto& t : tasks) { f.write(reinterpret_cast<const char*>(&t.latency), 8);
                uint8_t moved = t.moved; f.write(reinterpret_cast<const char*>(&moved), 1); }
            if (!f) throw std::runtime_error("outcome write failed");
        }
        double cpu_sum = 0, cpu_max = 0, duration = trace.end_us - trace.warmup_us;
        for (const auto& h : hosts) { cpu_sum += h.cpu_busy; cpu_max = std::max(cpu_max, h.cpu_busy); }
        auto ratio = [](double a, double b) { return b > 0 ? a / b : 0.0; };
        std::ostringstream j; j << std::setprecision(12) << "{\"method\":\"" << method
          << "\",\"trace_fingerprint\":\"" << trace.fingerprint << "\",\"generated\":" << tasks.size()
          << ",\"completed\":" << done << ",\"measured\":" << measured << ",\"drain_end_us\":" << now
          << ",\"p50_us\":" << all_hist.quantile(.5) << ",\"p99_us\":" << all_hist.quantile(.99)
          << ",\"p999_us\":" << all_hist.quantile(.999) << ",\"mean_us\":" << ratio(all_hist.sum, measured)
          << ",\"slo_miss_rate\":" << ratio(misses, measured)
          << ",\"slo_goodput_mrps\":" << ratio(measured - misses, duration)
          << ",\"offered_mrps\":" << ratio(measured, duration);
        for (int k = 0; k < 2; ++k) j << ",\"class" << k << "_p99_us\":" << class_hist[k].quantile(.99)
          << ",\"class" << k << "_slo_miss_rate\":" << ratio(class_miss[k], class_count[k]);
        j << ",\"control_msgs\":" << controls << ",\"control_bytes\":" << control_bytes
          << ",\"payload_bytes\":" << payload_bytes << ",\"control_cpu_mean\":" << cpu_sum / c.hosts / duration
          << ",\"control_cpu_max\":" << cpu_max / duration << ",\"ingress_cpu\":" << ingress_busy_us / duration
          << ",\"plans\":" << plans << ",\"grants\":" << grants << ",\"denied\":" << denied
          << ",\"expired\":" << expired << ",\"returned_batches\":" << returns
          << ",\"batches\":" << batches << ",\"migrated\":" << migrated << ",\"moved_work_us\":" << moved_work
          << ",\"mean_batch\":" << ratio(migrated, batches) << ",\"max_batch\":" << max_observed_batch
          << ",\"credit_messages\":" << credit_messages << ",\"mean_credit_batch\":" << ratio(credit_items, credit_messages)
          << ",\"inspected\":" << inspected << ",\"max_reserved_work_us\":" << max_reserved
          << ",\"events\":" << count << ",\"invalid_migration_rate\":null}";
        return j.str();
    }
};
} // namespace
std::string simulate(const Trace& trace, const Config& cfg, const std::string& method, const std::string& outcomes_path) {
    return Engine(trace, cfg, method).run(outcomes_path);
}
} // namespace qbr
