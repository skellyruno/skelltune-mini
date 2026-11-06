// v2.1: bounded synthesis timebase, causal source reads and separate plugin PDC.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdint>

static int checks = 0, failed = 0;
void check(bool ok, const char* message) {
    ++checks;
    if(!ok) { ++failed; std::printf("FAIL: %s\n", message); }
}
struct Result {
    skelly::CausalCycleSynth::Status st;
    double rms=0, pitch=0;
    double stereoError=0;
    std::vector<float> out;
};
// Shared fundamental with independent moving harmonics and stable inverted stereo.
Result synth(int fs, double f0, double ratio, bool clocked, bool stereo) {
    skelly::CausalCycleSynth s;
    s.prepare(fs, stereo ? 2 : 1);
    if (clocked) s.setClockLockedDelay(skelly::studioDelayContract(fs).reportedSamples);
    const int n=int(fs*.85);
    Result r; r.out.reserve(size_t(n));
    double phase=0, sum=0;
    float in[2]{}, out[2]{};
    for(int k=0;k<n;++k) {
        const double time=double(k)/fs;
        const double f=f0*(1+0.004*std::sin(2*skelly::kPi*4.2*time));
        phase+=2*skelly::kPi*f/fs;
        const float signal=float(.28*std::sin(phase)+.14*std::sin(2*phase+.13)
                          +.07*std::sin(3*phase-.34*std::sin(time*3)));
        in[0]=signal; in[1]=-signal;
        s.processSample(in,out,fs/f,ratio,true);
        r.out.push_back(out[0]);
        if(k>n/2) {
            sum+=double(out[0])*out[0];
            if(stereo) r.stereoError=std::max(r.stereoError,
                      std::abs(double(out[0])+double(out[1])));
        }
    }
    r.rms=std::sqrt(sum/(n-n/2-1));
    r.st=s.status();
    // Strong fundamental or second harmonic: measure pitch using autocorrelation
    // on the ending 0.16 seconds, constrained near the intended output pitch.
    const int length=fs/7;
    const int minLag=int(fs/(f0*ratio*1.09));
    const int maxLag=int(fs/(f0*ratio*.91));
    double best=-1e200;
    int bestLag=0;
    for(int lag=std::max(2,minLag);lag<=maxLag;++lag) {
        double xy=0, xx=1.e-12, yy=1.e-12;
        for(int i=n-length;i<n-lag;i+=2) {
            const double a=r.out[size_t(i)],b=r.out[size_t(i+lag)];
            xy+=a*b;xx+=a*a;yy+=b*b;
        }
        const double corr=xy/std::sqrt(xx*yy);
        if(corr>best){best=corr;bestLag=lag;}
    }
    if(bestLag>0) r.pitch=double(fs)/bestLag;
    return r;
}
void testStudio(int fs,double f0,double ratio) {
    auto r=synth(fs,f0,ratio,true,true);
    const auto expected=skelly::studioDelayContract(fs).reportedSamples;
    const double P=double(fs)/f0;
    const auto st=r.st;
    const double mean=st.meanWeightedSourceAge;
    const double maxOffset=st.maximumTimelineOffset;
    std::printf("fs=%d f0=%.1f ratio=%.2f PDC=%d sourceAge=%.1f (offset max %.1f) rms=%.4f pitch=%.2f events=%llu dropped=%llu\n",
        fs,f0,ratio,expected,mean,maxOffset,r.rms,r.pitch,
        (unsigned long long)st.spawned,(unsigned long long)st.dropped);
    check(st.timelineGrains>25,"Studio produces phase-locked grains");
    check(st.dropped==0,"No dropped grains in steady voiced audio");
    check(st.minimumSourceAge>3,"Never reads future audio");
    check(st.timelineGrains>0,"Timeline diagnostics count actual grains");
    check(std::abs(mean-expected)<P*.60,"Weighted wet source age remains near fixed timeline");
    check(maxOffset<P*.60,"Individual grain centres remain within a pitch-mark radius");
    check(std::abs(r.pitch-f0*ratio)/(f0*ratio)<.018,"Voiced output actually changes pitch");
    check(r.rms>.055 && r.rms<.65,"Voiced output level remains finite and nonzero");
    check(r.stereoError<1.e-6,"Clocked synthesis preserves inverted stereo");
}
void testPluginContract(int fs) {
    skelly::RealtimeTuner live,studio;
    live.prepare(fs,1);
    studio.prepare(fs,1,true);
    check(live.latencySamples()==int(std::lround(fs*.003)),"Live PDC stays at 3ms");
    check(studio.latencySamples()==int(std::lround(fs*.050)),"Studio PDC is exactly 50ms");
    check(!studio.outputDelayContract(true).fixedWetWaveformDelay,
          "Does not falsely claim sample-to-sample fixed shift delay");
    skelly::Parameters p; p.amount=0; p.mix=1; p.experimentalCycleSynthesis=true;
    studio.setParameters(p);
    const int length=int(fs*.12), event=fs/100;
    std::vector<float> audio(size_t(length),0.0f),out(size_t(length),0.0f);
    audio[size_t(event)]=0.8f;
    const float* in[]={audio.data()};float* dest[]={out.data()};
    studio.process(in,dest,length,1);
    bool exact=true;
    for(int k=0;k<length;++k) {
        const float x=(k==event+studio.latencySamples())?0.8f:0.0f;
        if(out[size_t(k)]!=x)exact=false;
    }
    check(exact,"Studio dry path matches declared 50ms sample-for-sample");
}
struct EngineRender {
    std::vector<float> output;
    skelly::CausalCycleSynth::Status status{};
    int latency=0;
};
EngineRender studioEngine(int fs,int block) {
    constexpr double inputHz=225.0;
    const int n=int(fs*.9), event=fs/2;
    std::vector<float> input(size_t(n),0.0f);
    for(int k=0;k<n;++k) {
        const double x=.28*std::sin(2*skelly::kPi*inputHz*k/fs)
                      +.11*std::sin(2*skelly::kPi*inputHz*2*k/fs+.33);
        input[size_t(k)]=float(x);
    }
    input[size_t(event)]+=0.92f;
    skelly::RealtimeTuner tuner;
    tuner.prepare(fs,1,true);
    skelly::Parameters p;
    p.speedMs=1.f; p.amount=1.f; p.mix=1.f;
    p.airPreservation=0.f; p.experimentalCycleSynthesis=true;
    tuner.setParameters(p);
    EngineRender r;
    r.latency=tuner.latencySamples();
    r.output.assign(size_t(n),0.0f);
    for(int at=0;at<n;at+=block) {
        const int len=std::min(n-at,block);
        const float* src[]={input.data()+at};
        float* dst[]={r.output.data()+at};
        tuner.process(src,dst,len,1);
    }
    r.status=tuner.cycleSynthStatus();
    check(std::abs(double(r.output[size_t(event+r.latency)])-double(input[size_t(event)]))<1e-6,
        "Studio preserves a sharp attack at declared 50ms delay under wet tuning");
    return r;
}
void testEngine(int fs) {
    auto a=studioEngine(fs,1);
    auto b=studioEngine(fs,257);
    double diff=0, rms=0;
    const int start=int(fs*.65),end=int(fs*.80);
    for(int k=0;k<int(a.output.size());++k) {
        diff=std::max(diff,std::abs(double(a.output[size_t(k)])-double(b.output[size_t(k)])));
        if(k>=start && k<end)rms+=double(a.output[size_t(k)])*a.output[size_t(k)];
    }
    rms=std::sqrt(rms/(end-start));
    std::printf("studio end-to-end %dHz max callback delta %.9f rms %.4f frames %llu mean source age %.2fms\n",
        fs,diff,rms,(unsigned long long)a.status.validFrames,
        1000*a.status.meanWeightedSourceAge/fs);
    check(diff<1e-6,"Engine output invariant under host callback block size");
    check(rms>.05,"Studio produces active wet vowel instead of silence");
    check(a.status.timelineGrains>40,"Studio integrates clocked grains in tuner");
    check(std::abs(a.status.meanWeightedSourceAge-a.latency)<fs/220.0*.60,
          "Integrated studio source age follows declared timeline");
}
void testChangingVoice() {
    constexpr int fs=48000;
    const int n=fs*13/10;
    skelly::CausalCycleSynth synth;
    synth.prepare(fs,2);
    synth.setClockLockedDelay(skelly::studioDelayContract(fs).reportedSamples);
    double phase=0,rms=0,maxStep=0,previous=0;
    int valid=0;
    for(int k=0;k<n;++k) {
        const bool second=k>fs*6/10;
        const double f=second ? 164.81 : 110.0;
        const double ratio=second ? .92 : 1.12;
        phase+=2*skelly::kPi*f/fs;
        float in[2]={float(.25*std::sin(phase)+.13*std::sin(2*phase+.1)),0.0f},out[2]{};
        in[1]=-in[0];
        synth.processSample(in,out,fs/f,ratio,true);
        check(std::isfinite(out[0]) && std::isfinite(out[1]),"Changing voiced output stays finite");
        if(k>fs/5) {
            rms+=double(out[0])*out[0];
            maxStep=std::max(maxStep,std::abs(double(out[0])-previous));
            valid+=synth.hasOutputFrame();
        }
        previous=out[0];
    }
    const auto st=synth.status();
    rms=std::sqrt(rms/(n-fs/5));
    std::printf("110->164.81Hz note change: rms %.4f peak step %.4f grains %llu max offset %.2fms\n",
      rms,maxStep,(unsigned long long)st.spawned,1000*st.maximumTimelineOffset/fs);
    check(valid>n/2,"Clocked path remains active through note change");
    check(st.maximumTimelineOffset < fs/110.0*.60,"Note transition does not escape source timeline");
    check(maxStep/std::max(1e-8,rms)<2.0,"Note transition has no extreme single-sample jump");
    check(st.dropped==0,"No scheduler exhaustion at note transition");
}
int main() {
    testChangingVoice();
    for(int fs:{44100,48000,96000}) {
        testPluginContract(fs);
        testEngine(fs);
        for(double f0:{85.0,110.0,220.0}) {
            testStudio(fs,f0,1.10);
            testStudio(fs,f0,0.90);
        }
    }
    std::printf("v2.1 clocked timebase %d/%d checks pass\n",checks-failed,checks);
    return failed!=0;
}
