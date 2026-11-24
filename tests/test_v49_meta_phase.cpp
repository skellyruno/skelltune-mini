// META LAB: phase-aligned grain jumps, PDC source age and stereo coherence.
// This is a deterministic regression, not a subjective quality score.
#include "MetaTest.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
static void need(bool ok,const char* reason) {
    if(!ok){std::cerr<<"FAIL: "<<reason<<"\n";std::exit(1);}
}
static constexpr double pi=3.14159265358979323846;
int main(){
    constexpr int rate=48000;
    std::array<float,skelly::MetaTest::kRing> ring{};
    const double period=double(rate)/220.0;
    for(size_t i=0;i<ring.size();++i) {
        const double phase=2.0*pi*220.0*double(i)/double(rate);
        ring[i]=float(0.65*std::sin(phase) +
                      0.22*std::sin(2*phase+0.23) +
                      0.11*std::sin(3*phase+1.1));
    }
    const double old=3580.31;
    const double proposed=old+period+7.0;
    const double selected=skelly::MetaTest::probeAlignedHead(
        ring.data(),old,proposed,12,proposed+20.0);
    const double before=skelly::MetaTest::probePhaseMismatch(ring.data(),old,proposed);
    const double after=skelly::MetaTest::probePhaseMismatch(ring.data(),old,selected);
    std::cout<<"META predictable phase mismatch before="<<before
             <<" after="<<after<<" adjustment="<<selected-proposed<<"\n";
    need(after < before*0.25,"phase search failed to correct a displaced harmonic grain");
    need(std::abs(selected-proposed)<=12.01,
         "phase alignment exceeded bounded candidate search");
    // Real engine runs two stereo channels through identical grain timing.
    skelly::MetaTest stereo; stereo.prepare(rate);
    stereo.setParameters(0x0fff,440,1,0.5);
    std::vector<float> left(rate*2),right(rate*2);
    double phase=0;
    for(int i=0;i<rate*2;++i) {
        const double hz=i<rate/2?228.0:i<rate?251.0:i<rate*3/2?297.0:220.0;
        phase+=2.0*pi*hz/rate;
        left[i]=float(0.19*std::sin(phase)+0.04*std::sin(2*phase+0.2));
        right[i]=float(0.7*left[i]);
    }
    for(int i=0;i<rate*2;i+=128){
        float* io[2]={left.data()+i,right.data()+i};
        stereo.process(io,2,128);
    }
    double mismatch=0,total=0;
    for(size_t i=128;i<left.size();++i){
        need(std::isfinite(left[i])&&std::isfinite(right[i]),
             "META stereo aligned processing became nonfinite");
        mismatch+=std::abs(0.7*left[i]-right[i]);
        total+=std::abs(left[i]);
    }
    std::cout<<"META stereo scaled mismatch="<<mismatch/(total+1.0e-9)
             <<" phase adjustments="<<stereo.phaseAlignmentAdjustments()<<"\n";
    need(mismatch/(total+1.0e-9)<1.0e-5,
         "per-channel phase search destabilized stereo image");
    // High pitches at 96kHz used to read younger than host dry PDC
    // because the old grain floor was a fixed 120 samples.
    skelly::MetaTest high;high.prepare(96000);
    high.setParameters(0x0fff,440,1,0.4);
    std::vector<float> signal(96000);
    phase=0;
    for(int i=0;i<96000;++i){
        phase+=2*pi*720.0/96000.0;
        signal[i]=float(0.19*std::sin(phase));
    }
    for(int i=0;i<96000;i+=128){
        float* io[1]={signal.data()+i};
        high.process(io,1,128);
    }
    for(float x:signal)
        need(std::isfinite(x)&&std::abs(x)<1.0f,"META high-pitch/high-rate head instability");
    std::cout<<"PASS: phase search correction, stereo phase and high-note PDC guard\n";
}