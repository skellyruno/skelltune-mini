#pragma once
#include <algorithm>
#include <cmath>

namespace skelly {
// Recover moderate wet-only level dips, not pitch or severe cancellation.
// Shared stereo gain, no allocation, no additional delay. Never attenuate
// or add overload to a sample; bypass, partial mix and attack anchors stay dry.
class WetLevelGuard {
public:
    void prepare(double rate) noexcept {
        rate=std::clamp(std::isfinite(rate)?rate:48000.,8000.,192000.);
        levelAlpha_=1.-std::exp(-1./(rate*.012));
        gainAlpha_=1.-std::exp(-1./(rate*.009));
        releaseAlpha_=1.-std::exp(-1./(rate*.003));
        reset();
    }
    void reset() noexcept { dryPower_=wetPower_=0.;gain_=goal_=1.; controlPhase_=0; }
    void process(const float dry[2],float wet[2],int channels,bool voiced,
                 double mix,double blend) noexcept {
        double a=0.,b=0.,peak=0.;
        for(int c=0;c<channels;++c){
            a+=double(dry[c])*dry[c];b+=double(wet[c])*wet[c];
            peak=std::max(peak,std::abs(double(wet[c])));
        }
        dryPower_+=levelAlpha_*(a/channels-dryPower_);
        wetPower_+=levelAlpha_*(b/channels-wetPower_);
        if(dryPower_<1.e-20)dryPower_=0.;
        if(wetPower_<1.e-20)wetPower_=0.;
        // Sample-clock control updates, independent of host block size.
        // 8 samples is <1ms even at the lowest supported rate. The gain
        // and protection/headroom limits still evolve every sample.
        if(controlPhase_==0){
            goal_=1.;
            if(voiced && dryPower_>1.e-6 && wetPower_>dryPower_*.2)
                goal_=std::clamp(std::sqrt(dryPower_/wetPower_),1.,1.35);
        }
        controlPhase_=(controlPhase_+1)&7;
        gain_+=(goal_<gain_?releaseAlpha_:gainAlpha_)*(goal_-gain_);
        if(gain_-1.<1.e-9)gain_=1.;
        if(!voiced||mix<=.95||blend<=.95||gain_==1.||peak>=1.)return;
        const double eligible=std::clamp((mix-.95)*20.,0.,1.)*
            std::clamp((blend-.95)*20.,0.,1.);
        // No boost on already overloaded input, and no new overload.
        const double desired=1.+(gain_-1.)*eligible*std::clamp(3.*(1.-peak),0.,1.);
        const double safe=peak*desired>1.?1./peak:desired;
        if(safe>1.)for(int c=0;c<channels;++c)wet[c]=float(wet[c]*safe);
    }
private:
    double levelAlpha_=0.,gainAlpha_=0.,releaseAlpha_=0.,dryPower_=0.,wetPower_=0.,gain_=1.,goal_=1.;
    unsigned controlPhase_=0;
};
} // namespace skelly
