// v3.8: a strongly sung periodic vowel should not turn into a different
// time-varying effect when its input gain changes. Measure gain-equivalence
// and preserved-transient activity across a wide but unclipped input level.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void check(bool ok,const char* reason) {
    if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}
}
struct Render {
    std::vector<float> out;
    float peak=0.0f;
    unsigned long long transients=0;
    double wet=0.0;
};
Render run(int rate,double level) {
    skelly::RealtimeTuner engine;
    engine.prepare(rate,1);
    skelly::Parameters p;
    p.speedMs=0; p.amount=1; p.mix=1;
    p.pitchClasses=(1u<<9);
    p.airPreservation=.65f; p.transientProtection=1.0f;
    engine.setParameters(p);
    const int count=int(rate*1.5);
    Render result;result.out.resize(size_t(count));
    float input[64]{},output[64]{};
    const float* in[]{input};float* out[]{output};
    for(int off=0;off<count;off+=64) {
        const int len=std::min(64,count-off);
        for(int i=0;i<len;++i) {
            const double t=double(off+i)/rate;
            const double phase=2.0*skelly::kPi*225.0*t;
            input[i]=float(level*(.68*std::sin(phase)
                        +.22*std::sin(phase*2.0)+.10*std::sin(phase*3.0)));
        }
        engine.process(in,out,len,1);
        for(int i=0;i<len;++i) {
            result.out[size_t(off+i)]=output[i];
            check(std::isfinite(output[i]),"nonfinite loud voice");
            result.peak=std::max(result.peak,std::abs(output[i]));
        }
    }
    result.transients=engine.preservedTransientSamples();
    result.wet=engine.effectiveWetMix();
    return result;
}
} // namespace
int main() {
    for(int rate : {44100,48000,96000}) {
        const auto low=run(rate,.19);
        const auto high=run(rate,.72);
        check(high.peak<1.0f,"high-volume vowel clipped below full-scale input");
        double sumDelta2=0, sumLow2=0, lowPeak=0, highPeak=0;
        const double factor=.72/.19;
        for(size_t i=size_t(rate/2);i<low.out.size();++i) {
            const double expected=double(low.out[i])*factor;
            const double actual=high.out[i];
            sumDelta2+=(actual-expected)*(actual-expected);
            sumLow2+=expected*expected;
            lowPeak=std::max(lowPeak,std::abs(expected));
            highPeak=std::max(highPeak,std::abs(actual));
        }
        const auto mismatch=std::sqrt(sumDelta2/std::max(1e-12,sumLow2));
        std::cout << rate << " Hz: gain-normalized waveform mismatch=" << mismatch
                  << " peaks(low/high)=" << low.peak << "/" << high.peak
                  << " marked attacks(low/high)=" << low.transients
                  << "/" << high.transients
                  << " wet(low/high)=" << low.wet << "/" << high.wet << '\n';
        // Loose guard for now: a dramatic level-driven timbre change means
        // one of the vowel/consonant protective paths is misclassifying tone.
        check(mismatch<0.22,"louder vocal changes retune behavior too much");
        check(high.wet>0.80,"loud periodic voice unexpectedly lost wet correction");
    }
    std::cout<<"PASS: loud sustained-vowel stability\n";
}
