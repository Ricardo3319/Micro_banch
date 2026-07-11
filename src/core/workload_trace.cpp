#include "sim/workloads/trace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace sim {
namespace {

uint64_t splitmix64(uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

class Sha256 {
public:
    void update(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < size; ++i) {
            buffer_[buffer_size_++] = bytes[i];
            if (buffer_size_ == 64) {
                transform(buffer_.data());
                bit_length_ += 512;
                buffer_size_ = 0;
            }
        }
    }

    std::string finish() {
        uint64_t total_bits = bit_length_ + static_cast<uint64_t>(buffer_size_) * 8ULL;
        buffer_[buffer_size_++] = 0x80;
        if (buffer_size_ > 56) {
            while (buffer_size_ < 64) buffer_[buffer_size_++] = 0;
            transform(buffer_.data());
            buffer_size_ = 0;
        }
        while (buffer_size_ < 56) buffer_[buffer_size_++] = 0;
        for (int shift = 56; shift >= 0; shift -= 8)
            buffer_[buffer_size_++] = static_cast<uint8_t>(total_bits >> shift);
        transform(buffer_.data());

        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (uint32_t value : state_) out << std::setw(8) << value;
        return out.str();
    }

private:
    static uint32_t rotate_right(uint32_t value, uint32_t count) {
        return (value >> count) | (value << (32U - count));
    }

    void transform(const uint8_t* block) {
        static constexpr std::array<uint32_t, 64> k = {
            0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
            0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
            0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
            0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
            0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
            0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
            0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
            0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
        };
        uint32_t w[64]{};
        for (int i = 0; i < 16; ++i) {
            int j = i * 4;
            w[i] = (static_cast<uint32_t>(block[j]) << 24U)
                 | (static_cast<uint32_t>(block[j + 1]) << 16U)
                 | (static_cast<uint32_t>(block[j + 2]) << 8U)
                 | static_cast<uint32_t>(block[j + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3U);
            uint32_t s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10U);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3];
        uint32_t e=state_[4],f=state_[5],g=state_[6],h=state_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            uint32_t s0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        state_[0]+=a; state_[1]+=b; state_[2]+=c; state_[3]+=d;
        state_[4]+=e; state_[5]+=f; state_[6]+=g; state_[7]+=h;
    }

    std::array<uint32_t, 8> state_{0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,
                                   0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
    std::array<uint8_t, 64> buffer_{};
    size_t buffer_size_ = 0;
    uint64_t bit_length_ = 0;
};

template <typename T>
void hash_scalar(Sha256& hash, const T& value) {
    static_assert(std::is_trivially_copyable<T>::value, "canonical scalar required");
    std::array<uint8_t, sizeof(T)> bytes{};
    std::memcpy(bytes.data(), &value, sizeof(T));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    std::reverse(bytes.begin(), bytes.end());
#endif
    hash.update(bytes.data(), bytes.size());
}

double normal_cdf(double value) {
    return 0.5 * std::erfc(-value / std::sqrt(2.0));
}

double sample_conditional_lognormal(std::mt19937_64& rng, bool short_rpc) {
    std::lognormal_distribution<double> distribution(W3_LOGNORMAL_MU, W3_LOGNORMAL_SIGMA);
    for (;;) {
        double value = distribution(rng);
        if ((value <= SLO_SHORT_SERVICE_THRESHOLD_US) == short_rpc) return value;
    }
}

} // namespace

const char* rpc_method_name(RpcMethod method) {
    return method == RpcMethod::SHORT_RPC ? "short" : "long";
}

double rpc_deadline_budget_us(RpcMethod method) {
    return method == RpcMethod::SHORT_RPC ? SLO_SHORT_US : SLO_LONG_US;
}

WorkloadTrace WorkloadTrace::generate(const TraceConfig& input) {
    if (input.core_count <= 0 || input.warmup_requests < 0 || input.measurement_requests <= 0)
        throw std::invalid_argument("invalid trace request/core counts");
    if (!(input.rho > 0.0) || !(input.effective_core_capacity > 0.0))
        throw std::invalid_argument("rho and capacity must be positive");

    WorkloadTrace trace;
    trace.config_ = input;
    const uint64_t seed = input.seed;
    std::mt19937_64 arrival_rng(splitmix64(seed ^ 0x4152524956414cULL));
    std::mt19937_64 service_rng(splitmix64(seed ^ 0x53455256494345ULL));
    std::mt19937_64 routing_rng(splitmix64(seed ^ 0x524f5554494e47ULL));

    const double mean_exec_us = W3_MEAN_SERVICE_US + T_host_us;
    const double target_average_lambda =
        input.rho * input.effective_core_capacity / mean_exec_us;
    const double burst_probability =
        W2_BURST_STAY_US / (W2_NORMAL_STAY_US + W2_BURST_STAY_US);
    const double mmpp_average_multiplier =
        1.0 + burst_probability * (W2_LAMBDA_BURST_FACTOR - 1.0);
    const double normal_lambda = input.workload == WorkloadType::W2_MMPP_BIMODAL
        ? target_average_lambda / mmpp_average_multiplier
        : target_average_lambda;

    bool in_burst = false;
    double state_end_us = 0.0;
    auto schedule_state_end = [&](double from) {
        double mean = in_burst ? W2_BURST_STAY_US : W2_NORMAL_STAY_US;
        std::exponential_distribution<double> state_duration(1.0 / mean);
        state_end_us = from + state_duration(arrival_rng);
    };
    if (input.workload == WorkloadType::W2_MMPP_BIMODAL) schedule_state_end(0.0);

    std::vector<int> hot_cores;
    auto refresh_hot_cores = [&]() {
        std::vector<int> all(input.core_count);
        std::iota(all.begin(), all.end(), 0);
        std::shuffle(all.begin(), all.end(), routing_rng);
        int count = std::max(1, std::min(input.core_count, input.w2_hot_core_count));
        hot_cores.assign(all.begin(), all.begin() + count);
    };

    const double short_probability = normal_cdf(
        (std::log(SLO_SHORT_SERVICE_THRESHOLD_US) - W3_LOGNORMAL_MU)
        / W3_LOGNORMAL_SIGMA);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    std::uniform_int_distribution<int> any_core(0, input.core_count - 1);
    const size_t request_count = static_cast<size_t>(input.warmup_requests)
                               + static_cast<size_t>(input.measurement_requests);
    trace.entries_.reserve(request_count);

    double generate_time_us = 0.0;
    bool prior_burst = false;
    for (size_t index = 0; index < request_count; ++index) {
        if (index > 0) {
            double lambda = normal_lambda;
            if (input.workload == WorkloadType::W2_MMPP_BIMODAL) {
                while (generate_time_us >= state_end_us) {
                    in_burst = !in_burst;
                    if (in_burst && !prior_burst) refresh_hot_cores();
                    prior_burst = in_burst;
                    schedule_state_end(state_end_us);
                }
                if (in_burst) lambda *= W2_LAMBDA_BURST_FACTOR;
            }
            std::exponential_distribution<double> gap(lambda);
            generate_time_us += gap(arrival_rng);
        }

        bool short_rpc = unit(service_rng) <
            (input.workload == WorkloadType::W3_POISSON_LOGNORMAL
                ? short_probability : BIMODAL_SHORT_PROB);
        RpcMethod method = short_rpc ? RpcMethod::SHORT_RPC : RpcMethod::LONG_RPC;
        double service_us = input.workload == WorkloadType::W3_POISSON_LOGNORMAL
            ? sample_conditional_lognormal(service_rng, short_rpc)
            : (short_rpc ? BIMODAL_SHORT_US : BIMODAL_LONG_US);

        int initial_core = any_core(routing_rng);
        if (input.workload == WorkloadType::W2_MMPP_BIMODAL && in_burst
            && !hot_cores.empty() && unit(routing_rng) < input.w2_hot_dispatch_prob) {
            std::uniform_int_distribution<size_t> hot(0, hot_cores.size() - 1);
            initial_core = hot_cores[hot(routing_rng)];
        }

        WorkloadTraceEntry entry;
        entry.id = static_cast<uint64_t>(index + 1);
        entry.generate_time_us = generate_time_us;
        entry.rpc_method = method;
        entry.service_time_us = service_us;
        entry.deadline_budget_us = rpc_deadline_budget_us(method);
        entry.initial_core = initial_core;
        entry.burst = in_burst;
        trace.entries_.push_back(entry);
        if (index >= static_cast<size_t>(input.warmup_requests))
            trace.measured_actual_work_us_ += service_us + T_host_us;
    }

    Sha256 hash;
    hash.update(RESCUE_TRACE_VERSION, std::strlen(RESCUE_TRACE_VERSION));
    int workload = static_cast<int>(input.workload);
    hash_scalar(hash, workload); hash_scalar(hash, input.rho); hash_scalar(hash, input.seed);
    hash_scalar(hash, input.core_count); hash_scalar(hash, input.effective_core_capacity);
    hash_scalar(hash, input.warmup_requests); hash_scalar(hash, input.measurement_requests);
    hash_scalar(hash, input.w2_hot_core_count); hash_scalar(hash, input.w2_hot_dispatch_prob);
    for (const auto& entry : trace.entries_) {
        hash_scalar(hash, entry.id); hash_scalar(hash, entry.generate_time_us);
        int method = static_cast<int>(entry.rpc_method); hash_scalar(hash, method);
        hash_scalar(hash, entry.service_time_us); hash_scalar(hash, entry.deadline_budget_us);
        hash_scalar(hash, entry.initial_core);
        uint8_t burst = entry.burst ? 1U : 0U; hash_scalar(hash, burst);
    }
    trace.sha256_ = hash.finish();
    return trace;
}

bool WorkloadTrace::write_csv(const std::string& path) const {
    std::filesystem::path output(path);
    if (!output.parent_path().empty())
        std::filesystem::create_directories(output.parent_path());
    std::ofstream csv(path);
    if (!csv.is_open()) return false;
    csv << "trace_version,trace_sha256,id,generate_time_us,rpc_method,service_time_us,deadline_budget_us,initial_core,burst\n";
    csv << std::setprecision(17);
    for (const auto& entry : entries_) {
        csv << RESCUE_TRACE_VERSION << ',' << sha256_ << ',' << entry.id << ','
            << entry.generate_time_us << ',' << rpc_method_name(entry.rpc_method) << ','
            << entry.service_time_us << ',' << entry.deadline_budget_us << ','
            << entry.initial_core << ',' << (entry.burst ? 1 : 0) << '\n';
    }
    return true;
}

} // namespace sim
