// v68: extended bass tracking and sample-exact attack preservation.
#include "RealtimeTuner.hpp"
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
#include "MetaTest.hpp"
#endif
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
namespace {
constexpr double pi=3.14159265358979323846;
void need(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
double pitch(const std::vector<float>& y,int rate){
    double first=0,last=0;int count=0;
    for(int i=rate/2;i<int(rate*.9);++i){const double a=y[size_t(i-1)],b=y[size_t(i)];
        if(a<=0&&b>0){const double crossing=i-1-a/(b-a);if(!count)first=crossing;last=crossing;++count;}}
    need(count>=18,"missing bass cycles");return (count-1)*rate/(last-first);
}
std::vector<float> run(const std::vector<float>& input,int rate,int mode,int block,bool bypass=false){
    std::vector<float> out(input.size());
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    if(mode==2){
        auto m=std::make_unique<skelly::MetaTest>();m->prepare(rate,1.5);m->setParameters(0xfff,440,1,0);
        out=input;for(size_t i=0;i<out.size();i+=block){float* io[1]={out.data()+i};m->process(io,1,int(std::min<size_t>(block,out.size()-i)),bypass);}
        need(m->latencySamples()==int(std::lround(rate*.0015)),"META latency changed");return out;
    }
#endif
    skelly::RealtimeTuner t;t.prepare(rate,1,false,false,1.5);skelly::Parameters p;p.speedMs=0;p.testTune=mode==1;p.amount=p.mix=1;t.setParameters(p);
    for(size_t i=0;i<input.size();i+=block){const float* in[1]={input.data()+i};float* dst[1]={out.data()+i};t.process(in,dst,int(std::min<size_t>(block,input.size()-i)),1);}
    need(t.latencySamples()==int(std::lround(rate*.0015)),"normal latency changed");return out;
}
}
int main(){
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    constexpr int modes=3;
#else
    constexpr int modes=2;
#endif
    for(int rate:{44100,48000,96000,192000}){
        for(double hz:{51.2,55.8,61.1,64.2}){
            std::vector<float> src(rate);for(int i=0;i<rate;++i){const double ph=2*pi*hz*i/rate;src[size_t(i)]=float(.16*std::sin(ph)+.037*std::sin(2*ph+.2)+.025*std::sin(3*ph+.6));}
            const double midi=std::round(69+12*std::log2(hz/440.));const double target=440*std::exp2((midi-69)/12);
            for(int mode=0;mode<modes;++mode){const auto y=run(src,rate,mode,127);for(float s:y)need(std::isfinite(s)&&std::abs(s)<.6f,"unstable bass audio");
                const double e=1200*std::log2(pitch(y,rate)/target);std::cout<<rate<<" mode="<<mode<<" bass="<<hz<<" error="<<e<<"c\n";need(std::abs(e)<2.,"sub-65Hz correction failed");}
        }
        // Pure line-frequency sine is not a vocal. Existing narrow notches
        // must still reject it once their causal history has settled.
        for(double hz:{50.,60.}){skelly::CausalPitchTracker t;t.prepare(rate);skelly::PitchEstimate e{};int total=0,accepted=0;
            for(int i=0;i<rate*2;++i){if(t.push(float(.18*std::sin(2*pi*hz*i/rate)),e)&&i>rate){++total;if(e.valid)++accepted;}}
            need(total>10&&accepted==0,"steady mains hum falsely selected as voice");}
        // Bass extension must not depend on whether the host passes odd or
        // ordinary blocks, or leave old rolling differences after a reset.
        std::vector<float> src(rate);double phase=0;
        for(int i=0;i<rate;++i){const double hz=i<rate/3?55.8:(i<2*rate/3?220.5:61.1);phase+=2*pi*hz/rate;src[size_t(i)]=float(.16*std::sin(phase)+.05*std::sin(2*phase));}
        for(int mode=0;mode<modes;++mode){const auto a=run(src,rate,mode,1),b=run(src,rate,mode,257);for(size_t i=0;i<a.size();++i)need(std::abs(a[i]-b[i])<1.e-5,"new tracker not callback invariant");}
    }
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    // A high-contrast transient embedded in a fully voiced note must
    // emerge at the declared 1.5ms dry delay, not the wet grain age.
    for(int rate:{44100,48000,96000}){
        std::vector<float> x(rate);for(int i=0;i<rate;++i)x[size_t(i)]=float(.13*std::sin(2*pi*225*i/rate)+.032*std::sin(4*pi*225*i/rate+.2));
        const int event=rate/2; x[size_t(event)]+=.78f;
        const int delay=int(std::lround(rate*.0015)); const auto y=run(x,rate,2,127);
        const double err=std::abs(y[size_t(event+delay)]-x[size_t(event)]);
        std::cout<<"META attack at "<<rate<<": aligned error="<<err<<'\n';need(err<1.e-6,"META sharp attack missed declared latency");
        for(int i=0;i<int(x.size());++i)need(std::isfinite(y[size_t(i)])&&std::abs(y[size_t(i)])<1.1f,"META attack overload");
        const auto dry=run(x,rate,2,257,true);for(int i=0;i<int(x.size());++i)need(dry[size_t(i)]==(i>=delay?x[size_t(i-delay)]:0.f),"META bypass lost exact dry delay");
    }
#endif
    std::cout<<"PASS: extended bass, line hum, callback invariance, attack and dry timing\n";
}
