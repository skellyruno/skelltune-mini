#include "shared/CausalFormantShift.hpp"
#include <cstdlib>
#include <iostream>
#include <array>
#include <cmath>
#include <algorithm>
static void need(bool b,const char* m){if(!b){std::cerr<<m<<'\n';std::exit(1);}}
using skell::research::CausalFormantShift;
int main(){
 for(int fs:{8000,44100,48000,96000,192000}){
  CausalFormantShift q;q.prepare(fs);
  need(q.latency()==0,"unexpected added delay");
  for(int i=0;i<fs/10;++i){const float x=.4f*std::sin(i*.21f);auto y=q.process(x,-.5f*x);need(y[0]==x&&y[1]==-.5f*x,"neutral not exact");}
  q.setSemitones(-7);for(int i=0;i<fs/4;++i){const float x=.25f*std::sin(i*.25f);auto y=q.process(x,-.5f*x,true);need(y[0]==x&&y[1]==-.5f*x,"bypass not exact");}
  auto run=[&](float shift){
   CausalFormantShift f;f.prepare(fs);f.setSemitones(shift);
   std::array<double,12> re{},im{};int n=fs*2;double step=6.283185307179586*200/fs;
   int crossings=0;float last=0.f;
   for(int i=0;i<n;++i){
    double x=0.;for(int h=1;h<=12;++h){double hz=h*200.;double amp=.065*std::exp(-.5*std::pow((hz-1100.)/350.,2))+.012/h;x+=amp*std::sin(step*h*i);}
    auto y=f.process(float(x),float(-.5*x));
    need(std::isfinite(y[0])&&std::abs(y[0])<1.f&&std::abs(y[0]+2*y[1])<2.e-6,"invalid stereo output");
    if(i>=fs)for(int h=1;h<=12;++h){re[h-1]+=y[0]*std::cos(step*h*i);im[h-1]+=y[0]*std::sin(step*h*i);}
    last=y[0];(void)last;(void)crossings;
   }
   double mass=0.,centroid=0.;for(int h=1;h<=12;++h){const double e=std::hypot(re[h-1],im[h-1]);mass+=e;centroid+=h*200.*e;}
   need(std::hypot(re[0],im[0])>1.e-3,"fundamental lost");return centroid/mass;
  };
  const double down=run(-7),base=run(0),up=run(7);
  std::cout<<fs<<" centroid Hz "<<down<<" / "<<base<<" / "<<up<<'\n';
  need(down<base-5&&up>base+5,"wrong spectral warp direction");
  CausalFormantShift impulse;impulse.prepare(fs);impulse.setSemitones(7);
  auto first=impulse.process(.5f,-.25f);need(first[0]==.5f&&first[1]==-.25f,"audio buffered");
 }
}
