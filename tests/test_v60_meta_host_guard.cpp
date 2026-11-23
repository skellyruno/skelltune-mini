// Regression for 14 targeted safety/UI improvements in the three-mode lab.
// This portable test focuses on hostile/unprepared host and edit data.
#include "MetaTest.hpp"
#include "PitchEditTimeline.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
static void check(bool ok,const char* msg) {
    if(!ok){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}
int main() {
    using skelly::PitchEditTimeline;
    PitchEditTimeline edits;
    check(!edits.hasEdits(),"empty editor must be transparent");
    edits.clear();
    check(!edits.hasEdits(),"repeat empty CLEAR changed state");
    check(edits.add({100,200,60})>=0 && edits.hasEdits(),"recorded edit invisible");
    edits.set(47,{100,200,60}); // identical write does not change target
    check(edits.targetAt(150)==60,"same-note editor write damaged note");
    check(edits.targetAt(99)==-1&&edits.targetAt(200)==-1,
          "no-op write changed timing edges");
    check(PitchEditTimeline::frameMs(100,480,48000.0)==110,
          "valid host sample clock changed");
    check(PitchEditTimeline::frameMs(100,480,
          std::numeric_limits<double>::infinity())==-1,
          "infinite host sample rate accepted");
    check(PitchEditTimeline::frameMs(100,480,
          std::numeric_limits<double>::quiet_NaN())==-1,
          "NaN host sample rate accepted");
    check(PitchEditTimeline::frameMs(100,-1,48000.0)==-1,
          "negative host sample offset accepted");
    check(PitchEditTimeline::frameMs(2147483647LL,256,48000.0)==-1,
          "out-of-range persisted host clock accepted");
    check(edits.framesUntilBoundary(100,-3,128,48000.0)==128,
          "invalid host offset improperly split callback");
    edits.clear();check(!edits.hasEdits(),"CLEAR left active edits");
    skelly::MetaTest meta;
    meta.prepare(std::numeric_limits<double>::quiet_NaN(),1.5);
    check(meta.latencySamples()==72,"NaN host rate broke 48k fallback PDC");
    meta.prepare(-22050,1.5);
    check(meta.latencySamples()==12,"negative rate broke safe minimum 8k PDC");
    meta.prepare(48000.0,1.5);
    meta.setParameters(0xffffu,std::numeric_limits<double>::infinity(),1,0);
    check(meta.latencySamples()==72,"META invalid A4 corrupted PDC");
    meta.process(nullptr,2,128);
    meta.process(nullptr,0,0);
    float mono=0.5f;
    float* malformed[2]={&mono,nullptr};
    meta.process(malformed,2,1);
    check(mono==0.5f,"incomplete stereo host buffer wrote audio");
    float* valid[1]={&mono};
    meta.process(valid,1,0);
    check(mono==0.5f,"zero-frame callback wrote audio");
    meta.process(valid,1,1);
    check(meta.tableCount()==84,"mask high bits or A4 corrupted note table");
    meta.setParameters(0xffffu,440,1,1.0);
    std::array<float,128> voice{};
    double phase=0;
    for(int block=0;block<500;++block) {
        for(int i=0;i<128;++i) {
            phase+=2*skelly::MetaTest::kPi*235.8/48000.0;
            voice[size_t(i)]=float(.19*std::sin(phase)+.06*std::sin(2*phase));
        }
        float* io[1]={voice.data()};
        meta.process(io,1,128);
    }
    check(meta.detectedHz()>100 && meta.targetHz()>100,"voiced baseline never acquired");
    std::array<float,128> bypassVoice{};
    bypassVoice.fill(.125f);
    float* bypassBuffer[1]={bypassVoice.data()};
    meta.process(bypassBuffer,1,128,true);
    for(float x:bypassVoice)check(x==.125f,"bypass changed source samples");
    check(meta.detectedHz()==0.0&&meta.targetHz()==0.0,
          "bypass advertised a stale green note");
    std::cout<<"PASS: bounded clock, idle editor, 1.5ms META, sanitized"
                " host state and bypass readout\n";
}
