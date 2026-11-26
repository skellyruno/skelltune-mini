#include "FormantAudition.hpp"
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <vector>
using skelly::playground::FormantAudition;
void require(bool condition){if(!condition)std::abort();}
int main(){int tests=0;for(int sr:{8000,44100,48000,96000,192000}){
 FormantAudition f; f.prepare(sr,false);require(f.latency()==0);f.setSemitones(-7);for(int i=0;i<2048;++i){float x=1.1f*std::sin(i*.3f);auto z=f.process(x,-x);require(z[0]==x&&z[1]==-x);}++tests;
 f.prepare(sr,true);require(f.latency()==1024);std::vector<float> x(4096);for(int i=0;i<4096;++i)x[i]=1.2f*std::sin(i*.4f);for(int i=0;i<4096;++i){auto z=f.process(x[i],-.5f*x[i]);float expected=i>=1024?x[i-1024]:0;require(z[0]==expected&&z[1]==-.5f*expected);}++tests;
 f.prepare(sr,true);f.setSemitones(-7);for(int i=0;i<4096;++i){auto z=f.process(x[i],-.5f*x[i],true);float expected=i>=1024?x[i-1024]:0;require(z[0]==expected&&z[1]==-.5f*expected);}++tests;
 for(float st:{-12.f,-7.f,7.f,12.f}){f.prepare(sr,true);f.setSemitones(st);int crossings=0;float last=0;for(int i=0;i<sr*2;++i){float x=.9f*std::sin(6.283185307179586*220*i/sr);auto y=f.process(x,-.5f*x);require(std::isfinite(y[0])&&std::isfinite(y[1])&&std::abs(y[0]+2*y[1])<1e-5f);if(i>=sr){require(std::abs(y[0])<=.601f);if(last<=0&&y[0]>0)++crossings;}last=y[0];}require(std::abs(crossings-220)<=3);++tests;}
 f.prepare(sr,true);f.setSemitones(NAN);auto y=f.process(NAN,INFINITY);require(y[0]==0&&y[1]==0);++tests;
 }std::cout<<tests<<" formant audition checks passed\n";}
