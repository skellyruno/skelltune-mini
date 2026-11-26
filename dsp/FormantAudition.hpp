#pragma once
#include "shared/AlignedSpectralPitchFormant.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace skelly::playground {
// Optional post-tune colour, not a new correction engine. Disabled means
// exact pass-through with no added delay. Enabled PDC is constant, even at 0 st.
class FormantAudition {
public:
    static constexpr int extraLatency = skell::research::AlignedSpectralPitchFormant::latencySamples;
    void prepare(double rate, bool enable) noexcept {
        enabled_ = enable;
        dsp_.prepare(rate);
        for (auto& lane : dry_) lane.fill(0.f);
        pos_ = 0; gain_ = 1.f; activity_ = 0.f; target_ = 0.f;
        const double sr = std::isfinite(rate) ? std::clamp(rate, 8000., 192000.) : 48000.;
        release_ = float(1. - std::exp(-1. / (.040 * sr)));
        smoothing_ = float(1. - std::exp(-1. / (.025 * sr)));
    }
    bool enabled() const noexcept { return enabled_; }
    int latency() const noexcept { return enabled_ ? extraLatency : 0; }
    void setSemitones(float st) noexcept {
        target_ = std::isfinite(st) ? std::clamp(st, -12.f, 12.f) : 0.f;
        dsp_.set(.5f, .5f + target_ / 24.f, 1.f, 1.f);
    }
    std::array<float,2> process(float l, float r, bool bypass = false) noexcept {
        if (!enabled_) return {l, r};
        l = std::isfinite(l) ? l : 0.f; r = std::isfinite(r) ? r : 0.f;
        const std::array<float,2> aligned{dry_[0][pos_], dry_[1][pos_]};
        dry_[0][pos_] = l; dry_[1][pos_] = r;
        if (++pos_ == extraLatency) pos_ = 0;
        const auto y = dsp_.process(l, r);
        // Smooth gain recovery; only limit genuinely shifted output. Zero
        // from a fresh preparation remains exact unity, even on hot input.
        activity_ += smoothing_ * ((std::abs(target_) > 1.e-5f ? 1.f : 0.f) - activity_);
        if (activity_ < 1.e-6f) { activity_ = 0.f; gain_ = 1.f; return aligned; }
        const float peak = std::max(std::abs(y[0]), std::abs(y[1]));
        // Conservative sample ceiling, not a certified true-peak limiter.
        const float ceiling = .60f;
        const float cap = peak > ceiling ? ceiling / peak : 1.f;
        gain_ = std::min(cap, gain_ + release_ * (1.f - gain_));
        return bypass ? aligned : std::array<float,2>{y[0]*gain_, y[1]*gain_};
    }
private:
    skell::research::AlignedSpectralPitchFormant dsp_;
    std::array<std::array<float,extraLatency>,2> dry_{};
    int pos_ = 0;
    bool enabled_ = false;
    float target_ = 0.f, gain_ = 1.f, activity_ = 0.f, release_ = .001f, smoothing_ = .001f;
};
} // namespace skelly::playground
