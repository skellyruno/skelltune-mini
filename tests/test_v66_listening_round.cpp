// Listening round: low notes, weak octave errors, causal phase transitions.
#include "RealtimeTuner.hpp"
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
#include "MetaTest.hpp"
#endif
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
constexpr int rate=48000;
void check(bool ok,const char* why) {if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
std::vector<float> transitions() {
    // Continuous-phase pitched input, 25 fast notes, 3 quiet dropouts.
    // No discontinuous samples are inserted. The original TESTTUNE
    // produced a +1200-cent excursion at the 2.81s weak octave error.
    const std::array<double,25> notes{{39.3,42.4,43.1,40.5,47.7,43.1,42.7,40.1,45.2,46.3,47.6,50.3,45.5,48.3,52.1,51.5,50.3,44.7,39.6,41.1,40.3,39.4,42.1,43.6,48.2}};
    std::vector<float> x(rate*5);double phase=0.0;
    for(size_t i=0;i<x.size();++i){
        const double t=double(i)/rate;
        const int n=std::min(24,int(t/.2));
        const double frac=t/.2-n;
        const double prev=notes[size_t(std::max(0,n-1))];
        const double midi=prev+(notes[size_t(n)]-prev)*std::min(frac/.05,1.0)+.18*std::sin(2*pi*5.2*t);
        phase+=2*pi*440*std::exp2((midi-69)/12)/rate;
        double value=0.0;
        for(int h=1;h<=8;++h)value+=.12*std::pow(double(h),-.8)*std::sin(h*phase+.19*h);
        double amp=std::min(1.0,std::min(t,5-t)*100);
        for(double start:{1.13,2.43,3.81})amp*=1-.99*std::exp(-std::pow((t-start)/.004,4));
        x[i]=float(value*amp);
    }
    return x;
}
double maxStep(const std::vector<float>& x) {
    double step=0.0;float prev=0;
    for(float v:x){check(std::isfinite(v),"non-finite audio");step=std::max(step,std::abs(double(v)-prev));prev=v;}
    return step;
}
}
int main(){
    const auto src=transitions();const double dryStep=maxStep(src);
    for(bool test:{false,true}) {
        skelly::RealtimeTuner engine;engine.prepare(rate,1,false,false,1.5);
        skelly::Parameters p;p.speedMs=0;p.testTune=test;p.pitchClasses=0x0fff;p.amount=p.mix=1;
        engine.setParameters(p);
        std::vector<float> out(src.size());
        double excursion=0.0;
        for(size_t i=0;i<src.size();++i){const float* in[1]={src.data()+i};float* dest[1]={out.data()+i};engine.process(in,dest,1,1);
            if(i>=size_t(rate*2.805)&&i<size_t(rate*2.835))excursion=std::max(excursion,std::abs(engine.appliedCents()));}
        const double step=maxStep(out);
        std::cout<<(test?"TESTTUNE":"REGULAR")<<" transition peak step="<<step<<" dry="<<dryStep<<" suspect-window cents="<<excursion<<'\n';
        check(step<dryStep*1.32,"excess transition discontinuity");
        if(test)check(excursion<300.0,"held note driven by incompatible octave period");
        check(engine.latencySamples()==72,"Mini 1.5ms transport changed");
    }
    // The last valid lag must have interpolation neighbours at the low
    // end of the detector's range. Exercise the floor at common rates.
    for(int fs:{44100,48000,96000}) {
        skelly::CausalPitchTracker tracker;tracker.prepare(fs);
        for(double hz:{65.5,66.2,70.7,81.8}) {
            tracker.reset();skelly::PitchEstimate estimate{};
            for(int i=0;i<fs/2;++i) {
                const double phase=2*pi*hz*i/fs;
                tracker.push(float(.18*std::sin(phase)+.045*std::sin(2*phase+.2)),estimate);
            }
            const auto last=tracker.last();
            check(last.valid && std::abs(1200*std::log2(last.hertz/hz))<12,"low-note period edge inaccurate");
        }
    }
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    {
        skelly::MetaTest meta;meta.prepare(rate,1.5);meta.setParameters(0x0fff,440,1,0);
        auto out=src;
        for(size_t i=0;i<out.size();i+=128){float* io[1]={out.data()+i};meta.process(io,1,int(std::min<size_t>(128,out.size()-i)));}
        const double step=maxStep(out);
        std::cout<<"META transition peak step="<<step<<" dry="<<dryStep<<'\n';
        check(step<dryStep*1.32,"META excess transition discontinuity");
        check(meta.latencySamples()==72,"META transport changed");
        // Dropouts in deep voiced notes previously bent the grain ratio
        // toward unity during release. Retain the last trusted input lag.
        meta.reset();meta.setParameters(0x0fff,440,1,0);
        const std::array<double,6> low{{66.2,70.7,81.8,106.0,131.2,225.0}};
        std::vector<float> deep(rate*6);double phase=0.0;uint32_t seed=42;
        for(size_t i=0;i<deep.size();++i){
            const double t=double(i)/rate, local=t-std::floor(t);
            phase+=2*pi*low[std::min<size_t>(5,size_t(t))]/rate;
            double value=0.0;
            for(int h=1;h<=9;++h)value+=.13*std::pow(double(h),-.8)*std::sin(h*phase+.19*h);
            seed=1664525u*seed+1013904223u;
            if(local>.45&&local<.462)value=.002*(double((seed>>8)&65535u)/32768.0-1);
            else value*=std::min(1.0,std::min(local,1-local)*100);
            deep[i]=float(value);
        }
        double previousLag=0.0;int misses=0;
        for(size_t i=0;i<deep.size();++i){
            float* io[1]={deep.data()+i};meta.process(io,1,1);
            const double lag=meta.lastRawLag();
            if(previousLag>0.0 && lag<=0.0 && meta.lastTargetPeriod()>0.0){
                ++misses;
                check(std::abs(meta.lastPeriod()-previousLag)<.001,
                      "META invalid hop steered the resampler toward the snapped target");
            }
            previousLag=lag;
        }
        const double deepStep=maxStep(deep);
        std::cout<<"META deep dropout peak step="<<deepStep<<" guarded misses="<<misses<<'\n';
        check(misses>0,"META weak-hop fixture did not exercise invalid tracking");
        // A silent left lane cannot suppress the common phase search.
        std::array<float,skelly::MetaTest::kRing> left{},right{};
        for(size_t i=0;i<right.size();++i)right[i]=float(.3*std::sin(2*pi*i/100.0));
        const double head=skelly::MetaTest::probeAlignedHead(left.data(),600.0,708.0,12,900.0,right.data());
        check(std::abs(head-700.0)<1.1,"right-only phase alignment selected the silent left lane");
    }
#endif
    std::cout<<"PASS: listening-round transition, deep tracker, stereo phase and PDC regression\n";
}
