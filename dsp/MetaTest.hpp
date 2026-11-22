#pragma once
// ============================================================================================
//  MetaTest.hpp  --  TEST-ONLY. NOT FOR RELEASE.
//
//  A port of the user's own "metatune.cpp" reconstruction into a compiling, self-contained engine
//  so it can be A/B tested next to the standard algorithm. It keeps the reconstruction's pipeline:
//
//     pre-filter -> period detector -> in-scale period table -> log-domain snap -> smoother
//                -> two-tap grain resampler (delay-line pitch shifter with one-period jumps)
//
//  It deliberately drops the reconstruction's binary-layout scaffolding (object offsets, padded
//  structs, parameter hash IDs), which is not needed for audio and did not compile as pasted.
//
//  This header refuses to compile unless SKELLY_META_TEST is defined, so a normal/release build can
//  never pick it up by accident.
//
//  Two things in the reconstruction do not work as written (see the probe results in the changelog),
//  so both are selectable. The plugin's META switch uses the working options (the defaults here):
//    detector:      fileDetector = false : the engine's causal pitch tracker finds the period (default).
//                   fileDetector = true  : the reconstruction's own detector, as written (finds no pitch).
//    smoother feed: smoothDetected = true  : the smoother is fed the DETECTED period, so the resample
//                                            ratio is detected/target (default; an assumption).
//                   smoothDetected = false : AS WRITTEN. It is fed the snapped TARGET period, which
//                                            makes the resample ratio ~1, i.e. no correction.
// ============================================================================================
#if !defined(SKELLY_META_TEST) || !SKELLY_META_TEST
#error "MetaTest.hpp is test-only. Define SKELLY_META_TEST=1 to build it. Never ship it."
#endif

#include "RealtimeTuner.hpp"   // CausalPitchTracker (optional working detector)
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace skelly {

class MetaTest {
public:
    static constexpr double kPi = 3.14159265358979323846264338327950288;
    static constexpr int kLanes = 2, kRing = 8192, kRingM = kRing - 1;
    static constexpr int kOctaves = 7, kSemis = 12, kMaxEntry = kOctaves * kSemis;   // 84
    static constexpr double kBaseDly = 120.0, kFloor = 8.0;
    static constexpr double kSrMin = 8000.0, kSrMax = 384000.0, kRefMin = 300.0, kRefMax = 600.0;
    static constexpr double kNoteBias = 24.0, kRefNote = 69.0;
    static constexpr double kCurveMul = 1.1, kExtremaMin = -0.1, kSecondary = 0.01;
    static constexpr double kReject = -DBL_MAX;
    static constexpr double kSpan = 2.0, kSpanNeg = -0.5;

    MetaTest() { reset(); }

    void prepare(double sampleRate) {
        sr_ = static_cast<uint32_t>(sampleRate);
        hop_ = std::max<uint32_t>(32, static_cast<uint32_t>(sampleRate * 0.0027));
        reset();
    }
    void reset() {
        n_ = fresh_ = 0; f1_ = f2_ = 0.0; curveRef_ = 1.0; w_ = 0;
        smooth_ = log_ = std::log2(200.0);
        raw_ = sel_ = conf_ = 0.0; period_ = 200.0; ratio_ = 1.0; target_ = 0.0;
        tracker_.prepare(double(sr_)); est_ = {};
        for (int l = 0; l < kLanes; ++l) { pos_[l] = out_[l] = 0.0; fade_[l] = flen_[l] = 0; }
        std::memset(anal_.data(), 0, sizeof(float) * anal_.size());
        for (auto& r : ring_) std::memset(r.data(), 0, sizeof(float) * r.size());
        tableDirty_ = true;
        setFilter();
    }
    // mask: C = bit 0 ... B = bit 11. speed is the reconstruction's own unit (default 0.5).
    void setParameters(uint16_t mask, double referenceA, double amount, double speed, double lowLatency = 0.0) {
        if (mask != mask_ || referenceA != reference_) tableDirty_ = true;
        mask_ = mask; reference_ = referenceA; amount_ = amount; speed_ = speed; lowLatency_ = lowLatency;
    }
    void setSmoothDetected(bool on) { smoothDetected_ = on; }
    void setFileDetector(bool on) { fileDetector_ = on; }

    // Introspection for tests.
    double lastRawLag() const { return raw_; }
    double lastConfidence() const { return conf_; }
    double lastTargetPeriod() const { return sel_; }
    // Display helpers (valid once a pitch has been detected).
    double detectedHz() const { return raw_ > 0.0 ? double(sr_) / raw_ : 0.0; }
    double targetHz() const { return sel_ > 0.0 ? double(sr_) / sel_ : 0.0; }
    double shiftCents() const { return sel_ > 0.0 && period_ > 0.0 ? 1200.0 * std::log2(period_ / sel_) : 0.0; }
    double lastPeriod() const { return period_; }
    int tableCount() const { return count_; }

    void process(float* const* io, int lanes, int frames, bool bypass = false) {
        lanes = std::min(lanes, kLanes);
        if (tableDirty_) buildTable();
        const double grain = std::max(1.0, 2.0 - lowLatency_);
        for (int i = 0; i < frames; ++i) {
            const uint32_t w = w_;
            const float in0 = lanes > 0 ? io[0][i] : 0.0f;
            for (int l = 0; l < lanes; ++l) ring_[l][w & kRingM] = io[l][i];
            if (bypass) { w_ = w + 1; continue; }

            if (fileDetector_) {
                if (n_ < kRing - 1) anal_[n_++] = prefilter(in0);
            } else {
                skelly::PitchEstimate e;
                if (tracker_.push(in0, e)) est_ = e;
            }
            ++fresh_;
            analyse();

            const double pin = period_;
            const double pt = sel_ > 0.0 ? sel_ : pin;
            if (!(pin > 0.0) || !(pt > 0.0)) { w_ = w + 1; continue; }
            const double srat = pin / pt;
            const double base = std::max(kBaseDly, grain * pt + kFloor);

            for (int l = 0; l < lanes; ++l) {
                if (flen_[l] == 0) {
                    pos_[l] = out_[l] = double(w) - base - pin;
                    flen_[l] = fade_[l] = static_cast<uint32_t>(pt);
                }
                const double d = double(w) - pos_[l];
                if (d > base + pin)      { out_[l] = pos_[l]; pos_[l] += pin; }
                else if (d < base)       { out_[l] = pos_[l]; pos_[l] -= pin; }
                if (pos_[l] > double(w)) pos_[l] = double(w);
                if (pos_[l] < double(w) - kRing + 8.0) pos_[l] = double(w) - kRing + 8.0;

                double k = 1.0;
                if (fade_[l]) {
                    const double g = 1.0 - double(fade_[l]) / double(flen_[l] + 1);
                    k = 0.5 - 0.5 * std::cos(kPi * g);
                    if (--fade_[l] == 0) out_[l] = pos_[l];
                }
                const double a = tap(ring_[l].data(), out_[l]);
                const double b = tap(ring_[l].data(), pos_[l]);
                io[l][i] = static_cast<float>(a + (b - a) * k);
                out_[l] += srat;
                pos_[l] += srat;
            }
            w_ = w + 1;
        }
    }

private:
    // --- pre-filter (as written in the reconstruction) -------------------------------------
    void setFilter() {
        const double sr = double(sr_);
        const double hp = 60.0, lp = std::min(2000.0, sr * 0.45);
        c50_ = 1.0 / (1.0 + 2.0 * kPi * hp / sr);
        const double k = 2.0 * kPi * lp / sr;
        c70_ = k / (1.0 + k);
    }
    float prefilter(float x) {
        const double h = c50_ * (double(x) - f1_);
        f1_ += h;
        f2_ += c70_ * (h - f2_);
        return static_cast<float>(f2_);
    }

    // --- in-scale period table (samples per cycle), descending ----------------------------
    void buildTable() {
        tableDirty_ = false; count_ = 0;
        const double srv = double(sr_);
        if (!(srv >= kSrMin && srv <= kSrMax) || !(reference_ >= kRefMin && reference_ <= kRefMax)) return;
        for (int oct = 0; oct < kOctaves; ++oct)
            for (int semi = 0; semi < kSemis; ++semi) {
                if (((mask_ >> semi) & 1u) == 0u) continue;
                const double e = (oct * 12.0 + kNoteBias + semi - kRefNote) / 12.0;
                tbl_[size_t(count_++)] = srv / (std::exp2(e) * reference_);
            }
    }
    // nearest enabled entry in the LOG domain
    double snapLog(double lg) const {
        if (count_ == 0) return lg;
        int lo = 0, hi = count_;
        while (lo < hi) {
            const int mid = (lo + hi) >> 1;
            if (std::log2(tbl_[size_t(mid)]) > lg) lo = mid + 1; else hi = mid;
        }
        const int a = lo ? lo - 1 : 0, b = std::min(lo, count_ - 1);
        const double la = std::log2(tbl_[size_t(a)]), lb = std::log2(tbl_[size_t(b)]);
        return std::fabs(la - lg) <= std::fabs(lb - lg) ? la : lb;
    }

    // --- detector (as written) --------------------------------------------------------------
    double detect() {
        const int n = int(n_);
        const float* x = anal_.data();
        if (n < 64) return 0.0;
        const int tmin = std::max(2, int(sr_ / 4000.0));
        const int tmax = std::min(n - 1, int(sr_ / 32.0));
        if (tmax <= tmin + 4) return 0.0;
        const double xn = x[n - 1];
        double sum = xn, best = kReject;
        for (int t = tmin; t <= tmax; ++t) {
            sum += x[n - 1 - t];
            const double v = (sum != 0.0) ? (t * xn / sum) : kReject;
            m_[size_t(t)] = (v < kExtremaMin) ? kReject : v;
            if (m_[size_t(t)] > best) best = m_[size_t(t)];
        }
        if (best == kReject) { conf_ = 0.0; return 0.0; }
        const double thr = curveRef_ * kCurveMul + kSecondary;
        int pick = -1;
        for (int t = tmin + 1; t < tmax; ++t) {
            if (m_[size_t(t)] == kReject) continue;
            if (m_[size_t(t)] >= thr && m_[size_t(t)] >= m_[size_t(t - 1)] && m_[size_t(t)] >= m_[size_t(t + 1)]) { pick = t; break; }
        }
        if (pick < 0) { conf_ = 0.0; return 0.0; }
        const double a = m_[size_t(pick - 1)], c = m_[size_t(pick + 1)], den = a - 2.0 * m_[size_t(pick)] + c;
        const double off = (den != 0.0) ? 0.5 * (a - c) / den : 0.0;
        conf_ = m_[size_t(pick)];
        if (conf_ > curveRef_) curveRef_ = conf_;
        return pick + std::max(-0.5, std::min(0.5, off));
    }

    // --- smoother (as written) --------------------------------------------------------------
    double smoothStep(double target, double subhop) {
        const double old = smooth_;
        const double z = std::fabs(subhop + speed_);
        const double alpha = 1.0 - 1.0 / (1.0 + z);
        const double nv = target + alpha * (old - target);
        smooth_ = nv;
        return old + (nv - old) * amount_ * speedScale_;
    }

    void analyse() {
        if (fresh_ < hop_) return;
        const uint32_t hop = fresh_;
        fresh_ = 0;
        double lag;
        if (fileDetector_) {
            lag = detect();
        } else {
            const bool ok = est_.valid && est_.confidence >= 0.6f && est_.hertz > 0.0f;
            lag = ok ? double(sr_) / double(est_.hertz) : 0.0;
            conf_ = ok ? double(est_.confidence) : 0.0;
        }
        raw_ = lag;
        double target = log_;
        double detectedLog = log_;
        if (lag > 0.0) {
            const double lg = std::log2(lag);
            const double sl = snapLog(lg);
            sel_ = std::exp2(sl);
            log_ = sl;
            target = sl;
            detectedLog = lg;
        }
        target_ = target;
        const double sub = double(hop) / double(hop_) * kSpan + kSpanNeg;
        const double sm = smoothStep(smoothDetected_ ? detectedLog : target, sub);
        period_ = std::exp2(sm);
        ratio_ = std::exp2(log_ - sm);
        if (!fileDetector_) return;
        const int n = int(n_);
        if (n > int(hop)) { std::memmove(anal_.data(), anal_.data() + hop, size_t(n - int(hop)) * sizeof(float)); n_ = uint32_t(n - int(hop)); }
        else n_ = 0;
    }

    static double tap(const float* r, double p) {
        const double f = std::floor(p);
        const uint32_t i = static_cast<uint32_t>(static_cast<int64_t>(f)) & kRingM;
        const double fr = p - f;
        return r[i] * (1.0 - fr) + r[(i + 1) & kRingM] * fr;
    }

    uint32_t sr_ = 48000, hop_ = 129, n_ = 0, fresh_ = 0, w_ = 0;
    uint16_t mask_ = 0x0fff; double reference_ = 440.0, amount_ = 1.0, speed_ = 0.5, speedScale_ = 1.0, lowLatency_ = 0.0;
    bool smoothDetected_ = true, fileDetector_ = false, tableDirty_ = true;
    skelly::CausalPitchTracker tracker_; skelly::PitchEstimate est_{};
    double f1_ = 0, f2_ = 0, c50_ = 0.99, c70_ = 0.2, curveRef_ = 1.0;
    double raw_ = 0, sel_ = 0, log_ = 0, period_ = 200, ratio_ = 1, target_ = 0, conf_ = 0, smooth_ = 0;
    double pos_[kLanes]{}, out_[kLanes]{}; uint32_t fade_[kLanes]{}, flen_[kLanes]{};
    std::array<float, kRing> anal_{};
    std::array<std::array<float, kRing>, kLanes> ring_{};
    std::array<double, kMaxEntry> tbl_{}; int count_ = 0;
    std::array<double, kRing> m_{};
};

} // namespace skelly
