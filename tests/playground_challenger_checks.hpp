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
inline int stableDelay(bool preview,double cents){skelly_research::NoteDecision d;d.prepare(48000);d.setFastStableTransitions(preview);for(int i=0;i<20;++i)d.update(hz(57),.98,0,120);for(int i=1;i<=60;++i){auto r=d.update(hz(58+cents/100.),.98,0,120,4095,440,false);if(r.targetMidi==58)return i*120;}return -1;}
inline void run(){for(double offset:{0.,25.}){int old=stableDelay(false,offset),now=stableDelay(true,offset);std::cout<<"Regular offset="<<offset<<" old_ms="<<old/48.<<" new_ms="<<now/48.<<'\n';require(old>=3000&&now>0&&now<old,"stable adjacent note confirmation");}
using skelly::RealtimeTuner;
require(RealtimeTuner::holdTestTuneBoundary(57,58,4095,57.55,0.),"boundary hold");require(!RealtimeTuner::holdTestTuneBoundary(57,58,4095,57.8,0.),"clear note transition");require(!RealtimeTuner::holdTestTuneBoundary(57,58,4095,57.55,.8),"strong onset release");require(!RealtimeTuner::holdTestTuneBoundary(57,58,1u<<10,57.55,0.),"forbidden held note");
for(int fs:{44100,48000,96000}){int n=fs*2;auto run=[&](bool enabled){skelly::playground::Bank b;b.setChallengerExperiments(enabled);b.prepare(fs,2,127,3,1);skelly::Parameters p;p.speedMs=25;std::vector<float> l(127),r(127);double phase=0;int switches=0,prior=-1;for(int off=0;off<n;off+=127){int count=std::min(127,n-off);for(int i=0;i<count;++i){double t=double(off+i)/fs;double midi=t<.3?57.:t<.55?57.+.5*(t-.3)/.25:57.5+.08*std::sin(2*skelly::kPi*5.5*t);phase+=2*skelly::kPi*hz(midi)/fs;l[i]=float(.15*std::sin(phase)+.045*std::sin(2*phase+.2));r[i]=.7f*l[i];}float* io[]={l.data(),r.data()};b.process(io,2,count,p,1);for(int i=0;i<count;++i)require(std::isfinite(l[i])&&std::isfinite(r[i]),"nonfinite");if(off>fs*.8){int note=b.telemetry().target;if(prior>=0&&note>=0&&note!=prior)++switches;if(note>=0)prior=note;}}return switches;};auto old=run(false),now=run(true);std::cout<<"TestTune fs="<<fs<<" boundary switches "<<old<<" -> "<<now<<'\n';require(now<old,"boundary flutter not reduced");}
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
for(int fs:{44100,48000,96000}){int n=4*fs;auto run=[&](bool enabled){skelly::playground::Bank b;b.setChallengerExperiments(enabled);b.prepare(fs,2,127,4,5);auto delay=b.latencySamples();std::vector<float> l(n),r(n);double f=55*std::exp2(.3/12.);for(int i=0;i<n;++i){double ph=2*skelly::kPi*f*i/fs;l[i]=float(.2*std::sin(ph)+.09*std::sin(2*ph+.2)+.03*std::sin(3*ph+.4));r[i]=.7f*l[i];}skelly::Parameters p;p.speedMs=0;p.pitchClasses=1u<<9;for(int off=0;off<n;off+=127){float* io[]={l.data()+off,r.data()+off};b.process(io,2,std::min(127,n-off),p,5);}auto tone=[&](double h){double re=0,im=0;for(int i=fs+delay;i<3*fs+delay;++i){double w=.5-.5*std::cos(2*skelly::kPi*(i-fs-delay)/(2*fs)),ph=2*skelly::kPi*h*i/fs;re+=l[i]*w*std::cos(ph);im+=l[i]*w*std::sin(ph);}return std::hypot(re,im);};require(delay==int(std::lround(fs*.005)),"bass PDC changed");return 20*std::log10(std::max(1e-12,tone(55))/std::max(1e-12,tone(f)));};double old=run(false),now=run(true);std::cout<<"OG bass fs="<<fs<<" wanted/input dB "<<old<<" -> "<<now<<'\n';require(old<0&&now>6,"OG sub-bass correction");}
#endif
std::cout<<"PASS: challenger regressions\n";
}
} // namespace challenger_checks
