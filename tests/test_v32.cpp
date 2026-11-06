#include "RealtimeTuner.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void ensure(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static double firstLockMs(double fs, double f0, bool weakFundamental) {
    skelly::CausalPitchTracker t;
    t.prepare(fs);
    t.setFastVocalAcquisitionEnabled(true);
    skelly::PitchEstimate e{};
    double phase=0;
    for (int i=0;i<int(fs*.3);++i) {
        phase += 2.*skelly::kPi*f0/fs;
        float x = float(weakFundamental ? (0.04*std::sin(phase)+0.5*std::sin(2*phase)+0.18*std::sin(3*phase)) :
                         (0.35*std::sin(phase)+0.22*std::sin(2*phase)+0.13*std::sin(3*phase)));
        if (t.push(x,e) && e.valid && std::abs(1200.0*std::log2(e.hertz/f0)) < 25)
            return 1000.0 * i / fs;
    }
    return 1000.;
}
int main() {
    for (double fs : {44100.,48000.,96000.}) {
        for (double f0 : {196.,220.,330.,440.}) {
            double ms=firstLockMs(fs,f0,false);
            ensure(ms < 38., "mid-high voiced acquisition exceeded bounded observation time");
        }
        // Low-register fundamentals still have a longer, octave-aware analysis path.
        // This test prevents shortening the 65-140 Hz analysis history in pursuit
        // of a better score on one higher-pitched reference vocal.
        for (double f0 : {110.,125.}) {
            const double ms=firstLockMs(fs,f0,true);
            ensure(ms < 75., "weak-fundamental low-note lock lost");
        }
    }
    std::cout << "v3.2 causal first-lock and weak-fundamental tests passed\n";
}
