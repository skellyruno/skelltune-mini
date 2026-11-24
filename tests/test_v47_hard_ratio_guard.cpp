// Prevent one-sample ratio jumps on hard retune while respecting 3ms host PDC.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
static void need(bool good,const char* msg){if(!good){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}}
int main(){
    constexpr int sr=48000;
    for(int mode=0;mode<2;++mode) {
        skelly::RealtimeTuner dsp;dsp.prepare(sr,1);
        skelly::Parameters p;p.speedMs=0;p.amount=1;p.mix=1;p.pitchClasses=0x0fffu;
        p.testTune=mode==1;dsp.setParameters(p);
        const int latency=dsp.latencySamples();
        need(latency==144,"audio PDC altered by ratio slew");
        double phase=0,prev=0,maxJump=0;
        for(int i=0;i<sr*2;++i){
            const double hz=i<sr/2?227.0:i<sr?340.0:i<sr*3/2?210.0:395.0;
            phase+=2.0*skelly::kPi*hz/sr;
            const float input=float(.19*std::sin(phase)+.04*std::sin(phase*2.0));
            float output=0.0f;
            const float* src[]={&input};float* dst[]={&output};
            dsp.process(src,dst,1,1);
            const double curr=dsp.appliedCents();
            maxJump=std::max(maxJump,std::abs(curr-prev));
            prev=curr;
            need(std::isfinite(curr)&&std::isfinite(output),"invalid hard-mode pitch/output");
        }
        const double limit=(mode?520000.0:390000.0)/sr+0.005;
        std::cout<<(mode?"TESTTUNE":"REGULAR")<<" hard note-switch max cents/sample="<<maxJump<<"\n";
        need(maxJump<=limit,"audio pitch ratio zipper not bounded");
    }
    std::cout<<"PASS: both hard algorithms have bounded ratio slew\n";
}