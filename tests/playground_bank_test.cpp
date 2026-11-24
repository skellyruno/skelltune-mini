#include "PlaygroundBank.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
static void check(bool v,const char* s){if(!v){std::cerr<<"FAIL: "<<s<<"\n";std::exit(1);}}
int main(){using namespace skelly::playground; for(int fs:{44100,48000,96000}){
    Bank b;b.prepare(fs,2);const int delay=b.latencySamples();check(delay>0,"no PDC");
    for(int mode=0;mode<modeCount;++mode){
        Bank fresh;fresh.prepare(fs,2);skelly::Parameters p;p.speedMs=0;p.pitchClasses=1u<<9;
        const int n=fs*2;std::vector<float> l(n),r(n);for(int i=0;i<n;++i){double phase=2*skelly::kPi*225*i/fs;l[i]=float(.18*std::sin(phase)+.04*std::sin(2*phase+.2));r[i]=l[i]*.7f;}
        auto src=l;auto dryL=l,dryR=r;
        for(int i=0;i<n;i+=127){float* io[]={l.data()+i,r.data()+i};fresh.process(io,2,std::min(127,n-i),p,mode);}
        for(int i=fs;i<n;++i)check(std::isfinite(l[i])&&std::isfinite(r[i]),"nonfinite wet");
        check(fresh.activeMode()==mode,"wrong engine");check(fresh.telemetry().target==57,"missing A3");
        // Independent complex projection avoids treating arbitrary phase as a pitch failure.
        auto tone=[&](double hz){double re=0,im=0;for(int i=fs;i<n;++i){const double ph=2*skelly::kPi*hz*i/fs;re+=l[i]*std::cos(ph);im+=l[i]*std::sin(ph);}return std::hypot(re,im);};
        check(tone(220)>4*tone(225),"requested engine failed pitch correction");
        for(int i=0;i<n;i+=127){float* io[]={dryL.data()+i,dryR.data()+i};fresh.process(io,2,std::min(127,n-i),p,mode,-1,true);}
        for(int i=delay+int(fs*.01);i<n;++i)check(dryL[i]==src[i-delay],"dry PDC mismatch");
        std::cout<<fs<<" "<<names[mode]<<" native="<<fresh.nativeLatency(mode)<<" common="<<delay<<" target=57"<<" wet tone="<<tone(220)<<"\n";
    }
    skelly::Parameters p;p.speedMs=0;double ph=0;float prev=0;double maxStep=0;for(int i=0;i<fs*3;++i){ph+=2*skelly::kPi*225/fs;float x=float(.15*std::sin(ph)),y=x*.7f;float* io[]={&x,&y};b.process(io,2,1,p,std::min(modeCount-1,i/(fs/2)));maxStep=std::max(maxStep,std::abs(double(x-prev)));prev=x;check(std::isfinite(x)&&std::isfinite(y),"switch nonfinite");}check(b.activeMode()==std::min(modeCount-1,5),"switch never completes");check(maxStep<.2,"switch transient too large");
    std::cout<<"switch max step "<<maxStep<<" PASS\n";
}std::cout<<"PLAYGROUND native engine routes + fixed dry PDC PASS\n";}
