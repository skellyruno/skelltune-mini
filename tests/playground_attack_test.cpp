#include "PlaygroundBank.hpp"
#include <iostream>
#include <cmath>
#include <cstdlib>
#include <vector>
static void need(bool yes,const char* msg){if(!yes){std::cerr<<"FAIL: "<<msg<<'\n';std::exit(1);}}
int main(){
 for(int fs:{44100,48000,96000})
 for(int profile=0;profile<int(skelly::playground::latencyNames.size());++profile)
 for(int mode=0;mode<skelly::playground::modeCount;++mode)
 for(float gain:{1.f,.1f,.01f}){
    std::vector<float> l(fs),r(fs);for(int i=0;i<fs;++i){double ph=2*skelly::kPi*235.8*i/fs;l[i]=gain*float(.13*std::sin(ph)+.032*std::sin(2*ph+.2));r[i]=.7f*l[i];}
    const int event=fs/2;l[event]+=gain*.78f;r[event]+=gain*.70f;r[3*fs/4]+=gain*.77f;
    const auto dryL=l,dryR=r;
    skelly::playground::Bank bank;bank.prepare(fs,2,127,profile,mode);const int delay=bank.latencySamples();
    skelly::Parameters p;p.speedMs=0;p.pitchClasses=1u<<9;p.transientProtection=1.f;
    for(int i=0;i<fs;i+=127){float* io[]={l.data()+i,r.data()+i};bank.process(io,2,std::min(127,fs-i),p,mode);}
    const double error=std::max({std::abs(double(l[event+delay]-dryL[event])),std::abs(double(r[event+delay]-dryR[event])),std::abs(double(r[3*fs/4+delay]-dryR[3*fs/4])),std::abs(double(l[3*fs/4+delay]-dryL[3*fs/4]))})/gain;
    std::cout<<fs<<" "<<skelly::playground::names[size_t(mode)]<<" profile="<<profile<<" gain="<<gain<<" normalized_attack_error="<<error<<'\n';
    for(int i=0;i<fs;++i)need(std::isfinite(l[i])&&std::isfinite(r[i])&&std::abs(l[i])<1.1f&&std::abs(r[i])<1.1f,"attack overload/nonfinite");
    need(error<1e-6,"level-relative sharp attack not aligned to declared PDC");
 }
 std::cout<<"PASS: exact stereo attack anchors at all available latencies and three input levels\n";
}
