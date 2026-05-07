#pragma once
#include "sim/common/constants.h"
#include <random>

namespace sim {

// Bimodal service time generator (W1/W2).
class BimodalService {
public:
    explicit BimodalService(std::mt19937_64& rng) : rng_(rng) {}

    double next() {
        std::uniform_real_distribution<double> u(0.0, 1.0);
        return (u(rng_) < BIMODAL_SHORT_PROB) ? BIMODAL_SHORT_US : BIMODAL_LONG_US;
    }

private:
    std::mt19937_64& rng_;
};

// MMPP(2-state) arrival process for W2.
// Normal state:  lambda_normal  (= base lambda from rho)
// Burst  state:  lambda_burst   (= 1.5 * lambda_normal)
// Sojourn times: exponential with mean Normal=5000us, Burst=500us.
class MMPPArrival {
public:
    MMPPArrival(double lambda_normal_per_us, std::mt19937_64& rng)
        : lambda_normal_(lambda_normal_per_us),
          lambda_burst_(lambda_normal_per_us * W2_LAMBDA_BURST_FACTOR),
          rng_(rng), in_burst_(false), state_end_us_(0.0) {
        schedule_state_end(0.0);
    }

    // Returns inter-arrival time in us.
    double next_interarrival(double current_time_us) {
        advance_state(current_time_us);
        double lam = in_burst_ ? lambda_burst_ : lambda_normal_;
        std::exponential_distribution<double> exp(lam);
        return exp(rng_);
    }

    bool is_burst() const { return in_burst_; }

private:
    void advance_state(double t) {
        while (t >= state_end_us_) {
            in_burst_ = !in_burst_;
            schedule_state_end(state_end_us_);
        }
    }

    void schedule_state_end(double from) {
        double mean = in_burst_ ? W2_BURST_STAY_US : W2_NORMAL_STAY_US;
        std::exponential_distribution<double> exp(1.0 / mean);
        state_end_us_ = from + exp(rng_);
    }

    double lambda_normal_;
    double lambda_burst_;
    std::mt19937_64& rng_;
    bool   in_burst_;
    double state_end_us_;
};

// Poisson arrival (for W1/W3).
class PoissonArrival {
public:
    PoissonArrival(double lambda_per_us, std::mt19937_64& rng)
        : dist_(lambda_per_us), rng_(rng) {}

    double next_interarrival() { return dist_(rng_); }

private:
    std::exponential_distribution<double> dist_;
    std::mt19937_64& rng_;
};

// Lognormal service time generator (W3).
// Parameters: mu, sigma such that E[S] = exp(mu + sigma^2/2) = 24us
class LognormalService {
public:
    LognormalService(double mu, double sigma, std::mt19937_64& rng)
        : dist_(mu, sigma), rng_(rng) {}

    double next() { return dist_(rng_); }

private:
    std::lognormal_distribution<double> dist_;
    std::mt19937_64& rng_;
};

} // namespace sim
