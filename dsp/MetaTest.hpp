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
#include "PhaseCrossfadeLut.hpp"
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
    // Retune speed now has true sample-rate-calibrated host milliseconds.

    MetaTest() { reset(); }

    void prepare(double sampleRate) {
        sr_ = static_cast<uint32_t>(sampleRate);
        hop_ = std::max<uint32_t>(32, static_cast<uint32_t>(sampleRate * 0.0027));
        // Preparing detector FIR coefficients is expensive; do it only
        // from prepareToPlay, NEVER from the audio callback's mode switch.
        tracker_.prepare(double(sr_));
        stereoRouter_.prepare(double(sr_));
        phaseCrossfade_.prepare(); // precompute Hann outside realtime process
        reset();
    }
    void reset() {
        n_ = fresh_ = 0; f1_ = f2_ = 0.0; curveRef_ = 1.0; w_ = 0;
        smooth_ = log_ = std::log2(200.0);
        raw_ = sel_ = conf_ = 0.0; period_ = 200.0; ratio_ = 1.0; target_ = 0.0;
        manualTargetMidi_ = -1;
        // Acquire from the first *detected* phrase, not a fixed 240Hz
        // startup period that can bend its leading vowel. A real unvoiced
        // pause can re-arm the same causal one-shot without allocations.
        rearmOnVoiced_ = true; unvoicedHops_ = 0;
        grainJumps_ = grainJumpCrossfades_ = 0;
        dryDelaySamples_ = std::max(1u, uint32_t(std::lround(double(sr_) * 0.003)));
        wetBlend_ = 0.0; detectedVoiced_ = false;
        voicedStableHops_ = 0;
        sourceRatio_ = 1.0; sourceBase_ = 0.0; grainGeometryValid_ = false;
        phaseAlignmentAdjustments_ = 0;
        phaseErrorBefore_ = phaseErrorAfter_ = 0.0;
        tracker_.reset(); stereoRouter_.reset(); est_ = {};
        for (int l = 0; l < kLanes; ++l) { pos_[l] = out_[l] = 0.0; fade_[l] = flen_[l] = 0; }
        std::memset(anal_.data(), 0, sizeof(float) * anal_.size());
        for (auto& r : ring_) std::memset(r.data(), 0, sizeof(float) * r.size());
        tableDirty_ = true;
        setFilter();
    }
    // mask: C=bit0..B=bit11. Speed is TRUE host milliseconds (0..200).
    void setParameters(uint16_t mask, double referenceA, double amount, double speed, double lowLatency = 0.0) {
        if (mask != mask_ || referenceA != reference_) tableDirty_ = true;
        mask_ = mask; reference_ = referenceA;
        amount_ = std::clamp(std::isfinite(amount) ? amount : 1.0, 0.0, 1.0);
        speed_ = std::clamp(std::isfinite(speed) ? speed : 25.0, 0.0, 200.0);
        lowLatency_ = std::clamp(std::isfinite(lowLatency) ? lowLatency : 0.0, 0.0, 1.0);
    }
    // Controlled note edit; independent of the normal period snap.
    void setManualTargetMidi(int midi) noexcept {
        manualTargetMidi_ = midi>=24 && midi<=107 ? midi : -1;
    }
    void setSmoothDetected(bool on) { smoothDetected_ = on; }
    void setFileDetector(bool on) { fileDetector_ = on; }
    // A/B test hook only; not a host parameter and never changed from
    // the audio thread. The safe default fades every grain read-head jump.
    void setJumpCrossfadeEnabled(bool on) { jumpCrossfadeEnabled_ = on; }
    // The legacy two-tap interpolator attenuated vocal harmonics at
    // fractional source-head positions. Use four-point causal Lagrange
    // interpolation by default; retain the old mode for measured A/B.
    void setHighQualityInterpolationEnabled(bool on) noexcept {
        highQualityInterpolation_ = on;
    }
    static float probeInterpolation(const float* ring, double position,
                                    bool highQuality) noexcept {
        return tap(ring, position, highQuality);
    }
    uint64_t grainJumps() const noexcept { return grainJumps_; }
    uint64_t grainJumpCrossfades() const noexcept { return grainJumpCrossfades_; }
    uint64_t phaseAlignmentAdjustments() const noexcept { return phaseAlignmentAdjustments_; }
    double cumulativePhaseMismatchBefore() const noexcept { return phaseErrorBefore_; }
    double cumulativePhaseMismatchAfter() const noexcept { return phaseErrorAfter_; }
    void setPhaseAlignedGrainsEnabled(bool on) noexcept { phaseAlignedGrains_ = on; }
    // Pure deterministic comparison probe for the exact same alignment
    // routine used by the live engine; requires a valid PCM history.
    static double probeAlignedHead(const float* history, double previousHead,
                                   double candidateHead, int radius,
                                   double youngestSafeHead) noexcept {
        return alignHead(history, previousHead, candidateHead, radius,
                         youngestSafeHead);
    }
    static double probePhaseMismatch(const float* history, double a,
                                     double b) noexcept {
        return phaseMismatch(history, a, b);
    }

    // Introspection for tests.
    double lastRawLag() const { return raw_; }
    double lastConfidence() const { return conf_; }
    double lastTargetPeriod() const { return sel_; }
    // Display helpers (valid once a pitch has been detected).
    double detectedHz() const { return raw_ > 0.0 ? double(sr_) / raw_ : 0.0; }
    double targetHz() const { return sel_ > 0.0 ? double(sr_) / sel_ : 0.0; }
    double shiftCents() const { return sel_ > 0.0 && period_ > 0.0 ? 1200.0 * std::log2(period_ / sel_) : 0.0; }
    double lastPeriod() const { return period_; }
    double retuneMs() const noexcept { return speed_; }
    // A true 1/e time constant in milliseconds, for any host sample rate.
    static double smoothingRetention(double speedMs, double rate,
                                     uint32_t elapsedSamples) noexcept {
        if (!(std::isfinite(speedMs) && speedMs > 0.0)) return 0.0;
        if (!(std::isfinite(rate) && rate > 0.0)) return 0.0;
        return std::exp(-double(elapsedSamples) /
                        (rate * (speedMs * 0.001)));
    }
    int tableCount() const { return count_; }

    void process(float* const* io, int lanes, int frames, bool bypass = false) {
        lanes = std::min(lanes, kLanes);
        if (tableDirty_) buildTable();
        const double grain = std::max(1.0, 2.0 - lowLatency_);
        // The grain's source age varies with pitch. Use the existing ring's
        // exact host-aligned 3ms dry tap for silence/consonants, with smooth
        // voiced-entry/exit transitions and no realtime allocations.
        const double entry = 1.0 - std::exp(-1.0 / (double(sr_) * 0.008));
        const double exit = 1.0 - std::exp(-1.0 / (double(sr_) * 0.004));
        // A few milliseconds of ratio slew avoid zippery resampler speed
        // changes when a new pitch frame or key arrives. The note decision
        // stays immediate, and the host's fixed 3ms PDC is unchanged.
        const double ratioAlpha = 1.0 - std::exp(-1.0 / (double(sr_) * 0.0025));
        const double baseAlpha = 1.0 - std::exp(-1.0 / (double(sr_) * 0.010));
        for (int i = 0; i < frames; ++i) {
            const uint32_t w = w_;
            const float input0 = lanes > 0 ? io[0][i] : 0.0f;
            // Host audio is untrusted: one NaN in the grain ring can poison
            // all future resampling heads, even when wetBlend is zero.
            // Keep bypass output untouched; sanitize only internal history
            // and the detector stream (same contract as RealtimeTuner).
            const float in0 = (std::isfinite(input0) &&
                std::abs(input0) >= 1.0e-30f) ? input0 : 0.0f;
            for (int l = 0; l < lanes; ++l) {
                const float value = io[l][i];
                ring_[l][w & kRingM] = (std::isfinite(value) &&
                    std::abs(value) >= 1.0e-30f) ? value : 0.0f;
            }
            if (bypass) {
                wetBlend_ = 0.0;
                detectedVoiced_ = false;
                rearmOnVoiced_ = true;
                voicedStableHops_ = 0;
                grainGeometryValid_ = false;
                w_ = w + 1;
                continue;
            }

            // The old META detector listened only to the left
            // channel. Right-heavy or polarity-inverted stereo vocals
            // could play audibly while META reported no pitch at all.
            // Match REGULAR's smooth mid/side rescue without changing
            // mono audio or the stereo synthesis/output signal.
            const float detectorSample = lanes == 2 ?
                stereoRouter_.push(in0, ring_[1][w & kRingM]) : in0;
            if (fileDetector_) {
                if (n_ < kRing - 1) anal_[n_++] = prefilter(detectorSample);
            } else {
                skelly::PitchEstimate e;
                if (tracker_.push(detectorSample, e)) est_ = e;
            }
            ++fresh_;
            analyse();

            const double pin = period_;
            const double pt = sel_ > 0.0 ? sel_ : pin;
            if (!(pin > 0.0) || !(pt > 0.0)) { w_ = w + 1; continue; }
            const double rawRatio = std::clamp(pin / pt, 0.5, 2.0);
            // The old 120-sample floor was YOUNGER than the advertised
            // 3ms dry tap at 48kHz and could read even younger on high
            // notes. Every cubic grain source must be older than PDC
            // plus interpolation margin, irrespective of note or rate.
            const double requiredBase = std::max(
                double(dryDelaySamples_) + kFloor + 4.0,
                grain * pt + kFloor);
            if (!grainGeometryValid_) {
                sourceRatio_ = rawRatio;
                sourceBase_ = requiredBase;
                grainGeometryValid_ = true;
            } else {
                sourceRatio_ += (rawRatio - sourceRatio_) * ratioAlpha;
                sourceBase_ += (requiredBase - sourceBase_) * baseAlpha;
            }
            const double srat = sourceRatio_;
            const double base = sourceBase_;
            const bool safeGeometry = base + pin + 16.0 < double(kRing);
            const bool wetReady = detectedVoiced_ && safeGeometry &&
                uint64_t(w) > uint64_t(dryDelaySamples_) +
                    uint64_t(std::ceil(base + pin + 4.0));
            double sharedPhaseAdjustment = 0.0;
            const double goal = wetReady ? amount_ : 0.0;
            wetBlend_ += (goal - wetBlend_) * (goal > wetBlend_ ? entry : exit);
            wetBlend_ = std::clamp(wetBlend_, 0.0, 1.0);
            // Release exactly to zero to avoid long-silence denormal
            // drift in an otherwise realtime-safe gain accumulator.
            if (wetBlend_ < 1.0e-9) wetBlend_ = 0.0;

            for (int l = 0; l < lanes; ++l) {
                if (flen_[l] == 0) {
                    pos_[l] = out_[l] = double(w) - base - pin;
                    flen_[l] = fade_[l] = static_cast<uint32_t>(pt);
                }
                const double d = double(w) - pos_[l];
                // The original reconstruction only crossfaded the very first
                // grain. Later one-period read-head jumps were *hard* switches
                // (k=1) and could create a large click at any pitch drift.
                // Carry the old head forward while blending toward the new
                // one over a bounded pitch period. Each lane remains lock-free.
                const bool jumpForward = d > base + pin;
                const bool jumpBackward = d < base;
                // Do not restart a crossfade while its old head is still
                // contributing: that was a second-order grain click when
                // detector periods moved rapidly near an in-scale boundary.
                if ((jumpForward || jumpBackward) && fade_[l] == 0) {
                    out_[l] = pos_[l];
                    const double oldHead = pos_[l];
                    const double proposed = pos_[l] + (jumpForward ? pin : -pin);
                    if (l == 0 && w > uint32_t(2.0 * (base + pin) + 64.0) &&
                        phaseAlignedGrains_) {
                        const int radius = std::clamp(int(std::lround(pt * 0.065)), 3, 12);
                        const double limit = double(w) - double(dryDelaySamples_) - 8.0;
                        const double selected = alignHead(ring_[0].data(),
                            oldHead, proposed, radius, limit);
                        sharedPhaseAdjustment = selected - proposed;
                        phaseErrorBefore_ += phaseMismatch(ring_[0].data(),
                                                           oldHead, proposed);
                        phaseErrorAfter_ += phaseMismatch(ring_[0].data(),
                                                           oldHead, selected);
                        if (std::abs(sharedPhaseAdjustment) > 0.1)
                            ++phaseAlignmentAdjustments_;
                    }
                    // One common phase offset for stereo; independent
                    // per-channel search would shift stereo width/phase.
                    pos_[l] = proposed + sharedPhaseAdjustment;
                    ++grainJumps_;
                    if (jumpCrossfadeEnabled_) {
                        const uint32_t length = uint32_t(std::clamp(
                            std::lround(pt), 16l, 512l));
                        flen_[l] = fade_[l] = length;
                        ++grainJumpCrossfades_;
                    } else {
                        flen_[l] = fade_[l] = 0;
                    }
                }
                if (pos_[l] > double(w)) pos_[l] = double(w);
                if (pos_[l] < double(w) - kRing + 8.0) pos_[l] = double(w) - kRing + 8.0;

                double k = 1.0;
                if (fade_[l]) {
                    // Same Hann phase trajectory, without an audio-thread cosine.
                    k = phaseCrossfade_.blend(int(flen_[l] - fade_[l]),
                                              int(flen_[l] + 1));
                    if (--fade_[l] == 0) out_[l] = pos_[l];
                }
                const double a = tap(ring_[l].data(), out_[l], highQualityInterpolation_);
                const double b = tap(ring_[l].data(), pos_[l], highQualityInterpolation_);
                const double wet = a + (b - a) * k;
                const float dry = ring_[l][(w - dryDelaySamples_) & kRingM];
                io[l][i] = static_cast<float>(dry * (1.0 - wetBlend_) +
                                              wet * wetBlend_);
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

    // --- calibrated physical millisecond smoother --------------------------------------
    double smoothStep(double target, uint32_t elapsedSamples) noexcept {
        const double alpha = smoothingRetention(speed_, double(sr_), elapsedSamples);
        smooth_ = target + alpha * (smooth_ - target);
        // Amount belongs to the wet/dry blend only. Never double-scale
        // the detected pitch period and create unpredictable response.
        return smooth_;
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
        // YIN occasionally loses one 2.7ms voiced frame on a legitimate
        // bright vowel. Instantly releasing the wet path on that frame
        // produced amplitude flutter and repeated 8ms attack fades.
        // Two confident frames arm wet; three misses disarm it.
        if (lag > 0.0 && conf_ >= 0.6) {
            voicedStableHops_ = std::min(voicedStableHops_ + 1, 3);
            if (voicedStableHops_ >= 2) detectedVoiced_ = true;
        } else {
            voicedStableHops_ = 0;
            if (unvoicedHops_ >= 2) detectedVoiced_ = false;
        }
        double target = log_;
        double detectedLog = log_;
        if (lag > 0.0) {
            const double lg = std::log2(lag);
            const double sl = manualTargetMidi_>=0 ?
                std::log2(double(sr_)/reference_)-double(manualTargetMidi_-69)/12.0 :
                snapLog(lg);
            // Do not start a newly detected vowel with the arbitrary
            // reset pitch. Preserve normal legato and short consonants.
            if (rearmOnVoiced_) {
                smooth_ = lg;
                period_ = lag;
                rearmOnVoiced_ = false;
                // Never restart a phrase on an old grain trajectory.
                // The dry lane has already taken over during the pause.
                grainGeometryValid_ = false;
                for (int lane = 0; lane < kLanes; ++lane)
                    fade_[lane] = flen_[lane] = 0;
            }
            unvoicedHops_ = 0;
            sel_ = std::exp2(sl);
            log_ = sl;
            target = sl;
            detectedLog = lg;
        } else {
            unvoicedHops_ = std::min<uint32_t>(unvoicedHops_ + 1, 1000000u);
            // The first re-entry after >=90ms of unvoiced audio is a
            // new phrase. Avoid recycling a stale smoother pitch.
            if (unvoicedHops_ * uint64_t(hop_) >=
                uint64_t(double(sr_) * 0.090)) {
                rearmOnVoiced_ = true;
                // Stop advertising the last pitched target through a
                // genuine unvoiced gap; leave grain continuity untouched.
                sel_ = 0.0;
            }
        }
        target_ = target;
        const double sm = smoothStep(smoothDetected_ ? detectedLog : target, hop);
        period_ = std::exp2(sm);
        ratio_ = std::exp2(log_ - sm);
        if (!fileDetector_) return;
        const int n = int(n_);
        if (n > int(hop)) { std::memmove(anal_.data(), anal_.data() + hop, size_t(n - int(hop)) * sizeof(float)); n_ = uint32_t(n - int(hop)); }
        else n_ = 0;
    }

    static double phaseMismatch(const float* r, double a, double b) noexcept {
        double diff = 0.0, energy = 1.0e-9;
        // Past-only 24-sample waveform comparison; no allocation, FFT,
        // locks or host calls on the realtime grain boundary.
        for (int i = 0; i < 12; ++i) {
            const double t = double(i) * 2.0;
            const double x = tap(r, a - t, true);
            const double y = tap(r, b - t, true);
            diff += (x - y) * (x - y);
            energy += x*x + y*y;
        }
        return diff / energy;
    }
    static double alignHead(const float* r, double previous,
                            double proposed, int radius,
                            double youngestSafe) noexcept {
        if (proposed > youngestSafe) return proposed;
        double best = proposed;
        const double originalError = phaseMismatch(r, previous, proposed);
        double bestError = originalError;
        radius = std::clamp(radius, 0, 16);
        for (int offset = -radius; offset <= radius; ++offset) {
            const double candidate = proposed + double(offset);
            if (candidate > youngestSafe || candidate < 30.0) continue;
            const double error = phaseMismatch(r, previous, candidate);
            // A modest geometric penalty prevents chasing a noise spike
            // away from a credible detector-provided pitch period.
            const double penalized = error + 0.001 * double(std::abs(offset));
            if (penalized < bestError) {
                bestError = penalized;
                best = candidate;
            }
        }
        // Require meaningful correlation improvement; random background
        // noise must not force jittery read-head offsets.
        if (best != proposed && bestError < originalError * 0.90)
            return best;
        return proposed;
    }
    static float tap(const float* r, double p, bool highQuality) noexcept {
        const double f = std::floor(p);
        const uint32_t i = static_cast<uint32_t>(static_cast<int64_t>(f)) & kRingM;
        const double t = p - f;
        if (!highQuality)
            return static_cast<float>(r[i] * (1.0 - t) +
                                      r[(i + 1) & kRingM] * t);
        // Four-point cubic Lagrange through (i-1,i,i+1,i+2).
        // This preserves vocal harmonics at fractional grain positions
        // better than two-point linear interpolation. All read taps are
        // older than the output's chosen causal source history.
        const double a = -t * (t - 1.0) * (t - 2.0) / 6.0;
        const double b = (t + 1.0) * (t - 1.0) * (t - 2.0) / 2.0;
        const double c = -(t + 1.0) * t * (t - 2.0) / 2.0;
        const double d = (t + 1.0) * t * (t - 1.0) / 6.0;
        return static_cast<float>(
            a * r[(i - 1u) & kRingM] + b * r[i] +
            c * r[(i + 1u) & kRingM] + d * r[(i + 2u) & kRingM]);
    }

    uint32_t sr_ = 48000, hop_ = 129, n_ = 0, fresh_ = 0, w_ = 0;
    uint16_t mask_ = 0x0fff; double reference_ = 440.0, amount_ = 1.0,
           speed_ = 25.0, lowLatency_ = 0.0;
    bool smoothDetected_ = true, fileDetector_ = false, tableDirty_ = true;
    bool rearmOnVoiced_ = true;
    bool jumpCrossfadeEnabled_ = true;
    bool highQualityInterpolation_ = true;
    PhaseCrossfadeLut phaseCrossfade_;
    uint32_t unvoicedHops_ = 0;
    uint32_t dryDelaySamples_ = 144;
    double wetBlend_ = 0.0;
    int manualTargetMidi_ = -1;
    bool detectedVoiced_ = false;
    int voicedStableHops_ = 0;
    double sourceRatio_ = 1.0, sourceBase_ = 0.0;
    bool grainGeometryValid_ = false, phaseAlignedGrains_ = true;
    uint64_t phaseAlignmentAdjustments_ = 0;
    double phaseErrorBefore_ = 0.0, phaseErrorAfter_ = 0.0;
    uint64_t grainJumps_ = 0, grainJumpCrossfades_ = 0;
    skelly::CausalPitchTracker tracker_;
    skelly::StereoPitchRouter stereoRouter_;
    skelly::PitchEstimate est_{};
    double f1_ = 0, f2_ = 0, c50_ = 0.99, c70_ = 0.2, curveRef_ = 1.0;
    double raw_ = 0, sel_ = 0, log_ = 0, period_ = 200, ratio_ = 1, target_ = 0, conf_ = 0, smooth_ = 0;
    double pos_[kLanes]{}, out_[kLanes]{}; uint32_t fade_[kLanes]{}, flen_[kLanes]{};
    std::array<float, kRing> anal_{};
    std::array<std::array<float, kRing>, kLanes> ring_{};
    std::array<double, kMaxEntry> tbl_{}; int count_ = 0;
    std::array<double, kRing> m_{};
};

} // namespace skelly
