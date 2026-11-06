#include "RealtimeTuner.hpp"
#include "CausalCycleSynth.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

int fail=0;
void check(bool p,const char* s){ std::printf("%s %s\n",p?"PASS":"FAIL",s);fail+=!p; }
constexpr double pi=3.14159265358979323846;

// In this stand-alone lifecycle test, a pitch-synchronous engine must release
// still-active causal grains instead of clearing its output in one sample.
void testGrainTail(int fs,double fundamental) {
    skelly::CausalCycleSynth s; s.prepare(fs,2);
    float in[2]{},out[2]{};
    const int cut=fs/2;
    int validTail=0,lateTail=0;
    double tailPeak=0, stereoErr=0;
    bool sawFresh=false, sawStop=false;
    for(int i=0;i<fs;++i) {
        const double t=i/double(fs);
        in[0]=float(.45*std::sin(2*pi*fundamental*t)+.15*std::sin(4*pi*fundamental*t+.3));
        in[1]=-in[0];
        s.processSample(in,out,double(fs)/fundamental,1.10,i<cut);
        if(i==cut-1) sawFresh=s.freshSynthesisAvailable();
        if(i==cut) sawStop=!s.freshSynthesisAvailable();
        if(i>=cut && s.hasOutputFrame()) { ++validTail; tailPeak=std::max(tailPeak,std::abs(double(out[0]))); }
        if(i>=cut+fs/30 && s.hasOutputFrame()) ++lateTail;
        if(i>=cut && i<cut+fs/30) stereoErr=std::max(stereoErr,std::abs(double(out[0]+out[1])));
    }
    std::printf("TAIL %.0fHz @ %dHz: %d valid samples, peak %.5f, late frames %d\n", fundamental,fs,validTail,tailPeak,lateTail);
    check(sawFresh && sawStop,"Fresh-voiced flag clears immediately on release");
    check(validTail>0 && tailPeak>0.005,"Existing causal grain audio survives immediate unvoiced transition");
    check(lateTail==0,"Released synthesis grains finish without infinite echo");
    check(stereoErr<1e-6,"Release preserves stereo polarity");
}

void testIntegratedExit(int fs,int block){
    const int n=fs*14/10, off=fs*3/5;
    skelly::RealtimeTuner tuner; tuner.prepare(fs,2);
    skelly::Parameters p; p.speedMs=8;p.amount=1;p.mix=1;p.airPreservation=0;
    p.transientProtection=0;p.experimentalCycleSynthesis=true;
    tuner.setParameters(p);
    std::vector<float> l(n),r(n),ol(n),orr(n);
    for(int i=0;i<n;++i){const double t=i/double(fs);l[i]=float(.45*std::sin(2*pi*225*t)+.12*std::sin(4*pi*225*t+.3));r[i]=-l[i];}
    int j=0;double blendBefore=0, blendImmediately=0, blendAfter=0, maxErr=0;
    while(j<n) {
        if(j==off) {blendBefore=tuner.currentCycleBlend();p.experimentalCycleSynthesis=false;tuner.setParameters(p);}
        int count=std::min(block,n-j);
        if(j<off)count=std::min(count,off-j);
        const float* a[]{l.data()+j,r.data()+j};float* b[]{ol.data()+j,orr.data()+j};
        tuner.process(a,b,count,2);
        if(j==off)blendImmediately=tuner.currentCycleBlend();
        if(j>=off+fs/50 && blendAfter==0)blendAfter=tuner.currentCycleBlend();
        j+=count;
    }
    for(int i=off;i<n;++i)maxErr=std::max(maxErr,std::abs(double(ol[i]+orr[i])));
    std::printf("EXIT fs%d block%d blend before %.6f firstBlock %.6f after %.6f stereoErr %.8g\n",fs,block,blendBefore,blendImmediately,blendAfter,maxErr);
    check(blendBefore>0.9,"Cycle mode fully engaged before exit");
    check(blendAfter<1e-6,"Cycle mode fades out within 20ms");
    check(maxErr<1e-5,"Stereo polarity persists through mode exit");
}
int main(){
    for(int fs : {44100,48000,96000}) testGrainTail(fs,220.0);
    for(int block : {1,64,257}) testIntegratedExit(48000,block);
    return fail?1:0;
}
