#include "MetaTest.hpp"
#include "PhaseCrossfadeLut.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
static void require(bool v,const char* msg){if(!v){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}}
int main(){
    constexpr double pi=3.14159265358979323846;
    skelly::PhaseCrossfadeLut fade; fade.prepare();
    double worst=0;
    for(int length:{16,37,120,256,512})for(int remaining=length;remaining>=1;--remaining){
        const double g=1.0-double(remaining)/double(length+1);
        const double ideal=0.5-0.5*std::cos(pi*g);
        const double actual=fade.blend(length-remaining,length+1);
        worst=std::max(worst,std::abs(ideal-actual));
        require(std::isfinite(actual)&&actual>=0&&actual<=1,"nonfinite fade");
        require(worst<2.5e-6,"LUT differs from original META Hann");
    }
    skelly::MetaTest dsp; dsp.prepare(48000.0);
    dsp.setParameters(0x0fff,440.0,1.0,0.0);
    std::array<float,128> l{},r{};double ph=0,peak=0;
    for(int block=0;block<800;++block){
        for(int i=0;i<128;++i){
            ph+=2*pi*225.0/48000.0;
            l[size_t(i)]=float(0.17*std::sin(ph)+0.03*std::sin(2*ph));
            r[size_t(i)]=l[size_t(i)];
        }
        float* io[2]={l.data(),r.data()};dsp.process(io,2,128);
        for(float x:l) {
            require(std::isfinite(x),"nonfinite processed META");
            peak=std::max(peak,std::abs(double(x)));
        }
    }
    require(peak>0.015&&peak<1.0,"unexpected META output peak");
    std::cout<<"PASS: original META fade preserved within "<<worst<<"\n";
}
