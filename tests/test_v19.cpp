// v1.9: never equate historical PSOLA source age with declared host latency.
// Verify exact bypass transport, bounded/finite grain ages, high-register
// baseline equivalence, and a genuinely modified low-vocal synthesis path.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static constexpr double pi=3.1415926535897932384626433832795;
static int failures=0;
static void check(bool ok,const char* message){
    std::printf("%s %s\n",ok?"PASS":"FAIL",message);failures+=!ok;
}
static void impulse(int fs){
    skelly::RealtimeTuner t;t.prepare(fs,1);
    skelly::Parameters p;p.amount=0;p.mix=1;t.setParameters(p);
    const int n=fs/8;
    std::vector<float> input(n,0),out(n,0);
    input[79]=1;
    const float* i[]{input.data()};float* o[]{out.data()};
    t.process(i,o,n,1);
    const int delay=t.latencySamples();
    bool exact=true;
    for(int k=0;k<n;++k) {
        const float expected=(k==79+delay)?1.f:0.f;
        if (out[size_t(k)]!=expected){exact=false;break;}
    }
    std::printf("fs=%d dry delay=%d samples (%.6fms)\n",fs,delay,1000.*delay/fs);
    check(exact,"Dry transport equals reported integer-sample delay");
}
static void cycleTest(int fs,double f0,bool high){
    skelly::CausalCycleSynth synth;
    const int n=fs*11/10;
    std::vector<float> audio(size_t(n),0),previous(size_t(n),0),updated(size_t(n),0);
    double phase=0;
    for(int k=0;k<n;++k){
        double t=k/double(fs);
        const double f=f0*(1+.008*std::sin(2*pi*5.1*t));
        phase+=2*pi*f/fs;
        // Strong upper-harmonic structure plus changing relative harmonic phase.
        audio[size_t(k)]=float(.34*std::sin(phase)+.20*std::sin(2*phase+.15*std::sin(2*pi*1.2*t))
            +.09*std::sin(4*phase+.3*std::cos(2*pi*1.8*t)));
    }
    skelly::CausalCycleSynth::Status old{}, now{};
    for(int mode=0;mode<2;++mode){
        synth.prepare(fs,1);
        synth.setWidePhaseReferenceEnabled(mode!=0);
        float in[2]{},output[2]{};
        for(int k=0;k<n;++k){
            in[0]=audio[size_t(k)];
            const double t=k/double(fs);
            const double period=fs/(f0*(1+.008*std::sin(2*pi*5.1*t)));
            synth.processSample(in,output,period,1.09,true);
            (mode?updated:previous)[size_t(k)]=output[0];
        }
        (mode?now:old)=synth.status();
    }
    double maxDelta=0;
    for(int k=fs/2;k<n;++k)maxDelta=std::max(maxDelta,std::abs(double(previous[size_t(k)]-updated[size_t(k)])));
    std::printf("fs=%d f0=%.1f PSOLA: old mean age %.3fms new %.3fms (max delta %.7f)\n",
        fs,f0,1000*old.meanWeightedSourceAge/fs,1000*now.meanWeightedSourceAge/fs,maxDelta);
    check(now.spawned>30 && now.dropped==0,"Steady PSOLA generates cycles without dropping grains");
    check(now.minimumSourceAge>=3,"All synthesized source samples are causal");
    check(now.meanWeightedSourceAge>=now.minimumSourceAge && now.meanWeightedSourceAge<=now.maximumSourceAge,
        "Weighted source age is within measured bounds");
    check(now.meanWeightedSourceAge>fs*.006,"Wet source age explicitly exceeds nominal 3ms dry path");
    check(high?maxDelta==0:maxDelta>1.e-6,
        high?"Higher-note matcher preserves v1.8 samples":"Low-note PSOLA actually uses broader pitch-mark evidence");
}
static void crossoverSafety() {
    constexpr int fs=48000;
    constexpr int n=fs*2;
    skelly::CausalCycleSynth synth;synth.prepare(fs,1);
    double phase=0,previous=0,maxStep=0,energy=0;
    float input[2]{},out[2]{};
    for(int k=0;k<n;++k) {
        const double t=k/double(fs);
        const double f= k<fs ? 124.0 + 2.0*std::sin(2*pi*4.3*t) : 159.0 + 2.0*std::sin(2*pi*4.3*t);
        phase+=2*pi*f/fs;
        input[0]=float(.3*std::sin(phase)+.12*std::sin(2*phase+.3));
        synth.processSample(input,out,fs/f,1.08,true);
        if(k>fs/3) {
            maxStep=std::max(maxStep,std::abs(double(out[0])-previous));
            energy+=double(out[0])*out[0];
        }
        previous=out[0];
        if(!std::isfinite(out[0])){check(false,"Crossover output finite");return;}
    }
    const auto st=synth.status();
    const double rms=std::sqrt(energy/double(n-fs/3));
    std::printf("Low-register crossover max step/rms %.3f; grains %llu, dropped %llu\n",
       maxStep/std::max(1e-8,rms),(unsigned long long)st.spawned,(unsigned long long)st.dropped);
    check(st.dropped==0 && st.spawned>200,"Low-register matcher transition avoids grain loss");
    check(maxStep/std::max(1e-8,rms)<1.5,"Low-register matcher crossover avoids extreme single-sample spikes");
}
int main(){
    for(const int fs:{44100,48000,96000})impulse(fs);
    for(const int fs:{44100,48000,96000})cycleTest(fs,95,false);
    cycleTest(48000,220,true);
    crossoverSafety();
    return failures!=0;
}
