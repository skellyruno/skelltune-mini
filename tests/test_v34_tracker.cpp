// v3.4: no-allocation pitch-analysis hot path and polarity-aware stereo tests.
#include "RealtimeTuner.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>

static void check(bool pass, const char* why) {
    if (!pass) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
static constexpr double pi = 3.14159265358979323846;

static void pitchTest(double rate, double hz) {
    skelly::CausalPitchTracker tracker;
    tracker.prepare(rate);
    skelly::PitchEstimate estimate{};
    int voiced=0, accurate=0;
    const int total=int(rate*0.45);
    for (int i=0; i<total; ++i) {
        const float signal=float(0.32*std::sin(2.0*pi*hz*i/rate));
        if (tracker.push(signal, estimate) && i>int(rate*0.20) && estimate.valid) {
            ++voiced;
            const double cents=1200.0*std::log2(double(estimate.hertz)/hz);
            if (std::abs(cents)<30.0) ++accurate;
        }
    }
    check(voiced>=20, "stable voiced tracking after FIR ring wrap");
    check(accurate*10>=voiced*9, "pitch estimate drift after FIR ring wrap");
    tracker.reset();
    for (int i=0; i<int(rate*0.15); ++i) {
        const auto updated=tracker.push(0.0f, estimate);
        if (updated && i>int(rate*0.08))
            check(!estimate.valid, "tracker reset must not produce a voiced silence estimate");
    }
}

static void stereoTest(double rate) {
    skelly::StereoPitchRouter router;
    router.prepare(rate);
    for (int i=0; i<int(rate*0.12); ++i) {
        const float left=float(0.28*std::sin(2.0*pi*193.0*i/rate));
        const float out=router.push(left, -left);
        if (i>int(rate*0.045))
            check(std::abs(out-left)<1.0e-6f, "anti-phase stereo must not cancel tracking input");
    }
    router.reset();
    for (int i=0; i<int(rate*0.12); ++i) {
        const float left=float(0.28*std::sin(2.0*pi*193.0*i/rate));
        const float out=router.push(left, 0.5f*left);
        if (i>int(rate*0.045))
            check(std::abs(out-0.75f*left)<1.0e-6f,
                  "correlated stereo must preserve mid-channel tracking");
    }
}

int main() {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        pitchTest(rate,220.0);
        pitchTest(rate,95.0);
        stereoTest(rate);
    }
    std::cout << "v3.4 tracker FIR and polarity regression passed\n";
    return 0;
}
