#include "CausalCycleSynth.hpp"
#include "RealtimeTuner.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdint>

static constexpr double pi=3.14159265358979323846;
static int checks=0,failures=0;
static void check(bool result,const char* why){++checks;if(!result){++failures;std::printf("FAIL: %s\n",why);}}
struct Render {
    std::vector<float> channel0,channel1;
    skelly::CausalCycleSynth::Status status;
};
static Render run(int fs,double f0,int latencyMs,bool upgraded,bool stereo) {
    skelly::CausalCycleSynth s;
    s.prepare(fs,stereo?2:1);
    s.setClockLockedDelay(fs*latencyMs/1000);
    s.setOverlapCoherenceEnabled(upgraded);
    const int n=fs;Render r; r.channel0.resize(n);r.channel1.resize(n);
    float input[2]{},output[2]{};
    for(int i=0;i<n;++i){
        const double t=double(i)/fs;
        const double theta=2*pi*f0*t;
        const double amplitude=.72+.14*std::sin(2*pi*3.4*t);
        input[0]=float(amplitude*(.16*std::sin(theta)+.30*std::sin(2*theta+.2)+
                    .12*std::sin(6*theta-.3)+.06*std::sin(14*theta+.1)));
        input[1]=stereo?-input[0]:0.0f;
        s.processSample(input,output,fs/f0,1.12,true);
        r.channel0[size_t(i)]=output[0];r.channel1[size_t(i)]=output[1];
        check(std::isfinite(output[0]) && std::isfinite(output[1]),"finite synthesized audio");
        if(stereo)check(std::abs(output[0]+output[1])<1.e-6,"antiphase stereo preserved");
    }
    r.status=s.status();return r;
}
int main(){
    for(int fs:{44100,48000,96000}) {
        for(double f0:{75.0,95.0,110.0,155.0,220.0}){
            const auto baseline=run(fs,f0,50,false,false);
            const auto upgraded=run(fs,f0,50,true,false);
            check(upgraded.status.spawned>30,"synthesis schedules grains");
            check(upgraded.status.dropped==0,"no dropped grains");
            check(upgraded.status.maximumTimelineOffset/fs < .012,"bounded studio mark drift");
            check(std::abs(upgraded.status.meanWeightedSourceAge/fs-.05)<.008,"declared studio timeline stays near 50ms");
            if(f0>=140.0) check(upgraded.channel0==baseline.channel0,"high-vocal studio path unchanged");
            auto liveOld=run(fs,f0,0,false,false);
            auto liveNew=run(fs,f0,0,true,false);
            check(liveOld.channel0==liveNew.channel0,"realtime path byte-identical");
            if(f0<140) {
                auto stereo=run(fs,f0,40,true,true);
                check(stereo.status.dropped==0,"low-voice Studio Fast grains remain valid");
            }
        }
    }
    // A stereo gain guard and exact-dry latency are separately checked in
    // the 23 preserved CMake suites; this targeted test validates the new path.
    std::printf("v2.5 phase and timeline: %d assertions, %d failures\n",checks,failures);
    return failures?1:0;
}
