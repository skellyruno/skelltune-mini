// META LAB ONLY: measured fractional interpolation and bounded cubic output.
// A pure fractional delay has a known analytic sine value. Compare the
// original two-tap interpolation against the proposed cubic Lagrange.
#include "MetaTest.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <algorithm>
static void need(bool yes,const char* msg) {
    if(!yes){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}
}
int main(){
    constexpr int rate=48000, ring=skelly::MetaTest::kRing;
    constexpr double pi=3.14159265358979323846;
    std::array<float,ring> s{};
    double linearError=0,cubicError=0;
    for(double hz:{700.0,2500.0,5600.0,8000.0}) {
        for(int i=0;i<ring;++i)
            s[size_t(i)]=float(std::sin(2.0*pi*hz*double(i)/rate));
        for(int i=100;i<1500;++i) {
            const double pos=double(i)+0.31;
            const double truth=std::sin(2.0*pi*hz*pos/rate);
            const double old=skelly::MetaTest::probeInterpolation(s.data(),pos,false);
            const double hi=skelly::MetaTest::probeInterpolation(s.data(),pos,true);
            linearError+=(old-truth)*(old-truth);
            cubicError+=(hi-truth)*(hi-truth);
            need(std::isfinite(hi)&&std::abs(hi)<=1.05,
                 "cubic interpolation overshot a bounded test waveform");
        }
    }
    std::cout<<"META fractional-sine SSE linear="<<linearError
             <<" cubic="<<cubicError
             <<" improvement="<<100.0*(1.0-cubicError/linearError)<<"%\n";
    need(cubicError<linearError*0.35,
         "cubic interpolation did not materially reduce fractional error");
    std::cout<<"PASS: four-point cubic fractional vocal harmonics preserved\n";
    return 0;
}