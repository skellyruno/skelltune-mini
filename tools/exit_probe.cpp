#include "RealtimeTuner.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cstdlib>
constexpr double pi=3.1415926535897932384626;
int main(int argc,char** argv){
 const char* file=argc>1?argv[1]:"exit_probe.csv";
 FILE* f=std::fopen(file,"w");if(!f)return 1;
 std::fprintf(f,"exit_sample,boundary_curvature,background_curvature,peak_release_curvature,cycle_blend_before,cycle_blend_after\n");
 const int fs=48000;
 for(int trial=0;trial<45;++trial){
  const int cut=int(fs*.56)+trial*7;
  const int n=cut+fs/200;
  skelly::RealtimeTuner t;t.prepare(fs,2);
  skelly::Parameters p;p.speedMs=8;p.amount=1;p.mix=1;p.transientProtection=0;p.airPreservation=0;p.experimentalCycleSynthesis=true;
  t.setParameters(p);
  std::vector<float> l(n),r(n),ol(n),orr(n);
  for(int i=0;i<n;++i){
   double ph=2*pi*225*double(i)/fs;
   l[i]=float(.45*std::sin(ph)+.13*std::sin(2*ph+.31)+.035*std::sin(3*ph+.17));r[i]=-l[i];
  }
  double blendBefore=0,blendAfter=0;
  for(int i=0;i<n;++i) {
   if(i==cut){blendBefore=t.currentCycleBlend();p.experimentalCycleSynthesis=false;t.setParameters(p);}
   const float* a[]={l.data()+i,r.data()+i};float* b[]={ol.data()+i,orr.data()+i};
   t.process(a,b,1,2);
   if(i==cut)blendAfter=t.currentCycleBlend();
  }
  auto curv=[&](int i){return std::abs(double(ol[i])-2.0*ol[i-1]+ol[i-2]);};
  double bg=0.0,pk=0.0;
  for(int i=cut-160;i<cut-10;i++)bg+=curv(i);bg/=150.0;
  for(int i=cut;i<std::min(cut+96,n);++i)pk=std::max(pk,curv(i));
  std::fprintf(f,"%d,%.9g,%.9g,%.9g,%.9g,%.9g\n",cut,curv(cut),bg,pk,blendBefore,blendAfter);
 }
 std::fclose(f);return 0;
}
