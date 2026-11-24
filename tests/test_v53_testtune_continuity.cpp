// Regression for TESTTUNE's fast note changes and unreliable-voice gaps.
// No host parameter ID or extra input buffering is permitted.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
static void require(bool ok,const char* message) {
    if(!ok){std::cerr<<"FAIL: "<<message<<"\n";std::exit(1);}
}
int main() {
    constexpr double pi=3.14159265358979323846;
    for(int fs:{44100,48000,96000}) {
        skelly::RealtimeTuner t;
        t.prepare(fs,1);
        skelly::Parameters p;
        p.testTune=true;
        p.speedMs=0.0f;
        p.amount=1.0f;
        p.mix=1.0f;
        p.pitchClasses=0x0fff;
        t.setParameters(p);
        double phase=0.0,peak=0.0,maxSlew=0.0,previousCents=0.0;
        int finalTarget=-1, middleTarget=-1;
        unsigned rng=0x78e1a23bu;
        for(int i=0;i<int(fs*0.90);++i) {
            const double seconds=double(i)/double(fs);
            const double f=seconds<0.36?225.0:(seconds<0.60?265.0:225.0);
            phase+=2.0*pi*f/double(fs);
            rng=1664525u*rng+1013904223u;
            const double hiss=double((rng>>8)&65535u)/32768.0-1.0;
            const bool dropout=(seconds>0.25&&seconds<0.26)||
                (seconds>0.51&&seconds<0.52);
            const float input=float(dropout?.003*hiss:
                .18*std::sin(phase)+.038*std::sin(2.0*phase+.19));
            float output=0.0f;
            const float* src[1]={&input};
            float* dst[1]={&output};
            t.process(src,dst,1,1);
            require(std::isfinite(output),"non-finite TESTTUNE output");
            peak=std::max(peak,std::abs(double(output)));
            const double cents=t.appliedCents();
            require(std::isfinite(cents),"non-finite correction");
            maxSlew=std::max(maxSlew,std::abs(cents-previousCents));
            previousCents=cents;
            if(i==int(fs*0.56))middleTarget=t.currentTargetNote();
        }
        finalTarget=t.currentTargetNote();
        // The direct period-domain note selector is still chromatic and
        // immediate after settling, despite the safer audio-rate slew.
        require(middleTarget==60,"TESTTUNE missed C4 mid-phrase");
        require(finalTarget==57,"TESTTUNE missed A3 after transition");
        require(maxSlew <= 300000.0/double(fs)+0.0001,
                "TESTTUNE audio-rate correction has a zipper-sized edge");
        require(peak<1.5,"TESTTUNE peak runaway");
        std::cout<<"fs="<<fs<<" max_step_cents="<<maxSlew
                 <<" peak="<<peak<<" final="<<finalTarget<<" PASS\n";
    }
    std::cout<<"PASS: TESTTUNE onset/transition slew regression\n";
}
