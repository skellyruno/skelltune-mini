#include "PlaygroundBank.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>
static void need(bool ok,const char* why){if(!ok){std::cerr<<"FAIL: "<<why<<'\n';std::exit(1);}}
struct Result{std::vector<float> l,r;int delay;};
static Result render(int rate,int mode,float mix){
    const int n=3*rate;
    Result out{std::vector<float>(n),std::vector<float>(n),0};
    for(int i=0;i<n;++i){const double phase=2*skelly::kPi*235.8*i/rate;out.l[i]=float(.15*std::sin(phase)+.04*std::sin(2*phase+.2));out.r[i]=.7f*out.l[i];}
    out.l[2*rate]+=.78f;out.r[2*rate]+=.7f;
    skelly::playground::Bank bank;bank.prepare(rate,2);out.delay=bank.latencySamples();
    skelly::Parameters p;p.speedMs=0;p.mix=mix;p.amount=1;p.pitchClasses=1u<<9;p.transientProtection=1;
    for(int i=0;i<n;i+=127){float* io[]={out.l.data()+i,out.r.data()+i};bank.process(io,2,std::min(127,n-i),p,mode);}
    for(int i=0;i<n;++i)need(std::isfinite(out.l[i])&&std::isfinite(out.r[i])&&std::abs(out.l[i])<=1.f&&std::abs(out.r[i])<=1.f,"nonfinite or overload");
    return out;
}
int main(){for(int rate:{44100,48000,96000})for(int mode=0;mode<skelly::playground::modeCount;++mode){
    const auto dry=render(rate,mode,0),wet=render(rate,mode,1),half=render(rate,mode,.5f);
    const int event=2*rate+dry.delay;
    need(half.l[event]==dry.l[event]&&half.r[event]==dry.r[event],"attack anchor changed");
    double power=0,ref=0;
    for(int i=event+rate*20/1000;i<event+rate*70/1000;++i){for(int c=0;c<2;++c){const float x=c?half.r[i]:half.l[i],d=c?dry.r[i]:dry.l[i],w=c?wet.r[i]:wet.l[i];power+=double(x)*x;ref+=.5*(double(d)*d+double(w)*w);}}
    const double db=10*std::log10(power/ref);
    std::cout<<rate<<" "<<skelly::playground::names[size_t(mode)]<<" post_attack_mix_dB="<<db<<'\n';
    // Small local power variation is expected as the two pitches beat.
    // Fail the pronounced reacquisition dip, not every difference in phase.
    need(db>-1.8&&db<1.5,"post-attack mix power lost or excessive");
}std::cout<<"PASS: partial mix recovery and exact stereo attack anchors\n";}
