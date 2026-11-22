// A user edit must actually change the tuned pitch in all three engines.
#include "RealtimeTuner.hpp"
#ifdef SKELLY_META_TEST
#include "MetaTest.hpp"
#endif
#include <cmath>
#include <cstdlib>
#include <iostream>
static void verify(bool okay,const char* what){if(!okay){std::cerr<<"FAIL "<<what<<"\n";std::exit(1);}}
int main(){
    constexpr int fs=48000;
    constexpr double pi=3.14159265358979323846;
    for(bool testTune:{false,true}){
        skelly::RealtimeTuner t;t.prepare(fs,1);
        skelly::Parameters p;p.speedMs=0;p.pitchClasses=0x0fff;
        p.testTune=testTune;t.setParameters(p);
        double phase=0;int during=-1,after=-1;
        for(int i=0;i<fs;++i){
            t.setManualTargetMidi(i>=fs/3&&i<2*fs/3?60:-1);
            phase+=2*pi*225.0/fs;
            float x=float(.21*std::sin(phase)+.06*std::sin(2*phase));
            float y=0;const float* in[]={&x};float* out[]={&y};
            t.process(in,out,1,1);
            verify(std::isfinite(y),"nonfinite edited sample");
            if(i==fs/2)during=t.currentTargetNote();
            if(i==fs-1)after=t.currentTargetNote();
        }
        verify(during==60&&after==57,"manual target not applied/released");
        std::cout<<(testTune?"TESTTUNE":"REGULAR")<<" edit->C4->A3 PASS\n";
    }
#ifdef SKELLY_META_TEST
    skelly::MetaTest m;m.prepare(fs);m.setParameters(0x0fff,440,1,0);
    double phase=0;double during=0,after=0;
    for(int i=0;i<fs;++i){
        m.setManualTargetMidi(i>=fs/3&&i<2*fs/3?60:-1);
        phase+=2*pi*225.0/fs;
        float x=float(.21*std::sin(phase)+.06*std::sin(2*phase));
        float* ch[]={&x};m.process(ch,1,1);
        verify(std::isfinite(x),"META nonfinite edited sample");
        if(i==fs/2)during=m.targetHz();
        if(i==fs-1)after=m.targetHz();
    }
    verify(std::abs(during-261.625565)<1&&std::abs(after-220)<2,
           "META editor did not override/release target period");
    std::cout<<"META edit->C4->A3 PASS\n";
#endif
}
