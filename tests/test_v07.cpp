#include "RealtimeTuner.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

static int failures=0;
static void check(bool value, const char* name) {
    std::cout << (value ? "PASS " : "FAIL ") << name << '\n';
    if (!value) ++failures;
}
struct Result {
    double minAge=0, maxAge=0, rms=0, lowEnvelope=0, avgEnvelope=0, highEnvelope=0;
    double peak=0, maxGain=0;
    unsigned long long wrapCount=0, activeWraps=0; double cycleCorr=0;
    std::vector<float> audio;
};
static Result render(int rate,int block,bool compensation,bool phaseMatch,bool invertedStereo=false) {
    constexpr double pi=3.14159265358979323846;
    skelly::RealtimeTuner engine;
    engine.prepare(rate,invertedStereo ? 2 : 1);
    skelly::Parameters p;
    p.speedMs=3; p.mix=1; p.amount=1; p.airPreservation=0;
    p.transientProtection=0;
    p.phaseAlignedJumps=phaseMatch;
    p.cycleLevelStabilization=compensation ? 1.f : 0.f;
    engine.setParameters(p);
    const int size=rate*3;
    std::vector<float> in(size),out(size),right(size),outRight(size);
    double phase=0;
    for (int i=0;i<size;++i) {
        const double t=double(i)/rate;
        const double hz=225.0+9.*std::sin(2*pi*6.7*t)+3.*std::sin(2*pi*13.2*t);
        phase+=2*pi*hz/rate;
        in[i]=float(0.27*std::sin(phase)+0.11*std::sin(2*phase+0.5)+
                    0.05*std::sin(3*phase+1.3)+0.01*std::sin(2*pi*3810.0*t));
        right[i]=-in[i];
    }
    for(int offset=0;offset<size;offset+=block) {
        const int n=std::min(block,size-offset);
        const float* src[]={in.data()+offset,right.data()+offset};
        float* dst[]={out.data()+offset,outRight.data()+offset};
        engine.process(src,dst,n,invertedStereo?2:1);
    }
    Result r;
    r.audio=std::move(out);
    r.minAge=engine.minimumWetSourceAgeSamples();
    r.maxAge=engine.maximumWetSourceAgeSamples();
    r.maxGain=engine.maximumOverlapGain(); r.wrapCount=engine.cycleWrapCount(); r.activeWraps=engine.activeWrapCount(); r.cycleCorr=engine.lastCycleCorrelation();
    std::vector<double> localRms;
    for(int start=rate;start+rate/250<size;start+=rate/250) {
        double sum=0;
        for(int j=start;j<start+rate/250;++j) {
            const auto x=double(r.audio[j]);
            sum+=x*x; r.peak=std::max(r.peak,std::abs(x));
        }
        localRms.push_back(std::sqrt(sum/(rate/250)));
    }
    std::sort(localRms.begin(),localRms.end());
    r.lowEnvelope=localRms[localRms.size()/10];
    r.avgEnvelope=localRms[localRms.size()/2];
    r.highEnvelope=localRms[localRms.size()*9/10];
    for(int i=rate;i<size;++i) r.rms+=double(r.audio[i])*r.audio[i];
    r.rms=std::sqrt(r.rms/(size-rate));
    if (invertedStereo) {
        double diff=0;
        for(int i=rate;i<size;++i) diff=std::max(diff, std::abs(double(r.audio[i]+outRight[i])));
        check(diff<1e-5,"stereo phase relationship still preserved with overlap normalization");
    }
    return r;
}
int main() {
    for (int rate : {44100,48000,96000}) {
        const auto r=render(rate,64,true,true,true);
        std::cout << "RATE "<<rate<<" minAge "<<r.minAge<<" maxAge "<<r.maxAge<<
          " publishedDelay "<<std::lround(rate*.003)<<"\n";
        check(r.minAge>=std::lround(rate*.003)+4,"wet source never newer than nominal transport latency");
        check(r.maxAge>r.minAge && std::isfinite(r.rms) && r.rms>0.02,"wet audio remains active with variable source age");
    }
    const auto off=render(48000,64,false,false);
    const auto on=render(48000,64,true,false);
    double changed = 0; for (size_t i=0;i<off.audio.size();++i) changed=std::max(changed,std::abs(double(off.audio[i])-on.audio[i]));
    std::cout << "CYCLE maximum change "<<changed<<"\n";
    std::cout << "CYCLE LEVEL baseline mean "<<off.rms<<" stabilized mean "<<on.rms
              <<" wraps "<<off.wrapCount<<" active "<<off.activeWraps<<" maxGain off="<<off.maxGain<<" on="<<on.maxGain<<" last-corr "<<off.cycleCorr<<" low-percentile "<<off.lowEnvelope<<" vs "<<on.lowEnvelope
              <<" high-percentile "<<off.highEnvelope<<" vs "<<on.highEnvelope
              <<" peaks "<<off.peak<<" vs "<<on.peak<<"\n";
    // v2.8's shorter high-register detector window changes wrap placements,
    // so this synthetic example requires less gain correction. Verify that
    // the compensator ACTIVATES and changes audio instead of insisting on
    // an arbitrary >=0.001 waveform difference from the old tracker.
    check(on.maxGain > off.maxGain + 0.001 && changed > 0.0001,
          "cycle-level normalization activates and changes wet overlap audio");
    check(on.lowEnvelope>=off.lowEnvelope*.998,"overlap compensation does not weaken low-level cycle windows");
    check(on.peak<1.2,"bounded normalization avoids large output peaks");
    const auto b1=render(48000,1,true,true);
    const auto b257=render(48000,257,true,true);
    double maxErr=0;
    for (size_t i=0;i<b1.audio.size();++i) maxErr=std::max(maxErr,std::abs(double(b1.audio[i])-b257.audio[i]));
    std::cout << "BLOCK maximum output difference " <<maxErr<<"\n";
    check(maxErr<1e-6,"overlap compensated output remains block-size independent");
    skelly::RealtimeTuner bad;
    bad.prepare(48000,1);
    skelly::Parameters p;
    const float nan=std::numeric_limits<float>::quiet_NaN();
    p.speedMs=nan; p.mix=nan; p.amount=nan; p.airPreservation=nan;
    p.flexCents=nan; p.referenceA=nan; p.cycleLevelStabilization=nan;
    bad.setParameters(p);
    std::array<float,1024> a{},b{};
    for(int i=0;i<1024;++i) a[size_t(i)]=float(.2*std::sin(2*skelly::kPi*220*i/48000.0));
    const float* src[]={a.data()}; float* dst[]={b.data()};
    for(int i=0;i<40;++i) bad.process(src,dst,1024,1);
    check(std::all_of(b.begin(),b.end(),[](float v){return std::isfinite(v);}),"nonfinite automation values are sanitized");
    return failures?1:0;
}
