// v1.1: regression tests for pitch-mark stereo invariance and cubic resampling.
#include "CausalCycleSynth.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr double pi=3.14159265358979323846;
int failures = 0;
void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += !ok;
}
struct Render {
    std::vector<float> data;
    skelly::CausalCycleSynth::Status stats;
    double invalidFraction = 0.0;
};
Render render(int fs, int sourceChannel, double ratio) {
    skelly::CausalCycleSynth s;
    s.prepare(fs, 2);
    Render result;
    result.data.reserve(size_t(fs * 2));
    uint64_t gaps = 0;
    for (int i=0; i<fs*2; ++i) {
        const double t = double(i)/fs;
        const double phase = 2.0*pi*(220.0*t + (3.4/6.0)*(1.0-std::cos(2.0*pi*6.0*t))/(2.0*pi));
        const double value = .45*std::sin(phase)+.18*std::sin(2.0*phase+.7)
            +.11*std::sin(3.0*phase+.3)+.035*std::sin(5.0*phase)
            +.02*std::sin(2*pi*617.0*t)+.03*std::sin(2*pi*1999.3*t);
        float in[2]{}, out[2]{};
        in[sourceChannel] = float(value);
        s.processSample(in,out,double(fs)/220.0,ratio,true);
        if (i>fs/4 && !s.hasOutputFrame()) ++gaps;
        result.data.push_back(out[sourceChannel]);
    }
    result.invalidFraction = double(gaps)/double(fs*2-fs/4);
    result.stats = s.status();
    return result;
}
double normalizedError(const Render& a,const Render& b,int start) {
    double sum=0, energy=1e-15;
    for(size_t i=size_t(start);i<a.data.size();++i) {
        const double d=double(a.data[i])-b.data[i];
        sum+=d*d; energy+=double(a.data[i])*a.data[i];
    }
    return std::sqrt(sum/energy);
}
// Test the exact interpolation polynomials used in the two engines on a
// known band-limited sinusoid; does not substitute for audible quality tests.
double interpolatorError(bool cubic, double f) {
    const double omega=2*pi*f/48000.;
    double sum=0;
    int count=0;
    for(int base=0;base<100;++base) for(int q=1;q<=9;++q){
        const double u=q*0.1;
        const auto y=[&](int delta){return std::sin(omega*(base+delta));};
        const double sampled=cubic ?
            (-u*(u-1)*(u-2)/6)*y(-1)
            +((u+1)*(u-1)*(u-2)/2)*y(0)
            -((u+1)*u*(u-2)/2)*y(1)
            +((u+1)*u*(u-1)/6)*y(2)
            : (1-u)*y(0)+u*y(1);
        const double error=sampled-std::sin(omega*(base+u));
        sum+=error*error;
        ++count;
    }
    return std::sqrt(sum/count);
}
} // namespace
int main() {
    const int fs=48000;
    for(double ratio : {0.84, 1.0, 1.16}) {
        auto left=render(fs,0,ratio);
        auto right=render(fs,1,ratio);
        const double err=normalizedError(left,right,fs/2);
        std::printf("RATIO %.2f swapped-channel waveform error %.9f, valid frames %llu, max source age %.1f samples\n",
          ratio,err,(unsigned long long)left.stats.validFrames,left.stats.maximumSourceAge);
        check(err<1.e-7,"identical stereo program yields identical pitched audio regardless of channel placement");
        check(left.stats.spawned>150 && left.stats.dropped==0,"nontrivial grains generated without dropping voices");
        check(left.invalidFraction<.001,"cycle output has no steady-state missing-frame gaps");
        check(left.stats.minimumSourceAge>=3.0 && left.stats.maximumSourceAge<fs*.1,
            "measured cycle source age is causal, bounded, and reported");
    }
    for(double f : {3000.0,6500.0}) {
        const double linear=interpolatorError(false,f);
        const double cubic=interpolatorError(true,f);
        std::printf("SINUSOID %.0f Hz interpolation RMS: linear %.7f cubic %.7f\n",f,linear,cubic);
        check(cubic<linear*.50,"cubic interpolation lowers synthetic HF resampling error");
    }
    skelly::CausalCycleSynth synth;
    synth.prepare(fs,2);
    float input[2]{0.5f,-0.5f},output[2]{};
    for(int i=0;i<fs/2;++i) synth.processSample(input,output,fs/220.0,1.0,true);
    synth.reset();
    check(!synth.hasOutputFrame() && synth.status().validFrames==0 && synth.status().maximumSourceAge==0,
        "reset immediately invalidates output frame and clears instrumentation");
    return failures ? 1 : 0;
}
