#include "RealtimeTuner.hpp"
#include <chrono>
#include <cmath>
#include <iostream>
#include <vector>
int main(int argc,char**argv) {
    const int rate=(argc>1)?std::stoi(argv[1]):48000;
    const int seconds=(argc>2)?std::stoi(argv[2]):10;
    skelly::RealtimeTuner dsp;
    dsp.prepare(rate,2);
    skelly::Parameters p;
    p.speedMs=8.0f;
    dsp.setParameters(p);
    constexpr int block=64;
    float a[block]{},b[block]{},c[block]{},d[block]{};
    const float* in[]={a,b}; float* out[]={c,d};
    const auto start=std::chrono::steady_clock::now();
    for (int offset=0;offset<rate*seconds;offset+=block) {
        for (int i=0;i<block;++i) {
            const double t=double(offset+i)/rate;
            a[i]=float(.25*std::sin(2.0*skelly::kPi*225.0*t)+
                       .11*std::sin(2.0*skelly::kPi*450.0*t));
            b[i]=a[i]*0.92f;
        }
        dsp.process(in,out,block,2);
    }
    const double elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"sampleRate="<<rate<<" simulated_seconds="<<seconds<<" cpu_seconds="<<elapsed
             <<" realtime_factor="<<seconds/elapsed<<" nominal_delay_samples="
             <<dsp.latencySamples()<<"\n";
}
