#include "RealtimeTuner.hpp"
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <iostream>
using namespace skelly;
constexpr double pi = 3.141592653589793;
static void require(bool a,const char *why) { if(!a) throw std::runtime_error(why); }
struct Result { std::vector<float> samples; uint64_t bridges=0; double finalMix=0.0; int latency=0; };
Result render(double fs,int block,bool bridge,int channels=1) {
    RealtimeTuner engine; engine.prepare(fs,channels);engine.setShortVoicedBridgeEnabled(bridge);
    Parameters p;p.speedMs=0;p.amount=1;p.mix=1;p.pitchClasses=uint16_t((1<<0)|(1<<2)|(1<<4)|(1<<6)|(1<<7)|(1<<9)|(1<<11));
    p.transientProtection=1.0f; p.airPreservation=0.65f;engine.setParameters(p);
    const size_t frames=size_t(fs*.76);
    Result r;r.samples.resize(frames*channels);r.latency=engine.latencySamples();
    std::vector<float> a(block),b(block),y(block),z(block);
    double phase=0;
    for(size_t base=0;base<frames;base+=block) {
        int count=int(std::min(size_t(block),frames-base));
        for(int j=0;j<count;++j) {
            const double t=double(base+j)/fs;
            // A sustained voiced note, short dropout, second note, then
            // a long silence that must never preserve stale retuning.
            const bool active=(t<.20)||(t>.213&&t<.43);
            const double pitch=t<.213?225.0:255.0;
            phase+=2*pi*pitch/fs;
            const float x=active?float(.30*std::sin(phase)+.13*std::sin(2*phase)+.06*std::sin(3*phase)):0.0f;
            a[j]=x;b[j]=-x;
        }
        const float* in[2]={a.data(),b.data()};float* out[2]={y.data(),z.data()};
        engine.process(in,out,count,channels);
        for(int j=0;j<count;++j) {
            r.samples[(base+j)*channels]=y[j];
            if(channels==2)r.samples[(base+j)*channels+1]=z[j];
        }
    }
    r.bridges=engine.bridgedHops();r.finalMix=engine.effectiveWetMix();return r;
}
int main() {
    for(double fs:{44100.,48000.,96000.}) {
        for(int ch:{1,2}) {
            const auto a=render(fs,1,true,ch),b=render(fs,257,true,ch),c=render(fs,257,false,ch);
            require(a.latency==int(std::lround(fs*.003)),"declared latency changed");
            require(a.samples.size()==b.samples.size(),"block size changed frame count");
            for(size_t i=0;i<a.samples.size();++i){
                require(std::isfinite(a.samples[i]),"non-finite audio");
                require(a.samples[i]==b.samples[i],"block size dependency");
            }
            require(a.finalMix<.00005,"correction held after long silence");
            require(b.finalMix<.00005,"stale wet mix on other block size");
            // Stereo anti-phase should be preserved by a linked controller.
            if(ch==2)for(size_t i=200;i<a.samples.size()/2;++i)
                require(std::abs(a.samples[2*i]+a.samples[2*i+1])<1.e-5,"stereo coherence");
            // An enabled bridge may only change the wet path, not latency.
            require(c.latency==a.latency,"bridge alters delay contract");
        }
    }
    std::cout<<"v3.1 timed voice-bridge, reset, stereo and block tests passed\n";
}
