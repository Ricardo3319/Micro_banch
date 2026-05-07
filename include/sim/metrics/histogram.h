#pragma once
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstddef>

namespace sim {

// Simple log-linear histogram sufficient for P99/P99.9 in the 1-10000 us range.
class Histogram {
public:
    explicit Histogram(double max_us = 10000.0, int buckets = 10000)
        : max_us_(max_us), buckets_(buckets),
          counts_(buckets + 1, 0), total_(0) {}

    void record(double value_us) {
        int idx = static_cast<int>(value_us / max_us_ * buckets_);
        if (idx < 0) idx = 0;
        if (idx > buckets_) idx = buckets_;
        ++counts_[idx];
        ++total_;
    }

    double percentile(double p) const {
        if (total_ == 0) return 0.0;
        uint64_t target = static_cast<uint64_t>(std::ceil(p * total_));
        if (target == 0) target = 1;
        uint64_t cum = 0;
        for (int i = 0; i <= buckets_; ++i) {
            cum += counts_[i];
            if (cum >= target)
                return (static_cast<double>(i) + 0.5) * max_us_ / buckets_;
        }
        return max_us_;
    }

    uint64_t count() const { return total_; }
    void reset() { std::fill(counts_.begin(), counts_.end(), 0); total_ = 0; }

private:
    double max_us_;
    int    buckets_;
    std::vector<uint64_t> counts_;
    uint64_t total_;
};

} // namespace sim
