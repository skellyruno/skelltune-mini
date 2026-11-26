#pragma once
#include "PlaygroundBank.hpp"
#include <cmath>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdlib>
namespace challenger_checks {
inline void require(bool ok,const char* msg){if(!ok){std::cerr<<"FAIL: "<<msg<<'\n';std::exit(1);}}
inline double hz(double midi){return 440.*std::exp2((midi-69.)/12.);}
inline void run(){
using skelly::RealtimeTuner;
require(RealtimeTuner::holdTestTuneBoundary(57,58,4095,57.55,0.),"boundary hold");require(!RealtimeTuner::holdTestTuneBoundary(57,58,4095,57.8,0.),"clear note transition");require(!RealtimeTuner::holdTestTuneBoundary(57,58,4095,57.55,.8),"strong onset release");require(!RealtimeTuner::holdTestTuneBoundary(57,58,1u<<10,57.55,0.),"forbidden held note");
for(int fs:{44100,48000,96000}){int n=fs*2;auto run=[&](bool enabled){skelly::playground::Bank b;b.setChallengerExperiments(enabled);b.prepare(fs,2,127,3,0);skelly::Parameters p;p.speedMs=25;std::vector<float> l(127),r(127);double phase=0;int switches=0,prior=-1;for(int off=0;off<n;off+=127){int count=std::min(127,n-off);for(int i=0;i<count;++i){double t=double(off+i)/fs;double midi=t<.3?57.:t<.55?57.+.5*(t-.3)/.25:57.5+.08*std::sin(2*skelly::kPi*5.5*t);phase+=2*skelly::kPi*hz(midi)/fs;l[i]=float(.15*std::sin(phase)+.045*std::sin(2*phase+.2));r[i]=.7f*l[i];}float* io[]={l.data(),r.data()};b.process(io,2,count,p,0);for(int i=0;i<count;++i)require(std::isfinite(l[i])&&std::isfinite(r[i]),"nonfinite");if(off>fs*.8){int note=b.telemetry().target;if(prior>=0&&note>=0&&note!=prior)++switches;if(note>=0)prior=note;}}return switches;};auto old=run(false),now=run(true);std::cout<<"TestTune fs="<<fs<<" boundary switches "<<old<<" -> "<<now<<'\n';require(now<old,"boundary flutter not reduced");}
std::cout<<"PASS: challenger regressions\n";
}
} // namespace challenger_checks
