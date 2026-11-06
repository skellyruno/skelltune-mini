#include "RealtimeTuner.hpp"
#include "FormantEnvelopeGuard.hpp"
#include "HarmonicAirGuard.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <limits>
static int passes=0,failures=0;
static void check(bool ok,const char* msg) {
    ++passes;
    if(!ok){++failures;std::printf("FAIL %s\n",msg);}
}
static constexpr double pi=3.14159265358979323846;
static void simpleGuard(int rate){
    skelly::FormantEnvelopeGuard guard;
    guard.prepare(rate,2);
    float d[2]{},w[2]{},o[2]{};
    double ampDry=0,ampWet=0,maxOutput=0;
    for(int i=0;i<rate;++i){
        const double t=i/double(rate);
        d[0]=float(.25*std::sin(2*pi*220*t)+.28*std::sin(2*pi*1150*t));d[1]=-d[0];
        w[0]=float(.29*std::sin(2*pi*330*t)+.08*std::sin(2*pi*1150*t));w[1]=-w[0];
        guard.process(d,w,o,2,0.85,true);
        check(std::isfinite(o[0]) && std::isfinite(o[1]),"Finite processing");
        check(std::abs(o[0]+o[1])<1.e-6,"Antiphase stereo shares gain and matches polarity");
        maxOutput=std::max(maxOutput,std::abs(double(o[0])));
        if(i>rate/2){
            ampDry+=o[0]*std::sin(2*pi*220*t);
            ampWet+=o[0]*std::sin(2*pi*330*t);
        }
    }
    const double leak=std::abs(ampDry)/std::max(1.e-5,std::abs(ampWet));
    std::printf("fs=%d direct wet-only test: leakage ratio=%g, max sample=%g, peak tracked gain=%g\n",
        rate,leak,maxOutput,guard.maximumObservedGain());
    check(leak<.045,"No substantial original-pitch tonal leakage from envelope controller");
    check(maxOutput<2.0 && guard.maximumObservedGain()<=1.497001,"Gain correction is bounded");
    check(guard.currentGain(2)>1.07,"Speech-region envelope compensates attenuated wet band");
    guard.process(d,w,o,2,0.0,true);
    check(o[0]==w[0] && o[1]==w[1],"Zero strength bypass is exactly unmodified wet even after gain correction");
    guard.reset();
    check(guard.currentGain(2)==1.0,"Reset clears spectral gains");
    d[0]=std::numeric_limits<float>::quiet_NaN();
    w[0]=std::numeric_limits<float>::infinity();
    guard.process(d,w,o,2,1.0,true);
    check(std::isfinite(o[0]) && std::isfinite(o[1]),"Non-finite input does not corrupt output");
}
static void periodicityClassifier(int fs){
    skelly::HarmonicAirGuard guard;
    guard.prepare(fs,2);
    const double f0=112.5;
    const double period=fs/f0;
    float high[2]{};
    for(int i=0;i<fs/2;++i){
        const double t=i/double(fs);
        high[0]=float(.18*std::sin(2*pi*36*f0*t));
        high[1]=-high[0];
        guard.push(high,2,period,true);
    }
    const double tonal=guard.periodicity();
    check(tonal>.65,"Periodic high harmonics recognized as tonal");
    // Use deterministic broadband noise; no periodic pitch relation.
    unsigned int seed=7159u;
    for(int i=0;i<fs/2;++i){
        seed=1664525u*seed+1013904223u;
        high[0]=float(.3*(double((seed>>8)&0xffffu)/32768.0-1.0));
        high[1]=-high[0];
        guard.push(high,2,period,true);
    }
    const double noisy=guard.periodicity();
    std::printf("fs=%d HF tonal-score %.3f noisy-score %.3f\n",fs,tonal,noisy);
    check(noisy<.25,"Aperiodic breath/noise not suppressed as tonal");
    guard.reset();
    check(guard.periodicity()==0.0,"Reset clears harmonic classifier");
}
static std::vector<float> runEngine(int rate,int block,int mode,float formant) {
    const int total=rate*3/4;
    std::vector<float> left(total),right(total),outL(total),outR(total);
    for(int i=0;i<total;++i){
        const double t=i/double(rate);
        const double attack=std::min(1.,t*80.);
        left[i]=float(attack*(.16*std::sin(2*pi*225*t)+.1*std::sin(4*pi*225*t)+
            .18*std::sin(2*pi*1125*t)+.08*std::sin(2*pi*2400*t)));
        right[i]=-left[i];
    }
    skelly::RealtimeTuner engine;
    engine.prepare(rate,2,mode==2, mode==3);
    skelly::Parameters p;
    p.speedMs=6;p.amount=1;p.mix=1;p.airPreservation=0;
    p.experimentalCycleSynthesis=mode!=0;p.formantPreservation=formant;
    engine.setParameters(p);
    for(int i=0;i<total;i+=block){
        const int n=std::min(block,total-i);
        const float* src[]{left.data()+i,right.data()+i};
        float* dst[]{outL.data()+i,outR.data()+i};
        engine.process(src,dst,n,2);
    }
    const auto status=engine.cycleSynthStatus();
    if(mode!=0)check(status.spawned>15,"End-to-end PSOLA produces grains");
    for(int i=0;i<total;++i){
        check(std::isfinite(outL[i]) && std::isfinite(outR[i]),"End-to-end output finite");
        check(std::abs(outL[i]+outR[i])<1.e-5,"End-to-end antiphase stereo remains intact");
    }
    return outL;
}
static void integration(int rate){
    auto rt0=runEngine(rate,257,0,0);
    auto rt1=runEngine(rate,64,0,1);
    check(rt0==rt1,"Realtime output unchanged by formant control and host block size");
    for(int mode:{1,2,3}){
        auto out0=runEngine(rate,257,mode,0.0f);
        auto out1=runEngine(rate,257,mode,0.85f);
        auto split=runEngine(rate,1,mode,0.85f);
        check(split==out1,"Formant mode is host-block-size invariant");
        double squared=0,peak=0;
        for(size_t i=rate/3;i<out1.size();++i){
            const double difference=out1[i]-out0[i];
            squared+=difference*difference;
            peak=std::max(peak,std::abs(double(out1[i])));
        }
        check(std::sqrt(squared/(out1.size()-rate/3))>1.e-5,
            "Formant guard changes active PSOLA audio");
        check(peak<2.0,"Wet formant reconstruction bounded");
    }
}
int main(){
    for(int fs:{44100,48000,96000}){simpleGuard(fs);periodicityClassifier(fs);}
    // One end-to-end rate, plus exact dry PDC already extensively tested in earlier suites.
    integration(48000);
    std::printf("v2.4 formant/protection %d checks, %d failures\n",passes,failures);
    return failures?1:0;
}
