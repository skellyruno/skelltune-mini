#pragma once
// Fixed-capacity DAW-transport pitch edits. No allocations or locks on the
// audio thread; edits stored in milliseconds survive sample-rate changes.
#include <array>
#include <atomic>
#include <cstddef>
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
        Slot& s=slots_[std::size_t(i)];
        s.seq.fetch_add(1,std::memory_order_acq_rel);
        s.start.store(r.startMs,std::memory_order_relaxed);
        s.end.store(r.endMs,std::memory_order_relaxed);
        s.note.store(r.targetMidi,std::memory_order_relaxed);
        s.seq.fetch_add(1,std::memory_order_release);
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
    int targetAt(int64_t hostMs) const noexcept {
        if(hostMs<0)return -1;
        for(int i=kCapacity-1;i>=0;--i) {
            const auto r=get(i);
            if(r.valid() && hostMs>=r.startMs && hostMs<r.endMs)
                return r.targetMidi;
        }
        return -1;
    }
    int add(PitchEditRecord r) noexcept {
        if(!acceptable(r))return -1;
        for(int i=0;i<kCapacity;++i)if(!get(i).valid()){set(i,r);return i;}
        const int i=nextOverwrite_++%kCapacity;set(i,r);return i;
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
        for(int i=0;i<kCapacity;++i)set(i,{});
        nextOverwrite_=0;
    }
private:
    struct Slot {
        std::atomic<unsigned> seq{0};
        std::atomic<int64_t> start{-1},end{-1};
        std::atomic<int> note{-1};
    };
    std::array<Slot,kCapacity> slots_{};
    int nextOverwrite_=0; // writer (message/state thread) only
};
} // namespace skelly
