// META research: unvoiced noise, release, amount=0 and bypass remain clean.
#include "MetaTest.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <limits>
static void need(bool good,const char* msg){if(!good){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}}
static constexpr double pi=3.14159265358979323846;
int main(){
    constexpr int sr=48000,frames=sr*2;
    std::vector<float> source(frames),out(frames);
    unsigned r=0x5732a3b1u;double phase=0;
    for(int i=0;i<frames;++i){
        const double t=double(i)/sr;
        r=r*1664525u+1013904223u;
        const double noise=(double((r>>8)&65535u)/32768.0-1.0)*0.08;
        phase+=2*pi*(t<1.0?235.8:325.0)/sr;
        source[i]=float(t<0.60?0.22*std::sin(phase):
            t<1.00?noise:t<1.40?0.0:0.18*std::sin(phase));
    }
    skelly::MetaTest tuned;tuned.prepare(sr);
    tuned.setParameters(0x0fffu,440,1,1.25);
    out=source;
    for(int i=0;i<frames;i+=128){float* io[1]={out.data()+i};tuned.process(io,1,128);}
    for(float v:out)need(std::isfinite(v)&&std::abs(v)<1.5f,"META output not finite/bounded");
    double err=0,noiseEnergy=0;
    const int delay=144;
    // After unvoiced release has completed, META must not smear
    // fricatives by repeatedly resampling the previous sung pitch.
    for(int i=int(.78*sr);i<int(.96*sr);++i){
        err=std::max(err,std::abs(double(out[i]-source[i-delay])));
        noiseEnergy+=double(out[i])*out[i];
    }
    std::cout<<"META unvoiced max delayed-dry error="<<err<<"\n";
    need(err<1.0e-3,"META stretched unvoiced fricatives instead of delayed dry");
    need(noiseEnergy>1.0,"unvoiced passage incorrectly silenced");
    skelly::MetaTest dry;dry.prepare(sr);dry.setParameters(0x0fffu,440,0,1.25);
    std::vector<float> dryOut=source;
    for(int i=0;i<frames;i+=128){float* io[1]={dryOut.data()+i};dry.process(io,1,128);}
    for(int i=delay+32;i<frames;++i)
        need(std::abs(dryOut[i]-source[i-delay])<1e-6f,
             "META Amount=0 is not sample-exact delayed dry");
    skelly::MetaTest bypass;bypass.prepare(sr);
    std::vector<float> bypassOut=source;
    for(int i=0;i<frames;i+=128){
        float* io[1]={bypassOut.data()+i};bypass.process(io,1,128,true);
    }
    // A bypassed plugin still reports PDC to the host. Both its normal
    // Amount=0 path and bypass must emit the SAME delayed dry PCM.
    need(bypass.latencySamples()==delay,"META bypass latency differs from dry");
    need(bypassOut==dryOut,"META bypass must match exact delayed dry PCM");
    // A host can send NaNs, infinities and subnormals while switching
    // devices or loading projects. Reject those within META's internal
    // pitch tracker and grain history without contaminating future audio.
    skelly::MetaTest corrupt;corrupt.prepare(sr);
    corrupt.setParameters(0x0fffu,440,1,1.25);
    std::vector<float> bad=source;
    for(int i=4000;i<frames-1;i+=1777) {
        bad[i]=std::numeric_limits<float>::quiet_NaN();
        bad[i+1]=std::numeric_limits<float>::infinity();
    }
    for(int i=7000;i<frames;i+=2603)
        bad[i]=std::numeric_limits<float>::denorm_min();
    for(int i=0;i<frames;i+=128){
        float* io[1]={bad.data()+i};corrupt.process(io,1,128);
    }
    for(float v:bad)
        need(std::isfinite(v) && std::abs(v)<1.5f,
             "invalid host input poisoned META grain buffer or output");
    std::cout<<"PASS: META voiced/unvoiced dry protection, PDC, bypass and bad host samples\n";
}