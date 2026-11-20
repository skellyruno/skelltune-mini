// v3.3.2: while a note is voiced the engine must stay on the wet path even as the needed
// correction passes through zero. v3.3.1 faded to dry there, crossfading two copies of the voice
// at different delays (not a whole number of periods apart), which mangled a cycle or two at
// every note centre a singer sweeps through. Also checks an explicit Amount of 0 still goes dry.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
using namespace skelly;
static constexpr double pi = 3.14159265358979323846;

static double minWetMix(float amount, double& maxAbsCents) {
    const double rate = 48000.0; const int block = 64;
    RealtimeTuner t; t.prepare(rate, 1);
    Parameters p; p.speedMs = 0.0f; p.amount = amount; p.mix = 1.0f; p.flexCents = 0.0f;
    p.vibratoPreservation = 0.0f; p.pitchClasses = uint16_t(1u << 7);   // G only
    t.setParameters(p);
    std::vector<float> in(block), out(block); const float* src[1] = {in.data()}; float* dst[1] = {out.data()};
    const double f0c = 440.0 * std::pow(2.0, (55.0 - 69.0) / 12.0);     // G3
    const double total = 1.4; double phase = 0.0, minMix = 1.0; maxAbsCents = 0.0; long n = 0;
    for (int b = 0; b < int(total * rate / block); ++b) {
        for (int i = 0; i < block; ++i, ++n) {
            const double ts = double(n) / rate;
            // 0.6 s hold at -15 cents, then a slow 0.6 s sweep to +15 cents (50 cents/s) through 0.
            const double g = std::clamp((ts - 0.6) / 0.6, 0.0, 1.0);
            const double cents = -15.0 + 30.0 * g;
            phase += 2.0 * pi * f0c * std::pow(2.0, cents / 1200.0) / rate;
            double s = 0.0;
            for (int h = 1; h <= 10; ++h) s += std::sin(h * phase + 0.3 * h) * (h <= 2 ? 0.5 : 1.0 / std::sqrt(double(h)));
            in[i] = float(0.2 * s);
        }
        t.process(src, dst, block, 1);
        const double ts = double(n) / rate;
        if (ts > 0.5 && t.currentTargetNote() == 55 && !t.targetPending()) {
            minMix = std::min(minMix, t.effectiveWetMix());
            maxAbsCents = std::max(maxAbsCents, std::abs(t.appliedCents()));
        }
    }
    return minMix;
}

int main() {
    double mc = 0.0;
    const double wet = minWetMix(1.0f, mc);
    std::cout << "minimum wet mix while sweeping through zero correction: " << wet << " (max |cents| " << mc << ")\n";
    if (mc < 10.0) { std::cerr << "FAIL: test did not exercise a real correction\n"; return 1; }
    if (wet < 0.98) { std::cerr << "FAIL: engine faded toward dry as the correction crossed zero\n"; return 1; }
    double mc0 = 0.0;
    const double dry = minWetMix(0.0f, mc0);
    std::cout << "Amount=0: minimum wet mix " << dry << "\n";
    if (dry > 0.05) { std::cerr << "FAIL: Amount=0 must still return to dry\n"; return 1; }
    std::cout << "PASS: v3.3.2 stays wet through zero correction\n"; return 0;
}
