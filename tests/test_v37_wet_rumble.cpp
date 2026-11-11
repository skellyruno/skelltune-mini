// v3.7: independent 12 Hz wet rumble guard and complete live-engine checks.
#include "RealtimeTuner.hpp"
#include "WetRumbleGuard.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition,const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
double response(int fs,double frequency) {
    skelly::WetRumbleGuard guard;
    guard.prepare(fs);
    constexpr double pi=3.14159265358979323846;
    double reference=0.0,actual=0.0;
    for(int i=0;i<fs*2;++i) {
        const double input=0.3*std::sin(2.0*pi*frequency*i/fs);
        const double out=guard.process(0,input);
        require(std::isfinite(out),"filter nonfinite");
        if(i>=fs) {reference+=input*input;actual+=out*out;}
    }
    return std::sqrt(actual/reference);
}
void testRumbleGuard(int fs) {
    const auto d05=response(fs,5.0),d10=response(fs,10.0);
    const auto d20=response(fs,20.0),d65=response(fs,65.0);
    const auto d95=response(fs,95.0),d220=response(fs,220.0);
    std::cout << fs << " Hz: 5 Hz=" << d05 << " 10 Hz=" << d10
              << " 20 Hz=" << d20 << " 65 Hz=" << d65
              << " 95 Hz=" << d95 << " 220 Hz=" << d220 << '\n';
    require(d05<0.20 && d10<0.50 && d20<0.84,"insufficient subsonic rejection");
    require(d65>0.95 && d95>0.97 && d220>0.99,"vocal fundamental loss");
    skelly::WetRumbleGuard guard;guard.prepare(fs);
    for(int i=0;i<fs*2;++i){
        const double left=guard.process(0,.25),right=guard.process(1,-.25);
        require(std::abs(left+right)<1e-10,"stereo linked symmetry changed");
        if(i>fs)require(std::abs(left)<1e-3,"DC not rejected");
    }
    guard.reset();
    require(std::abs(guard.process(0,.25)-guard.process(1,.25))<1e-12,
            "reset left or right stale");
    guard.reset();
    for(int i=0;i<fs*3;++i) {
        const double output=guard.process(0, i<256 ? 0.25 : 0.0);
        require(std::isfinite(output),"wet filter silence recovery became nonfinite");
        require(output==0.0 || std::fpclassify(output)==FP_NORMAL,
                "subnormal wet filter state on long silence");
        if(i==fs*3-1)require(output==0.0,"wet filter should settle to exact zero");
    }
}
void testEngineDC(int fs) {
    skelly::RealtimeTuner dsp;
    dsp.prepare(fs,2);
    skelly::Parameters p;
    p.speedMs=0.0f; p.mix=1.0f; p.amount=1.0f;
    p.pitchClasses=1u<<9; // A, target 220 Hz
    dsp.setParameters(p);
    const int block=64, n=fs*2;
    float inputA[block],inputB[block],outputA[block],outputB[block];
    const float* input[]{inputA,inputB};
    float* output[]{outputA,outputB};
    double mean=0.0,energy=0.0;
    constexpr double pi=3.14159265358979323846;
    for(int offset=0; offset<n; offset+=block) {
        const int count=std::min(block,n-offset);
        for(int i=0;i<count;++i) {
            const double t=(offset+i)/double(fs);
            const float sample=float(.08 + .28*std::sin(2*pi*225*t)
                +.08*std::sin(4*pi*225*t));
            inputA[i]=sample;inputB[i]=-sample;
        }
        dsp.process(input,output,count,2);
        if(offset>fs)for(int i=0;i<count;++i){
            mean+=outputA[i];energy+=outputA[i]*outputA[i];
            require(std::abs(outputA[i]+outputB[i])<0.000002,"stereo DC symmetry");
        }
    }
    const auto count=double(n-fs-block);
    std::cout << "engine " << fs << " Hz: wet output mean="
              << mean/count << ", RMS=" << std::sqrt(energy/count)
              << ", effective wet=" << dsp.effectiveWetMix()
              << ", PDC=" << dsp.latencySamples() << '\n';
    require(dsp.latencySamples()==int(std::lround(fs*0.003)),"PDC changed");
    require(std::abs(mean/count)<0.030,"wet signal retained low-frequency DC/rumble");
}
} // namespace
int main(){
    for(int fs:{44100,48000,96000}) {testRumbleGuard(fs);testEngineDC(fs);}
    std::cout << "PASS: wet rumble rejection and stable low-vocal response\n";
}
