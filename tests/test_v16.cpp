#include "RealtimeTuner.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace skelly;
static void addTone(RealtimeTuner& e, double rate, double frequency, double seconds,
                    uint64_t& clock, int blockSize = 73) {
    const int total = int(rate*seconds);
    std::vector<float> input(size_t(blockSize),0), output(size_t(blockSize),0);
    for(int done=0; done<total;) {
        const int n=std::min(blockSize,total-done);
        for(int j=0;j<n;++j) input[size_t(j)] = frequency <= 0 ? 0.f :
            float(.3*std::sin(2*kPi*frequency*double(clock+j)/rate));
        const float* ptrIn[]={input.data()}; float* ptrOut[]={output.data()};
        e.process(ptrIn,ptrOut,n,1);
        for(int j=0;j<n;++j)assert(std::isfinite(output[size_t(j)]));
        done+=n;clock+=uint64_t(n);
    }
}

int main(){
    for(double rate: {44100.,48000.,96000.}) {
        // Silence interrupts the rolling history. A new note must recover
        // reliably without a stale accumulated difference from the old vowel.
        CausalPitchTracker tracker; tracker.prepare(rate);
        PitchEstimate estimate{};
        for(int i=0;i<int(rate*0.22);++i)
            tracker.push(float(.28*std::sin(2*kPi*110*i/rate)),estimate);
        for(int i=0;i<int(rate*0.35);++i)
            tracker.push(0.f,estimate);
        int valid=0, accurate=0;
        for(int i=0;i<int(rate*0.16);++i) {
            if(tracker.push(float(.30*std::sin(2*kPi*330*i/rate)),estimate) && estimate.valid) {
                ++valid;
                if(std::abs(1200.0*std::log2(estimate.hertz/330.0f)) < 25) ++accurate;
            }
        }
        assert(valid>10);
        assert(accurate>=int(valid*.90));
        RealtimeTuner e; e.prepare(rate,1);
        Parameters p; p.speedMs=25; p.mix=1; p.amount=1;
        e.setParameters(p);
        uint64_t time=0;
        addTone(e,rate,225.,.65,time);
        const double tuned=e.appliedCents();
        assert(std::abs(tuned)>10.0);
        addTone(e,rate,0.,.22,time);
        const double recovered=e.appliedCents();
        // Voice loss must erase the *desired* tuning so that the previous
        // syllable doesn't affect the next attack. Previous version kept it.
        assert(std::abs(recovered)<1.0);
        addTone(e,rate,329.,.40,time);
        assert(e.currentTargetNote()==64); // E4, newly confirmed
        assert(std::isfinite(e.appliedCents()));
        std::cout<<rate<<" Hz: re-entry "<<accurate<<"/"<<valid
            <<" estimates, correction "<<tuned<<" -> "<<recovered<<" cents\n";
    }
    std::cout<<"v1.6 voice re-entry and stale-correction regression passed\n";
}
