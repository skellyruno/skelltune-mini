#include "RealtimeTuner.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
int main() {
 constexpr int frames=64, rate=48000, n=rate*8;
 for(int mode=0;mode<2;++mode){
  skelly::RealtimeTuner dsp;dsp.prepare(rate,2);
  skelly::Parameters params;params.experimentalCycleSynthesis=mode!=0;
  params.speedMs=8;params.transientProtection=0.0f;params.airPreservation=0;
  dsp.setParameters(params);
  std::array<float,frames> left{},right{},outL{},outR{};
  const float* src[2]{left.data(),right.data()};float* dst[2]{outL.data(),outR.data()};
  volatile double sink=0;
  auto start=std::chrono::steady_clock::now();
  for(int offset=0;offset<n;offset+=frames){
   for(int j=0;j<frames;++j){double t=(offset+j)/double(rate);left[j]=float(.4*std::sin(2*skelly::kPi*225*t)+.12*std::sin(4*skelly::kPi*225*t));right[j]=-left[j];}
   dsp.process(src,dst,frames,2);sink+=outL[0];
  }
  const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
  printf("mode=%s seconds=%.4f realtime_factor=%.1f grains=%llu minSourceAge=%.1f lastSourceAge=%.1f sink=%.3f\n",mode?"experimental-cycle":"realtime",elapsed,8.0/elapsed,(unsigned long long)dsp.cycleSynthStatus().spawned,dsp.cycleSynthStatus().minimumSourceAge,dsp.cycleSynthStatus().lastSourceAge,double(sink));
 }
}
