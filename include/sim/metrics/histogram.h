#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace sim {

// Exact latency sample store. One million doubles use roughly 8 MiB and avoid
// the former silent 10 ms clipping of tail observations.
class Histogram {
public:
    explicit Histogram(double = 0.0, int = 0) {}

    void reserve(size_t count) { samples_.reserve(count); }
    void record(double value_us) { samples_.push_back(value_us); }

    double percentile(double p) const {
        if (samples_.empty()) return 0.0;
        std::vector<double> ordered(samples_);
        size_t rank = static_cast<size_t>(std::ceil(p * ordered.size()));
        if (rank == 0) rank = 1;
        if (rank > ordered.size()) rank = ordered.size();
        std::nth_element(ordered.begin(), ordered.begin() + (rank - 1), ordered.end());
        return ordered[rank - 1];
    }

    uint64_t count() const { return static_cast<uint64_t>(samples_.size()); }
    void reset() { samples_.clear(); }

private:
    std::vector<double> samples_;
};

} // namespace sim
