#pragma once
// SkellyTune Realtime: original, causal pitch-correction proof-of-concept.
// Copyright 2026. See LICENSE. All audio-thread operations are allocation-free after prepare().
#include "NoteDecision.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace skelly {
static constexpr double kPi = 3.14159265358979323846;

struct Parameters {
    float speedMs = 25.0f;       // correction slew 1..200 ms
    float amount = 1.0f;         // 0..1
    float mix = 1.0f;            // 0..1
    float referenceA = 440.0f;   // Hz
    bool phaseAlignedJumps = true; // compare actual waveform cycles at shifter wraps
    float transientProtection = 1.0f; // 0..1, protect sibilants and attacks
    uint16_t pitchClasses = 0x0fff; // C=bit0, C#=bit1, ... B=bit11
};

struct PitchEstimate {
    float hertz = 0.0f;
    float confidence = 0.0f;
    bool valid = false;
};

class CausalPitchTracker {
public:
    void prepare(double rate) noexcept {
        factor_ = std::clamp(int(std::lround(rate / 12000.0)), 1, 16);
        reducedRate_ = rate / double(factor_);
        lowLag_ = std::clamp(int(reducedRate_ / 850.0), 2, 250);
        highLag_ = std::clamp(int(std::ceil(reducedRate_ / 65.0)), lowLag_ + 1, 500);
        window_ = std::clamp(int(std::round(reducedRate_ * 0.024)), 160, 384);
        hop_ = std::max(1, int(std::lround(64.0 / factor_)));
        // Causal FIR low-pass before decimation. 33 taps and no dynamically allocated state.
        constexpr int count = 33;
        double norm = 0.0;
        const double cutoff = 0.43 / double(factor_); // cycles/input sample
        for (int i = 0; i < count; ++i) {
            const int offset = i - count / 2;
            const double sinc = offset == 0 ? 2.0 * cutoff :
                std::sin(2.0 * kPi * cutoff * offset) / (kPi * offset);
            const double hann = 0.5 - 0.5 * std::cos(2.0 * kPi * i / double(count - 1));
            taps_[i] = float(sinc * hann);
            norm += taps_[i];
        }
        for (auto& t : taps_) t = float(t / norm);
        reset();
    }

    void reset() noexcept {
        samples_.fill(0.0f); fir_.fill(0.0f);
        firIndex_ = 0; write_ = 0; downCount_ = 0; decimated_ = 0;
        sinceUpdate_ = 0; estimate_ = {};
    }

    // Returns true only when a new estimate is available; the estimate may be unvoiced.
    bool push(float in, PitchEstimate& estimate) noexcept {
        if (!std::isfinite(in)) in = 0.0f;
        fir_[firIndex_] = in;
        firIndex_ = (firIndex_ + 1) % int(fir_.size());
        if (++downCount_ < factor_) return false;
        downCount_ = 0;
        float filtered = 0.0f;
        int pos = firIndex_;
        for (int i = 0; i < int(taps_.size()); ++i) {
            filtered += taps_[i] * fir_[pos];
            pos = (pos + 1) % int(fir_.size());
        }
        samples_[write_] = filtered;
        write_ = (write_ + 1) & (int(samples_.size()) - 1);
        ++decimated_;
        if (++sinceUpdate_ < hop_) return false;
        sinceUpdate_ = 0;
        estimate_ = analyze();
        estimate = estimate_;
        return true;
    }
    int hopHostSamples() const noexcept { return factor_ * hop_; }
    PitchEstimate last() const noexcept { return estimate_; }
    double reducedRate() const noexcept { return reducedRate_; }

private:
    float past(int lag) const noexcept {
        return samples_[(write_ - 1 - lag) & (int(samples_.size()) - 1)];
    }
    PitchEstimate analyze() noexcept {
        if (decimated_ < uint64_t(window_ + highLag_ + 3)) return {};
        double energy = 0.0;
        for (int i = 0; i < window_; ++i) {
            const double s = past(i);
            energy += s * s;
        }
        if (energy / window_ < 0.000004) return {};

        // YIN cumulative mean normalized difference function, computed on causal history.
        // We avoid a 2*N FFT and keep the history short. Worst-case bounded work/hop.
        difference_[0] = 0.0f;
        for (int lag = 1; lag <= highLag_; ++lag) {
            double diff = 0.0;
            for (int j = 0; j < window_; ++j) {
                const float a = past(j);
                const float b = past(j + lag);
                const float e = a - b;
                diff += double(e) * e;
            }
            difference_[lag] = float(diff);
        }
        double running = 0.0;
        cmnd_[0] = 1.0f;
        for (int lag = 1; lag <= highLag_; ++lag) {
            running += difference_[lag];
            cmnd_[lag] = float(difference_[lag] * lag / std::max(1e-20, running));
        }
        int best = -1;
        for (int lag = lowLag_; lag < highLag_ - 1; ++lag) {
            if (cmnd_[lag] < 0.16f && cmnd_[lag] <= cmnd_[lag+1]) {
                best = lag;
                break;
            }
        }
        if (best < 0) {
            best = lowLag_;
            for (int lag = lowLag_+1; lag < highLag_-1; ++lag)
                if (cmnd_[lag] < cmnd_[best]) best = lag;
        }
        const float confidence = std::clamp(1.0f - cmnd_[best], 0.0f, 1.0f);
        if (confidence < 0.62f) return {0.0f, confidence, false};
        // Quadratic peak interpolation in the CMND valley.
        double lag = best;
        if (best > 1 && best < highLag_-1) {
            const double a = cmnd_[best-1], b = cmnd_[best], c = cmnd_[best+1];
            const double denom = a - 2.0*b + c;
            if (std::abs(denom) > 1e-9) lag += std::clamp(0.5 * (a-c)/denom, -0.5, 0.5);
        }
        const float f0 = float(reducedRate_ / lag);
        return {f0, confidence, std::isfinite(f0) && f0 >= 65.0f && f0 <= 850.0f};
    }
    int factor_ = 4, lowLag_ = 14, highLag_ = 200, window_ = 288, hop_ = 16;
    int firIndex_ = 0, write_ = 0, downCount_ = 0, sinceUpdate_ = 0;
    uint64_t decimated_ = 0;
    double reducedRate_ = 12000.0;
    std::array<float,33> taps_{};
    std::array<float,33> fir_{};
    std::array<float,4096> samples_{};
    std::array<float,512> difference_{};
    std::array<float,512> cmnd_{};
    PitchEstimate estimate_{};
};

// Selects a coherent pitch-analysis signal without changing audio output.
// Mid-channel cancellation is a common failure mode for opposite-polarity stereo
// recordings; the original average(L,R) silently discarded the fundamental.
// Uses exponentially smoothed energy/covariance, no allocations or locks.
class StereoPitchRouter {
public:
    void prepare(double rate) noexcept {
        const double tau = 0.012;
        alpha_ = std::exp(-1.0 / (rate * tau));
        reset();
    }
    void reset() noexcept { leftPower_ = rightPower_ = covariance_ = 0.0; }
    float push(float left, float right) noexcept {
        const double k = 1.0 - alpha_;
        leftPower_ = alpha_ * leftPower_ + k * double(left)*left;
        rightPower_ = alpha_ * rightPower_ + k * double(right)*right;
        covariance_ = alpha_ * covariance_ + k * double(left)*right;
        // Near-inverted stereo will cancel in the mid signal. Use a single
        // channel for analysis, but preserve original stereo in the output.
        const double joint = std::sqrt(leftPower_*rightPower_);
        if (joint > 1.0e-12 && covariance_ < -0.60 * joint)
            return (leftPower_ >= rightPower_) ? left : right;
        // If almost all of the program exists on one channel, prefer it.
        if (leftPower_ > rightPower_ * 16.0) return left;
        if (rightPower_ > leftPower_ * 16.0) return right;
        return 0.5f * (left + right);
    }
private:
    double alpha_ = 0.998, leftPower_ = 0.0, rightPower_ = 0.0, covariance_ = 0.0;
};

// Original causal attack cue: combines fast/slow energy envelopes, a refractory
// counter, and a brief onset hold. It never alters the audio samples and never
// allocates. A sudden level increase is only used to speed note decisions when
// the pitch tracker independently produces a high-confidence new candidate.
class CausalOnsetCue {
public:
    void prepare(double rate) noexcept {
        fastA_ = 1.0 - std::exp(-1.0 / (rate * 0.002));
        slowA_ = 1.0 - std::exp(-1.0 / (rate * 0.045));
        holdLength_ = std::max(1, int(std::round(rate * 0.050)));
        refractoryLength_ = std::max(1, int(std::round(rate * 0.075)));
        reset();
    }
    void reset() noexcept {
        fast_ = slow_ = 0.0;
        hold_ = refractory_ = 0;
    }
    void push(float sample) noexcept {
        const double x = std::abs(std::isfinite(sample) ? sample : 0.0f);
        fast_ += fastA_ * (x - fast_);
        slow_ += slowA_ * (x - slow_);
        if (hold_ > 0) --hold_;
        if (refractory_ > 0) --refractory_;
        if (refractory_ == 0 && fast_ > 0.045 &&
            fast_ > 2.30 * (slow_ + 0.0010)) {
            hold_ = holdLength_;
            refractory_ = refractoryLength_;
        }
    }
    float score() const noexcept { return hold_ > 0 ? 1.0f : 0.0f; }
private:
    double fastA_ = 0.01, slowA_ = 0.001;
    double fast_ = 0.0, slow_ = 0.0;
    int hold_ = 0, refractory_ = 0, holdLength_ = 1, refractoryLength_ = 1;
};

// Per-sample causal voiced/noise discriminator. The high-pass energy ratio
// helps suppress pitch processing across broadband sibilants even when an
// earlier pitch estimate remains latched. These envelopes are not FFT based.
class TransientNoiseCue {
public:
    void prepare(double rate) noexcept {
        highpassA_ = std::exp(-2.0 * kPi * 1700.0 / rate);
        envelopeA_ = 1.0 - std::exp(-1.0 / (rate * 0.004));
        reset();
    }
    void reset() noexcept { lowpass_ = 0; highPower_ = lowPower_ = 0; }
    float push(float x) noexcept {
        lowpass_ = highpassA_ * lowpass_ + (1.0 - highpassA_) * x;
        const double h = x - lowpass_;
        highPower_ += envelopeA_ * (h*h - highPower_);
        lowPower_ += envelopeA_ * (x*x - lowPower_);
        const double ratio = highPower_ / (lowPower_ + 1e-8);
        // Continuous 0..1 broadband/unvoiced likelihood (starts near 0.45).
        return float(std::clamp((ratio - 0.45) * 3.8, 0.0, 1.0));
    }
private:
    double highpassA_ = 0, envelopeA_ = 0, lowpass_ = 0;
    double highPower_ = 0, lowPower_ = 0;
};

class RealtimeTuner {
public:
    // prepare() / reset() are called with the audio callback suspended.
    void prepare(double sampleRate, int maxChannels = 2) {
        rate_ = std::clamp(sampleRate, 8000.0, 192000.0);
        channels_ = std::clamp(maxChannels, 1, 2);
        // Fixed, nominal audio delay ~= 3 ms, rounded to integer samples.
        latency_ = int(std::lround(rate_ * 0.003));
        int capacity = 1;
        while (capacity < int(rate_ * 0.20) + 8192) capacity <<= 1;
        mask_ = capacity - 1;
        history_.assign(size_t(capacity) * size_t(channels_), 0.0f);
        tracker_.prepare(rate_);
        stereoRouter_.prepare(rate_);
        onsetCue_.prepare(rate_);
        noiseCue_.prepare(rate_);
        notes_.prepare(rate_);
        reset();
    }
    void reset() noexcept {
        std::fill(history_.begin(), history_.end(), 0.0f);
        write_ = 0;
        readDelay_ = latency_;
        jumpFrom_ = jumpTo_ = 0.0;
        jumpPosition_ = jumpLength_ = 0;
        cents_ = 0.0;
        desiredCents_ = 0.0;
        voiceMix_ = 0.0;
        f0_ = smoothF0_ = 220.0;
        hasF0_ = false;
        phaseJumpCount_ = 0;
        phaseJumpError_ = 0.0;
        mismatchBefore_ = mismatchAfter_ = 0.0;
        targetNote_ = -1;
        voiced_ = false;
        tracker_.reset();
        stereoRouter_.reset();
        onsetCue_.reset();
        noiseCue_.reset();
        notes_.reset();
        targetPending_ = false;
        params_ = {};
    }
    int latencySamples() const noexcept { return latency_; }
    PitchEstimate lastPitch() const noexcept { return tracker_.last(); }
    int currentTargetNote() const noexcept { return targetNote_; }
    bool targetPending() const noexcept { return targetPending_; }
    double appliedCents() const noexcept { return cents_; }
    uint64_t phaseAlignedJumpCount() const noexcept { return phaseJumpCount_; }
    double phaseMatchGainPercent() const noexcept {
        return mismatchBefore_ > 0.0 ? 100.0 * (1.0 - mismatchAfter_/mismatchBefore_) : 0.0;
    }
    double averageJumpMismatchBefore() const noexcept { return phaseJumpCount_ ? mismatchBefore_/phaseJumpCount_ : 0.0; }
    double averageJumpMismatchAfter() const noexcept { return phaseJumpCount_ ? mismatchAfter_/phaseJumpCount_ : 0.0; }
    double averageJumpPeriodError() const noexcept {
        return phaseJumpCount_ ? phaseJumpError_/double(phaseJumpCount_) : 0.0;
    }
    void setParameters(Parameters params) noexcept {
        params.speedMs = std::clamp(params.speedMs, 1.0f, 200.0f);
        params.amount = std::clamp(params.amount, 0.0f, 1.0f);
        params.mix = std::clamp(params.mix, 0.0f, 1.0f);
        params.referenceA = std::clamp(params.referenceA, 400.0f, 480.0f);
        params.pitchClasses &= 0x0fff;
        params.transientProtection = std::clamp(params.transientProtection, 0.0f, 1.0f);
        params_ = params;
    }
    // in/out may alias: each sample is captured before writing any output channels.
    // Planar buffers; sample-count and channel count may vary per audio callback.
    void process(const float* const* input, float* const* output, int frames, int numChannels) noexcept {
        if (!input || !output || frames <= 0 || history_.empty()) return;
        numChannels = std::clamp(numChannels, 1, channels_);
        const int capacity = mask_ + 1;
        const double smooth = std::exp(-1.0 / (rate_ * params_.speedMs * .001));
        const double gateSmooth = std::exp(-1.0 / (rate_ * 0.006));
        for (int i = 0; i < frames; ++i) {
            float in[2] = {0.0f, 0.0f};
            for (int c = 0; c < numChannels; ++c) {
                in[c] = std::isfinite(input[c][i]) ? input[c][i] : 0.0f;
                history_[size_t(c) * capacity + write_] = in[c];
            }
            const float trackingSample = (numChannels == 2) ? stereoRouter_.push(in[0], in[1]) : in[0];
            onsetCue_.push(trackingSample);
            const double noiseScore = noiseCue_.push(trackingSample);
            PitchEstimate e;
            if (tracker_.push(trackingSample, e)) {
                const auto result = notes_.update(e.valid ? e.hertz : 0.0,
                    e.valid ? e.confidence : 0.0, onsetCue_.score(),
                    uint32_t(tracker_.hopHostSamples()), params_.pitchClasses, params_.referenceA);
                targetNote_ = result.targetMidi;
                targetPending_ = result.pending;
                if (e.valid && targetNote_ >= 0 && !result.pending) {
                    f0_ = e.hertz;
                    if (!hasF0_ || std::abs(std::log2(f0_/smoothF0_)) > 0.65) {
                        smoothF0_ = f0_;
                    } else {
                        // Track pitch period smoothly, but still follow vibrato.
                        const double blend = 1.0 - std::exp(-double(tracker_.hopHostSamples())/(rate_*0.012));
                        smoothF0_ += blend*(f0_-smoothF0_);
                    }
                    hasF0_ = true;
                    const double targetHz = params_.referenceA *
                        std::pow(2.0, (targetNote_ - 69) / 12.0);
                    desiredCents_ = std::clamp(1200.0 * std::log2(targetHz / f0_), -1200.0, 1200.0);
                    voiced_ = true;
                } else {
                    // A new pitch candidate has not been accepted yet. The original
                    // engine forced this *new* input pitch toward the *old* target
                    // during the entire confirmation window (up to 65 ms), making
                    // transitions audibly wrong. Temporarily fade to delayed dry
                    // instead, then resume tuning after a confirmed new note.
                    // Also protects unvoiced consonants from stale correction.
                    voiced_ = false;
                    if (result.pending) desiredCents_ = 0.0;
                }
            }
            cents_ = cents_ * smooth + (desiredCents_ * params_.amount) * (1.0 - smooth);
            // A broadband attack (especially /s/ and /sh/) should not inherit
            // a stale tonal pitch ratio. Keep the dry stream time-aligned.
            const double noiseAttenuation = std::clamp(1.0 - double(params_.transientProtection) * noiseScore, 0.0, 1.0);
            const double targetGate = (voiced_ && targetNote_ >= 0) ? double(params_.mix) * noiseAttenuation : 0.0;
            voiceMix_ = voiceMix_ * gateSmooth + targetGate * (1.0 - gateSmooth);
            const double ratio = std::clamp(std::pow(2.0, cents_ / 1200.0), 0.5, 2.0);
            const double period = rate_ / std::clamp(smoothF0_, 65.0, 850.0);
            const double slope = 1.0 - ratio;
            // Pitch-synchronous variable-delay shifter: a continuous read head moves at
            // ratio * sampleRate; when it must wrap, switch by one input pitch period.
            // Pitch-period-sized resets preserve harmonic phase rather than resetting
            // at arbitrary grain boundaries. Crossfade over <= half a pitch period.
            const double lowDelay = 3.0;
            const double highDelay = lowDelay + period;
            const int nextJumpLength = std::clamp(int(std::round(period * 0.48)), 16, 768);
            if (jumpPosition_ < jumpLength_) {
                jumpFrom_ += slope;
                jumpTo_ += slope;
            } else {
                readDelay_ += slope;
                const double margin = std::abs(slope) * nextJumpLength + 0.5;
                if (slope < -1e-6 && readDelay_ < lowDelay + margin) {
                    jumpFrom_ = readDelay_;
                    jumpTo_ = readDelay_ + period;
                    if (params_.phaseAlignedJumps) jumpTo_ = choosePhaseAlignedDestination(jumpFrom_,jumpTo_,period);
                    jumpLength_ = nextJumpLength;
                    jumpPosition_ = 0;
                } else if (slope > 1e-6 && readDelay_ > highDelay + margin) {
                    jumpFrom_ = readDelay_;
                    jumpTo_ = readDelay_ - period;
                    if (params_.phaseAlignedJumps) jumpTo_ = choosePhaseAlignedDestination(jumpFrom_,jumpTo_,period);
                    jumpLength_ = nextJumpLength;
                    jumpPosition_ = 0;
                }
            }
            const bool jumping = jumpPosition_ < jumpLength_;
            const double jumpBlend = jumping ?
                (0.5-0.5*std::cos(kPi * (jumpPosition_+1.0)/jumpLength_)) : 0.0;
            const int dryIdx = (write_ - latency_) & mask_;
            for (int c = 0; c < numChannels; ++c) {
                const float* ring = history_.data() + size_t(c) * capacity;
                const float dry = ring[dryIdx];
                const float wet = jumping ?
                    float((1.0-jumpBlend)*interpolate(ring,jumpFrom_) +
                          jumpBlend*interpolate(ring,jumpTo_)) :
                    interpolate(ring,readDelay_);
                output[c][i] = float(dry * (1.0 - voiceMix_) + wet * voiceMix_);
                if (!std::isfinite(output[c][i])) output[c][i] = 0.0f;
            }
            if (jumping && ++jumpPosition_ == jumpLength_) {
                readDelay_ = jumpTo_;
            }
            write_ = (write_ + 1) & mask_;
        }
    }
private:
    // Search the neighboring source pitch period for coherent overlap. Exact
    // period prediction alone is insufficient on real voices with vibrato,
    // modest tracker error and waveform asymmetry. Search uses ONLY samples
    // already in the ring; no lookahead or audio-thread allocations.
    double choosePhaseAlignedDestination(double from, double nominal, double period) noexcept {
        const int maxAdjust = std::clamp(int(std::lround(period * 0.085)), 2, 32);
        const int length = std::clamp(int(std::lround(period * 0.12)), 12, 42);
        const int capacity = mask_ + 1;
        double bestScore = 1e40, best = nominal, baseline = -1.0, chosenMismatch = 0.0;
        for (int offset = -maxAdjust; offset <= maxAdjust; ++offset) {
            const double to = nominal + offset;
            if (to < 3.0 || to > capacity - 8.0) continue;
            double mismatch=0.0, power=1e-8;
            for (int k = 0; k < length; k+=2) {
                // We use recent history slightly behind the candidate position,
                // not the future overlap region, to choose the next read cycle.
                for (int ch=0; ch<channels_; ++ch) {
                    const float* ring=history_.data()+size_t(ch)*capacity;
                    const double a=interpolate(ring,from+k+3.0);
                    const double b=interpolate(ring,to+k+3.0);
                    const double d=a-b;
                    mismatch+=d*d;
                    power+=a*a+b*b;
                }
            }
            const double normalized = mismatch/power;
            if (offset == 0) baseline = normalized;
            const double normalizedOffset = double(offset)/double(maxAdjust);
            const double penalty = 0.028*normalizedOffset*normalizedOffset;
            const double score=normalized+penalty;
            if (score<bestScore) { bestScore=score; best=to; chosenMismatch=normalized; }
        }
        ++phaseJumpCount_;
        phaseJumpError_ += std::abs(best-nominal);
        mismatchBefore_ += std::max(0.0,baseline);
        mismatchAfter_ += chosenMismatch;
        return best;
    }
    float interpolate(const float* ring, double delay) const noexcept {
        // Four-point cubic Lagrange interpolation on the history ring.
        delay = std::clamp(delay, 2.0, double(mask_ - 3));
        const int d = int(std::floor(delay));
        const double u = delay - d;
        const float y0 = ring[(write_ - d + 1) & mask_];
        const float y1 = ring[(write_ - d) & mask_];
        const float y2 = ring[(write_ - d - 1) & mask_];
        const float y3 = ring[(write_ - d - 2) & mask_];
        const double a = -u*(u-1)*(u-2)/6.0;
        const double b = (u+1)*(u-1)*(u-2)/2.0;
        const double c = -(u+1)*u*(u-2)/2.0;
        const double e = (u+1)*u*(u-1)/6.0;
        return float(a*y0 + b*y1 + c*y2 + e*y3);
    }
    double rate_ = 48000.0;
    int channels_ = 2, latency_ = 144, mask_ = 0, write_ = 0;
    double readDelay_ = 144.0, jumpFrom_ = 0, jumpTo_ = 0;
    int jumpPosition_ = 0, jumpLength_ = 0;
    double cents_ = 0, desiredCents_ = 0, voiceMix_ = 0, f0_ = 220.0, smoothF0_ = 220.0;
    uint64_t phaseJumpCount_ = 0;
    double phaseJumpError_ = 0, mismatchBefore_ = 0, mismatchAfter_ = 0;
    bool hasF0_ = false;
    bool voiced_ = false;
    bool targetPending_ = false;
    int targetNote_ = -1;
    std::vector<float> history_;
    Parameters params_{};
    CausalPitchTracker tracker_;
    StereoPitchRouter stereoRouter_;
    CausalOnsetCue onsetCue_;
    TransientNoiseCue noiseCue_;
    skelly_research::NoteDecision notes_;
};
} // namespace skelly
