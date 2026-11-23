#include "MetaTest.hpp"
#include "RealtimeTuner.hpp"
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
static void check(bool good,const char* msg){
    if(!good){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}
int main(){
    for(double rate:{44100.0,48000.0,96000.0}){
        skelly::MetaTest meta; meta.prepare(rate,1.5);
        meta.setParameters(0x0fffu,440.0,1.0,0.0);
        const auto delay=int(std::lround(rate*0.0015));
        check(meta.latencySamples()==delay,"META 1.5ms PDC changed");
        std::vector<float> voice(std::size_t(128));
        double phase=0.0;
        for(int block=0;block<300;++block){
            for(int i=0;i<128;++i){
                phase+=2*skelly::MetaTest::kPi*235.8/rate;
                voice[std::size_t(i)]=float(.19*std::sin(phase)+.07*std::sin(2*phase));
            }
            float* io[1]={voice.data()};
            meta.process(io,1,128);
            for(float x:voice)check(std::isfinite(x),"META tuned signal nonfinite");
        }
        check(meta.tableCount()==84,"META full-mask table truncated");
        meta.setParameters(0x0200u,442.5,1.0,10.0);
        float silence[128]{};
        float* io[1]={silence};
        meta.process(io,1,128);
        check(meta.tableCount()==7,"META masked note table stale");
        for(float x:silence)check(std::isfinite(x),"META switching scale nonfinite");
        const uint64_t mark=uint64_t(UINT32_MAX)-12u;
        meta.setSampleClockForUnitTests(mark);
        meta.setParameters(0x0fffu,440.0,0.0,20.0);
        float after[64]{};
        float* out[1]={after};
        meta.process(out,1,64);
        check(meta.processedSamples()==mark+64u,
              "META 32-bit clock wrapped after long session");
        for(float x:after)check(std::isfinite(x),
              "META 32-bit rollover poisoned output");
        skelly::RealtimeTuner rt;
        rt.prepare(std::numeric_limits<double>::quiet_NaN(),1,false,false,1.5);
        check(rt.latencySamples()==72,
              "REGULAR finite sample-rate fallback lost 1.5ms PDC");
    }
    std::cout<<"PASS: cached META masks, x64 long-session clock, low PDC\n";
}
