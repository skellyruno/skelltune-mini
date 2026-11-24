// v3.3.3: transient dry/wet handoff must not click when a voiced phrase
// is already pitch-corrected. Preserve its *original* sample-exact 3ms PDC
// while using only the history already present in the audio callback.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
struct Run {std::vector<float> input, out;int latency=0;};
static void check(bool yes,const char* why){if(!yes){std::cerr<<"FAIL: "<<why<<"\n";std::exit(1);}}
static Run run(int fs,int block,bool prefade) {
    skelly::RealtimeTuner t; t.prepare(fs,1);
    t.setAnticipatoryTransientFadeEnabled(prefade);
    skelly::Parameters p; p.speedMs=0;p.amount=1;p.mix=1;
    p.transientProtection=1.0f;p.airPreservation=.65f;p.pitchClasses=0x0fff;
    t.setParameters(p);
    const int samples=int(fs*.85);
    Run x; x.latency=t.latencySamples();
    x.input.resize(size_t(samples));x.out.resize(size_t(samples));
    double phase=0;
    for(int i=0;i<samples;++i) {
        phase+=2.0*skelly::kPi*225.0/double(fs);
        float sample=float(.16*std::sin(phase)+.065*std::sin(2*phase+.33));
        if(i==int(fs*.42))sample+=.95f; // 100% wet is established by here
        if(i==int(fs*.63))sample+=.82f;
        x.input[size_t(i)]=sample;
    }
    for(int i=0;i<samples;i+=block) {
        const int n=std::min(block,samples-i);
        const float* in[1]={x.input.data()+i};float* out[1]={x.out.data()+i};
        t.process(in,out,n,1);
    }
    return x;
}
int main(){
    for(int fs:{44100,48000,96000}){
        const auto old=run(fs,1,false),safe=run(fs,257,true);
        const auto safeTiny=run(fs,1,true);
        check(old.latency==safe.latency,"changing transient fade changes host PDC");
        for(size_t i=0;i<safe.out.size();++i) {
            check(std::isfinite(safe.out[i]),"nonfinite prefade sample");
            check(std::abs(safe.out[i]-safeTiny.out[i])<1e-5,
                  "prefade is not invariant across audio callback blocks");
        }
        for(double attack:{.42,.63}) {
            const int event=int(fs*attack);
            const int at=event+safe.latency;
            check(std::abs(safe.out[size_t(at)]-safe.input[size_t(event)])<1e-6,
                  "protected attack no longer exactly aligned with host PDC");
            // In steady wet audio the prefade must alter the wet/dry
            // transition PRIOR to the exact protected sample.
            bool changed=false;
            for(int k=std::max(0,at-int(fs*.0007));k<at;++k)
                if(std::abs(safe.out[size_t(k)]-old.out[size_t(k)])>0.0001)
                    changed=true;
            check(changed,"wet/dry prefade did not engage before the attack");
        }
        std::cout<<"fs="<<fs<<" latency="<<safe.latency
                 <<" exact protected attacks and callback invariance PASS\n";
    }
    std::cout<<"PASS: anticipatory transient prefade preserves PDC and audio timing\n";
    return 0;
}
