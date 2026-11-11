#pragma once
// SkellyTune Mini: causal subsonic cleanup for reconstructed WET audio only.
// Leaves the delayed dry path, note detection, host PDC, and transient anchors
// unchanged. Two 12-Hz DC blockers reject generated rumble/DC while retaining
// vocal fundamentals >= 65 Hz within approximately 0.3 dB.
#include <algorithm>
#include <array>
#include <cmath>

namespace skelly {
class WetRumbleGuard final {
public:
    void prepare(double sampleRate) noexcept {
        const double rate = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
                                        8000.0, 192000.0);
        constexpr double pi = 3.14159265358979323846;
        const double pole = std::exp(-2.0 * pi * 12.0 / rate);
        coefficient_ = pole;
        normalizer_ = 0.5 * (1.0 + pole); // unity magnitude at Nyquist
        reset();
    }
    void reset() noexcept {
        for (auto& channel : previousInput_) channel.fill(0.0);
        for (auto& channel : previousOutput_) channel.fill(0.0);
    }
    double process(int channel, double sample) noexcept {
        const int ch = std::clamp(channel, 0, 1);
        if (!std::isfinite(sample)) sample = 0.0;
        for (int stage = 0; stage < 2; ++stage) {
            const double prior = previousInput_[size_t(ch)][size_t(stage)];
            double y = normalizer_ * (sample - prior)
                + coefficient_ * previousOutput_[size_t(ch)][size_t(stage)];
            // Keep even stand-alone/offline renders denormal-free after
            // a long silence. This threshold is hundreds of dB below
            // any meaningful audio, so it cannot create audible gating.
            if (std::abs(y) < 1.0e-24) y = 0.0;
            previousInput_[size_t(ch)][size_t(stage)] = sample;
            previousOutput_[size_t(ch)][size_t(stage)] = y;
            sample = y;
        }
        return sample;
    }
private:
    double coefficient_ = 0.999, normalizer_ = 0.9995;
    std::array<std::array<double, 2>, 2> previousInput_{};
    std::array<std::array<double, 2>, 2> previousOutput_{};
};
} // namespace skelly
