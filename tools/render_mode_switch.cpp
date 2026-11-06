// Offline reproducible test: write native little-endian float32 PCM for an
// unvoiced release / explicit PSOLA->Realtime mode change at 0.56 seconds.
// Use Python wave or scipy to convert to a listening WAV; NOT a DAW latency test.
#include "RealtimeTuner.hpp"
#include <cstdio>
#include <cmath>
#include <algorithm>
int main(int argc,char**argv){
 if(argc!=3)return 2;
 FILE* out=std::fopen(argv[1],"wb"); FILE* dry=std::fopen(argv[2],"wb");
 if(!out || !dry)return 1;
 constexpr int fs=48000,n=fs*12/10,off=fs*56/100;
 constexpr double pi=3.14159265358979323846;
 skelly::RealtimeTuner dsp;dsp.prepare(fs,1);
 skelly::Parameters p;p.speedMs=8;p.amount=1;p.mix=1;p.transientProtection=0;p.airPreservation=0;p.experimentalCycleSynthesis=true;
 dsp.setParameters(p);
 for(int i=0;i<n;++i){
  if(i==off){p.experimentalCycleSynthesis=false;dsp.setParameters(p);}
  double phase=2*pi*225*double(i)/fs;
  float input=float(.45*std::sin(phase)+.13*std::sin(2*phase+.31)+.035*std::sin(3*phase+.17));
  float output=0;
  const float* src[]={&input}; float* dest[]={&output};
  dsp.process(src,dest,1,1);
  std::fwrite(&output,sizeof(float),1,out);
  std::fwrite(&input,sizeof(float),1,dry);
 }
 std::fclose(out);std::fclose(dry);return 0;
}
