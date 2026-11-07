#pragma once
// Precomputed Hann crossfade for hard-tuned cycle wraps.
// Prepared outside the real-time audio callback; no heap allocations in blend().
#include <algorithm>
#include <array>
#include <cmath>

namespace skelly {
class PhaseCrossfadeLut {
public:
    static constexpr int kSegments = 1024;
    void prepare() noexcept {
        constexpr double pi = 3.14159265358979323846;
        for (int i = 0; i <= kSegments; ++i)
            table_[size_t(i)] = 0.5 - 0.5 * std::cos(pi * double(i) / kSegments);
        table_[0] = 0.0;
        table_[kSegments] = 1.0;
    }
    double blend(int position, int total) const noexcept {
        if (total <= 0) return 0.0;
        const double x = std::clamp((double(position) + 1.0) / double(total), 0.0, 1.0)
                         * double(kSegments);
        const int index = std::min(int(x), kSegments - 1);
        const double fraction = x - double(index);
        return table_[size_t(index)] * (1.0 - fraction)
             + table_[size_t(index + 1)] * fraction;
    }
private:
    std::array<double, kSegments + 1> table_{};
};
} // namespace skelly
