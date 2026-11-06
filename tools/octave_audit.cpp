#include "RealtimeTuner.hpp"
#include <cmath>
#include <cstdio>
using namespace skelly;
int main(){for(double fs:{44100.,48000.,96000.})for(double f:{75.,85.,95.,110.,147.,196.,220.,330.})for(int kind=0;kind<3;++kind){CausalPitchTracker tr;tr.prepare(fs);PitchEstimate e;double sum=0;int count=0;for(int i=0;i<int(fs*.60);++i){double t=i/fs;double x=(kind==0?(.5*sin(2*kPi*f*t)+.2*sin(4*kPi*f*t)):(kind==1?(.06*sin(2*kPi*f*t)+.43*sin(4*kPi*f*t)+.10*sin(6*kPi*f*t)+.36*sin(8*kPi*f*t)):(.45*sin(4*kPi*f*t)+.30*sin(8*kPi*f*t))));if(tr.push(float(x),e)&&i>int(fs*.25)&&e.valid){sum+=e.hertz;++count;}}printf("%.0f,%.0f,%d,%.4f\n",fs,f,kind,count?sum/count:0.0);}}
