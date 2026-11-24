#pragma once
#include "PlaygroundBank.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
// Amount is a pitch-distance control, not a wet/dry crossfade. Test real
// output spectra rather than trusting the engine's own correction meter.
inline void playgroundAmountChecks(int fs){
 for(int profile=0;profile<int(skelly::playground::latencyNames.size());++profile)
 for(int mode=0;mode<skelly::playground::modeCount;++mode){
    skelly::playground::Bank bank;bank.prepare(fs,2,127,profile,mode);
    const int n=2*fs;
    std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i){const double ph=2*skelly::kPi*235.8*i/fs;l[i]=float(.16*std::sin(ph)+.05*std::sin(2*ph+.2)+.025*std::sin(3*ph+.6));r[i]=.7f*l[i];}
    skelly::Parameters p;p.speedMs=0;p.pitchClasses=1u<<9;p.amount=.5f;p.mix=1;
    for(int i=0;i<n;i+=127){float* io[]={l.data()+i,r.data()+i};bank.process(io,2,std::min(127,n-i),p,mode);}
    const auto tone=[&](const std::vector<float>& lane,double hz){double re=0,im=0;for(int i=fs;i<n;++i){const double ph=2*skelly::kPi*hz*i/fs,w=.5-.5*std::cos(2*skelly::kPi*(i-fs)/fs);re+=w*lane[i]*std::cos(ph);im+=w*lane[i]*std::sin(ph);}return std::hypot(re,im);};
    const double midpoint=std::sqrt(235.8*220.);
    double ghostDb=-300.;bool pass=true;
    for(const auto* lane:{&l,&r}){
        for(float x:*lane)pass=pass&&std::isfinite(x)&&std::abs(x)<1.f;
        const double wanted=tone(*lane,midpoint),ghost=std::max(tone(*lane,220.),tone(*lane,235.8));
        const double db=20*std::log10(std::max(ghost,1e-12)/std::max(wanted,1e-12));
        ghostDb=std::max(ghostDb,db);pass=pass&&wanted>fs*.01&&db<-35.;
    }
    std::cout<<fs<<" "<<skelly::playground::names[size_t(mode)]<<" profile="<<profile<<" half_amount_endpoint_ghost_dBc="<<ghostDb<<'\n';
    if(!pass){std::cerr<<"FAIL: partial Amount produced wrong pitch or endpoint ghost\n";std::exit(1);}
 }
 std::cout<<"PASS: independent correction Amount across available engines and latencies\n";
}
