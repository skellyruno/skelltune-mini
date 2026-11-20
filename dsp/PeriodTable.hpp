#pragma once
// PeriodTable: scale mask -> table of in-scale note PERIODS (samples per cycle).
//
// Used by the "testtune" algorithm. For every MIDI note n in [0,128) whose pitch class
// (n mod 12, C = 0) is enabled in the mask, the period is
//
//     period = sampleRate / (referenceA * 2^((n - 69) / 12))        (note 69 = A4)
//
// i.e. standard equal-temperament frequency expressed as samples per cycle. Entries are emitted
// in ascending note order, so periods are strictly DESCENDING. The nearest-note search is done in
// the period domain (linear in samples) rather than in log-frequency, so the boundary between two
// notes is the arithmetic mean of their periods (the harmonic mean of their frequencies), about
// 0.6 cents below the geometric midpoint used by the default engine.
//
// Accepted ranges (outside them the table is empty and the caller falls back to the default
// algorithm): sample rate 8000..384000 Hz (the range the engine already clamps to), and
// reference pitch 400..480 Hz (this plugin's own A4 parameter range).
//
// Real-time safe: fixed storage, no allocation. rebuild() is a no-op unless an input changed.
#include <array>
#include <cmath>
#include <cstdint>

namespace skelly {

class PeriodTable {
public:
    static constexpr double kMinSampleRate = 8000.0;
    static constexpr double kMaxSampleRate = 384000.0;
    static constexpr double kMinReference = 400.0;
    static constexpr double kMaxReference = 480.0;
    static constexpr int kSemitones = 12;
    static constexpr int kMaxEntries = 128;                     // MIDI notes 0..127

    // Returns the number of entries (0 when the inputs are outside the accepted ranges or the
    // mask enables no note). Cheap no-op when sample rate, reference and mask are unchanged.
    int rebuild(double sampleRate, double referenceA, uint16_t mask) noexcept {
        if (valid_ && sampleRate == rate_ && referenceA == reference_ && mask == mask_) return count_;
        valid_ = true; rate_ = sampleRate; reference_ = referenceA; mask_ = mask; count_ = 0;
        if (!(sampleRate >= kMinSampleRate && sampleRate <= kMaxSampleRate) ||
            !(referenceA >= kMinReference && referenceA <= kMaxReference))
            return 0;
        for (int note = 0; note < kMaxEntries; ++note) {
            if (((mask >> (note % kSemitones)) & 1u) == 0u) continue;
            const double exponent = double(note - 69) / 12.0;
            periods_[size_t(count_)] = sampleRate / (std::exp2(exponent) * referenceA);
            notes_[size_t(count_)] = note;
            ++count_;
        }
        return count_;
    }

    int size() const noexcept { return count_; }
    double period(int index) const noexcept { return periods_[size_t(index)]; }
    int note(int index) const noexcept { return notes_[size_t(index)]; }

    // Index of the table entry whose period is closest (in samples) to `periodSamples`, or -1
    // when the table is empty or the input is not a positive finite number.
    int nearest(double periodSamples) const noexcept {
        if (count_ <= 0 || !(periodSamples > 0.0) || !std::isfinite(periodSamples)) return -1;
        // periods_ is descending. Find the first index whose period is <= the input.
        int lo = 0, hi = count_;
        while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (periods_[size_t(mid)] > periodSamples) lo = mid + 1; else hi = mid;
        }
        if (lo == 0) return 0;
        if (lo == count_) return count_ - 1;
        const double longer = periods_[size_t(lo - 1)] - periodSamples;   // distance to the lower note
        const double shorter = periodSamples - periods_[size_t(lo)];       // distance to the higher note
        return longer <= shorter ? lo - 1 : lo;
    }

private:
    std::array<double, kMaxEntries> periods_{};
    std::array<int, kMaxEntries> notes_{};
    int count_ = 0;
    bool valid_ = false;
    double rate_ = 0.0, reference_ = 0.0;
    uint16_t mask_ = 0;
};

} // namespace skelly
