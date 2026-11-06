// v2.3: stereo-linked moderate phase-cancellation level stabilization.
// Independent of JUCE, testable at exact sample rate and host block sizes.
#include "RealtimeTuner.hpp"
#include "CoherentMixGuard.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
static int assertions=0, failures=0;
static void check(bool passed,const char* msg) {
    ++assertions;
    if(!passed) {++failures;std::printf("FAIL %s\n",msg);}
}
static double runGuard(double fs,double phaseOffset,bool guardOn, bool invertedStereo=false) {
    skelly::CoherentMixGuard guard;
    guard.prepare(fs);
    double acc=0.0;
    const int count=int(fs*0.75);
    for(int n=0;n<count;++n) {
        const double phase=2.0*skelly::kPi*225*n/fs;
        const float x=float(0.4*std::sin(phase));
        const float y=float(0.4*std::sin(phase+phaseOffset));
        const float d[2] = {x, invertedStereo ? -x : x};
        const float w[2] = {y, invertedStereo ? -y : y};
        const double g=guardOn?guard.process(d,w,2,.5,true):1.0;
        check(std::isfinite(g) && g>=1.0 && g<=1.4126, "Finite bounded guard gain");
        if(n>count/2) {
            const double z=.5*(x+y)*g;
            acc+=z*z;
        }
    }
    return std::sqrt(acc/(count-count/2-1));
}
static void guardTests(int fs) {
    const auto baseline=runGuard(fs,skelly::kPi/2,false);
    const auto restored=runGuard(fs,skelly::kPi/2,true);
    check(restored>baseline*1.20,"Quadrature overlap gains 20+% RMS");
    check(restored<baseline*1.43,"Restoration is bounded");
    check(std::abs(runGuard(fs,0,true)-runGuard(fs,0,false))<1e-3,
          "Already coherent audio is not boosted");
    check(std::abs(runGuard(fs,skelly::kPi/2,true,true)-restored)<1e-5,
          "Stereo polarity inversion does not change common gain");
    skelly::CoherentMixGuard g;
    g.prepare(fs);
    const float dry[2]{.2f,-.2f},wet[2]{-.05f,.05f};
    for(int i=0;i<fs/2;++i)g.process(dry,wet,2,.5,true);
    check(g.process(dry,wet,2,0.0,true)==1.0,"Fully dry has unity gain without waiting");
    check(g.process(dry,wet,2,1.0,true)==1.0,"Fully wet has unity gain without waiting");
    check(g.process(dry,wet,2,.5,false)==1.0,"Protected transient has unity gain");
    g.reset();
    check(g.gain()==1.0,"Reset clears gain history");
    const float corrupt[2]{NAN,INFINITY};
    check(std::isfinite(g.process(corrupt,corrupt,2,.5,true)),"NaN/Inf safety");
}
static void processExact(int fs,int block,int studio) {
    skelly::RealtimeTuner engine;
    engine.prepare(fs,2,studio!=0,studio==2);
    skelly::Parameters p;
    p.speedMs=8;p.amount=0;p.mix=.5;p.experimentalCycleSynthesis=studio!=0;
    engine.setParameters(p);
    const int n=fs/4;
    std::vector<float> x(size_t(n),0),right(size_t(n),0),l(size_t(n),0),r(size_t(n),0);
    const int impulse=fs/100;
    x[size_t(impulse)]=.8f;right[size_t(impulse)]=-.8f;
    for(int i=0;i<n;i+=block) {
        const int k=std::min(block,n-i);
        const float* in[]{x.data()+i,right.data()+i};
        float* out[]{l.data()+i,r.data()+i};
        engine.process(in,out,k,2);
    }
    const int delay=engine.latencySamples();
    bool same=true;
    for(int i=0;i<n;++i) {
        const float v=i==impulse+delay?.8f:0;
        if(l[size_t(i)]!=v||r[size_t(i)]!=-v){same=false;break;}
    }
    check(same,"Exact bypass and stereo PDC independent of guard");
}
int main(){
    for(int fs:{44100,48000,96000}) {
        guardTests(fs);
        for(int studio=0;studio<3;++studio)
            for(int block:{1,64,257})processExact(fs,block,studio);
    }
    std::printf("v2.3 mix guard: %d assertions, %d failures\n",assertions,failures);
    return failures?1:0;
}
