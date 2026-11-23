// 1.5ms Mini plugin PDC is a real sample-accurate dry/transient delay,
// not a merely reduced setLatencySamples() number. Test historical 3ms too.
#include "RealtimeTuner.hpp"
#ifdef SKELLY_META_TEST
#include "MetaTest.hpp"
#endif
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace skelly;
static void require(bool yes,const char* what) {
    if (!yes) {std::cerr << "FAIL: " << what << "\n"; std::exit(1);}
}
int main() {
    for(int fs:{44100,48000,96000}) {
        const int expected=int(std::lround(fs*0.0015));
        const int original=int(std::lround(fs*0.003));
        RealtimeTuner previous; previous.prepare(fs,2);
        require(previous.latencySamples()==original,
                "legacy 3ms DSP contract changed unexpectedly");
        for(int block:{1,127,257}) {
            RealtimeTuner mini; mini.prepare(fs,2,false,false,1.5);
            require(mini.latencySamples()==expected,"realtime PDC incorrect");
            require(mini.outputDelayContract(false).reportedSamples==expected &&
                    mini.outputDelayContract(true).reportedSamples==expected,
                    "REGULAR/TESTTUNE fixed PDC contract mismatch");
            Parameters p; p.mix=0; p.amount=1;p.pitchClasses=0x0fff;
            mini.setParameters(p);
            const int frames=fs/2;
            std::vector<float> in(std::size_t(frames)),left(std::size_t(frames)),
                               right(std::size_t(frames));
            for(int i=0;i<frames;++i)
                in[std::size_t(i)]=i==371 ? 0.9f : i==452 ? -0.6f :
                    i>fs/4 ? float(0.1*std::sin(2*kPi*321.0*i/fs)) : 0.0f;
            for(int at=0;at<frames;at+=block) {
                const int count=std::min(block,frames-at);
                const float* src[2]={in.data()+at,in.data()+at};
                float* out[2]={left.data()+at,right.data()+at};
                mini.process(src,out,count,2);
            }
            for(int i=expected+20;i<frames;++i) {
                require(std::abs(left[std::size_t(i)]-
                                in[std::size_t(i-expected)])<1e-5f,
                        "realtime mix=0 is not delayed dry at exact PDC");
                require(std::abs(left[std::size_t(i)]-
                                right[std::size_t(i)])<1e-7f,
                        "stereo dry delay lost channel alignment");
            }
            RealtimeTuner wet;wet.prepare(fs,1,false,false,1.5);
            p.mix=1; p.amount=1; p.speedMs=0; p.transientProtection=1.0f;
            wet.setParameters(p);
            float phase=0,peak=0;
            std::vector<float> input(static_cast<std::size_t>(block));
            std::vector<float> output(static_cast<std::size_t>(block));
            for(int at=0;at<fs;at+=block) {
                const int count=std::min(block,fs-at);
                for(int i=0;i<count;++i) {
                    phase+=float(2*kPi*226.0/fs);
                    input[std::size_t(i)]=0.22f*std::sin(phase)+0.04f*std::sin(2*phase);
                    if(at+i==fs/2)input[std::size_t(i)]+=0.57f;
                }
                const float* src[1]={input.data()}; float* out[1]={output.data()};
                wet.process(src,out,count,1);
                for(int i=0;i<count;++i) {
                    const float v=output[std::size_t(i)];
                    require(std::isfinite(v),"nonfinite full-wet low-latency audio");
                    peak=std::max(peak,std::abs(v));
                }
            }
            require(peak>0.06f&&peak<1.0f,"unsafe full-wet low-latency peak");
        }
#ifdef SKELLY_META_TEST
        MetaTest meta;meta.prepare(fs,1.5);
        require(meta.latencySamples()==expected,"META PDC mismatches main engine");
        meta.setParameters(0x0fff,440,0,0);
        std::vector<float> x(std::size_t(fs/2)),out(std::size_t(fs/2));
        for(int i=0;i<int(x.size());++i)
            x[std::size_t(i)]=i==314 ? 0.83f :
                float(0.08*std::sin(2*kPi*315*i/fs));
        out=x;
        for(int i=0;i<int(x.size());i+=127) {
            float* io[1]={out.data()+i};
            meta.process(io,1,std::min(127,int(x.size())-i));
        }
        for(int i=expected+20;i<int(x.size());++i)
            require(std::abs(out[std::size_t(i)]-
                             x[std::size_t(i-expected)])<1e-6f,
                    "META mix=0 dry differs from 1.5ms delayed input");
        meta.reset();
        require(meta.latencySamples()==expected,
                "META mode reset reverted to 3ms");
#endif
        std::cout<<"PASS: low-PDC "<<fs<<" Hz -> "<<expected
                 <<" samples ("<<(1000.0*expected/fs)<<" ms)\n";
    }
}
