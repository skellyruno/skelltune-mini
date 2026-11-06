#include "RealtimeTuner.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>
using namespace skelly;

struct Counts {int valid=0, correct=0; double average=0;};
static Counts measure(double rate,double voice,double mains,double strength) {
    CausalPitchTracker tracker; tracker.prepare(rate);
    PitchEstimate estimated{};
    Counts results{};
    const int length=int(rate*.80);
    for(int i=0;i<length;++i){
        const double time=double(i)/rate;
        const float input=float(.22*std::sin(2*kPi*voice*time)
                          +.08*std::sin(4*kPi*voice*time)
                          +strength*std::sin(2*kPi*mains*time));
        if(tracker.push(input,estimated) && i>int(rate*.35) && estimated.valid){
            ++results.valid;results.average+=estimated.hertz;
            if(std::abs(1200.0*std::log2(double(estimated.hertz)/voice)) < 30.0) ++results.correct;
        }
    }
    if(results.valid)results.average/=results.valid;
    return results;
}
static void engineRegression(bool experimental) {
    RealtimeTuner tuned; tuned.prepare(48000,1);
    Parameters p; p.speedMs=12; p.experimentalCycleSynthesis=experimental;
    tuned.setParameters(p);
    float in[1]={},out[1]={}; const float* src[]={in};float* dest[]={out};
    constexpr int total=48000;
    double peak=0;
    for(int i=0;i<total;++i){
        double t=double(i)/48000.0;
        in[0]=float(.22*std::sin(2*kPi*225.0*t)
                 +.08*std::sin(2*kPi*450.0*t)
                 +.25*std::sin(2*kPi*60.0*t));
        tuned.process(src,dest,1,1);
        assert(std::isfinite(out[0]));
        peak=std::max(peak,std::abs(double(out[0])));
    }
    assert(std::abs(tuned.lastPitch().hertz-225.0f)<1.5f);
    assert(tuned.currentTargetNote()==57); // A3 = 220 Hz, not hum-subharmonic
    assert(tuned.appliedCents() < -15.0 && tuned.appliedCents() > -70.0);
    assert(peak<2.0);
    if(experimental)assert(tuned.cycleSynthStatus().spawned>30);
}
int main(){
    int scenarios=0;
    for(double rate:{44100.,48000.,96000.}){
        for(double voice:{70.,75.,85.,110.,147.,220.,330.}){
            for(double mains:{50.,60.}){
                const auto result=measure(rate,voice,mains,.25);
                if(result.valid<80 || result.correct<result.valid*.95){
                    std::cerr<<"Failed mains test: "<<rate<<" "<<voice<<" "<<mains
                             <<" correct="<<result.correct<<"/"<<result.valid
                             <<" mean="<<result.average<<"\n";
                    return 1;
                }
                ++scenarios;
            }
        }
    }
    // Clean near-bass tones should not be suppressed merely for lying near 60Hz.
    for(double voice:{70.,75.,85.,110.,220.}){
        auto result=measure(48000.,voice,60.,0.);
        assert(result.valid>100 && result.correct>=result.valid*.99);
        ++scenarios;
    }
    engineRegression(false);engineRegression(true);scenarios+=2;
    std::cout<<"v1.8 causal 50/60 Hz analysis hum suppression: "<<scenarios
             <<" clean, noisy and full-engine cases passed.\n";
}
