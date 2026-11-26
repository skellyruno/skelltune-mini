#pragma once
#include "PlaygroundBank.hpp"
#include <cmath>
#include <vector>
#include <iostream>
#include <cstdlib>

inline void playgroundStereoChecks(int fs) {
    for(int mode=0;mode<skelly::playground::modeCount;++mode){
        const int n=2*fs;
        std::vector<float> l(n),r(n);
        for(int i=0;i<n;++i){const double ph=2*skelly::kPi*235.8*i/fs;l[i]=float(.18*std::sin(ph)+.04*std::sin(2*ph+.2));r[i]=-l[i];}
        skelly::playground::Bank bank;bank.prepare(fs,2);
        skelly::Parameters p;p.speedMs=0;p.pitchClasses=1u<<9;
        for(int i=0;i<n;i+=127){float* io[]={l.data()+i,r.data()+i};bank.process(io,2,std::min(127,n-i),p,mode);}
        auto tone=[&](const std::vector<float>& lane,double hz){double re=0,im=0;for(int i=fs;i<n;++i){const double ph=2*skelly::kPi*hz*i/fs;const double w=.5-.5*std::cos(2*skelly::kPi*(i-fs)/fs);re+=w*lane[i]*std::cos(ph);im+=w*lane[i]*std::sin(ph);}return std::hypot(re,im);};
        double ll=0,rr=0,lr=0;
        for(int i=fs;i<n;++i){ll+=double(l[i])*l[i];rr+=double(r[i])*r[i];lr+=double(l[i])*r[i];}
        const double corr=lr/std::sqrt(std::max(1.e-20,ll*rr));
        const double rejected=std::max(tone(l,235.8)/std::max(1.e-20,tone(l,220.)),tone(r,235.8)/std::max(1.e-20,tone(r,220.)));
        std::cout<<fs<<" "<<skelly::playground::names[size_t(mode)]<<" inverted_stereo_corr="<<corr<<" input_target_ratio="<<rejected<<'\n';
        if(!(tone(l,220.)>fs*.015&&tone(r,220.)>fs*.015&&rejected<.25&&corr<-.90)){
            std::cerr<<"FAIL: inverted stereo lost pitch or polarity\n";std::exit(1);
        }
    }
}
