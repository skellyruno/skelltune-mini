// v3.3.2: "testtune" alternate algorithm.
//  1. PeriodTable: scale mask -> in-scale note periods (samples per cycle), validation ranges, cache.
//  2. Nearest-note search happens in the PERIOD domain (boundary = arithmetic mean of periods).
//  3. Engine: switch defaults OFF; with it ON a steady off-pitch tone is still tuned to the nearest
//     note, and (unlike the default algorithm) there is no hysteresis at a note boundary.
#include "RealtimeTuner.hpp"
#include "PeriodTable.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
using namespace skelly;
static constexpr double pi = 3.14159265358979323846;

static void check(bool pass, const char* why) {
    if (!pass) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}

static void tableMath() {
    PeriodTable t;
    check(t.rebuild(48000.0, 440.0, 0x0fff) == 128, "chromatic mask yields all 128 MIDI notes");
    int a4 = -1;
    for (int i = 0; i < t.size(); ++i) if (t.note(i) == 69) a4 = i;
    check(a4 >= 0, "table contains A4");
    check(std::abs(t.period(a4) - 48000.0 / 440.0) < 1e-9, "A4 period is sampleRate / 440");
    check(t.note(0) == 0 && t.note(127) == 127, "notes span MIDI 0..127");
    for (int i = 1; i < t.size(); ++i)
        check(t.note(i) > t.note(i - 1) && t.period(i) < t.period(i - 1), "ascending notes, strictly descending periods");
    // Octaves are exact halves of the period.
    int a3 = -1; for (int i = 0; i < t.size(); ++i) if (t.note(i) == 57) a3 = i;
    check(std::abs(t.period(a3) - 2.0 * t.period(a4)) < 1e-9, "octave doubles the period");

    uint16_t cMajor = 0; for (int pc : {0, 2, 4, 5, 7, 9, 11}) cMajor |= uint16_t(1u << pc);
    check(t.rebuild(48000.0, 440.0, cMajor) == 75, "C major mask yields 75 of the 128 MIDI notes");
    for (int i = 0; i < t.size(); ++i) {
        const int pc = t.note(i) % 12;
        check(pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11, "only enabled pitch classes emitted");
    }
    check(t.rebuild(48000.0, 440.0, 0) == 0, "empty mask yields an empty table");

    // Accepted ranges: sample rate 8000..384000 Hz, reference 400..480 Hz.
    check(t.rebuild(7999.0, 440.0, 0x0fff) == 0, "sample rate below 8000 rejected");
    check(t.rebuild(8000.0, 440.0, 0x0fff) == 128, "sample rate 8000 accepted");
    check(t.rebuild(384000.0, 440.0, 0x0fff) == 128, "sample rate 384000 accepted");
    check(t.rebuild(384001.0, 440.0, 0x0fff) == 0, "sample rate above 384000 rejected");
    check(t.rebuild(48000.0, 399.9, 0x0fff) == 0, "reference below 400 rejected");
    check(t.rebuild(48000.0, 400.0, 0x0fff) == 128, "reference 400 accepted");
    check(t.rebuild(48000.0, 480.0, 0x0fff) == 128, "reference 480 accepted");
    check(t.rebuild(48000.0, 480.1, 0x0fff) == 0, "reference above 480 rejected");
    check(t.rebuild(std::numeric_limits<double>::quiet_NaN(), 440.0, 0x0fff) == 0, "NaN sample rate rejected");
    check(t.nearest(100.0) == -1, "empty table has no nearest entry");

    // Cache: unchanged inputs keep the table; a changed input rebuilds it.
    check(t.rebuild(44100.0, 440.0, cMajor) == 75 && t.rebuild(44100.0, 440.0, cMajor) == 75, "cache keeps an unchanged table");
    check(t.rebuild(44100.0, 440.0, 0x0fff) == 128, "changed mask rebuilds");
    check(std::abs(t.period(a4) - 44100.0 / 440.0) < 1e-9, "changed sample rate rebuilds periods");
}

static void periodDomainNearest() {
    const double sr = 48000.0;
    PeriodTable t; t.rebuild(sr, 440.0, 0x0fff);
    check(t.nearest(0.0) == -1 && t.nearest(-5.0) == -1 &&
          t.nearest(std::numeric_limits<double>::quiet_NaN()) == -1 &&
          t.nearest(std::numeric_limits<double>::infinity()) == -1, "invalid periods return -1");
    check(t.note(t.nearest(sr / 440.0)) == 69, "an exact A4 period maps to A4");
    check(t.note(t.nearest(1.0e6)) == 0 && t.note(t.nearest(1.0)) == 127, "out-of-range periods clamp to the table ends");

    // Boundary between A3 (57) and A#3 (58) is the ARITHMETIC MEAN of their periods.
    int i57 = -1; for (int i = 0; i < t.size(); ++i) if (t.note(i) == 57) i57 = i;
    const double p57 = t.period(i57), p58 = t.period(i57 + 1);
    const double mean = 0.5 * (p57 + p58);
    check(t.note(t.nearest(mean * 1.0001)) == 57, "just longer than the mean period -> lower note");
    check(t.note(t.nearest(mean * 0.9999)) == 58, "just shorter than the mean period -> higher note");
    // The default (cents) rule splits at the GEOMETRIC mean frequency. The harmonic mean implied by the
    // period domain sits ~0.63 cents below it, so a pitch 0.3 cents below the geometric midpoint is
    // already closer to the higher note here, and one 0.9 cents below is not.
    const double geo = std::sqrt((sr / p57) * (sr / p58));
    check(t.note(t.nearest(sr / (geo * std::pow(2.0, -0.3 / 1200.0)))) == 58, "0.3 cents below the geometric midpoint -> higher note");
    check(t.note(t.nearest(sr / (geo * std::pow(2.0, -0.9 / 1200.0)))) == 57, "0.9 cents below the geometric midpoint -> lower note");
}

struct Run { int target = -1; double cents = 0.0; int switches = 0; };

// Feed a harmonic tone; optionally alternate its pitch around a note boundary.
static Run run(bool testTune, double f0a, double f0b, double halfPeriodSeconds, double seconds) {
    const double rate = 48000.0; const int block = 64;
    RealtimeTuner t; t.prepare(rate, 1);
    Parameters p; p.speedMs = 0.0f; p.amount = 1.0f; p.mix = 1.0f; p.flexCents = 0.0f;
    p.vibratoPreservation = 0.0f; p.pitchClasses = 0x0fff; p.testTune = testTune;
    t.setParameters(p);
    std::vector<float> in(block), out(block); const float* src[1] = {in.data()}; float* dst[1] = {out.data()};
    double phase = 0.0; long n = 0; Run r; int last = -1;
    for (int b = 0; b < int(seconds * rate / block); ++b) {
        for (int i = 0; i < block; ++i, ++n) {
            const double ts = double(n) / rate;
            const bool high = f0b > 0.0 && (int(ts / halfPeriodSeconds) % 2 == 1);
            const double f0 = high ? f0b : f0a;
            phase += 2.0 * pi * f0 / rate;
            double s = 0.0;
            for (int h = 1; h <= 10; ++h) s += std::sin(h * phase + 0.3 * h) * (h <= 2 ? 0.5 : 1.0 / std::sqrt(double(h)));
            in[i] = float(0.2 * s);
        }
        t.process(src, dst, block, 1);
        if (double(n) / rate > 0.35) {
            const int tg = t.currentTargetNote();
            if (last >= 0 && tg >= 0 && tg != last) ++r.switches;
            if (tg >= 0) last = tg;
        }
    }
    r.target = t.currentTargetNote(); r.cents = t.appliedCents();
    return r;
}

static void engineBehavior() {
    Parameters defaults;
    check(!defaults.testTune, "testTune defaults to OFF");

    // 225 Hz is A3 (220 Hz) + 38.9 cents: both algorithms tune it down to A3 (MIDI 57).
    for (bool tt : {false, true}) {
        const Run r = run(tt, 225.0, 0.0, 0.0, 0.9);
        std::cout << (tt ? "testtune " : "standard ") << "steady 225 Hz -> target " << r.target << ", cents " << r.cents << "\n";
        check(r.target == 57, "steady off-pitch tone targets A3");
        check(std::abs(r.cents - (-38.9)) < 4.0, "applied correction is about -39 cents");
    }

    // Hover across the A3/A#3 boundary (about 226.5 Hz) with 12 ms excursions to each side.
    // The default algorithm holds its note through the hysteresis; testtune snaps every time.
    const double lo = 224.0, hi = 229.0;
    const Run standard = run(false, lo, hi, 0.012, 1.2);
    const Run test = run(true, lo, hi, 0.012, 1.2);
    std::cout << "boundary hover: standard switches " << standard.switches << ", testtune switches " << test.switches << "\n";
    check(test.switches > standard.switches, "testtune has no hysteresis: it switches targets more often than the default at a boundary");
    check(test.switches >= 8, "testtune snaps to the nearest note on each excursion");
}

int main() {
    tableMath();
    periodDomainNearest();
    engineBehavior();
    std::cout << "PASS: v3.3.2 testtune period table and algorithm switch\n";
    return 0;
}
