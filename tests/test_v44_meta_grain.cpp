// META LAB ONLY: prevent an unfaded source-head wrap from clicking on the
// user's real vocal. Test the original hard-jump mode against the corrected
// one, without changing any normal Mini algorithm or host parameter ID.
#include "MetaTest.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
static constexpr double pi=3.14159265358979323846;
struct Result {double step=0,peak=0,energy=0;uint64_t jumps=0,fades=0;};
Result render(bool fade){
    skelly::MetaTest m;m.prepare(48000.0);m.setParameters(0x0fff,440.0,1.0,1.25);
    m.setJumpCrossfadeEnabled(fade);
    double phase=0,prev=0;
    Result r;
    constexpr int rate=48000,block=128;
    for(int i=0;i<rate*4;i+=block){
        float buf[block]{};
        for(int j=0;j<block;++j){
            const double time=double(i+j)/rate;
            const double hz=(time<2?225.0:249.0)*
                std::pow(2.0,22.0*std::sin(2*pi*5.0*time)/1200.0);
            phase+=2*pi*hz/rate;
            buf[j]=float(0.20*std::sin(phase)+0.10*std::sin(2*phase+0.2)+
                         0.06*std::sin(3*phase+0.6));
        }
        float* channels[1]={buf};m.process(channels,1,block);
        for(int j=0;j<block;++j){
            const double v=double(buf[j]);
            if(!std::isfinite(v)) {std::cerr<<"nonfinite META output\n";std::exit(1);}
            if(i+j>rate/2) r.step=std::max(r.step,std::abs(v-prev));
            r.peak=std::max(r.peak,std::abs(v));
            r.energy+=v*v;
            prev=v;
        }
    }
    r.jumps=m.grainJumps();r.fades=m.grainJumpCrossfades();
    return r;
}
int main(){
    const auto unsafe=render(false),safe=render(true);
    std::cout<<"META old hard-jumps: count="<<unsafe.jumps<<", maxStep="<<unsafe.step
             <<"; crossfaded: count="<<safe.jumps<<", fades="<<safe.fades
             <<", maxStep="<<safe.step<<"\n";
    if(unsafe.jumps==0 || safe.jumps==0 ||
       unsafe.fades!=0 || safe.fades!=safe.jumps ||
       safe.peak>2.0 || safe.energy<100.0) {
        std::cerr<<"FAIL: META grain-jump crossfade regression\n";return 1;
    }
    // No aesthetic ranking: recorded voices carry more meaningful waveform
    // and listening evidence, but this hook proves *every* jump is faded.
    std::cout<<"PASS: every live META source-head jump starts a bounded crossfade\n";
    return 0;
}
