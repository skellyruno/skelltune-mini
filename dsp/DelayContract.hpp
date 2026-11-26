#pragma once
#include <algorithm>
#include <cmath>

namespace skelly {
// What the host may compensate, rather than an estimate of the ages of samples
// recycled by a periodic pitch shifter.  Both modes share one fixed dry/transient
// reference, while their continuous tonal output is time-warped and therefore
// has NO single transport delay.  Never use source-cycle age as host PDC.
struct DelayContract {
    int reportedSamples = 0;
    bool drySampleExact = true;
    bool transientAnchored = true;
    bool fixedWetWaveformDelay = false;
};
// A second STUDIO product offers a shorter fixed timebase without changing
// an existing plugin's identity or its saved-session latency behavior.
// This 40 ms profile uses unchanged full-period analysis/synthesis windows.
inline DelayContract studioFastDelayContract(double sampleRate) noexcept {
    const double fs = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
                                 8000.0, 192000.0);
    return {int(std::lround(fs * 0.040)), true, true, false};
}
// Separate STUDIO plugin instance: fixed synthesis timeline, 50 ms host PDC.
// The PSOLA grains are anchored to this timeline within one pitch-mark search
// radius; tonal waveform samples are time-warped, not simple delayed copies.
inline DelayContract studioDelayContract(double sampleRate) noexcept {
    const double fs = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
                                 8000.0, 192000.0);
    return {int(std::lround(fs*0.050)), true, true, false};
}
inline DelayContract delayContract(double sampleRate, bool /*experimentalCycle*/,
                                   double realtimeLatencyMs = 3.0, bool playgroundExtended = false) noexcept {
    const double fs = std::clamp(std::isfinite(sampleRate) ? sampleRate : 48000.0,
                                 8000.0, 192000.0);
    // Explicit preparation-time realtime latency. Old DSP callers keep the
    // 3ms compatibility default; Mini instances request 1.5ms from JUCE.
    // Grain source age is nonlinear and must never be advertised as PDC.
    const double ms = std::clamp(
        std::isfinite(realtimeLatencyMs) ? realtimeLatencyMs : 3.0, 1.5, playgroundExtended ? 10.0 : 3.0);
    return {int(std::lround(fs * (ms * 0.001))), true, true, false};
}
} // namespace skelly
