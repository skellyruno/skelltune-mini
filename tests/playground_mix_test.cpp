#include "PlaygroundBank.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
static void need(bool yes,const char* msg){if(!yes){std::cerr<<"FAIL: "<<msg<<'\n';std::exit(1);}}
static std::vector<float> render(int fs,int mode,float mix,const std::vector<float>& src){
 skelly::playground::Bank bank;bank.prepare(fs,2);auto l=src,r=src;for(float& x:r)x*=.7f;
 skelly::Parameters p;p.speedMs=0;p.amount=1;p.mix=mix;p.pitchClasses=1u<<9;
 for(size_t i=0;i<l.size();i+=127){float* io[]={l.data()+i,r.data()+i};bank.process(io,2,int(std::min<size_t>(127,l.size()-i)),p,mode);}
 for(size_t i=0;i<l.size();++i){need(std::isfinite(l[i])&&std::isfinite(r[i]),"nonfinite mix");need(std::abs(r[i]-.7f*l[i])<.045f,"stereo gain imbalance");}
 return l;
}
int main(){for(int fs:{44100,48000,96000})for(int mode=0;mode<skelly::playground::modeCount;++mode){
 std::vector<float> src(size_t(fs)*3);for(size_t i=0;i<src.size();++i){double ph=2*skelly::kPi*235.8*i/fs;src[i]=float(.15*std::sin(ph)+.04*std::sin(2*ph+.2));}
 const auto dry=render(fs,mode,0,src),wet=render(fs,mode,1,src),half=render(fs,mode,.5f,src);
 double pd=0,pw=0,ph=0,pl=0,peak=0;for(size_t i=fs;i<size_t(fs)*3;++i){pd+=double(dry[i])*dry[i];pw+=double(wet[i])*wet[i];ph+=double(half[i])*half[i];const double linear=.5*(dry[i]+wet[i]);pl+=linear*linear;peak=std::max(peak,std::abs(double(half[i])));}
 const double reference=(pd+pw)*.5,linDb=10*std::log10(pl/reference),halfDb=10*std::log10(ph/reference);
 std::cout<<fs<<" "<<skelly::playground::names[size_t(mode)]<<" linear_dB="<<linDb<<" actual_dB="<<halfDb<<" peak="<<peak<<'\n';
 need(linDb<-1.5,"fixture not decorrelated");need(halfDb>linDb+1.0,"partial mix does not restore moderate power loss");need(halfDb<.8,"partial mix overcompensates");need(peak<.45,"partial mix overload");
 }std::cout<<"PASS: bounded stereo mix compensation all available engines\n";}
