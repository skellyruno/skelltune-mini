// Research-only META retune-speed response calibration.
#include "MetaTest.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>
static void expect(bool b,const char* why){if(!b){std::cerr<<"FAIL: "<<why<<"\n";std::exit(1);}}
int main(){
    constexpr double expected=0.36787944117144233;
    for(double rate:{44100.,48000.,96000.,192000.})
        for(double ms:{10.,25.,100.,200.}) {
            const auto elapsed=uint32_t(std::lround(rate*ms*.001));
            const double alpha=skelly::MetaTest::smoothingRetention(ms,rate,elapsed);
            expect(std::abs(alpha-expected)<1e-4,"not a calibrated millisecond time constant");
            expect(skelly::MetaTest::smoothingRetention(0.,rate,elapsed)==0.,
                   "0ms should immediately acquire the detected pitch");
        }
    skelly::MetaTest params;params.prepare(48000);
    for(double ms:{0.,10.,25.,100.,200.}) {
        params.setParameters(0x0fff,440.,1.,ms);
        expect(params.retuneMs()==ms,"physical milliseconds lost in parameter mapping");
    }
    params.setParameters(0x0fff,440.,1.,5000.);
    expect(params.retuneMs()==200.,"maximum legacy milliseconds not clamped");
    params.setParameters(0x0fff,440.,1.,std::numeric_limits<double>::quiet_NaN());
    expect(params.retuneMs()==25.,"nonfinite META retune parameter not sanitized");
    // Test on real processing with a sudden note change, not just an
    // isolated mathematical coefficient.
    constexpr int rate=48000;
    std::vector<float> source(rate*3);
    double phase=0.0;
    for(int i=0;i<int(source.size());++i) {
        const double hz=i<int(1.4*rate)?224.0:i<int(2.4*rate)?338.0:224.0;
        phase+=2*skelly::MetaTest::kPi*hz/rate;
        source[size_t(i)]=float(.18*std::sin(phase)+.045*std::sin(2*phase+.27));
    }
    double fast=0,slow=0;
    for(int mode=0;mode<2;++mode){
        skelly::MetaTest dsp; dsp.prepare(rate);
        dsp.setParameters(0x0fff,440.,1.,mode?100.:0.);
        std::vector<float> audio=source;
        for(int i=0;i<int(audio.size());i+=128){
            float* io[1]={audio.data()+i};
            dsp.process(io,1,128);
            if(i>=int(1.50*rate)&&i<int(1.58*rate)){
                if(mode==0)fast+=dsp.lastPeriod();
                else slow+=dsp.lastPeriod();
            }
        }
    }
    std::cout<<"META pitch response sums 0ms="<<fast<<" 100ms="<<slow<<"\n";
    expect(std::isfinite(fast)&&std::isfinite(slow),"invalid response on harmonic vocal");
    expect(std::abs(fast-slow)>1.0,"0ms and 100ms produced identical pitch trajectories");
    std::cout<<"PASS: META 0/10/25/100ms physical retune semantics across rates\n";
}
