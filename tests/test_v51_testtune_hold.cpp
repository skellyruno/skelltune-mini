// TESTTUNE weak-pitch fallback must not select the stale REGULAR note.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
static void expect(bool yes,const char* why) {
    if(!yes){std::cerr<<"FAIL: "<<why<<"\n";std::exit(1);}
}
int main(){
    const uint16_t all=0x0fff, onlyA=uint16_t(1u<<9);
    using T=skelly::RealtimeTuner;
    expect(T::canBridgeTestTuneTarget(69,all,true,true,0.66f),
           "TESTTUNE lost weak-but-voiced period note");
    expect(!T::canBridgeTestTuneTarget(69,all,true,true,0.95f),
           "strong pitches should snap immediately");
    expect(!T::canBridgeTestTuneTarget(69,all,false,true,0.66f),
           "consonants must not retain note target");
    expect(!T::canBridgeTestTuneTarget(69,all,true,false,0.66f),
           "invalid detector period cannot hold note");
    expect(!T::canBridgeTestTuneTarget(60,onlyA,true,true,0.66f),
           "forbidden note must be removed on scale change");
    expect(!T::canBridgeTestTuneTarget(-1,all,true,true,0.66f),
           "undefined target cannot appear");
    constexpr double pi=3.14159265358979323846;
    for(int fs:{44100,48000,96000})for(int block:{1,127,256}){
        T engine;engine.prepare(fs,1);
        skelly::Parameters p;p.testTune=true;p.speedMs=0;p.amount=1;
        p.mix=1;p.pitchClasses=all;
        engine.setParameters(p);
        unsigned seed=0x98771a21u;double phase=0.0,peak=0.0;int active=0,blocks=0;
        const int total=int(fs*1.2);
        for(int start=0;start<total;start+=block){
            float in[256]{},out[256]{};
            const int n=std::min(block,total-start);
            for(int i=0;i<n;++i) {
                const double t=double(start+i)/fs;
                phase+=2*pi*(t<0.65?225.0:264.0)/fs;
                seed=1664525u*seed+1013904223u;
                const double noise=(double((seed>>8)&65535u)/32768.0-1.0);
                const bool weak=(t>.4&&t<.43)||(t>.75&&t<.78);
                in[i]=float((weak?.025:.20)*std::sin(phase)+
                            .045*std::sin(2*phase+.21)+(weak?.018:.001)*noise);
            }
            const float* src[1]={in};float* dst[1]={out};
            engine.process(src,dst,n,1);
            for(int i=0;i<n;++i){
                expect(std::isfinite(out[i]),"nonfinite output");
                peak=std::max(peak,std::abs(double(out[i])));
            }
            if(engine.currentTargetNote()>=0)++active;
            ++blocks;
        }
        expect(peak<1.5 && active>blocks/3,
               "TESTTUNE lost active notes or runaway output on noisy vocal");
        std::cout<<"rate="<<fs<<" block="<<block<<" peak="<<peak<<" PASS\n";
    }
    std::cout<<"PASS: separate TESTTUNE target and noise-aware phase matching\n";
}