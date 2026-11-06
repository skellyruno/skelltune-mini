#include "RealtimeTuner.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

// Original, deterministic missing-fundamental/weak-fundamental regression.
// This is only a detector test, not a listening-quality or transport-latency test.
using namespace skelly;
enum class Timbre { Natural, WeakFundamental, MissingFundamental, WeakOdd, PureEven };
struct Count { int valid=0, correct=0, octaveHigh=0; double meanHz=0; };
static Count run(double rate, double fundamental, Timbre kind, bool noise=false) {
    CausalPitchTracker tracker; tracker.prepare(rate);
    PitchEstimate pitch;
    Count stats;
    uint32_t rng=123456789;
    const int total=int(rate*0.65);
    for (int i=0;i<total;++i) {
        const double t=double(i)/rate;
        const double s1=std::sin(2*kPi*fundamental*t);
        const double s2=std::sin(4*kPi*fundamental*t);
        const double s3=std::sin(6*kPi*fundamental*t);
        const double s4=std::sin(8*kPi*fundamental*t);
        double input=0.0;
        switch(kind){
        case Timbre::Natural:           input=.5*s1+.2*s2;break;
        case Timbre::WeakFundamental:   input=.04*s1+.40*s2+.30*s3;break;
        case Timbre::MissingFundamental:input=.40*s2+.34*s3+.20*s4;break;
        case Timbre::WeakOdd:           input=.06*s1+.43*s2+.10*s3+.36*s4;break;
        case Timbre::PureEven:          input=.45*s2+.30*s4;break;
        }
        if(noise) {
            rng=rng*1664525u+1013904223u;
            input+=.025*(double(int((rng>>16)&65535))-32768.0)/32768.0;
        }
        if (tracker.push(float(input),pitch) && i>int(rate*.25) && pitch.valid) {
            ++stats.valid;
            stats.meanHz+=pitch.hertz;
            const double cents=1200.0*std::log2(double(pitch.hertz)/fundamental);
            if (std::abs(cents)<25) ++stats.correct;
            if (std::abs(cents-1200.0)<25) ++stats.octaveHigh;
        }
    }
    if(stats.valid)stats.meanHz/=stats.valid;
    return stats;
}
int main(){
    int checks=0;
    for(double rate:{44100.0,48000.0,96000.0}) {
        for(double f:{75.,85.,95.,110.,147.,196.,220.,330.}) {
            for(Timbre timbre:{Timbre::Natural,Timbre::WeakFundamental,Timbre::MissingFundamental,Timbre::WeakOdd}) {
                const auto c=run(rate,f,timbre);
                assert(c.valid>100);
                if(c.correct < int(c.valid*.94)) {
                    std::cerr<<"Misidentified fundamental: "<<rate<<"/"<<f<<"/"<<int(timbre)
                             <<" correct "<<c.correct<<"/"<<c.valid<<" avg "<<c.meanHz<<"\n";
                    return 1;
                }
                ++checks;
            }
            // With only even harmonics, the acoustic fundamental cannot be
            // recovered uniquely. Never invent a confident octave-down note.
            const auto even=run(rate,f,Timbre::PureEven);
            assert(even.valid>100);
            assert(even.octaveHigh >= int(even.valid*.94));
            ++checks;
        }
        // Inject moderate deterministic noise into the weak-odd-harmonic case.
        for(double f:{85.,110.,220.}) {
            const auto noisy=run(rate,f,Timbre::WeakOdd,true);
            assert(noisy.valid>100);
            if (noisy.correct < int(noisy.valid*.85)) {
                std::cerr<<"Noisy weak-odd harmonic failed at "<<f<<" "<<rate<<" avg "<<noisy.meanHz<<"\n";
                return 1;
            }
            ++checks;
        }
    }
    // End-to-end: octave arbitration must reach the note-decision and audio
    // reconstruction layers, not just improve a standalone F0 number.
    for(bool cycleMode : {false, true}) {
        RealtimeTuner engine;
        engine.prepare(48000.0, 1);
        Parameters p; p.speedMs=20.0f;
        p.experimentalCycleSynthesis=cycleMode;
        engine.setParameters(p);
        float input[1]{}, output[1]{};
        const float* src[]={input};float* dst[]={output};
        for(int i=0;i<48000;++i) {
            const double t=double(i)/48000.0;
            const double f=112.5;
            input[0]=float(.06*std::sin(2*kPi*f*t)+.43*std::sin(4*kPi*f*t)
                +.10*std::sin(6*kPi*f*t)+.36*std::sin(8*kPi*f*t));
            engine.process(src,dst,1,1);
            assert(std::isfinite(output[0]));
        }
        assert(std::abs(engine.lastPitch().hertz-112.5f)<1.0f);
        assert(engine.currentTargetNote()==45);  // A2, not A3
        assert(std::abs(engine.appliedCents())>10.0);
        if(cycleMode) assert(engine.cycleSynthStatus().spawned>40);
        ++checks;
    }
    std::cout<<"v1.7 octave decision: "<<checks<<" voiced/harmonic and engine cases passed;"
             <<" pure-even ambiguity explicitly preserved.\n";
}
