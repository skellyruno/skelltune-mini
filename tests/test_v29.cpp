#include "RealtimeTuner.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <cstdlib>

static int assertions=0;
static void require(bool condition,const char* message){++assertions;if(!condition){std::fprintf(stderr,"FAILED: %s\n",message);std::abort();}}
struct Output {std::vector<float> left,right;double meanWet=0.0;int counted=0;};
static Output render(int rate,int channels,bool upgraded,bool consonants,bool tuning,bool antiphase=false){
    skelly::RealtimeTuner d;d.prepare(rate,channels);d.setHarmonicAwareWetGateEnabled(upgraded);
    skelly::Parameters p; p.speedMs=0;p.amount=1;p.mix=1;
    p.pitchClasses=uint16_t(1u<<9);p.airPreservation=.65f;
    p.transientProtection=tuning?1.0f:0.0f;
    d.setParameters(p);
    const int n=int(rate*.85);Output result;result.left.resize(size_t(n));result.right.resize(size_t(n));
    uint32_t seed=0x1a4ec29;
    constexpr int block=73;
    float left[block]{},right[block]{},ol[block]{},orr[block]{};
    for(int offset=0;offset<n;offset+=block){
        const int len=std::min(block,n-offset);
        for(int i=0;i<len;++i){
            const double t=double(i+offset)/rate;
            // 225Hz input -> 220Hz A3: bright upper harmonics are voiced,
            // whereas subsequent broadband high-frequency input is not.
            double wave=0.24*std::sin(2*skelly::kPi*225*t)+
                0.18*std::sin(4*skelly::kPi*225*t)+
                0.13*std::sin(12*skelly::kPi*225*t)+
                0.08*std::sin(18*skelly::kPi*225*t);
            if(consonants && t>.54 && t<.68) {
                seed=seed*1664525u+1013904223u;
                const double noise=2.0*double(seed>>8)/16777216.0-1.0;
                wave=.17*wave + .16*noise;
            }
            if(t<.020)wave*=t/.020;
            left[i]=float(wave);right[i]=antiphase?-left[i]:left[i];
        }
        const float* src[2]={left,right};float* dst[2]={ol,orr};
        d.process(src,dst,len,channels);
        for(int i=0;i<len;++i){
            const int idx=offset+i;
            result.left[size_t(idx)]=ol[i];
            result.right[size_t(idx)]=channels==2?orr[i]:0;
            require(std::isfinite(ol[i]),"finite output");
            if(channels==2)require(std::isfinite(orr[i]),"finite second channel");
            if(channels==2 && antiphase)
                require(std::abs(ol[i]+orr[i])<1e-5,"stereo antiphase remains linked");
        }
        const double t=double(offset+len)/rate;
        if(t>.20 && t<.52){result.meanWet+=d.effectiveWetMix();++result.counted;}
    }
    result.meanWet/=std::max(1,result.counted);
    require(d.latencySamples()==int(std::lround(rate*.003)),"3ms declared latency preserved");
    return result;
}
int main(){
    for(int rate:{44100,48000,96000}){
        // Changing the harmonic classifier must have NO effect with 0%
        // transient/noise gate: proof that the modified code is isolated.
        auto zeroOld=render(rate,1,false,false,false);
        auto zeroNew=render(rate,1,true,false,false);
        require(zeroOld.left==zeroNew.left,"disabled transient guard stays bit exact");
        auto brightOld=render(rate,1,false,true,true);
        auto brightNew=render(rate,1,true,true,true);
        require(brightNew.meanWet>=brightOld.meanWet-1e-6,"voiced gate never reduces sustained wet mix");
        require(brightNew.meanWet<=1.0+1e-5,"bounded wet mix");
        auto stereo=render(rate,2,true,true,true,true);
        require(stereo.left.size()==brightOld.left.size(),"stereo frame length");
        double sumDeltaV=0,sumDeltaN=0;int v=0,n=0;
        for(size_t i=0;i<brightOld.left.size();++i){
            const double t=double(i)/rate;
            const double delta=std::abs(double(brightOld.left[i]-brightNew.left[i]));
            if(t>.25 && t<.50){sumDeltaV+=delta;++v;}
            if(t>.56 && t<.67){sumDeltaN+=delta;++n;}
        }
        // Absolute noise preservation is more meaningful than requiring
        // byte parity across a deliberate processing change.
        const double vowelDelta=sumDeltaV/std::max(1,v);
        const double noiseDelta=sumDeltaN/std::max(1,n);
        std::printf("%dHz: mean wet old=%.4f new=%.4f, voxel delta=%.6f noise delta=%.6f\n",
                    rate,brightOld.meanWet,brightNew.meanWet,vowelDelta,noiseDelta);
        require(noiseDelta<.13,"consonant change bounded");
    }
    std::printf("v2.9 harmonic-aware wet gate: %d assertions passed\n",assertions);
}
