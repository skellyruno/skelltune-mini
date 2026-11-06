#ifndef SKELLY_V04_NOTE_DECISION_HPP
#define SKELLY_V04_NOTE_DECISION_HPP
// Copyright 2026. Original research reference implementation.
// Allocation-free, causal note target decision model; NOT a pitch detector or shifter.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace skelly_research {
class NoteDecision final {
public:
    struct Result {
        int targetMidi = -1;
        bool changed = false;
        bool voiced = false;
        bool pending = false;
    };

    void prepare(double hostRate) noexcept {
        sampleRate_ = std::clamp(hostRate, 8000.0, 384000.0);
        reset();
    }
    void reset() noexcept {
        activeNote_ = -1;
        pendingNote_ = -1;
        pendingSamples_ = 0;
        untrustedSamples_ = 0;
    }
    // Invoke at detector hop boundaries. elapsedSamples is the number of HOST samples
    // since the last update, NOT the observation window duration.
    Result update(double f0Hz, double confidence, double onsetScore,
                  uint32_t elapsedSamples, uint16_t allowedPitchClasses = 0x0FFF,
                  double a4Hz = 440.0) noexcept {
        const uint32_t delta = std::min<uint32_t>(elapsedSamples, 384000);
        confidence = std::clamp(std::isfinite(confidence) ? confidence : 0.0, 0.0, 1.0);
        onsetScore = std::clamp(std::isfinite(onsetScore) ? onsetScore : 0.0, 0.0, 1.0);
        const bool usable = std::isfinite(f0Hz) && f0Hz >= 50.0 && f0Hz <= 1500.0 &&
                            confidence >= 0.58 && (allowedPitchClasses & 0x0FFF) &&
                            std::isfinite(a4Hz) && a4Hz >= 400.0 && a4Hz <= 480.0;
        Result result{};
        if (!usable) {
            // Hold through a brief fricative or low-confidence estimate, but don't
            // latch a note indefinitely after silence.
            untrustedSamples_ = satAdd(untrustedSamples_, delta);
            pendingNote_ = -1;
            pendingSamples_ = 0;
            if (untrustedSamples_ >= toSamples(0.18)) {
                result.changed = (activeNote_ != -1);
                activeNote_ = -1;
            }
            result.targetMidi = activeNote_;
            result.voiced = false;
            return result;
        }
        untrustedSamples_ = 0;
        const int candidate = closestAllowed(f0Hz, allowedPitchClasses, a4Hz);
        if (activeNote_ == -1) {
            activeNote_ = candidate;
            result.changed = true;
        } else if (candidate == activeNote_) {
            pendingNote_ = -1;
            pendingSamples_ = 0;
        } else {
            const bool strongNewOnset = onsetScore >= 0.72 && confidence >= 0.72;
            if (strongNewOnset) {
                activeNote_ = candidate;
                pendingNote_ = -1;
                pendingSamples_ = 0;
                result.changed = true;
            } else {
                if (pendingNote_ != candidate) {
                    pendingNote_ = candidate;
                    pendingSamples_ = delta;
                } else {
                    pendingSamples_ = satAdd(pendingSamples_, delta);
                }
                // Wait for evidence that persists beyond a short vibrato excursion;
                // strong onsets bypass this timeout, preserving genuine short notes.
                if (pendingSamples_ >= toSamples(0.065) && confidence >= 0.65) {
                    activeNote_ = candidate;
                    pendingNote_ = -1;
                    pendingSamples_ = 0;
                    result.changed = true;
                }
            }
        }
        result.targetMidi = activeNote_;
        result.voiced = true;
        result.pending = pendingNote_ != -1;
        return result;
    }
private:
    static uint32_t satAdd(uint32_t x, uint32_t y) noexcept {
        return (std::numeric_limits<uint32_t>::max() - x < y)
             ? std::numeric_limits<uint32_t>::max() : x + y;
    }
    uint32_t toSamples(double seconds) const noexcept {
        return static_cast<uint32_t>(std::ceil(sampleRate_ * seconds));
    }
    static int closestAllowed(double f0, uint16_t mask, double a4) noexcept {
        const double midi = 69.0 + 12.0 * std::log2(f0/a4);
        int best = -1;
        double bestError = 1000.0;
        const int center = static_cast<int>(std::round(midi));
        for (int n = std::max(0, center-12); n <= std::min(127, center+12); ++n) {
            if ((mask & (1u << (n % 12))) == 0) continue;
            const double error = std::abs(midi - n);
            if (error < bestError) { bestError = error; best = n; }
        }
        return best;
    }
    double sampleRate_ = 48000.0;
    int activeNote_ = -1;
    int pendingNote_ = -1;
    uint32_t pendingSamples_ = 0;
    uint32_t untrustedSamples_ = 0;
};
} // namespace skelly_research

#endif // SKELLY_V04_NOTE_DECISION_HPP
