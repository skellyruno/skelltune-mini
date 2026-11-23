#include "MetaTest.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>
static void check(bool ok,const char* label) {
    if(!ok){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}
}
int main(){
    for(double sr:{44100.0,48000.0,96000.0}) {
        skelly::MetaTest meta;
        meta.prepare(sr,1.5);
        const int delay=meta.latencySamples();
        check(delay==int(std::lround(sr*0.0015)),"expected 1.5ms PDC");
        meta.setParameters(0x0fffu,440,1.0,0.0);
        constexpr int n=1024;
        std::vector<float> left(n),right(n),sourceL(n),sourceR(n);
        for(int i=0;i<n;++i) {
            sourceL[i]=float(.4*std::sin(i*.23)+.1*std::cos(i*.17));
            sourceR[i]=float(.3*std::sin(i*.14)+.05*std::cos(i*.11));
        }
        left=sourceL;right=sourceR;
        for(int i=0;i<n;i+=53) {
            const int len=std::min(53,n-i);
            float* io[2]={left.data()+i,right.data()+i};
            meta.process(io,2,len,true);
        }
        for(int i=0;i<n;++i) {
            const float a=i>=delay?sourceL[std::size_t(i-delay)]:0.0f;
            const float b=i>=delay?sourceR[std::size_t(i-delay)]:0.0f;
            check(left[std::size_t(i)]==a&&right[std::size_t(i)]==b,
                  "bypass not sample-aligned to both dry channels");
        }
        check(meta.detectedHz()==0.0 && meta.targetHz()==0.0 &&
              meta.estimatedOutputCorrectionCents()==0.0,
              "bypass retained incorrect live pitch readout");
        meta.reset();
        meta.setParameters(0x0fffu,440,0.0,0.0);
        std::vector<float> dry(768),source(768);
        for(int i=0;i<768;++i)
            source[std::size_t(i)]=float(.2*std::sin(.26*i));
        dry=source;
        for(int i=0;i<768;i+=29) {
            float* io[1]={dry.data()+i};
            meta.process(io,1,std::min(29,768-i));
        }
        for(int i=0;i<768;++i)
            check(dry[std::size_t(i)]==
                  (i>=delay?source[std::size_t(i-delay)]:0.0f),
                  "zero-Amount pitch path not aligned with bypass PDC");
        check(meta.estimatedOutputCorrectionCents()==0.0,
              "dry META display falsely claims pitch correction");
        meta.reset();
        meta.setParameters(0x0fffu,440,1.0,0.0);
        for(int block=0;block<450;++block) {
            std::array<float,128> voice{};
            for(int i=0;i<128;++i)
                voice[std::size_t(i)]=float(.2*std::sin(
                    (block*128+i)*235.8*2*skelly::MetaTest::kPi/sr));
            float* io[1]={voice.data()};
            meta.process(io,1,128);
        }
        meta.setParameters(0x0fffu,440,0.0,0.0);
        for(int block=0;block<150;++block) {
            std::array<float,128> silence{};
            float* io[1]={silence.data()};
            meta.process(io,1,128);
        }
        check(std::abs(meta.estimatedOutputCorrectionCents())<1.0e-5,
              "wet release meter incorrectly held correction after silence");
    }
    std::cout<<"PASS: META 1.5ms mono/stereo bypass, dry states, true display\n";
}
