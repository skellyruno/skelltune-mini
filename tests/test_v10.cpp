#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <limits>

static int failures=0;
static void check(bool ok, const char* desc){std::printf("%s %s\n",ok?"PASS":"FAIL",desc);failures+=!ok;}
struct Render {
 std::vector<float> left,right;
 skelly::CausalCycleSynth::Status status;
 double blend=0;
};
static Render render(int fs,bool cycle,int chunk,int switchAt=-1, bool amountOff=false){
 constexpr double pi=3.14159265358979323846;
 const int n=fs*2;
 Render ret;ret.left.resize(n);ret.right.resize(n);
 std::vector<float> a(n),b(n);
 for(int i=0;i<n;++i){
  double t=i/double(fs),env=(i<fs/12?std::min(1.0,i/double(fs/12)):1.0);
  double x=(0.55*std::sin(2*pi*225.0*t)+0.13*std::sin(2*pi*450*t))*env;
  if(i>fs && i<fs+fs/5) x*=0.08;
  a[i]=float(x);b[i]=-a[i];
 }
 skelly::RealtimeTuner dsp;dsp.prepare(fs,2);
 skelly::Parameters p;p.speedMs=8;p.transientProtection=0.0f;p.airPreservation=0;p.mix=1;p.experimentalCycleSynthesis=cycle;p.amount=amountOff?0.f:1.f;
 int j=0;
 while(j<n){
  if(switchAt>=0) p.experimentalCycleSynthesis=j<switchAt?false:true;
  dsp.setParameters(p);
  const int count=std::min(chunk,n-j);
  const float* src[2]{a.data()+j,b.data()+j};float* dst[2]{ret.left.data()+j,ret.right.data()+j};
  dsp.process(src,dst,count,2);j+=count;
 }
 ret.status=dsp.cycleSynthStatus();ret.blend=dsp.currentCycleBlend();return ret;
}
static double estimatedHz(const std::vector<float>& x, int fs, double target){
  const int begin=fs*3/2, end=begin+fs/5;
  double best=-1e30;int bestLag=0;
  for(int lag=int(fs/(target*1.12));lag<=int(fs/(target*.88));++lag){
    double xx=1e-10,yy=1e-10,xy=0;
    for(int i=begin;i<end-lag;i+=2){
      double a=x[size_t(i)],b=x[size_t(i+lag)];xx+=a*a;yy+=b*b;xy+=a*b;
    }
    double c=xy/std::sqrt(xx*yy);
    if(c>best){best=c;bestLag=lag;}
  }
  return bestLag?double(fs)/bestLag:0.0;
}
static double rmsdiff(const std::vector<float>& a,const std::vector<float>& b,size_t start){
 double e=0;for(size_t i=start;i<a.size();++i){const double d=double(a[i])-b[i];e+=d*d;}return std::sqrt(e/std::max<size_t>(1,a.size()-start));
}
int main(){
 const int fs=48000;
 auto defaultA=render(fs,false,257);
 auto defaultB=render(fs,false,1);
 check(rmsdiff(defaultA.left,defaultB.left,0)<1e-6,"Default realtime mode stays block invariant");
 check(defaultA.status.spawned==0,"Default mode never generates experimental grains");
 auto quality=render(fs,true,257);
 check(quality.status.spawned>50 && quality.status.dropped==0,"Experimental PSOLA is genuinely active and avoids grain loss");
 check(quality.blend>0.75,"Experimental mode reaches stable selected blend");
 const double cyclePitch=estimatedHz(quality.left,fs,220.0);
 std::printf("Experimental integrated output pitch %.3f Hz; target 220 Hz\n",cyclePitch);
 check(std::abs(1200.0*std::log2(cyclePitch/220.0))<35.0,"Integrated cycle mode corrects 225 Hz toward A3");
 check(quality.status.minimumSourceAge>=3.0,"Integrated cycle mode never reads future audio");
 check(rmsdiff(defaultA.left,quality.left,fs/2)>0.001,"New mode actually changes rendered audio");
 double stereoDiff=0,maxValue=0;
 for(size_t k=fs/2;k<quality.left.size();++k){stereoDiff=std::max(stereoDiff,std::abs(double(quality.left[k]+quality.right[k])));maxValue=std::max(maxValue,std::abs(double(quality.left[k])));}
 check(stereoDiff<1e-5,"Experimental path preserves antiphase stereo");
 check(maxValue<3 && std::isfinite(maxValue),"Experimental output stays finite and bounded");
 auto qualityB=render(fs,true,1);
 check(rmsdiff(quality.left,qualityB.left,0)<1e-6,"Experimental mode stays block invariant");
 auto automated=render(fs,true,257,fs);
 check(automated.status.spawned>0 && std::isfinite(automated.blend),"Switching synthesis mode while processing is functional");
 auto disabled=render(fs,true,257,-1,true);
 auto disabledBaseline=render(fs,false,257,-1,true);
 check(rmsdiff(disabled.left,disabledBaseline.left,0)<1e-5,"Zero-correction option retains transparent baseline");
 return failures?1:0;
}
