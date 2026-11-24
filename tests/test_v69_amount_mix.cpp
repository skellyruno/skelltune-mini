// v69: amount vs mix semantics, empty scales and manual edits, plus
// bounded detector history on irregular host block sizes.
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
struct Render{std::vector<float> y;int target=-1;int delay=0;};
Render run(int mode,int rate,float amount,float mix,uint16_t mask,int manual=-1,int block=127){
    const int n=rate*2; std::vector<float> input(n);Render r;r.y.resize(n);
    for(int i=0;i<n;++i){const double ph=2*pi*235.8*i/rate;input[size_t(i)]=float(.16*std::sin(ph)+.05*std::sin(2*ph+.2)+.025*std::sin(3*ph+.6));}
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    if(mode==2){auto meta=std::make_unique<skelly::MetaTest>();meta->prepare(rate,1.5);meta->setParameters(mask,440,amount,0,0,mix);meta->setManualTargetMidi(manual);r.y=input;
        for(int i=0;i<n;i+=block){float* io[1]={r.y.data()+i};meta->process(io,1,std::min(block,n-i));}
        const double targetHz=meta->targetHz();r.target=targetHz>0?int(std::lround(69+12*std::log2(targetHz/440.))):-1;r.delay=meta->latencySamples();
    }else
#endif
    {skelly::RealtimeTuner e;e.prepare(rate,1,false,false,1.5);skelly::Parameters p;p.speedMs=0;p.amount=amount;p.mix=mix;p.pitchClasses=mask;p.testTune=mode==1;e.setParameters(p);e.setManualTargetMidi(manual);
        for(int i=0;i<n;i+=block){const float* in[1]={input.data()+i};float* out[1]={r.y.data()+i};e.process(in,out,std::min(block,n-i),1);}r.target=e.currentTargetNote();r.delay=e.latencySamples();}
    need(r.delay==int(std::lround(rate*.0015)),"published transport changed");
    for(float x:r.y)need(std::isfinite(x)&&std::abs(x)<.8f,"unstable output");
    if(amount==0 || mix==0 || (mask==0&&manual<0)){
        for(int i=rate/2;i<n;++i)need(std::abs(r.y[size_t(i)]-input[size_t(i-r.delay)])<1e-6,"dry or empty scale changed audio");
    }
    return r;
}
// Independent complex projection of the actual audio onto a known frequency.
// A window over one second separates corrected, dry and halfway pitches;
// this cannot be fooled by an autocorrelation average of two voices.
double tone(const Render& r,int rate,double hz){double re=0,im=0;for(int i=0;i<rate;++i){const double w=.5-.5*std::cos(2*pi*i/rate);const double phase=2*pi*hz*i/rate;const double x=r.y[size_t(rate/2+i)]*w;re+=x*std::cos(phase);im+=x*std::sin(phase);}return std::hypot(re,im);}
}
int main(){
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    constexpr int modes=3;
#else
    constexpr int modes=2;
#endif
    for(int rate:{44100,48000,96000,192000})for(int mode=0;mode<modes;++mode){
        const auto half=run(mode,rate,.5f,1.f,1u<<9);
        const double target=std::sqrt(235.8*220.0),a=tone(half,rate,target);
        const double ghost=std::max(tone(half,rate,220.),tone(half,rate,235.8));
        const double relative=20*std::log10(std::max(ghost,1e-12)/std::max(a,1e-12));
        std::cout<<rate<<" mode="<<mode<<" half-amount ghost dBc="<<relative<<'\n';
        need(a>rate*.015 && relative<-35.,"Amount created two voices instead of half-strength correction");
        const auto dry=run(mode,rate,1,0,1u<<9);
        const auto zero=run(mode,rate,0,1,1u<<9);
        const auto empty=run(mode,rate,1,1,0);
        need(empty.target==-1,"disabled scale reported an invented target");
        const auto edit=run(mode,rate,1,1,0,57);
        need(edit.target==57,"manual edit failed when scale disabled");
        need(tone(edit,rate,220)>20*tone(edit,rate,235.8),"manual edit did not reach audio");
        if(rate==48000){const auto odd=run(mode,rate,.5f,1,1u<<9,-1,1);for(size_t i=0;i<half.y.size();++i)need(std::abs(half.y[i]-odd.y[i])<1e-5,"partial amount differs across block sizes");}
    }
    std::cout<<"PASS: independent amount and mix, empty scale, explicit pitch edit, host block invariance\n";
}
