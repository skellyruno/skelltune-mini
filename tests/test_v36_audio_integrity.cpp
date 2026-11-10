// v3.6 end-to-end audio-safety and timing checks, with the production engine.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
void check(bool ok, const char* reason) {
    if (!ok) { std::cerr << "FAIL: " << reason << '\n'; std::exit(1); }
}
struct Render {
    std::vector<float> left, right;
    double cpuSeconds = 0.0;
    int reportedDelay = 0;
};
Render run(const std::vector<float>& input, int fs, int block,
           const skelly::Parameters& params, bool injectBadSamples = false) {
    skelly::RealtimeTuner engine;
    engine.prepare(fs, 2);
    engine.setParameters(params);
    Render result;
    result.reportedDelay = engine.latencySamples();
    result.left.resize(input.size());
    result.right.resize(input.size());
    std::vector<float> l(input), r(input.size());
    for (size_t i = 0; i < input.size(); ++i)
        r[i] = -l[i];
    if (injectBadSamples) {
        for (size_t i=2048; i<input.size(); i+=1237) {
            l[i] = std::numeric_limits<float>::quiet_NaN();
            r[i] = std::numeric_limits<float>::infinity();
        }
        for (size_t i=3011; i<input.size(); i+=997) {
            l[i] = std::numeric_limits<float>::denorm_min();
            r[i] = -std::numeric_limits<float>::denorm_min();
        }
    }
    const auto before = std::chrono::steady_clock::now();
    for (int pos=0;pos<int(input.size());pos+=block) {
        const int n=std::min(block,int(input.size())-pos);
        const float* in[2] = {l.data()+pos, r.data()+pos};
        float* out[2] = {result.left.data()+pos, result.right.data()+pos};
        engine.process(in,out,n,2);
    }
    const auto after = std::chrono::steady_clock::now();
    result.cpuSeconds = std::chrono::duration<double>(after-before).count();
    for (size_t i=0;i<input.size();++i) {
        check(std::isfinite(result.left[i]) && std::isfinite(result.right[i]),
              "nonfinite sample reached the audio output");
        const auto normalOrZero = [](float x) {
            return x == 0.0f || std::fpclassify(x) == FP_NORMAL;
        };
        check(normalOrZero(result.left[i]) && normalOrZero(result.right[i]),
              "denormal sample reached the audio output");
    }
    check(result.reportedDelay==int(std::lround(fs*.003)),
          "realtime reported transport delay drifted");
    return result;
}
std::vector<float> makeVoice(int fs, double seconds) {
    const int n=int(fs*seconds);
    std::vector<float> out(size_t(n),0.0f);
    double phase=0.0;
    uint32_t rng=0x9e3779b9u;
    for(int i=0;i<n;++i) {
        const double t=double(i)/fs;
        const double f0=(t<.20 ? 108.0 : (t<.42 ? 220.0 : 330.0));
        phase+=2.0*skelly::kPi*f0/fs;
        rng=rng*1664525u+1013904223u;
        const double air=(double((rng>>8)&0xffffu)/32768.0-1.0);
        out[size_t(i)]=float(0.17*std::sin(phase)
                      +0.055*std::sin(2.0*phase)
                      +0.004*air);
    }
    return out;
}
void dryImpulse(int fs) {
    std::vector<float> input(size_t(fs/8),0.0f);
    const int impulse=fs/64;
    input[size_t(impulse)]=0.7f;
    skelly::Parameters p;
    p.amount=0.0f; p.mix=0.0f;
    auto r=run(input,fs,257,p);
    for(size_t i=0;i<input.size();++i){
        const auto index=int(i)-r.reportedDelay;
        const float want=index>=0?input[size_t(index)]:0.0f;
        check(r.left[i]==want && r.right[i]==-want,
              "delayed-dry impulse not exact at reported sample");
    }
}
void pitchedSignal(int fs) {
    auto input=makeVoice(fs,.64);
    skelly::Parameters p;
    p.speedMs=0.0f; p.mix=1.0f; p.amount=1.0f;
    p.transientProtection=1.0f; p.airPreservation=.65f;
    p.pitchClasses=0x0fffu;
    auto small=run(input,fs,1,p);
    auto large=run(input,fs,257,p);
    double peak=0.0;
    double maxStep=0.0;
    for(size_t i=0;i<input.size();++i) {
        check(std::abs(small.left[i]-large.left[i])<1e-5,
              "audio differs between one-sample and 257-frame callbacks");
        check(std::abs(small.right[i]-large.right[i])<1e-5,
              "stereo audio differs between callback sizes");
        peak=std::max(peak,std::abs(double(large.left[i])));
        if (i>0) maxStep=std::max(maxStep,
            std::abs(double(large.left[i])-double(large.left[i-1])));
    }
    // Headroom test input peaks below 0.23. The processor must not amplify
    // normal vocal-level material to full-scale / produce full-scale clicks.
    check(peak<0.98,"vocal-headroom test produced clipping");
    check(maxStep<0.9,"vocal-headroom test produced a discontinuity");
    const auto bad=run(input,fs,191,p,true);
    for(float value:bad.left)check(std::abs(value)<0.98f,
        "nonfinite input introduced clipping / invalid audio");
    std::cout << fs << " Hz peak=" << peak << " max-step=" << maxStep
              << " cpu/audio=" << large.cpuSeconds/.64 << "x\n";
}
}
int main(){
    for(int fs : {44100,48000,96000}) {
        dryImpulse(fs);
        pitchedSignal(fs);
    }
    std::cout<<"PASS: v3.6 real-engine signal safety, dry PDC, callback invariance\n";
    return 0;
}
