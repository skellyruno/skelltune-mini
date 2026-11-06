#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr int sr = 48000;
int failed = 0;
void check(bool cond, const char* name) {
    std::cout << (cond ? "PASS " : "FAIL ") << name << '\n';
    if (!cond) ++failed;
}
struct Outcome {
    double correctionMean=0, correctionStd=0, outputPower=0;
    bool finite=true;
    int target=-1;
    std::vector<float> out;
};
Outcome run(float sharpCents, float vibratoCents, float preserve, float flex, int block, bool keepOutput=false) {
    skelly::RealtimeTuner engine;
    engine.prepare(sr,1);
    skelly::Parameters p;
    p.speedMs=6; p.transientProtection=0; p.airPreservation=0;
    // A-only mask removes intentionally competing targets; isolates vibrato behavior.
    p.pitchClasses = uint16_t(1u << 9);
    p.vibratoPreservation=preserve;
    p.flexCents=flex;
    engine.setParameters(p);
    constexpr int samples=sr*3;
    std::vector<float> input(samples), output(samples);
    double phase=0;
    for(int i=0;i<samples;++i) {
        const double t=double(i)/sr;
        const double cents=sharpCents+vibratoCents*std::sin(2*pi*5.2*t);
        const double f=220.0*std::pow(2.,cents/1200.);
        phase += 2*pi*f/sr;
        input[size_t(i)]=float(.35*std::sin(phase) + .12*std::sin(2*phase));
    }
    double sum=0,sum2=0,power=0;
    int count=0;
    Outcome result;
    for(int offset=0;offset<samples;offset+=block) {
        const float* src[]={input.data()+offset};
        float* dst[]={output.data()+offset};
        const int n=std::min(samples-offset,block);
        engine.process(src,dst,n,1);
        // Approximate per-hop controller statistic; skip first second.
        if(offset > sr && offset < 2*sr) {
            const double cents=engine.appliedCents();
            sum+=cents;sum2+=cents*cents; ++count;
        }
    }
    for(int i=sr;i<2*sr;++i) {
        if(!std::isfinite(output[size_t(i)])) result.finite=false;
        power+=double(output[size_t(i)])*output[size_t(i)];
    }
    result.correctionMean = sum / std::max(1,count);
    result.correctionStd = std::sqrt(std::max(0., sum2 / std::max(1,count) - result.correctionMean*result.correctionMean));
    result.outputPower = power/sr;
    result.target=engine.currentTargetNote();
    if(keepOutput) result.out=std::move(output);
    return result;
}
}
int main(){
    const auto base=run(38,0,0,0,64);
    const auto neutralVibrato=run(38,52,0,0,64);
    const auto preservedVibrato=run(38,52,1,0,64);
    const auto nearHard=run(14,0,0,0,64);
    const auto nearFlex=run(14,0,0,35,64);
    const auto farHard=run(65,0,0,0,64);
    const auto farFlex=run(65,0,0,35,64);
    const auto a=run(38,52,1,25,1,true);
    const auto b=run(38,52,1,25,257,true);
    std::cout<<"BASE "<<base.correctionMean<<" std "<<base.correctionStd<<"\n";
    std::cout<<"VIBRATO baseline="<<neutralVibrato.correctionStd<<" preserved="<<preservedVibrato.correctionStd<<"\n";
    std::cout<<"FLEX near hard="<<nearHard.correctionMean<<" softened="<<nearFlex.correctionMean<<" far hard="<<farHard.correctionMean<<" far soft="<<farFlex.correctionMean<<"\n";
    check(base.target==57 && std::abs(base.correctionMean) > 18,"existing chromatic correction remains engaged");
    check(neutralVibrato.target==57 && preservedVibrato.target==57,"vibrato remains on committed A3 note");
    check(preservedVibrato.correctionStd < .72 * neutralVibrato.correctionStd,"vibrato preservation reduces cancellation control modulation");
    check(std::abs(nearFlex.correctionMean) < .5 * std::abs(nearHard.correctionMean),"flex tune softens near-note correction");
    check(std::abs(farFlex.correctionMean) > .8 * std::abs(farHard.correctionMean),"flex tune still corrects far off notes");
    check(a.finite && b.finite && a.outputPower>0.001 && b.outputPower>0.001,"corrected output remains finite and audible");
    double maxDiff=0;
    for(size_t i=0;i<a.out.size();++i) maxDiff=std::max(maxDiff,std::abs(double(a.out[i])-b.out[i]));
    std::cout<<"BLOCK max_diff="<<maxDiff<<"\n";
    check(maxDiff<1e-6,"audio output independent of host block size");
    return failed ? 1 : 0;
}
