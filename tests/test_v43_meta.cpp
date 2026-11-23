// TEST ONLY (built with SKELLY_META_TEST=1). Tests the Meta engine port in dsp/MetaTest.hpp.
//  1. With the working detector and the detected-period smoother (the plugin's META defaults) a
//     steady detuned voice is tuned to the nearest enabled note.
//  2. The scale mask is respected (nearest ENABLED note, not nearest chromatic note).
//  3. Bypass is sample-exact with the SAME fixed host PDC; extreme input stays finite.
//  4. CHARACTERIZATION of the reconstruction AS WRITTEN: its detector finds (almost) no pitch and
//     its smoother wiring makes no correction. These two checks document the file's current
//     behaviour; delete or invert them if you fix the reconstruction.
#include "MetaTest.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

static void check(bool pass, const char* why) { if (!pass) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); } }
static constexpr double kPi = 3.14159265358979323846;
static constexpr double kRate = 48000.0;

static std::vector<float> voice(double hz, int frames) {
    std::vector<float> x(static_cast<size_t>(frames)); double ph = 0.0;
    for (int i = 0; i < frames; ++i) {
        ph += 2.0 * kPi * hz / kRate; double s = 0.0;
        for (int h = 1; h <= 10; ++h) s += std::sin(h * ph + 0.3 * h) * (h <= 2 ? 0.5 : 1.0 / std::sqrt(double(h)));
        x[size_t(i)] = float(0.2 * s);
    }
    return x;
}
// First normalized-autocorrelation peak within 5% of the best one, with parabolic interpolation.
static double pitchOf(const std::vector<float>& x, int s0, int s1) {
    std::vector<double> r; const int t0 = int(kRate / 700), t1 = int(kRate / 80); double best = -1.0;
    for (int t = t0; t < t1; ++t) {
        double c = 0, e0 = 0, e1 = 0;
        for (int i = s0; i + t < s1; ++i) { c += x[size_t(i)] * x[size_t(i + t)]; e0 += x[size_t(i)] * x[size_t(i)]; e1 += x[size_t(i + t)] * x[size_t(i + t)]; }
        r.push_back(c / std::sqrt(e0 * e1 + 1e-12)); best = std::max(best, r.back());
    }
    for (size_t k = 1; k + 1 < r.size(); ++k)
        if (r[k] >= 0.95 * best && r[k] >= r[k - 1] && r[k] >= r[k + 1]) {
            const double a = r[k - 1], b = r[k], c = r[k + 1], d = a - 2 * b + c, o = d != 0 ? 0.5 * (a - c) / d : 0.0;
            return kRate / (double(t0) + double(k) + o);
        }
    return 0.0;
}
static double cents(double a, double b) { return 1200.0 * std::log2(a / b); }

// Runs x through the engine in 128-frame blocks, advancing through the buffer.
static std::vector<float> run(skelly::MetaTest& m, std::vector<float> x, bool bypass = false) {
    std::vector<float> r = x;
    for (size_t i = 0; i + 128 <= x.size(); i += 128) { float* io[2] = {x.data() + i, r.data() + i}; m.process(io, 2, 128, bypass); }
    return x;
}

int main() {
    // 1. Detuned voices are tuned to A3 (chromatic).
    for (double detune : {30.0, -35.0, 12.0}) {
        skelly::MetaTest m; m.prepare(kRate); m.setParameters(0x0fff, 440.0, 1.0, 0.5);
        const double in = 220.0 * std::pow(2.0, detune / 1200.0);
        const auto out = run(m, voice(in, 96000));
        const double fo = pitchOf(out, 60000, 92000);
        std::cout << "chromatic: in " << detune << " ct -> out " << cents(fo, 220.0) << " ct from A3\n";
        check(std::abs(cents(fo, 220.0)) < 4.0, "META tunes a steady detuned voice to the nearest note");
    }
    // 2. Scale mask: C major (no A#). 235.8 Hz is A3 + 120 ct; nearest ENABLED note is B3 (246.94 Hz).
    {
        uint16_t cMajor = 0; for (int pc : {0, 2, 4, 5, 7, 9, 11}) cMajor |= uint16_t(1u << pc);
        skelly::MetaTest m; m.prepare(kRate); m.setParameters(cMajor, 440.0, 1.0, 0.5);
        const auto out = run(m, voice(220.0 * std::pow(2.0, 120.0 / 1200.0), 96000));
        const double fo = pitchOf(out, 60000, 92000), b3 = 440.0 * std::pow(2.0, (59.0 - 69.0) / 12.0);
        std::cout << "C major: A3+120ct -> " << fo << " Hz (B3 = " << b3 << ")\n";
        check(std::abs(cents(fo, b3)) < 4.0, "META respects the scale mask");
    }
    // 3. Bypass retains the declared host dry delay; zero input stays finite.
    {
        skelly::MetaTest m; m.prepare(kRate); m.setParameters(0x0fff, 440.0, 1.0, 0.5);
        const auto in = voice(223.8, 24000); auto out = run(m, in, true);
        const int delay=m.latencySamples();
        for(std::size_t i=0;i<in.size();++i) {
            const float expected=i>=std::size_t(delay)?in[i-std::size_t(delay)]:0.0f;
            check(out[i]==expected,
                  "bypass must remain sample-exact after fixed host PDC");
        }
        skelly::MetaTest n; n.prepare(kRate); n.setParameters(0x0fff, 440.0, 1.0, 0.5);
        std::vector<float> loud(48000); for (size_t i = 0; i < loud.size(); ++i) loud[i] = (i / 100) % 2 ? 1.0f : -1.0f;
        for (const auto& sig : {std::vector<float>(48000, 0.0f), loud}) {
            const auto o = run(n, sig);
            for (float v : o) check(std::isfinite(v), "output is always finite");
        }
    }
    // 4. CHARACTERIZATION of the reconstruction as written.
    {
        skelly::MetaTest m; m.prepare(kRate); m.setParameters(0x0fff, 440.0, 1.0, 0.5);
        m.setFileDetector(true);
        const auto x = voice(220.0, 96000); std::vector<float> y = x; int found = 0, hops = 0;
        for (size_t i = 0; i + 128 <= x.size(); i += 128) {
            float* io[2] = {y.data() + i, y.data() + i}; m.process(io, 2, 128);
            if (i > 24000) { ++hops; if (m.lastRawLag() > 0.0) ++found; }
        }
        std::cout << "file detector (as written) found a period in " << found << "/" << hops << " hops\n";
        check(found * 20 < hops, "CHARACTERIZATION: the reconstruction's own detector finds (almost) no pitch");

        skelly::MetaTest w; w.prepare(kRate); w.setParameters(0x0fff, 440.0, 1.0, 0.5); w.setSmoothDetected(false);
        const double in = 220.0 * std::pow(2.0, 30.0 / 1200.0);
        const auto out = run(w, voice(in, 96000));
        const double fo = pitchOf(out, 60000, 92000);
        std::cout << "smoother fed the snapped target (as written): in +30 ct -> out " << cents(fo, 220.0) << " ct\n";
        check(std::abs(cents(fo, in)) < 4.0, "CHARACTERIZATION: as written, the pitch ratio never uses the detected pitch (no correction)");
    }
    std::cout << "PASS: META engine tests\n";
    return 0;
}
