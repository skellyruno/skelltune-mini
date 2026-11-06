#include "../dsp/RealtimeTuner.hpp"
#define skelly skelly_v04
#define skelly_research skelly_research_v04
#include "baseline/NoteDecision.hpp"
#include "baseline/RealtimeTuner_v04.hpp"
#undef skelly
#undef skelly_research
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
#include <cmath>
#include <array>
#include <algorithm>

static int fails=0, tests=0;
void check(bool result, const char* name) { ++tests; if(!result)++fails;std::cout<<(result?"PASS ":"FAIL ")<<name<<'\n'; }
uint32_t rng=1234567;
float noise() {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return float(double(rng)/4294967295.0*2-1);}

void rollingParity(int rate, int kind) {
    skelly::CausalPitchTracker fresh;
    skelly_v04::CausalPitchTracker old;
    fresh.prepare(rate); old.prepare(rate);
    const int samples=rate*2;
    double phase=0, maxCents=0, maxConfidence=0;
    int checked=0, mismatchedValidity=0, truthChecked=0, truthCorrect=0, previousCorrect=0;
    for(int i=0;i<samples;++i) {
        const double t=double(i)/rate;
        const double freq=kind==0 ? 80.0 + 100.0*t : kind==1 ? 225.0 + 5.0*std::sin(2*skelly::kPi*6*t) :
                           kind==2 ? (i < samples/2 ? 225.0 : 440.0) : 155.0 + 50*std::sin(2*skelly::kPi*0.8*t);
        phase+=2*skelly::kPi*freq/rate;
        const float x=float(0.25*std::sin(phase)+0.10*std::sin(2*phase+0.3)+
                            0.04*std::sin(3*phase+0.2)+(kind==3?0.14*noise():0.005*noise()));
        skelly::PitchEstimate newer;
        skelly_v04::PitchEstimate older;
        const bool a=fresh.push(x,newer),b=old.push(x,older);
        if (a!=b) ++mismatchedValidity;
        if(a&&b) {
            if(newer.valid!=older.valid) ++mismatchedValidity;
            if(newer.valid && older.valid) {
                ++checked;
                if (i > int(rate*0.15)) {
                    ++truthChecked;
                    if (std::abs(1200.0*std::log2(newer.hertz/freq))<25.0) ++truthCorrect;
                    if (std::abs(1200.0*std::log2(older.hertz/freq))<25.0) ++previousCorrect;
                }
                maxCents=std::max(maxCents,std::abs(1200.0*std::log2(newer.hertz/older.hertz)));
            }
            maxConfidence=std::max(maxConfidence,std::abs(double(newer.confidence)-older.confidence));
        }
    }
    std::cout<<"  rate="<<rate<<" kind="<<kind<<" verified="<<checked
             <<" new/old truth within25="<<truthCorrect<<"/"<<previousCorrect<<" out of "<<truthChecked
             <<" max cents="<<maxCents<<" max conf="<<maxConfidence<<" validity differences="<<mismatchedValidity<<'\n';
    // v2.8 intentionally uses a shorter multi-resolution YIN window, so
    // sample-for-sample parity with the original 24ms tracker is not a
    // meaningful invariant. Check the actual known synthetic input pitch.
    // Include a stronger required fraction of accurate voiced estimates;
    // a few transient hops around jumps may remain uncertain.
    const double minShare = kind == 3 ? 0.50 : (kind == 0 ? 0.85 : 0.96);
    check(truthChecked>200 && double(truthCorrect)/truthChecked>minShare &&
          truthCorrect >= previousCorrect,
          "adaptive YIN follows synthetic ground truth without losing accuracy");
}

double rmsDiff(const std::vector<float>& a,const std::vector<float>& b,int first,int last) {
    double num=0,den=0;
    for(int i=first;i<last;++i){double v=a[i]-b[i];num+=v*v;den+=double(b[i])*b[i];}
    return std::sqrt(num/std::max(1.e-12,den));
}
void transparentAtUnity(bool mixed) {
    constexpr int rate=48000,n=rate*4;
    skelly::RealtimeTuner t;t.prepare(rate,1);
    skelly::Parameters p;p.speedMs=5;p.mix=mixed ? 0.5f:1.0f;p.amount=1.0f;
    t.setParameters(p);
    std::vector<float> input(n),output(n),dry(n);
    double phase=0;
    for(int i=0;i<n;++i){double f=i<rate?225.0:220.0; phase+=2*skelly::kPi*f/rate;
        input[i]=float(.35*std::sin(phase)+.13*std::sin(2*phase)+.05*std::sin(3*phase));
        if(i>=t.latencySamples()) dry[i]=input[i-t.latencySamples()];
    }
    for(int off=0;off<n;off+=127) {
        const float* src[]={input.data()+off};float* dst[]={output.data()+off};
        t.process(src,dst,std::min(127,n-off),1);
    }
    const double early=rmsDiff(output,dry,rate/2,rate*3/4);
    const double late=rmsDiff(output,dry,rate*3,rate*4);
    std::cout<<"  mix="<<p.mix<<" pitch-active residual="<<early<<" centered residual="<<late
             <<" read delay="<<t.currentReadDelaySamples()<<" dry delay="<<t.latencySamples()<<'\n';
    check(early>0.01,"active tuning genuinely modifies audio");
    check(late<0.025,"after pitch reaches target, output converges to exact delayed dry");
    check(std::abs(t.currentReadDelaySamples()-(t.latencySamples()+6.0))<1e-3,"unity correction re-centers guarded read head");
}
void highBandPreservation() {
    constexpr int rate=48000,n=rate*2;
    skelly::RealtimeTuner plain,air;plain.prepare(rate,1);air.prepare(rate,1);
    skelly::Parameters p;p.speedMs=4;p.mix=1;p.amount=1;p.transientProtection=0.45f;
    p.airPreservation=0;plain.setParameters(p);p.airPreservation=1;air.setParameters(p);
    std::vector<float> in(n),o1(n),o2(n);
    double phase=0;
    for(int i=0;i<n;++i) {
        const double t=double(i)/rate;phase+=2*skelly::kPi*225.0/rate;
        const double hiss=(i>rate/2&&i<rate*3/2) ? 0.11*std::sin(2*skelly::kPi*6000*t) : 0;
        in[i]=float(0.25*std::sin(phase)+0.09*std::sin(2*phase)+hiss);
    }
    for(int off=0;off<n;off+=64) {
        const float* src[]={in.data()+off};float* a[]={o1.data()+off};float* b[]={o2.data()+off};
        plain.process(src,a,std::min(64,n-off),1);air.process(src,b,std::min(64,n-off),1);
    }
    // Measure a fixed 6000-Hz sinusoid's amplitude using coherent demodulation
    // over a 0.25-second interval during active correction.
    auto amp=[&](const std::vector<float>& signal) {
        double re=0,im=0;const int start=rate, count=rate/4;
        for(int i=start;i<start+count;++i) {
            double ang=2*skelly::kPi*6000*(i-plain.latencySamples())/rate;
            re+=signal[i]*std::cos(ang);im+=signal[i]*std::sin(ang);
        }
        return 2*std::hypot(re,im)/count;
    };
    double dryAmp=amp(in),plainAmp=amp(o1),preservedAmp=amp(o2);
    std::cout<<"  6kHz magnitude raw="<<dryAmp<<" plain="<<plainAmp<<" air="<<preservedAmp<<'\n';
    check(std::abs(preservedAmp-dryAmp)<std::abs(plainAmp-dryAmp),"air mode moves sibilant HF energy closer to input");
    check(std::all_of(o2.begin(),o2.end(),[](float v){return std::isfinite(v)&&std::abs(v)<2.0f;}),"air processing remains finite/bounded");
}
void benchmarkDetector() {
    constexpr int rate=48000, n=rate*4;
    std::vector<float> audio(n);
    for(int i=0;i<n;++i){double t=double(i)/rate;audio[i]=float(.3*std::sin(2*skelly::kPi*210*t)
                               +.12*std::sin(2*skelly::kPi*420*t)+.08*noise());}
    skelly_v04::CausalPitchTracker old;skelly::CausalPitchTracker modern;
    auto run=[&](auto& tracker){tracker.prepare(rate);auto before=std::chrono::steady_clock::now();
        for (float x:audio) {auto e=tracker.last();tracker.push(x,e);}
        return std::chrono::duration<double>(std::chrono::steady_clock::now()-before).count();};
    double baseline=run(old),updated=run(modern);
    std::cout<<"  4 sec tracker run baseline="<<baseline<<"s rolling="<<updated<<"s speedup="<<baseline/updated<<"x\n";
    check(updated<baseline*0.85,"rolling detector measurably faster than full-window v0.4");
}
void blockPartitionInvariance() {
    constexpr int rate=48000, n=rate;
    skelly::RealtimeTuner a,b;
    a.prepare(rate,1);b.prepare(rate,1);
    skelly::Parameters p;p.speedMs=7;p.mix=1;p.amount=1;p.airPreservation=0.65f;
    a.setParameters(p);b.setParameters(p);
    std::vector<float> in(n),outA(n),outB(n);
    double phase=0;
    for(int i=0;i<n;++i) {
       double f = i<rate/2?225:255;phase+=2*skelly::kPi*f/rate;
       in[i]=float(.32*std::sin(phase)+.14*std::sin(2*phase)+.02*std::sin(31*phase));
    }
    for(int i=0;i<n;++i) {
        const float* src[]={in.data()+i};float* dst[]={outA.data()+i};a.process(src,dst,1,1);
    }
    for(int i=0;i<n;i+=257) {
        const float* src[]={in.data()+i};float* dst[]={outB.data()+i};
        b.process(src,dst,std::min(257,n-i),1);
    }
    double maxDiff=0;
    for(int i=0;i<n;++i)maxDiff=std::max(maxDiff,std::abs(double(outA[i])-outB[i]));
    std::cout<<"  block partition max sample difference="<<maxDiff<<'\n';
    check(maxDiff==0.0,"1-sample and 257-sample callbacks produce identical output");
}
void zeroAmountTransparency() {
    constexpr int rate=48000,n=rate;
    skelly::RealtimeTuner t;t.prepare(rate,1);
    skelly::Parameters p;p.amount=0;p.mix=1;
    t.setParameters(p);
    std::vector<float> in(n),out(n);
    for(int i=0;i<n;++i)in[i]=float(.4*std::sin(2*skelly::kPi*225*i/rate));
    const float* src[]={in.data()};float* dst[]={out.data()};
    t.process(src,dst,n,1);
    bool exact=true;
    for(int i=0;i<n;++i){float expected=i>=t.latencySamples()?in[i-t.latencySamples()]:0;
        if(out[i]!=expected)exact=false;}
    check(exact,"zero correction amount is sample-transparent after nominal dry delay");
}
int main(){
    for(int r:{44100,48000,96000}) for(int kind=0;kind<4;++kind)rollingParity(r,kind);
    transparentAtUnity(false);transparentAtUnity(true);
    highBandPreservation();blockPartitionInvariance();zeroAmountTransparency();benchmarkDetector();
    std::cout<<"v0.5 tests="<<tests<<" failures="<<fails<<'\n';return fails?1:0;
}
