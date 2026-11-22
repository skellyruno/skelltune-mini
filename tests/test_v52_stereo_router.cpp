// Detector-only mid/side crossover: audio remains untouched by this router.
// A 0->180 degree phase sweep used to cause a single-sample pitch-analysis
// discontinuity when anti-phase routing abruptly switched from mid to left.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

static void require(bool good, const char* message) {
    if (!good) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main() {
    constexpr double pi = 3.1415926535897932384626;
    for (const int rate : {44100, 48000, 96000}) {
        skelly::StereoPitchRouter router;
        router.prepare(rate);
        double peakDelta = 0.0, lateEnergy = 0.0;
        int lateCount = 0;
        float previous = 0.0f;
        for (int i = 0; i < int(rate * 0.85); ++i) {
            const double t = double(i) / rate;
            const double phase = pi * std::clamp((t - 0.1) / 0.3, 0.0, 1.0);
            const float left = float(0.25 * std::sin(2.0 * pi * 220.0 * t));
            const float right = float(0.25 * std::sin(2.0 * pi * 220.0 * t + phase));
            const float output = router.push(left, right);
            require(std::isfinite(output), "router emitted a nonfinite detector sample");
            if (i > int(rate * 0.05))
                peakDelta = std::max(peakDelta, std::abs(double(output - previous)));
            if (t > 0.65) { lateEnergy += double(output) * output; ++lateCount; }
            previous = output;
        }
        const double invertedRms = std::sqrt(lateEnergy / lateCount);
        std::cout << "rate=" << rate << " max adjacent detector step="
                  << peakDelta << " inverted RMS=" << invertedRms << '\n';
        require(peakDelta < 0.03, "mid/side switch injected a pitch-analysis impulse");
        require(invertedRms > 0.12 && invertedRms < 0.20,
                "anti-phase rescue failed to preserve a usable voice");
        router.reset();
        for (int i = 0; i < 1000; ++i)
            require(router.push(0.0f, 0.0f) == 0.0f, "reset kept stale detector audio");
    }
    std::cout << "PASS: hysteretic, click-free stereo pitch analysis routing\n";
}
