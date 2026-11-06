#include "RealtimeTuner.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static void saveWav(const std::string& path, const std::vector<float>& audio, int fs) {
    std::ofstream f(path, std::ios::binary);
    const uint32_t dataSize=uint32_t(audio.size()*2);
    const uint32_t riffSize=36u+dataSize;
    const uint16_t fmt=1,ch=1,bits=16,align=2;
    const uint32_t byteRate=uint32_t(fs*align), fmtSize=16;
    auto put=[&](auto value) {f.write(reinterpret_cast<const char*>(&value),sizeof(value));};
    f.write("RIFF",4);put(riffSize);f.write("WAVEfmt ",8);put(fmtSize);
    put(fmt);put(ch);put(uint32_t(fs));put(byteRate);put(align);put(bits);
    f.write("data",4);put(dataSize);
    for(float x:audio){const int16_t v=int16_t(std::lround(std::clamp(x,-1.f,1.f)*32767.f));put(v);}
}
int main(int argc,char** argv) {
    const std::string folder=argc>1?argv[1]:".";
    constexpr int fs=48000;
    constexpr double pi=3.1415926535897932384626433832795;
    const int total=int(fs*1.30);
    std::vector<float> input(size_t(total),0.0f);
    double phase=0.0;
    for(int k=0;k<total;++k) {
        const double time=double(k)/fs;
        const double f=110.0*(1+0.0035*std::sin(2*pi*5*time));
        phase+=2*pi*f/fs;
        const double vowel=.28*std::sin(phase)+.15*std::sin(2*phase+.2)
              +.09*std::sin(3*phase+.4*std::sin(time*1.3));
        input[size_t(k)]=float(vowel);
        if(k==fs/2)input[size_t(k)]+=0.72f;
    }
    std::vector<float> legacy(size_t(total),0.f),locked(size_t(total),0.f);
    skelly::CausalCycleSynth s0,s1;
    s0.prepare(fs,1);s1.prepare(fs,1);
    s0.setClockLockedDelay(skelly::studioDelayContract(fs).reportedSamples);
    s1.setClockLockedDelay(skelly::studioFastDelayContract(fs).reportedSamples);
    std::ofstream csv(folder+"/fast_studio_timeline.csv");
    csv<<"input_time_ms,studio50_age_ms,fast40_age_ms,fast40_mark_offset_max_ms\n";
    for(int k=0;k<total;++k) {
        const double t=double(k)/fs;
        const double hz=110.0*(1+0.0035*std::sin(2*pi*5*t));
        const float in[2]={input[size_t(k)],0}; float a[2]{},b[2]{};
        s0.processSample(in,a,fs/hz,1.12,true);
        s1.processSample(in,b,fs/hz,1.12,true);
        legacy[size_t(k)]=a[0];locked[size_t(k)]=b[0];
        if(k%480==0) {
            const auto u=s0.status(),v=s1.status();
            csv<<t*1000<<","<<u.lastWeightedSourceAge*1000/fs<<","<<v.lastWeightedSourceAge*1000/fs
               <<","<<v.maximumTimelineOffset*1000/fs<<"\n";
        }
    }
    saveWav(folder+"/source_110hz.wav",input,fs);
    saveWav(folder+"/studio_50ms.wav",legacy,fs);
    saveWav(folder+"/studio_fast_40ms.wav",locked,fs);
    const auto a=s0.status(),b=s1.status();
    std::printf("Original Studio mean source age %.3f ms / Fast Studio mean %.3f ms\n",
                a.meanWeightedSourceAge*1000/fs,b.meanWeightedSourceAge*1000/fs);
    std::printf("Fast maximum phase-mark deviation %.3f ms, grains %llu, dropped %llu\n",
                b.maximumTimelineOffset*1000/fs,
                (unsigned long long)b.spawned,(unsigned long long)b.dropped);
    return 0;
}
