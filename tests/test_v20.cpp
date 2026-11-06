// v2.0 output-delay contract: measured sample timings, not PSOLA cycle age.
#include "RealtimeTuner.hpp"
#include <vector>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <cstdint>

namespace {
int checks=0,failures=0;
void require(bool good,const char* message){++checks;if(!good){++failures;std::printf("FAIL: %s\n",message);}}
struct Render {
    std::vector<float> left,right,srcLeft,srcRight;
    int reported = 0;
    uint64_t protectedFrames=0;
};
Render render(int rate,bool cycle,int channels,int block,int eventAt,bool dryOnly=false){
    const int n=rate*3/4;
    Render r;r.left.resize(n);r.right.resize(n);r.srcLeft.resize(n);r.srcRight.resize(n);
    skelly::RealtimeTuner t;t.prepare(rate,channels);
    skelly::Parameters p;p.speedMs=1.0f;p.amount=dryOnly?0.0f:1.0f;
    p.mix=1.0f;p.transientProtection=0.0f;p.airPreservation=0;
    p.experimentalCycleSynthesis=cycle;t.setParameters(p);
    r.reported=t.outputDelayContract(cycle).reportedSamples;
    for(int i=0;i<n;++i){
        const float wave=float(.23*std::sin(2.0*skelly::kPi*225.0*i/rate));
        r.srcLeft[i]=wave;
        if(i==eventAt)r.srcLeft[i]+=0.95f;
        r.srcRight[i]=-r.srcLeft[i];
    }
    for(int at=0;at<n;at+=block){
        int len=std::min(block,n-at);
        const float* input[2]={r.srcLeft.data()+at,r.srcRight.data()+at};
        float* output[2]={r.left.data()+at,r.right.data()+at};
        t.process(input,output,len,channels);
    }
    r.protectedFrames=t.preservedTransientSamples();
    return r;
}
}
void testModeAutomation(int fs,bool fromCycle){
    const int total=int(fs*.8), change=int(fs*.4), event=fs/2;
    skelly::RealtimeTuner t;t.prepare(fs,1);
    skelly::Parameters p;p.speedMs=1;p.amount=1;p.mix=1;
    p.transientProtection=0;p.airPreservation=0;p.experimentalCycleSynthesis=fromCycle;
    t.setParameters(p);
    std::vector<float> input(size_t(total),0.f),out(size_t(total),0.f);
    for(int k=0;k<total;++k)input[size_t(k)]=float(.23*std::sin(2*skelly::kPi*225.*k/fs));
    input[size_t(event)]+=.95f;
    const int delay=t.latencySamples();
    const float* ptr[]={input.data()};float* dst[]={out.data()};
    t.process(ptr,dst,change,1);
    p.experimentalCycleSynthesis=!fromCycle;t.setParameters(p);
    const float* remaining[]={input.data()+change};float* rest[]={out.data()+change};
    t.process(remaining,rest,total-change,1);
    require(delay==t.latencySamples(),"Automation does not change the host compensation delay");
    require(std::abs(out[size_t(event+delay)]-input[size_t(event)])<1e-6,
            "Transient stays sample aligned when synthesis mode changes");
}
int main(){
    for(int fs:{44100,48000,96000}){
        testModeAutomation(fs,false);
        testModeAutomation(fs,true);
        for(int mode=0;mode<2;++mode){
            const auto contract=skelly::delayContract(fs,mode!=0);
            require(contract.reportedSamples==int(std::lround(fs*.003)),"Declared delay rounds to samples");
            require(contract.drySampleExact&&contract.transientAnchored&&!contract.fixedWetWaveformDelay,
                "No false fixed-wet guarantee");
            const int event=fs/2;
            for(int channels:{1,2}){
                auto wet=render(fs,mode!=0,channels,257,event);
                auto tiny=render(fs,mode!=0,channels,1,event);
                auto dry=render(fs,mode!=0,channels,13,event,true);
                const int shifted=event+contract.reportedSamples;
                // More than 300 ms of correction has already converged before
                // this attack; the protected sample cannot sneak past the test
                // by remaining in dry mode at startup.
                require(std::abs(wet.left[shifted]-wet.srcLeft[event])<1e-6,
                    "Hard transient arrives at exactly host-reported sample at 100% wet");
                require(std::abs(wet.right[shifted]+wet.left[shifted])<1e-6 || channels==1,
                    "Attack preserves inverted-stereo polarity");
                require(wet.reported==dry.reported,"Wet/dry contract identical for host compensation");
                bool exactDry=true;
                for(int k=0;k<std::min(fs/20,3000);++k) {
                    const int index=event+contract.reportedSamples+k;
                    if(index>=int(wet.left.size()))break;
                    if(k<fs/400 && std::abs(wet.left[index]-wet.srcLeft[event+k])>1e-6)
                        exactDry=false;
                }
                require(exactDry,"First 2.5ms of transient protected at exact dry timing");
                const int compare=event+contract.reportedSamples+int(fs*.05);
                require(std::abs(wet.left[compare]-tiny.left[compare])<1e-6,
                    "Audio output does not depend on callback block size");
                require(wet.protectedFrames>0,"Transient lane active during running correction");
                if(fs==48000&&channels==1)
                    std::printf("mode=%s fs=%d reported=%d measured=%d peak=%0.4f protected=%llu\n",
                         mode?"PSOLA":"Realtime",fs,wet.reported,shifted-event,wet.left[shifted],
                         static_cast<unsigned long long>(wet.protectedFrames));
            }
        }
    }
    std::printf("v2.0 PDC/transient checks %d/%d passed\n",checks-failures,checks);
    return failures ? 1:0;
}
