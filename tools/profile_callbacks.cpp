// Offline callback timing probe: reports timing distribution; no DAW or realtime guarantees.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
int main(){
 constexpr int rate=48000;
 for(int studio=0;studio<3;++studio)for(int block:{16,32,64,128,256}) {
  skelly::RealtimeTuner dsp;
  dsp.prepare(rate,2,studio!=0,studio==2);
  skelly::Parameters p; p.speedMs=8; p.mix=.55f; p.amount=1;
  p.experimentalCycleSynthesis=studio!=0;
  dsp.setParameters(p);
  std::vector<float> a(static_cast<size_t>(block),0.0f);
  std::vector<float> b(static_cast<size_t>(block),0.0f);
  std::vector<float> l(static_cast<size_t>(block),0.0f);
  std::vector<float> r(static_cast<size_t>(block),0.0f);
  const float* in[]{a.data(),b.data()};float* out[]{l.data(),r.data()};
  std::vector<double> times;
  times.reserve((rate*3)/block+2);
  const double budget=1e6*block/double(rate);
  for(int offset=0;offset<rate*3;offset+=block) {
   for(int i=0;i<block;++i) {
    const double t=(offset+i)/double(rate);
    const double f=t<1?110:(t<2?225:330);
    a[size_t(i)]=float(.25*std::sin(2*skelly::kPi*f*t)
                      +.10*std::sin(4*skelly::kPi*f*t));
    b[size_t(i)]=-a[size_t(i)];
   }
   const auto start=std::chrono::steady_clock::now();
   dsp.process(in,out,block,2);
   const auto end=std::chrono::steady_clock::now();
   if(offset>=rate/2) times.push_back(std::chrono::duration<double,std::micro>(end-start).count());
  }
  std::sort(times.begin(),times.end());
  const auto quantile=[&](double q){return times[size_t(q*double(times.size()-1))];};
  printf("%s,%d,%.2f,%.2f,%.2f,%.2f,%.3f\n",
    studio==0?"live":studio==1?"studio50":"fast40",
    block,budget,quantile(.5),quantile(.95),times.back(),quantile(.95)/budget);
 }
}
