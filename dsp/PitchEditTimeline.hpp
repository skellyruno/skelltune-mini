#pragma once
// Fixed-capacity DAW-transport pitch edits. No allocations or locks on the
// audio thread; edits stored in milliseconds survive sample-rate changes.
#include <array>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <cstdint>
namespace skelly {
struct PitchEditRecord {
    int64_t startMs=-1, endMs=-1;
    int targetMidi=-1;
    bool valid() const noexcept {
        return startMs>=0 && endMs>startMs && endMs<=2147483647LL &&
               targetMidi>=0 && targetMidi<=127;
    }
};
class PitchEditTimeline {
public:
    static constexpr int kCapacity=48;
    static bool acceptable(PitchEditRecord r) noexcept {
        return r.valid() && r.endMs-r.startMs<=120000;
    }
    void set(int i,PitchEditRecord r) noexcept {
        if(i<0||i>=kCapacity)return;
        if(!acceptable(r))r={};
        const auto before=get(i);
        // A repeated editor state/clear request must not churn seqlocks
        // read by the realtime thread when the record is already equal.
        if(before.startMs==r.startMs && before.endMs==r.endMs &&
           before.targetMidi==r.targetMidi)return;
        const bool wasValid=before.valid();
        Slot& s=slots_[std::size_t(i)];
        s.seq.fetch_add(1,std::memory_order_acq_rel);
        s.start.store(r.startMs,std::memory_order_relaxed);
        s.end.store(r.endMs,std::memory_order_relaxed);
        s.note.store(r.targetMidi,std::memory_order_relaxed);
        s.seq.fetch_add(1,std::memory_order_release);
        if(wasValid != r.valid())
            activeCount_.fetch_add(r.valid()?1:-1,std::memory_order_release);
    }
    PitchEditRecord get(int i) const noexcept {
        if(i<0||i>=kCapacity)return {};
        const Slot& s=slots_[std::size_t(i)];
        const unsigned seq1=s.seq.load(std::memory_order_acquire);
        if(seq1&1u)return {};
        PitchEditRecord r{s.start.load(std::memory_order_relaxed),
                          s.end.load(std::memory_order_relaxed),
                          s.note.load(std::memory_order_relaxed)};
        const unsigned seq2=s.seq.load(std::memory_order_acquire);
        return seq1==seq2 && acceptable(r) ? r : PitchEditRecord{};
    }
    bool hasEdits() const noexcept {
        return activeCount_.load(std::memory_order_acquire)>0;
    }
    int targetAt(int64_t hostMs) const noexcept {
        // Unedited vocal: avoid scanning 48 atomics on every host buffer.
        if(hostMs<0 || activeCount_.load(std::memory_order_acquire)==0)return -1;
        for(int i=kCapacity-1;i>=0;--i) {
            const auto r=get(i);
            if(r.valid() && hostMs>=r.startMs && hostMs<r.endMs)
                return r.targetMidi;
        }
        return -1;
    }
    // Return first edit start/end strictly after the playhead. This lets
    // the DSP split a host buffer exactly at edit boundaries rather than
    // waiting for the next block, without per-sample 48-slot scanning.
    int64_t nextBoundaryAfter(int64_t hostMs) const noexcept {
        if(hostMs<0 || activeCount_.load(std::memory_order_acquire)==0)return -1;
        int64_t next=-1;
        for(int i=0;i<kCapacity;++i) {
            const auto r=get(i);
            if(!r.valid())continue;
            if(r.startMs>hostMs && (next<0||r.startMs<next))
                next=r.startMs;
            if(r.endMs>hostMs && (next<0||r.endMs<next))
                next=r.endMs;
        }
        return next;
    }
    // Message-thread undo of the latest occupied edit slot. Undo never
    // modifies an older automation parameter, preset or plugin identity.
    bool undoLast() noexcept {
        for(int i=kCapacity-1;i>=0;--i)
            if(get(i).valid()) {set(i,{});return true;}
        return false;
    }
    // Zero-allocation helper for precise mid-buffer retune edits. Edits
    // are in whole milliseconds, so boundary resolution is <=1ms; the
    // host buffer size must not delay the correction by another block.
    static int64_t frameMs(int64_t blockMs,int offset,double sampleRate) noexcept {
        // Reject broken host rates and requests beyond the bounded
        // persisted edit clock. Never cast NaN/inf/overflow to int64_t.
        if(blockMs<0 || blockMs>2147483647LL || offset<0 ||
           !std::isfinite(sampleRate) || !(sampleRate>0.0))return -1;
        const double position=double(blockMs)+
            std::floor(double(offset)*1000.0/sampleRate);
        if(!std::isfinite(position) || position>2147483647.0)return -1;
        return int64_t(position);
    }
    int framesUntilBoundary(int64_t blockMs,int offset,int remaining,
                            double sampleRate) const noexcept {
        if(remaining<=0)return 0;
        const auto now=frameMs(blockMs,offset,sampleRate);
        if(now<0)return remaining;
        const auto boundary=nextBoundaryAfter(now);
        if(boundary<=now)return remaining;
        // Bound the double->int conversion before casting, even when a
        // user edit lies days ahead of this playhead position.
        const double count=(double(boundary-blockMs)*sampleRate/1000.0)-
                            double(offset);
        if(!(count>0.0) || count>=double(remaining))return remaining;
        return std::clamp(int(std::ceil(count)),1,remaining);
    }
    int add(PitchEditRecord r) noexcept {
        if(!acceptable(r))return -1;
        // Keep the newest edit in the highest slot even after erasing
        // older ones. The old first-free-slot policy let an older
        // overlapping note override a freshly redrawn pitch blob.
        // Fixed 48-slot shift runs ONLY on the UI thread.
        for(int i=0;i<kCapacity-1;++i)set(i,get(i+1));
        set(kCapacity-1,r);
        return kCapacity-1;
    }
    bool eraseAt(int64_t hostMs) noexcept {
        if(hostMs<0)return false;
        for(int i=kCapacity-1;i>=0;--i){
            const auto r=get(i);
            if(r.valid()&&hostMs>=r.startMs&&hostMs<r.endMs){
                set(i,{});return true;
            }
        }
        return false;
    }
    void clear() noexcept {
        if(!hasEdits())return; // preserve realtime seqlocks on empty project
        for(int i=0;i<kCapacity;++i)set(i,{});
    }
private:
    struct Slot {
        std::atomic<unsigned> seq{0};
        std::atomic<int64_t> start{-1},end{-1};
        std::atomic<int> note{-1};
    };
    std::array<Slot,kCapacity> slots_{};
    // UI/state thread is the sole writer. Release follows the slot's
    // final even sequence number, so audio observes a published edit.
    std::atomic<int> activeCount_{0};
 };
} // namespace skelly
