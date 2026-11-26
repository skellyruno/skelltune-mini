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
    // 3+ low-note periods can exceed 8192 samples at 192/384kHz.
    // Keep enough preallocated PCM history for causal cubic grains at
    // every advertised sample rate. The legacy analysis buffers stay small.
    static constexpr int kLanes = 2, kRing = 32768, kRingM = kRing - 1;
    static constexpr int kAnalysisRing = 8192;
    static constexpr int kOctaves = 7, kSemis = 12, kMaxEntry = kOctaves * kSemis;   // 84
    static constexpr double kBaseDly = 120.0, kFloor = 8.0;
    static constexpr double kSrMin = 8000.0, kSrMax = 384000.0, kRefMin = 300.0, kRefMax = 600.0;
    static constexpr double kNoteBias = 24.0, kRefNote = 69.0;
    static constexpr double kCurveMul = 1.1, kExtremaMin = -0.1, kSecondary = 0.01;
    static constexpr double kReject = -DBL_MAX;
    // Retune speed now has true sample-rate-calibrated host milliseconds.

    MetaTest() { reset(); }

    void prepare(double sampleRate, double dryLatencyMs = 3.0) {
        // Reject NaN, infinity, negative and out-of-engine sample rates
        // before converting to an unsigned integer or preparing the tracker.
        const double safeRate=std::clamp(
            std::isfinite(sampleRate)?sampleRate:48000.0,kSrMin,kSrMax);
        sr_ = static_cast<uint32_t>(std::lround(safeRate));
        dryLatencyMs_ = std::clamp(
            std::isfinite(dryLatencyMs) ? dryLatencyMs : 3.0, 1.5, 3.0);
        hop_ = std::max<uint32_t>(32, static_cast<uint32_t>(double(sr_) * 0.0027));
        // Preparing detector FIR coefficients is expensive; do it only
        // from prepareToPlay, NEVER from the audio callback's mode switch.
        // exp2 factors are independent of A4 and song key. Prepare
        // once outside the realtime callback before host automation.
        if(!periodFactorsReady_) {
            for(int midi=0;midi<kMaxEntry;++midi)
                periodFactors_[std::size_t(midi)] =
                    std::exp2((kRefNote-(double(midi)+kNoteBias))/12.0);
            periodFactorsReady_=true;
        }
        // Fixed sample-rate coefficients belong to prepare(), not every
        // host sub-block (which can be split at manual pitch-edit edges).
        entryAlpha_ = 1.0 - std::exp(-1.0 / (double(sr_) * 0.008));
        exitAlpha_ = 1.0 - std::exp(-1.0 / (double(sr_) * 0.004));
        ratioAlpha_ = 1.0 - std::exp(-1.0 / (double(sr_) * 0.0025));
        baseAlpha_ = 1.0 - std::exp(-1.0 / (double(sr_) * 0.010));
        transientMeterAlpha_=1.0-std::exp(-1.0/(double(sr_)*0.030));
        transientRelease_=std::exp(-1.0/(double(sr_)*0.003));
        transientEntry_=1.0-std::exp(-1.0/(double(sr_)*0.00012));
        attackHoldSamples_=int(std::lround(double(sr_)*0.002));
        attackAdvanceSamples_=unsigned(std::max(1l,std::lround(double(sr_)*0.00075)));
        tracker_.prepare(double(sr_));
        stereoRouter_.prepare(double(sr_));
        phaseCrossfade_.prepare(); // precompute Hann outside realtime process
        reset();
    }
    void reset() {
        n_ = fresh_ = 0; f1_ = f2_ = 0.0; curveRef_ = 1.0; w_ = 0;
        wasBypassed_ = false;
        resetAttacks();
        attackMarks_.fill(0);
        smooth_ = log_ = std::log2(200.0);
        smoothedCorrectionLog_ = 0.0;
        raw_ = sel_ = conf_ = lastTrustedLag_ = 0.0; period_ = 200.0; ratio_ = 1.0; target_ = 0.0;
        manualTargetMidi_ = -1;
        // Acquire from the first *detected* phrase, not a fixed 240Hz
        // startup period that can bend its leading vowel. A real unvoiced
        // pause can re-arm the same causal one-shot without allocations.
        rearmOnVoiced_ = true; unvoicedHops_ = 0;
        grainJumps_ = grainJumpCrossfades_ = 0;
        // Same fixed dry/transient transport as REGULAR + TESTTUNE.
        // Keep this setting across META's per-mode reset() calls.
        dryDelaySamples_ = std::max(
            1u, uint32_t(std::lround(double(sr_) * dryLatencyMs_ * 0.001)));
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
    void setParameters(uint16_t mask, double referenceA, double amount, double speed, double lowLatency = 0.0, double mix = 1.0) {
        // Defend against untrusted host state and standalone A/B inputs.
        // The 12-bit chromatic mask must never read reserved bits.
        mask=uint16_t(mask & 0x0fffu);
        if(!std::isfinite(referenceA) || referenceA<kRefMin ||
           referenceA>kRefMax)referenceA=440.0;
        if (mask != mask_ || referenceA != reference_) tableDirty_ = true;
        mask_ = mask; reference_ = referenceA;
        amount_ = std::clamp(std::isfinite(amount) ? amount : 1.0, 0.0, 1.0);
        mix_ = std::clamp(std::isfinite(mix) ? mix : 1.0, 0.0, 1.0);
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
                                   double youngestSafeHead,
                                   const float* right=nullptr) noexcept {
        return alignHead(history, previousHead, candidateHead, radius,
                         youngestSafeHead,nullptr,nullptr,right);
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
    double detectedHz() const {
        // UI-only: a single unreliable 2.7ms tracker hop must not flash
        // pitch to NO TARGET while the wet-voice lock stays engaged.
        // Never return a held pitch on silence or after voiced release.
        const double lag=raw_>0.0?raw_:
            (detectedVoiced_?lastTrustedLag_:0.0);
        return lag>0.0?double(sr_)/lag:0.0;
    }
    double targetHz() const { return (count_ > 0 || manualTargetMidi_ >= 0) && sel_ > 0.0 ? double(sr_) / sel_ : 0.0; }
    double shiftCents() const { return sel_ > 0.0 && period_ > 0.0 ? 1200.0 * std::log2(period_ / sel_) : 0.0; }
    // Meter/graph estimate of the AUDIO actually heard: the slewed source
    // ratio multiplied by the present wet contribution. Unlike shiftCents(),
    // this returns zero on bypass, dry Mix/Amount, or before voice acquisition.
    // Blended dry/wet pitch is not a single pure frequency; this is a UI estimate.
    double estimatedOutputCorrectionCents() const noexcept {
        if (!(wetBlend_ > 0.0) || !(sourceRatio_ > 0.0) ||
            !std::isfinite(sourceRatio_)) return 0.0;
        return 1200.0 * std::log2(sourceRatio_) * wetBlend_ * (1.0-attackDryBlend_);
    }
    double lastPeriod() const { return period_; }
    double retuneMs() const noexcept { return speed_; }
    int latencySamples() const noexcept { return int(dryDelaySamples_); }
    uint64_t processedSamples() const noexcept { return w_; }
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    void setSampleClockForUnitTests(uint64_t frame) noexcept {
        w_=frame;
        grainGeometryValid_=false;
        wetBlend_=0.0;
        for(int l=0;l<kLanes;++l)fade_[l]=flen_[l]=0;
    }
#endif
    // A true 1/e time constant in milliseconds, for any host sample rate.
    static double smoothingRetention(double speedMs, double rate,
                                     uint32_t elapsedSamples) noexcept {
        if (!(std::isfinite(speedMs) && speedMs > 0.0)) return 0.0;
        if (!(std::isfinite(rate) && rate > 0.0)) return 0.0;
        return std::exp(-double(elapsedSamples) /
                        (rate * (speedMs * 0.001)));
    }
    int tableCount() const { return count_; }

    void process(float* const* io, int lanes, int frames, bool bypass = false, uint8_t* pitchValid=nullptr) {
        // A host can probe an unprepared instance with zero frames or
        // channels. Reject missing planar pointers before dereferencing.
        if(frames<=0 || lanes<=0 || io==nullptr)return;
        lanes=std::min(lanes,kLanes);
        for(int c=0;c<lanes;++c)if(io[c]==nullptr)return;
        if (bypass && !wasBypassed_) {
            // Host bypass is still a plugin audio path: preserve the reported
            // 1.5ms transport delay instead of moving dry audio 72 samples
            // early relative to REGULAR, TESTTUNE, and DAW compensation.
            raw_=sel_=conf_=lastTrustedLag_=0.0;
            est_={}; wetBlend_=0.0; detectedVoiced_=false;
            rearmOnVoiced_=true; voicedStableHops_=0;
            unvoicedHops_=0; grainGeometryValid_=false;
            n_=fresh_=0; f1_=f2_=0.0;
            tracker_.reset(); stereoRouter_.reset();
            resetAttacks();
        }
        wasBypassed_=bypass;
        if (!bypass && tableDirty_) buildTable();
        const double grain = std::max(1.0, 2.0 - lowLatency_);
        // The grain's source age varies with pitch. Use the existing ring's
        // exact host-aligned 3ms dry tap for silence/consonants, with smooth
        // voiced-entry/exit transitions and no realtime allocations.
        const double entry = entryAlpha_;
        const double exit = exitAlpha_;
        // A few milliseconds of ratio slew avoid zippery resampler speed
        // changes when a new pitch frame or key arrives. The note decision
        // stays immediate, and the host's fixed 3ms PDC is unchanged.
        const double ratioAlpha = ratioAlpha_;
        const double baseAlpha = baseAlpha_;
        for (int i = 0; i < frames; ++i) {
            // No uint32 rollover at ~24h on long live sessions.
            if(pitchValid) pitchValid[i]=0;
            const uint64_t w = w_;
            const float input0 = lanes > 0 ? io[0][i] : 0.0f;
            // Host audio is untrusted: one NaN in the grain ring can poison
            // all future resampling heads, even when wetBlend is zero.
            // Sanitize internal history and maintain a delayed dry bypass,
            // matching the fixed host PDC in all three test algorithms.
            const float in0 = (std::isfinite(input0) &&
                std::abs(input0) >= 1.0e-30f) ? input0 : 0.0f;
            for (int l = 0; l < lanes; ++l) {
                const float value = io[l][i];
                ring_[l][w & kRingM] = (std::isfinite(value) &&
                    std::abs(value) >= 1.0e-30f) ? value : 0.0f;
            }
            if (bypass) {
                attackMarks_[w & kRingM]=0;
                for(int l=0;l<lanes;++l)
                    io[l][i] = ring_[l][(w - dryDelaySamples_) & kRingM];
                w_ = w + 1;
                continue;
            }

            // Preserve isolated consonant/attack anchors using the existing
            // dry transport history. A stricter relative derivative and energy
            // threshold than REGULAR/TESTTUNE protects the approved META
            // vocal tone; no extra audio lookahead.
            double derivative=0.0, power=0.0;
            for(int l=0;l<lanes;++l) {
                const double sample=ring_[l][w & kRingM];
                derivative=std::max(derivative,std::abs(sample-attackPrevious_[l]));
                power=std::max(power,sample*sample);
                attackPrevious_[l]=sample;
            }
            const double diffPower=derivative*derivative;
            const bool attack=diffPower>std::max({0.0625,3.0*attackInputPower_,64.0*attackDiffPower_});
            attackInputPower_+=transientMeterAlpha_*(std::min(power,4.0)-attackInputPower_);
            attackDiffPower_+=transientMeterAlpha_*(std::min(diffPower,1.0)-attackDiffPower_);
            if(attack)attackHold_=attackHoldSamples_;
            attackMarks_[w & kRingM]=attackHold_>0?1:0;
            if(attackHold_>0)--attackHold_;
            const auto dryIndex=(w-dryDelaySamples_)&kRingM;
            const unsigned advance=std::min(attackAdvanceSamples_,dryDelaySamples_>2?dryDelaySamples_-2:0u);
            if(attackMarks_[dryIndex])attackDryBlend_=1.0;
            else if(advance && attackMarks_[(dryIndex+advance)&kRingM])
                attackDryBlend_+=(1.0-attackDryBlend_)*transientEntry_;
            else attackDryBlend_*=transientRelease_;
            if(attackDryBlend_<1.0e-9)attackDryBlend_=0.0;

            // The old META detector listened only to the left
            // channel. Right-heavy or polarity-inverted stereo vocals
            // could play audibly while META reported no pitch at all.
            // Match REGULAR's smooth mid/side rescue without changing
            // mono audio or the stereo synthesis/output signal.
            const float detectorSample = lanes == 2 ?
                stereoRouter_.push(in0, ring_[1][w & kRingM]) : in0;
            if (fileDetector_) {
                if (n_ < kAnalysisRing - 1) anal_[n_++] = prefilter(detectorSample);
            } else {
                skelly::PitchEstimate e;
                if (tracker_.push(detectorSample, e)) est_ = e;
            }
            ++fresh_;
            analyse();
            if(pitchValid) pitchValid[i]=raw_>0.0 || (detectedVoiced_ && lastTrustedLag_>0.0);

            const double pin = period_;
            const double pt = sel_ > 0.0 ? sel_ : pin;
            // Grain wraps are in measured INPUT cycles, not retune-
            // smoothed periods. Otherwise a 25/100ms pitch change wraps
            // into unrelated phase and generates a brief extra tone.
            const double cycle = lastTrustedLag_ > 0.0 ? lastTrustedLag_ : pin;
            if (!(pin > 0.0) || !(pt > 0.0)) { w_ = w + 1; continue; }
            // Amount changes the correction in cents, not the wet/dry gain.
            // The full-amount path retains its exact previous arithmetic.
            const double fullRatio = std::clamp(pin / pt, 0.5, 2.0);
            const double rawRatio = amount_ == 1.0 ? fullRatio :
                std::exp2(amount_ * std::log2(fullRatio));
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
            const bool safeGeometry = base + cycle + 16.0 < double(kRing);
            const bool wetReady = detectedVoiced_ && safeGeometry &&
                uint64_t(w) > uint64_t(dryDelaySamples_) +
                    uint64_t(std::ceil(base + cycle + 4.0));
            double sharedPhaseAdjustment = 0.0;
            // A disabled scale must not resample an otherwise in-tune voice.
            // Explicit graph edits can still supply a target with no scale.
            const bool enabled = (count_ > 0 || manualTargetMidi_ >= 0) && amount_ > 0.0;
            const double goal = wetReady && enabled ? mix_ : 0.0;
            wetBlend_ += (goal - wetBlend_) * (goal > wetBlend_ ? entry : exit);
            wetBlend_ = std::clamp(wetBlend_, 0.0, 1.0);
            // Release exactly to zero to avoid long-silence denormal
            // drift in an otherwise realtime-safe gain accumulator.
            if (wetBlend_ < 1.0e-9) wetBlend_ = 0.0;

            const double audibleWet=wetBlend_*(1.0-attackDryBlend_);
            for (int l = 0; l < lanes; ++l) {
                if (flen_[l] == 0) {
                    pos_[l] = out_[l] = double(w) - base - cycle;
                    flen_[l] = fade_[l] = static_cast<uint32_t>(pt);
                }
                const double d = double(w) - pos_[l];
                // The original reconstruction only crossfaded the very first
                // grain. Later one-period read-head jumps were *hard* switches
                // (k=1) and could create a large click at any pitch drift.
                // Carry the old head forward while blending toward the new
                // one over a bounded pitch period. Each lane remains lock-free.
                const bool jumpForward = d > base + cycle;
                const bool jumpBackward = d < base;
                // Do not restart a crossfade while its old head is still
                // contributing: that was a second-order grain click when
                // detector periods moved rapidly near an in-scale boundary.
                if ((jumpForward || jumpBackward) && fade_[l] == 0) {
                    out_[l] = pos_[l];
                    const double oldHead = pos_[l];
                    const double proposed = pos_[l] + (jumpForward ? cycle : -cycle);
                    if (l == 0 && w > uint64_t(2.0 * (base + cycle) + 64.0) &&
                        phaseAlignedGrains_) {
                        const int radius = std::clamp(int(std::lround(pt * 0.065)), 3, 12);
                        const double limit = double(w) - double(dryDelaySamples_) - 8.0;
                        double before=0.0, after=0.0;
                        const double selected = alignHead(ring_[0].data(),
                            oldHead, proposed, radius, limit,&before,&after,
                            lanes == 2 ? ring_[1].data() : nullptr);
                        sharedPhaseAdjustment = selected - proposed;
                        // Reuse identical phase comparisons from alignment.
                        phaseErrorBefore_ += before;
                        phaseErrorAfter_ += after;
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
                // Cubic interpolation uses two younger taps. Both the
                // outgoing and incoming grain must remain causal, even if
                // a large target jump moves the smoothed base underneath
                // an active crossfade. Keep older taps in ring history too.
                const double youngest = double(w) - double(dryDelaySamples_) - 4.0;
                const double oldest = double(w) - double(kRing) + 8.0;
                pos_[l] = std::clamp(pos_[l], oldest, youngest);
                out_[l] = std::clamp(out_[l], oldest, youngest);

                double k = 1.0;
                if (fade_[l]) {
                    // Same Hann phase trajectory, without an audio-thread cosine.
                    k = phaseCrossfade_.blend(int(flen_[l] - fade_[l]),
                                              int(flen_[l] + 1));
                    if (--fade_[l] == 0) out_[l] = pos_[l];
                }
                const double b = tap(ring_[l].data(), pos_[l], highQualityInterpolation_);
                // Only evaluate the outgoing head while it contributes.
                // At unity blend the old head is redundant; no waveform or
                // phase offset changes, but most samples need one cubic tap.
                const double a = k == 1.0 ? b :
                    tap(ring_[l].data(), out_[l], highQualityInterpolation_);
                const double wet = a + (b - a) * k;
                const float dry = ring_[l][(w - dryDelaySamples_) & kRingM];
                io[l][i] = static_cast<float>(dry * (1.0 - audibleWet) +
                                              wet * audibleWet);
                out_[l] += srat;
                pos_[l] += srat;
            }
            w_ = w + 1;
        }
    }

private:
    void resetAttacks() noexcept {
        attackPrevious_.fill(0.0);attackInputPower_=attackDiffPower_=attackDryBlend_=0.0;attackHold_=0;
    }
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
        if (!(srv >= kSrMin && srv <= kSrMax) ||
            !(reference_ >= kRefMin && reference_ <= kRefMax)) return;
        // Direct portable calls without prepare() still work, while the
        // normal JUCE path always precomputes on the setup thread.
        if(!periodFactorsReady_) {
            for(int midi=0;midi<kMaxEntry;++midi)
                periodFactors_[std::size_t(midi)] =
                    std::exp2((kRefNote-(double(midi)+kNoteBias))/12.0);
            periodFactorsReady_=true;
        }
        const double base=srv/reference_;
        for (int oct = 0; oct < kOctaves; ++oct)
            for (int semi = 0; semi < kSemis; ++semi) {
                if (((mask_ >> semi) & 1u) == 0u) continue;
                const int midi=oct*kSemis+semi;
                const double period=base*periodFactors_[std::size_t(midi)];
                tbl_[std::size_t(count_)]=period;
                logTbl_[std::size_t(count_)]=std::log2(period);
                ++count_;
            }
    }
    // nearest enabled entry in the LOG domain
    double snapLog(double lg) const {
        if (count_ == 0) return lg;
        int lo = 0, hi = count_;
        while (lo < hi) {
            const int mid = (lo + hi) >> 1;
            if (logTbl_[size_t(mid)] > lg) lo = mid + 1; else hi = mid;
        }
        const int a = lo ? lo - 1 : 0, b = std::min(lo, count_ - 1);
        const double la = logTbl_[size_t(a)], lb = logTbl_[size_t(b)];
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
            lastTrustedLag_ = lag; // trusted period for both audio and display
            voicedStableHops_ = std::min(voicedStableHops_ + 1, 3);
            if (voicedStableHops_ >= 2) detectedVoiced_ = true;
        } else {
            voicedStableHops_ = 0;
        }
        double target = log_;
        // A short invalid detector hop is NOT evidence that the input
        // pitch has reached the target. Hold the last measured period
        // instead of slewing toward the snapped period (a spurious unity
        // excursion during the audible wet-release envelope).
        double detectedLog = lastTrustedLag_ > 0.0
            ? std::log2(lastTrustedLag_) : smooth_;
        if (lag > 0.0) {
            const double lg = std::log2(lag);
            const double sl = manualTargetMidi_>=0 ?
                std::log2(double(sr_)/reference_)-double(manualTargetMidi_-69)/12.0 :
                snapLog(lg);
            // Do not start a newly detected vowel with the arbitrary
            // reset pitch. Preserve normal legato and short consonants.
            if (rearmOnVoiced_) {
                smooth_ = lg;
                smoothedCorrectionLog_ = lg-sl;
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
            // Release exactly on the third consecutive unvoiced hop.
            if(unvoicedHops_>=3)detectedVoiced_=false;
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
        // A manual edit is an explicit host note: it may begin exactly
        // during a brief pitch-detector miss. Apply it at the next
        // detector hop while the previously recognized voice remains
        // locked, never through released consonants/silence.
        if(lag<=0.0 && manualTargetMidi_>=0 && detectedVoiced_) {
            const double sl=std::log2(double(sr_)/reference_)-
                            double(manualTargetMidi_-69)/12.0;
            sel_=std::exp2(sl);
            log_=target=sl;
        }
        target_ = target;
        const double sm = smoothStep(smoothDetected_ ? detectedLog : target, hop);
        // Smooth the correction, not the detected pitch: changing input
        // notes should not inherit an obsolete source period in the ratio.
        const double requestedCorrection = detectedLog-log_;
        const double retention = smoothingRetention(speed_,double(sr_),hop);
        smoothedCorrectionLog_ = requestedCorrection + retention *
            (smoothedCorrectionLog_ - requestedCorrection);
        const double effectiveLog = smoothDetected_ && speed_ > 0.0
            ? log_+smoothedCorrectionLog_ : sm;
        period_ = std::exp2(effectiveLog);
        ratio_ = std::exp2(log_ - effectiveLog);
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
                            double youngestSafe,
                            double* before=nullptr,double* after=nullptr,
                            const float* right=nullptr) noexcept {
        // Cache the outgoing grain's exact cubic samples once. All
        // candidate offsets share them, including stereo phase scoring.
        // The arithmetic order and mono/stereo weights stay identical.
        std::array<std::array<float,2>,12> outgoing{};
        const int lanes=right?2:1;
        const float* histories[2]={r,right};
        for(int i=0;i<12;++i)
            for(int lane=0;lane<lanes;++lane)
                outgoing[size_t(i)][size_t(lane)]=tap(histories[lane],previous-double(i)*2.0,true);
        const auto mismatch = [&](double b) noexcept {
            double diff=0.0, energy=1.0e-9;
            for(int i=0;i<12;++i)
                for(int lane=0;lane<lanes;++lane) {
                    const double x=outgoing[size_t(i)][size_t(lane)];
                    const double y=tap(histories[lane],b-double(i)*2.0,true);
                    diff+=(x-y)*(x-y); energy+=x*x+y*y;
                }
            return diff/energy;
        };
        // One set of comparisons supplies both head choice and mismatch
        // telemetry. The strict phase-improvement gate is unchanged.
        const double originalError = mismatch(proposed);
        if(before)*before=originalError;
        if(after)*after=originalError;
        if (proposed > youngestSafe) return proposed;
        double best = proposed;
        double bestError = originalError, bestRawError=originalError;
        radius = std::clamp(radius, 0, 16);
        for (int offset = -radius; offset <= radius; ++offset) {
            const double candidate = proposed + double(offset);
            if (candidate > youngestSafe || candidate < 30.0) continue;
            const double error = mismatch(candidate);
            const double penalized = error + 0.001 * double(std::abs(offset));
            if (penalized < bestError) {
                bestError = penalized;
                bestRawError=error;
                best = candidate;
            }
        }
        if(best != proposed && bestError < originalError * 0.90) {
            if(after)*after=bestRawError;
            return best;
        }
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

    std::array<uint8_t,kRing> attackMarks_{};
    std::array<double,2> attackPrevious_{};
    double attackInputPower_=0.0,attackDiffPower_=0.0,attackDryBlend_=0.0;
    double transientMeterAlpha_=0.0006942,transientRelease_=0.9930796,transientEntry_=0.1593763;
    unsigned attackAdvanceSamples_=36;
    int attackHoldSamples_=96,attackHold_=0;
    uint32_t sr_ = 48000, hop_ = 129, n_ = 0, fresh_ = 0;
    uint64_t w_ = 0;
    uint16_t mask_ = 0x0fff; double reference_ = 440.0, amount_ = 1.0,
           speed_ = 25.0, lowLatency_ = 0.0, mix_ = 1.0;
    bool smoothDetected_ = true, fileDetector_ = false, tableDirty_ = true;
    bool rearmOnVoiced_ = true, wasBypassed_ = false;
    double entryAlpha_ = 1.0 - std::exp(-1.0 / (48000.0 * 0.008));
    double exitAlpha_ = 1.0 - std::exp(-1.0 / (48000.0 * 0.004));
    double ratioAlpha_ = 1.0 - std::exp(-1.0 / (48000.0 * 0.0025));
    double baseAlpha_ = 1.0 - std::exp(-1.0 / (48000.0 * 0.010));
    bool jumpCrossfadeEnabled_ = true;
    bool highQualityInterpolation_ = true;
    PhaseCrossfadeLut phaseCrossfade_;
    uint32_t unvoicedHops_ = 0;
    uint32_t dryDelaySamples_ = 144;
    double dryLatencyMs_ = 3.0;
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
    double lastTrustedLag_ = 0.0, smoothedCorrectionLog_=0.0;
    double pos_[kLanes]{}, out_[kLanes]{}; uint32_t fade_[kLanes]{}, flen_[kLanes]{};
    std::array<float, kAnalysisRing> anal_{};
    std::array<std::array<float, kRing>, kLanes> ring_{};
    std::array<double, kMaxEntry> tbl_{},logTbl_{},periodFactors_{};
    bool periodFactorsReady_=false;
    int count_ = 0;
    std::array<double, kAnalysisRing> m_{};
};

} // namespace skelly
