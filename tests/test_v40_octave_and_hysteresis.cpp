// v3.3.2 regression tests. Written so they FAIL on v3.3.1 and pass on v3.3.2.
//  1. Tracker must not report the octave below on clean harmonic voices whose period
//     lands on a fractional 12 kHz lag (rounding error used to fake "octave evidence").
//  2. NoteDecision must not ping-pong between two notes when the voice hovers within
//     ~25 cents of the boundary between them, but must still commit decisively and fast
//     when the pitch moves clearly onto the new note.
#include "RealtimeTuner.hpp"
#include "NoteDecision.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>

static void check(bool pass, const char* why) {
    if (!pass) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
static constexpr double pi = 3.14159265358979323846;

// Harmonic-rich, deterministic voice. A period of N+0.5 samples at the tracker's 12 kHz
// analysis rate made v3.3.1 report the octave below on 100% of frames, even for a clean
// steady tone, because integer-lag rounding error faked "octave evidence".
static int octaveDownFrames(double f0, int mode, int& voicedFrames) {
    const double rate = 48000.0;
    skelly::CausalPitchTracker tracker;
    tracker.prepare(rate);
    skelly::PitchEstimate est{};
    int octaveDown = 0; voicedFrames = 0;
    uint32_t seed = 12345u;
    double phase = 0.0, amp = 1.0;
    long cycle = 0;
    for (int i = 0; i < int(rate * 0.8); ++i) {
        phase += 2.0 * pi * f0 / rate;
        if (phase >= 2.0 * pi) {
            phase -= 2.0 * pi; ++cycle;
            seed = seed * 1664525u + 1013904223u;
            const double r = ((seed >> 8) & 0xFFFF) / 65535.0 - 0.5;
            amp = mode == 0 ? 1.0 : (mode == 1 ? 1.0 + 0.20 * r
                                               : 1.0 + 0.10 * ((cycle & 1) ? 1.0 : -1.0) + 0.04 * r);
        }
        double s = 0.0;
        for (int h = 1; h <= 12; ++h)
            s += std::sin(h * phase + 0.3 * h) * (h <= 2 ? 0.5 : 1.0 / std::sqrt(double(h)));
        if (tracker.push(float(0.2 * amp * s), est) && i > int(rate * 0.2) &&
            est.valid && est.confidence > 0.6f) {
            ++voicedFrames;
            if (std::abs(double(est.hertz) / f0 - 0.5) < 0.05) ++octaveDown;
        }
    }
    return octaveDown;
}

static void trackerOctaveTest() {
    for (double lag : {42.5, 48.5, 54.5, 60.5, 66.5})
        for (int mode = 0; mode < 3; ++mode) {   // clean, 10% shimmer, 10% cycle alternation
            const double f0 = 12000.0 / lag;
            int voiced = 0;
            const int bad = octaveDownFrames(f0, mode, voiced);
            check(voiced > 50, "tracker produced too few voiced estimates");
            if (bad != 0)
                std::cerr << "octave-down frames at " << f0 << " Hz (mode " << mode << "): "
                          << bad << "/" << voiced << '\n';
            check(bad == 0, "tracker reported the octave below on a steady fractional-lag voice");
        }
}

static double midiHz(double midi) { return 440.0 * std::pow(2.0, (midi - 69.0) / 12.0); }

static void hysteresisTest() {
    const double rate = 48000.0;
    // G major pitch classes: G A B C D E F#  -> 7 9 11 0 2 4 6
    uint16_t mask = 0;
    for (int pc : {7, 9, 11, 0, 2, 4, 6}) mask |= uint16_t(1u << pc);
    const uint32_t hop = 64; // ~1.33 ms at 48 kHz
    auto runBoundary = [&](double lowMidi, double highMidi) {
        skelly_research::NoteDecision nd; nd.prepare(rate);
        // Establish D (midi 50) with a clean, long, centered note.
        int changes = 0;
        for (int i = 0; i < 400; ++i)
            changes += nd.update(midiHz(50.0), 0.98, 0.0, hop, mask, 440.0, true).changed ? 1 : 0;
        check(nd.update(midiHz(50.0), 0.98, 0.0, hop, mask, 440.0, true).targetMidi == 50,
              "setup: D must be the active note");
        // Hover around the D/E boundary (51.0) with 12 ms excursions to each side.
        int switches = 0, last = 50;
        for (int cycle = 0; cycle < 20; ++cycle) {
            for (int side = 0; side < 2; ++side)
                for (int i = 0; i < 9; ++i) {            // 9 hops ~ 12 ms
                    const auto r = nd.update(midiHz(side ? lowMidi : highMidi), 0.97, 0.0, hop, mask, 440.0, true);
                    if (r.targetMidi != last) { ++switches; last = r.targetMidi; }
                }
        }
        return switches;
    };
    const int hovering = runBoundary(50.88, 51.12); // within 12 cents of the boundary
    if (hovering > 1) std::cerr << "target switches while hovering on the boundary: " << hovering << '\n';
    check(hovering <= 1, "NoteDecision ping-pongs between notes while the voice hovers on a boundary");

    // A decisive move onto the next note must still commit fast (<= 12 ms) at max retune.
    skelly_research::NoteDecision nd; nd.prepare(rate);
    for (int i = 0; i < 400; ++i) nd.update(midiHz(50.0), 0.98, 0.0, hop, mask, 440.0, true);
    int hopsToCommit = -1;
    for (int i = 0; i < 40 && hopsToCommit < 0; ++i) {
        const auto r = nd.update(midiHz(52.0), 0.98, 0.0, hop, mask, 440.0, true);
        if (r.targetMidi == 52) hopsToCommit = i + 1;
    }
    check(hopsToCommit > 0, "decisive note change never committed");
    const double ms = hopsToCommit * hop * 1000.0 / rate;
    if (ms > 12.0) std::cerr << "decisive commit took " << ms << " ms\n";
    check(ms <= 12.0, "decisive note change must still commit within 12 ms");
}

int main() {
    trackerOctaveTest();
    hysteresisTest();
    std::cout << "PASS: v3.3.2 octave evidence + boundary hysteresis\n";
    return 0;
}
