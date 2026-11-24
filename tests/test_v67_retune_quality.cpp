// v67: tracker reset isolation, deep-note grain capacity, and true retune
// correction timing. Portable tests; no JUCE or DAW is required.
#include "RealtimeTuner.hpp"
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
#include "MetaTest.hpp"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
void check(bool yes,const char* why){if(!yes){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
void trackerReset(){
    constexpr int rate=48000;
    skelly::CausalPitchTracker fresh,reused;
    fresh.prepare(rate);reused.prepare(rate);
    skelly::PitchEstimate e{},a{},b{};
    // Establish the shorter-period continuity preference, then reset
    // while the audio is stopped. A new weak-fundamental voice must be
    // treated exactly as it would by a freshly constructed tracker.
    for(int i=0;i<rate/4;++i)reused.push(float(.3*std::sin(2*pi*220*i/rate)),e);
    reused.reset();
    for(int i=0;i<rate/3;++i){
        const double ph=2*pi*110*i/rate;
        const float x=float(.06*std::sin(ph)+.43*std::sin(2*ph)+.10*std::sin(3*ph)+.36*std::sin(4*ph));
        const bool ha=fresh.push(x,a),hb=reused.push(x,b);
        check(ha==hb,"reset changed detector hop timing");
        if(ha)check(a.valid==b.valid && a.hertz==b.hertz && a.confidence==b.confidence,
                    "stale octave preference survived tracker reset");
    }
}
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
// Independent waveform measurement on the rendered sine: interpolate
// positive-going zero crossings, not the engine's pitch-display estimate.
double measuredPitch(const std::vector<float>& y,int rate){
    double first=0,last=0;int count=0;
    for(int i=int(rate*.55);i<int(rate*1.45);++i){
        const double a=y[size_t(i-1)],b=y[size_t(i)];
        if(a<=0.0&&b>0.0){const double crossing=i-1-a/(b-a);if(!count)first=crossing;last=crossing;++count;}
    }
    check(count>30,"missing deep-note output cycles");
    return (count-1)*rate/(last-first);
}
void deepGrainHistory(){
    for(int rate:{48000,96000,192000,384000}){
        auto meta=std::make_unique<skelly::MetaTest>();
        meta->prepare(rate,1.5);meta->setParameters(0xfffu,440.,1.,0.);
        std::vector<float> y(size_t(rate)*2);
        for(size_t i=0;i<y.size();++i){
            const double ph=2*pi*66.2*i/rate;
            const double t=double(i)/rate;
            y[i]=float((.18*std::sin(ph)+.025*std::sin(2*ph+.19))*
                std::min(1.0,std::min(t,2-t)*50));
        }
        for(size_t i=0;i<y.size();i+=127){float* io[1]={y.data()+i};meta->process(io,1,int(std::min<size_t>(127,y.size()-i)));}
        for(float v:y)check(std::isfinite(v)&&std::abs(v)<.35f,"deep high-rate grain out of range");
        const double target=440.*std::exp2((36.-69.)/12.);
        const double error=1200*std::log2(measuredPitch(y,rate)/target);
        std::cout<<"META "<<rate<<" Hz 66.2->C2 output cents="<<error<<'\n';
        check(std::abs(error)<2.,"META low-note correction silently dropped to dry at high sample rate");
        check(meta->latencySamples()==int(std::lround(rate*.0015)),"deep grain history changed dry transport");
    }
}
void correctionTime(){
    constexpr int rate=48000;
    for(double speed:{0.,25.,100.}){
        auto meta=std::make_unique<skelly::MetaTest>();meta->prepare(rate,1.5);
        meta->setParameters(0xfffu,440.,1.,speed);
        double phase=0;
        auto run=[&](int n){for(int i=0;i<n;++i){phase+=2*pi*225./rate;float x=float(.18*std::sin(phase)+.035*std::sin(2*phase+.2));float* io[1]={&x};meta->process(io,1,1);check(std::isfinite(x),"retune step nonfinite");}};
        run(rate/2);
        const double initial=meta->shiftCents();
        meta->setManualTargetMidi(59); // +200 cent change while INPUT pitch stays fixed.
        run(int(rate*.012));
        const double early=meta->shiftCents()-initial;
        run(int(rate*.088));
        const double later=meta->shiftCents()-initial;
        run(rate);
        const double final=meta->shiftCents()-initial;
        std::cout<<"META "<<speed<<"ms correction step 12ms="<<early<<" 100ms="<<later<<" settled="<<final<<'\n';
        check(std::abs(final-200.)<1.,"manual pitch target never settled");
        if(speed==0.)check(early>195.,"hard retune lost immediate target response");
        else {
            // The hop scheduler can shift onset by <=2.7 ms. A 25/100ms
            // correction must never jump the entire 200 cents immediately.
            const double lo=200*(1-std::exp(-9./speed));
            const double hi=200*(1-std::exp(-15./speed));
            check(early>=lo-2.&&early<=hi+2.,"retune smooths input period instead of correction");
            check(later>early&&later<final,"slow retune trajectory is not monotone");
        }
    }
}
#endif
}
int main(){trackerReset();
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    deepGrainHistory();correctionTime();
#endif
    std::cout<<"PASS: reset-isolated detector and high-rate META grain/retune regression\n";
}
