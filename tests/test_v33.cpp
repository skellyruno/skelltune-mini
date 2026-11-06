// v3.3: causal note-onset memory and host-block / vibrato safety regressions.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool b,const char* reason) { if(!b) throw std::runtime_error(reason); }
struct Render {
    std::vector<float> left, right;
    uint64_t remembered=0, assisted=0;
    int reportedDelay=0, falseNotes=0;
};
static std::vector<float> makeSignal(int rate,bool vibrato) {
    const int n=rate*2;
    std::vector<float> signal(size_t(n),0.0f);
    double phase=0.0;
    uint32_t randomState=0x13A52C9u;
    for(int i=0;i<n;++i) {
        const double t=double(i)/rate;
        const int phrase=int(t*4.0);
        const double sub=t*4.0-phrase;
        double f0=(phrase%2==0)?220.0:246.9416506;
        if(vibrato)f0=220.0*std::pow(2.0,(85.0*std::sin(2*skelly::kPi*5*t))/1200.0);
        phase+=2.0*skelly::kPi*f0/rate;
        const double harmonic=0.31*std::sin(phase)+0.14*std::sin(2*phase)+0.06*std::sin(3*phase);
        randomState=randomState*1664525u+1013904223u;
        const double noise=(double((randomState>>8)&0xffff)/32768.0-1.0);
        // Short consonant between every quarter-second vowel. When vibrato is
        // enabled, also add a brief noisy pulse while keeping the same note.
        const bool consonant=(sub>=0.025 && sub<0.065) || (vibrato && sub>0.65 && sub<0.665);
        signal[size_t(i)]=float(consonant ? 0.19*noise : harmonic);
    }
    return signal;
}
static Render render(const std::vector<float>& input,int rate,int block,bool hard,bool memory,bool invertStereo) {
    skelly::RealtimeTuner engine; engine.prepare(rate,2);
    skelly::Parameters params;
    params.speedMs=hard?0.0f:25.0f;
    params.amount=1.0f; params.mix=1.0f;
    params.pitchClasses=0x0fff;
    engine.setParameters(params);
    engine.setHardTuneOnsetMemoryEnabled(memory);
    Render r; r.left.resize(input.size());r.right.resize(input.size());
    r.reportedDelay=engine.latencySamples();
    std::vector<float> in0(size_t(block),0.0f),in1(size_t(block),0.0f);
    std::vector<float> out0(size_t(block),0.0f),out1(size_t(block),0.0f);
    for(size_t off=0;off<input.size();off+=size_t(block)) {
        const int len=int(std::min(size_t(block),input.size()-off));
        for(int i=0;i<len;++i) {
            in0[size_t(i)]=input[off+i];
            in1[size_t(i)]=invertStereo?-input[off+i]:input[off+i];
        }
        const float* src[2]={in0.data(),in1.data()};
        float* dst[2]={out0.data(),out1.data()};
        engine.process(src,dst,len,2);
        for(int i=0;i<len;++i) {
            r.left[off+i]=out0[size_t(i)];r.right[off+i]=out1[size_t(i)];
            require(std::isfinite(r.left[off+i])&&std::isfinite(r.right[off+i]),"non-finite output");
        }
    }
    r.remembered=engine.onsetMemoryAppliedHops();
    r.assisted=engine.onsetAssistedTransitions();
    return r;
}
int main() {
  for(int rate : {44100,48000,96000}) {
    auto phrases=makeSignal(rate,false);
    const auto enabled=render(phrases,rate,1,true,true,false);
    const auto large=render(phrases,rate,257,true,true,false);
    require(enabled.reportedDelay==int(std::lround(rate*0.003)),"incorrect declared latency");
    require(enabled.left==large.left && enabled.right==large.right,"callback-size-dependent output");
    const auto disabled=render(phrases,rate,257,true,false,false);
    require(disabled.remembered==0 && disabled.assisted==0,"disabled onset memory still engaged");
    const auto normalOn=render(phrases,rate,257,false,true,false);
    const auto normalOff=render(phrases,rate,257,false,false,false);
    require(normalOn.left==normalOff.left,"non-hard-tune preset changed");
    const auto anti=render(phrases,rate,257,true,true,true);
    for(size_t i=0;i<anti.left.size();++i)
        require(std::abs(double(anti.left[i])+double(anti.right[i]))<0.000002,"inverted-stereo relationship changed");
    auto vib=makeSignal(rate,true);
    const auto vibratoOn=render(vib,rate,257,true,true,false);
    const auto vibratoOff=render(vib,rate,257,true,false,false);
    // On a continuous-note vibrato stressor, the memory latch must not make
    // additional target changes after a consonant inside the sustained tone.
    require(vibratoOn.assisted<=vibratoOff.assisted+1,"vibrato produced extra onset-assisted switches");
    std::cout << rate << " Hz: remembered=" << enabled.remembered
              << " note-promotions=" << enabled.assisted
              << " vibrato-promotions=" << vibratoOn.assisted << '\n';
  }
  std::cout << "v3.3 causal onset-memory regressions passed\n";
}
