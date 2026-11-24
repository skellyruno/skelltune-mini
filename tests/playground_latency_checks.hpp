#pragma once
#include "PlaygroundBank.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
inline void latencyNeed(bool ok,const char* reason){if(!ok){std::cerr<<"FAIL: "<<reason<<'\n';std::exit(1);}}
inline void playgroundLatencyChecks(int fs){
 for(int profile=1;profile<int(skelly::playground::latencyNames.size());++profile)for(int mode=0;mode<skelly::playground::modeCount;++mode){
    skelly::playground::Bank bank;bank.prepare(fs,2,127,profile,mode);
    const int pdc=bank.latencySamples(),native=bank.nativeLatency(mode),target=int(std::lround(fs*skelly::playground::latencyTargets[size_t(profile)]*.001));
    latencyNeed(pdc==std::max(native,target)&&pdc>0,"invalid real delay");
    latencyNeed(bank.lockedMode()==mode&&bank.latencyProfile()==profile,"wrong prepared profile");
    const int n=fs*2;std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i){double ph=2*skelly::kPi*225.*i/fs;l[i]=float(.15*std::sin(ph)+.04*std::sin(2*ph+.2));r[i]=.7f*l[i];}
    const auto src=l,srcR=r;
    skelly::Parameters p;p.speedMs=0;p.amount=1;p.mix=1;p.pitchClasses=1u<<9;
    // A host automation change cannot change delay geometry mid-audition.
    for(int i=0;i<n;i+=127){float* io[]={l.data()+i,r.data()+i};bank.process(io,2,std::min(127,n-i),p,(mode+1)%skelly::playground::modeCount);}
    latencyNeed(bank.activeMode()==mode&&bank.latencySamples()==pdc,"latency changed during playback");
    double re=0,im=0,oldRe=0,oldIm=0;
    for(int i=fs;i<n;++i){latencyNeed(std::isfinite(l[i])&&std::isfinite(r[i])&&std::abs(l[i])<1.f&&std::abs(r[i])<1.f,"invalid wet");double a=2*skelly::kPi*220.*i/fs,b=2*skelly::kPi*225.*i/fs;re+=l[i]*std::cos(a);im+=l[i]*std::sin(a);oldRe+=l[i]*std::cos(b);oldIm+=l[i]*std::sin(b);}
    const double ratio=std::hypot(re,im)/std::max(1.e-9,std::hypot(oldRe,oldIm));
    std::cout<<fs<<' '<<skelly::playground::names[size_t(mode)]<<" profile="<<profile<<" native="<<native<<" host="<<pdc<<" target_ratio="<<ratio<<std::endl;
    latencyNeed(ratio>4.,"pitch failed at selected latency");
    l=src;r=srcR;p.mix=0;
    for(int i=0;i<n;i+=127){float* io[]={l.data()+i,r.data()+i};bank.process(io,2,std::min(127,n-i),p,mode);}
    for(int i=pdc+fs/50;i<n;++i)latencyNeed(l[i]==src[i-pdc]&&r[i]==srcR[i-pdc],"wrong dry PDC");
 }
 std::cout<<"PASS: native latency profiles, actual pitch and exact stereo dry PDC\n";
}
