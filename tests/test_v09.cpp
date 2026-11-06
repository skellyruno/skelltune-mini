#include "CausalCycleSynth.hpp"
#include "RealtimeTuner.hpp"
#include <cmath>
#include <iostream>
#include <vector>

#include <algorithm>
#include <limits>

static int failed=0;
static void check(bool okay,const char* what) {
    std::cout<<(okay?"PASS ":"FAIL ")<<what<<'\n';
    failed+=!okay;
}
static double getPitch(const std::vector<float>& x,int fs,int start,double expected) {
    // Normalized correlation, on 0.2 second steady-state suffix.
    const int count=std::min(int(x.size())-start,fs/5);
    double best=-2;int bestLag=0;
    for(int lag=int(fs/(expected*1.10));lag<=int(fs/(expected*.90));++lag){
        double xy=0,xx=0,yy=0;
        for(int j=0;j<count-lag;++j){
            const double a=x[size_t(start+j)],b=x[size_t(start+j+lag)];
            xy+=a*b;xx+=a*a;yy+=b*b;
        }
        const double corr=xy/(std::sqrt(xx*yy)+1e-16);
        if(corr>best){best=corr;bestLag=lag;}
    }
    return bestLag?double(fs)/bestLag:0.0;
}
static void testShift(int fs,double inputHz,double ratio){
    skelly::CausalCycleSynth synth;
    synth.prepare(fs,2);
    constexpr double pi=3.14159265358979323846;
    std::vector<float> left(size_t(fs*2)),right(left.size());
    for(int i=0;i<fs*2;++i){
        const double t=double(i)/fs;
        const float x=float(0.6*std::sin(2*pi*inputHz*t)+0.14*std::sin(2*pi*2*inputHz*t));
        const float in[2]={x,-x};float out[2]{};
        synth.processSample(in,out,double(fs)/inputHz,ratio,true);
        left[size_t(i)]=out[0];right[size_t(i)]=out[1];
    }
    const auto metrics=synth.status();
    // Use the most recent ~200ms for periodicity; this is an experimental
    // algorithm and not meant to be a commercial quality claim.
    const double pitch=getPitch(left,fs,fs+fs/2,inputHz*ratio);
    const double desired=inputHz*ratio;
    const double cents=1200.0*std::log2(std::max(1e-6,pitch)/desired);
    double maxDiff=0, energy=0,finite=1;
    for(int i=fs;i<fs*2;++i){maxDiff=std::max(maxDiff,std::abs(double(left[i]+right[i])));
      energy+=double(left[i])*left[i]; finite=finite && std::isfinite(left[i]);}
    std::cout<<"CYCLE fs="<<fs<<" input="<<inputHz<<" ratio="<<ratio
             <<" outHz="<<pitch<<" errC="<<cents
             <<" minSourceAge="<<metrics.minimumSourceAge
             <<" grains="<<metrics.spawned<<" drops="<<metrics.dropped<<" energy="<<energy<<'\n';
    check(metrics.spawned>100 && metrics.dropped==0,"cycle scheduler has stable grain availability");
    check(metrics.minimumSourceAge>=3.0,"cycle resynthesis accesses only available past samples");
    check(std::abs(cents)<35.0,"experimental PSOLA output reaches target pitch on synthetic tone");
    check(energy>100 && finite,"experimental resynthesis outputs bounded, nonzero audio");
    check(maxDiff<1e-6,"two antiphase channels retain relative phase");
}

static void testResetAndTransitions(){
    constexpr int fs=48000;
    skelly::CausalCycleSynth synth; synth.prepare(fs,1);
    float out[2]{},in[2]{};
    bool good=true;
    for(int i=0;i<fs;++i){
        in[0]=float(0.5*std::sin(i*2*skelly::kPi*220./fs));
        const bool voiced=(i<fs/3 || i>fs/2);
        const double ratio=(i<fs/2)?1.1:0.9;
        synth.processSample(in,out,double(fs)/220.,ratio,voiced);
        good = good && std::isfinite(out[0]) && std::abs(out[0])<3.f;
        // v1.2 preserves only historical grains briefly after unvoicing;
        // once the existing grain windows finish, the output is silent.
        if(!voiced && i > fs/3+fs/25 && std::abs(out[0])>1e-6) good=false;
    }
    check(good,"grain generator stays bounded through unvoiced transitions and ratio automation");
    synth.reset();
    check(synth.status().spawned==0 && synth.status().minimumSourceAge==0,
        "reset discards grain and latency diagnostics");
    in[0]=std::numeric_limits<float>::quiet_NaN();
    synth.processSample(in,out,200.,1.1,true);
    check(std::isfinite(out[0]),"nonfinite audio input cannot poison experimental synthesis");
}

int main(){
 testResetAndTransitions();
 testShift(48000,220.0,1.10);
 testShift(48000,220.0,0.90);
 testShift(44100,145.0,1.12);
 testShift(96000,300.0,0.88);
 return failed?1:0;
}
