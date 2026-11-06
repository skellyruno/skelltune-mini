// v2.2 40ms Fast Studio latency contract and audio path regression.
// Source-age is NOT an impulse/group delay of an arbitrary pitch-shifted vowel.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
static int checks=0,failures=0;
static void check(bool v, const char* name) { ++checks; if(!v){++failures;std::printf("FAIL %s\n", name);} }
struct Run {
    skelly::CausalCycleSynth::Status st;
    double rms=0.0, stereoError=0.0;
    int valid=0;
};
static Run synthCase(int fs,double f0,double ratio) {
    skelly::CausalCycleSynth synth;
    synth.prepare(fs,2);
    synth.setClockLockedDelay(skelly::studioFastDelayContract(fs).reportedSamples);
    const int n=fs*65/100;
    Run r; double phase=0, sum=0;
    for(int k=0;k<n;++k) {
        const double t=double(k)/fs;
        const double hz=f0*(1.0+0.0035*std::sin(2*skelly::kPi*4.1*t));
        phase+=2*skelly::kPi*hz/fs;
        const float x=float(.31*std::sin(phase)+.13*std::sin(2*phase+0.13)+.07*std::sin(3*phase));
        float input[2]{x,-x},output[2]{};
        synth.processSample(input,output,fs/hz,ratio,true);
        if(k>fs/4) {
            r.valid+=int(synth.hasOutputFrame());
            sum+=double(output[0])*output[0];
            r.stereoError=std::max(r.stereoError,std::abs(double(output[0])+output[1]));
        }
    }
    r.rms=std::sqrt(sum/(n-fs/4));
    r.st=synth.status();
    return r;
}
static void testSynthesis(int fs,double hz,double ratio) {
    auto r=synthCase(fs,hz,ratio);
    const double period=fs/hz;
    const double offset=std::abs(r.st.meanWeightedSourceAge-skelly::studioFastDelayContract(fs).reportedSamples);
    if(r.st.dropped || r.st.timelineOutOfBounds>9 || r.valid<(fs*.24))
       std::printf("WARN synthesis fs=%d hz=%.1f ratio=%.2f grains=%llu invalid=%llu rejected=%llu\n",fs,hz,ratio,(unsigned long long)r.st.timelineGrains,(unsigned long long)r.st.timelineOutOfBounds,(unsigned long long)r.st.dropped);
    if (r.st.timelineGrains<=20) std::printf("low grain case: %d Hz %.1f f0 %.2f ratio %llu grains %d valid samples\n",fs,hz,ratio,(unsigned long long)r.st.timelineGrains,r.valid);
    check(r.st.timelineGrains>15,"Fast Studio emits voiced grains across supported low range");
    check(r.st.dropped==0,"No grain pool exhaustion");
    // Startup may suppress a few incomplete grains before the clocked history is
    // full. These are explicitly counted and do not imply unsafe memory reads.
    check(r.st.timelineOutOfBounds<10,"Startup/phase rejects remain bounded");
    check(r.valid>fs*.24,"Voiced output does not drop out");
    check(r.st.minimumSourceAge>3.0,"All reads remain causal");
    check(r.st.maximumTimelineOffset<period*.60,"Bounded mark deviation");
    check(offset<period*.60,"Weighted source timeline follows 40ms contract");
    check(r.rms>.04 && r.rms<.55,"Voiced energy remains in healthy range");
    check(r.stereoError<1.e-6,"Inverted stereo remains inverted");
}
static void testDelay(int fs) {
    const int fast=int(std::lround(fs*.040));
    skelly::RealtimeTuner live,studio,fastStudio;
    live.prepare(fs,1);
    studio.prepare(fs,1,true);
    fastStudio.prepare(fs,1,true,true);
    check(live.latencySamples()==int(std::lround(fs*.003)),"Live PDC stays 3ms");
    check(studio.latencySamples()==int(std::lround(fs*.050)),"Original Studio PDC stays 50ms");
    check(fastStudio.latencySamples()==fast,"Studio Fast declares 40ms");
    check(fastStudio.outputDelayContract(true).reportedSamples==fast,"JUCE contract uses 40ms");
    check(!fastStudio.outputDelayContract(true).fixedWetWaveformDelay,"No false fixed tonal delay claim");
    skelly::Parameters dry; dry.amount=0.0f; dry.mix=1; dry.experimentalCycleSynthesis=true;
    fastStudio.setParameters(dry);
    int n=fs/7,impulse=fs/100;
    std::vector<float> x(size_t(n),0),y(size_t(n),0);
    x[size_t(impulse)]=0.85f;
    const float* src[]{x.data()};float* dst[]{y.data()};
    fastStudio.process(src,dst,n,1);
    bool exact=true;
    for(int k=0;k<n;++k) {
        const float expected=k==impulse+fast?0.85f:0.0f;
        if(y[size_t(k)]!=expected) {exact=false;break;}
    }
    check(exact,"40ms bypass is sample-exact including zeros");
}
struct Full {
    int latency=0;
    std::vector<float> samples;
    skelly::CausalCycleSynth::Status st;
};
static Full render(int fs,int block) {
    skelly::RealtimeTuner engine;
    engine.prepare(fs,1,true,true);
    skelly::Parameters p;p.speedMs=1;p.amount=1;p.mix=1;
    p.airPreservation=0;p.experimentalCycleSynthesis=true;
    engine.setParameters(p);
    int n=fs*85/100,impulse=fs*45/100;
    std::vector<float> x(size_t(n),0),y(size_t(n),0);
    for(int k=0;k<n;++k)
        x[size_t(k)]=float(.31*std::sin(2*skelly::kPi*225.*k/fs)+.12*std::sin(2*skelly::kPi*450.*k/fs+.23));
    x[size_t(impulse)]+=0.9f;
    for(int pos=0;pos<n;pos+=block) {
        int len=std::min(block,n-pos);
        const float* src[]{x.data()+pos};float* dst[]{y.data()+pos};
        engine.process(src,dst,len,1);
    }
    Full r;r.samples=std::move(y);r.latency=engine.latencySamples();r.st=engine.cycleSynthStatus();
    check(std::abs(r.samples[size_t(impulse+r.latency)]-x[size_t(impulse)])<1e-6,
          "Protected vocal attack is precisely 40ms delayed even at 100% wet");
    return r;
}
static void testFull(int fs){
    auto a=render(fs,1), b=render(fs,257);
    double maxDiff=0,sum=0;
    int begin=fs*6/10,end=fs*8/10;
    for(size_t k=0;k<a.samples.size();++k){
        maxDiff=std::max(maxDiff,std::abs(double(a.samples[k])-b.samples[k]));
        if(int(k)>=begin && int(k)<end)sum+=a.samples[k]*a.samples[k];
    }
    check(maxDiff<1e-6,"Host block partition has no influence on samples");
    check(std::sqrt(sum/(end-begin))>.05,"Actually pitch-corrected wet output is active");
    check(a.st.timelineGrains>35,"Clocked pitch cycles integrated in whole tuner");
    check(std::abs(a.st.meanWeightedSourceAge-a.latency)<fs/220.0*.60,
          "Whole-engine voice synthesis follows 40ms timeline");
}
static void testTransition(){
    constexpr int fs=48000;
    skelly::CausalCycleSynth synth;
    synth.prepare(fs,2);
    synth.setClockLockedDelay(skelly::studioFastDelayContract(fs).reportedSamples);
    double phase=0,prev=0,peak=0,sum=0;int voicedFrames=0;
    for(int k=0;k<fs;++k) {
        double f=k<fs/2 ? 65.0 : 110.0;
        double ratio=k<fs/2 ? 1.10 : .92;
        phase+=2*skelly::kPi*f/fs;
        const float x=float(.31*std::sin(phase)+.13*std::sin(2*phase+.13));
        float input[2]{x,-x},out[2]{};
        synth.processSample(input,out,fs/f,ratio,true);
        if(k>fs/4){sum+=out[0]*out[0];peak=std::max(peak,std::abs(double(out[0])-prev));voicedFrames+=synth.hasOutputFrame();}
        prev=out[0];
    }
    auto st=synth.status();
    const double rms=std::sqrt(sum/(fs-fs/4));
    check(voicedFrames>fs/2,"65-to-110 Hz note change retains output");
    check(st.dropped==0,"Note transition never exhausts grain slots");
    check(st.timelineOutOfBounds==0,"No invalid clock source after transition");
    check(peak/std::max(.001,rms)<2.0,"No extreme single-sample transient at note change");
}
int main() {
    testTransition();
    for(int fs:{44100,48000,96000}) {
        testDelay(fs);testFull(fs);
        for(double hz:{65.0,70.0,85.0,110.0,220.0})
            for(double ratio:{.70,.90,1.10,1.40})testSynthesis(fs,hz,ratio);
    }
    std::printf("v2.2 40ms clocked latency: %d/%d checks passed\n",checks-failures,checks);
    return failures!=0;
}
