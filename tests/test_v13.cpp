// v1.3: fractional mark refinement, causal safety and stereo invariance.
#include "CausalCycleSynth.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdint>

namespace {
constexpr double pi=3.1415926535897932384626433832795;
int errors=0;
void check(bool yes,const char* why){std::printf("%s: %s\n",yes?"PASS":"FAIL",why);errors+=!yes;}
struct Result{std::vector<float> signal;skelly::CausalCycleSynth::Status stats;double invalid=0;};
Result render(double f0,double ratio,int fs,bool refine,int sourceChannel=0,bool warble=false,bool lookup=true){
    skelly::CausalCycleSynth synth;synth.prepare(fs,2);synth.setFractionalMarksEnabled(refine);synth.setWindowLookupEnabled(lookup);
    Result r;r.signal.reserve(fs*2);
    int gaps=0;
    double phase=0;
    for(int i=0;i<fs*2;++i){
        const double pitch = warble ? f0+3.2*std::sin(2*pi*5.2*i/fs) : f0;
        phase+=2*pi*pitch/fs;
        const float x=float(.45*std::sin(phase)+.16*std::sin(2*phase+.4)+.10*std::sin(3*phase+.8));
        float in[2]{},out[2]{};in[sourceChannel]=x;
        synth.processSample(in,out,fs/pitch,ratio,true);
        if(i>fs/2 && !synth.hasOutputFrame())++gaps;
        r.signal.push_back(out[sourceChannel]);
    }
    r.stats=synth.status();r.invalid=double(gaps)/double(fs*3/2);
    return r;
}
// Correlation of the output with itself; parabolic sub-sample interpolation of
// the lag peak. This is an objective frequency measurement, not an audio rating.
double estimateFrequency(const std::vector<float>& signal,int fs,double expected){
    const int first=int(signal.size())-fs/2;
    const int target=int(std::round(fs/expected));
    const int lo=std::max(2,target-9),hi=target+9;
    double best=-1e30;int bestLag=target;
    double prev=0,center=0,next=0;
    std::vector<double> corr(size_t(hi-lo+1),0.0);
    for(int lag=lo;lag<=hi;++lag){
        double xy=0,xx=1e-20,yy=1e-20;
        for(int i=first;i<int(signal.size());i+=2){
            double x=signal[i],y=signal[i-lag];
            xy+=x*y;xx+=x*x;yy+=y*y;
        }
        const double score=xy/std::sqrt(xx*yy);
        corr[size_t(lag-lo)]=score;
        if(score>best){best=score;bestLag=lag;}
    }
    double interpolated=bestLag;
    if(bestLag>lo && bestLag<hi){
        prev=corr[size_t(bestLag-1-lo)];center=corr[size_t(bestLag-lo)];next=corr[size_t(bestLag+1-lo)];
        const double denom=prev-2*center+next;
        if(std::abs(denom)>1e-14)interpolated+=std::clamp(.5*(prev-next)/denom,-.5,.5);
    }
    return fs/interpolated;
}
}
int main(){
    const int fs=48000;
    bool seenFractional=false;
    for(auto [source,ratio]:std::vector<std::pair<double,double>>{{217.31,1.11},{233.37,.89},{111.43,1.13},{311.81,.83}}){
        auto base=render(source,ratio,fs,false,0);
        auto updated=render(source,ratio,fs,true,0);
        auto right=render(source,ratio,fs,true,1);
        const double target=source*ratio;
        const double measured=estimateFrequency(updated.signal,fs,target);
        const double cents=1200*std::log2(measured/target);
        double diff=0,power=1e-20,swapError=0;
        for(int i=fs;i<fs*2;++i){
            const double a=base.signal[size_t(i)],b=updated.signal[size_t(i)];
            diff+=(a-b)*(a-b);power+=b*b;
            swapError=std::max(swapError,std::abs(b-double(right.signal[size_t(i)])));
        }
        const double relative=std::sqrt(diff/power);
        std::printf("F0 %.2f ratio %.3f -> measured %.3f (%+.2f cents); %llu refined marks, mean offset %.3f samples; A/B RMS %.3f\n",
            source,ratio,measured,cents,(unsigned long long)updated.stats.refinedPitchMarks,
            updated.stats.meanAbsoluteMarkRefinement,relative);
        check(std::abs(cents)<12,"Noninteger-period test reaches requested frequency");
        check(updated.stats.dropped==0 && updated.invalid<0.002,"No grain drops / output gaps");
        check(updated.stats.minimumSourceAge>=3.0 && updated.stats.maximumSourceAge<fs*.1,"Only historical samples read");
        check(swapError<1e-6,"Stereo-channel placement does not change reconstruction");
        check(base.stats.refinedPitchMarks==0,"Fractional A/B switch disables refinements");
        seenFractional|=updated.stats.refinedPitchMarks>10 && updated.stats.meanAbsoluteMarkRefinement>0.001;
    }
    auto warble=render(217.31,1.11,fs,true,0,true);
    auto oldWarble=render(217.31,1.11,fs,false,0,true);
    std::printf("VIBRATO: refined %llu fractional marks, mean absolute adjustment %.6f samples\n",
        (unsigned long long)warble.stats.refinedPitchMarks,warble.stats.meanAbsoluteMarkRefinement);
    seenFractional|=warble.stats.refinedPitchMarks>50 && warble.stats.meanAbsoluteMarkRefinement>0.001;
    check(warble.stats.dropped==0 && warble.invalid<.002,"Vibrato: no dropped synthesis frames");
    check(seenFractional,"Fractional peak interpolation is exercised on nonstationary waveforms");
    // The lookup must not trade reduced CPU usage for a noticeably different
    // grain window or loss of continuity in the pitched output.
    const auto lookup=render(225.17,1.14,fs,true,0,false,true);
    const auto trig=render(225.17,1.14,fs,true,0,false,false);
    double windowErr=0,windowPower=1e-20,maxDelta=0;
    for (int i=fs;i<fs*2;++i) {
        const double delta=lookup.signal[size_t(i)]-trig.signal[size_t(i)];
        windowErr+=delta*delta;windowPower+=double(trig.signal[size_t(i)])*trig.signal[size_t(i)];
        maxDelta=std::max(maxDelta,std::abs(delta));
    }
    windowErr=std::sqrt(windowErr/windowPower);
    std::printf("WINDOW LOOKUP: normalized RMS error %.9g, max sample delta %.9g\n",windowErr,maxDelta);
    check(windowErr<0.00001 && maxDelta<0.00003,"Lookup window matches direct sine-squared normalization");
    return errors?1:0;
}
