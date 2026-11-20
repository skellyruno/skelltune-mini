// v3.3.2: during a note change the engine must not fall back to raw pitch,
// and the held-note switch must be a true A/B (disable -> old dry behavior).
#include "RealtimeTuner.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
using namespace skelly;
static double meanWet(bool hold) {
    const double sr = 48000.0;
    RealtimeTuner t; t.prepare(sr, 1);
    Parameters p; p.speedMs = 0; p.amount = 1; p.mix = 1;
    p.pitchClasses = 0x0AB5; // C major
    t.setParameters(p); t.setHoldActiveNoteWhilePending(hold);
    std::vector<float> in(256), out(256); const float* src[1] = {in.data()}; float* dst[1] = {out.data()};
    double phase = 0, wetSum = 0; int n = 0; long sample = 0;
    for (int blk = 0; blk < 700; ++blk) {
        for (int i = 0; i < 256; ++i, ++sample) {
            const double ts = double(sample) / sr;
            // steady 270 Hz, then a one-semitone glide. Synthetic smoke check only: it guards
            // against regressions, it does not reproduce the real-vocal gain.
            double f = ts < 1.5 ? 270.0 : 270.0 * std::pow(2.0, std::min(1.0, (ts - 1.5) / 0.15) / 12.0);
            phase += 2 * kPi * f / sr;
            in[i] = float(0.3 * std::sin(phase) + 0.15 * std::sin(2 * phase) + 0.08 * std::sin(3 * phase));
        }
        t.process(src, dst, 256, 1);
        const double ts = double(sample) / sr;
        if (ts > 1.5 && ts < 1.9) { wetSum += t.effectiveWetMix(); ++n; }
    }
    return n ? wetSum / n : 0.0;
}
int main() {
    const double with = meanWet(true), without = meanWet(false);
    std::printf("mean wet mix across note change: hold=%.3f legacy=%.3f\n", with, without);
    if (!(with >= without - 1e-9)) { std::puts("FAIL: hold reduced wet mix"); return 1; }
    if (!(with > 0.5)) { std::puts("FAIL: wet mix collapsed during note change"); return 1; }
    std::puts("PASS: v3.3.2 note-change hold"); return 0;
}
