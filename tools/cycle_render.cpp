#include "RealtimeTuner.hpp"
#include "CausalCycleSynth.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
struct Wav {
    int rate=48000, channels=1;
    std::vector<float> samples; // interleaved
};
static uint16_t rd16(const unsigned char*p) {return uint16_t(p[0]|p[1]<<8);}
static uint32_t rd32(const unsigned char*p) {return uint32_t(p[0]|p[1]<<8|p[2]<<16|p[3]<<24);}
static void wr16(std::ofstream& f,uint16_t x) {char b[]={char(x),char(x>>8)}; f.write(b,2);}
static void wr32(std::ofstream& f,uint32_t x) {char b[]={char(x),char(x>>8),char(x>>16),char(x>>24)};f.write(b,4);}
static bool load(const std::string& name,Wav& w) {
    std::ifstream f(name,std::ios::binary); if(!f)return false;
    char riff[12]; if(!f.read(riff,12) || std::memcmp(riff,"RIFF",4) || std::memcmp(riff+8,"WAVE",4))return false;
    uint16_t format=0,bits=0; std::vector<unsigned char> data;
    while(f) {
        unsigned char h[8]; if(!f.read(reinterpret_cast<char*>(h),8))break;
        const uint32_t size=rd32(h+4);
        if(size>512*1024*1024)return false;
        std::vector<unsigned char> chunk(size);
        if(!f.read(reinterpret_cast<char*>(chunk.data()),size))return false;
        if(size&1)f.ignore(1);
        if(std::memcmp(h,"fmt ",4)==0 && size>=16){
            format=rd16(chunk.data()); w.channels=rd16(chunk.data()+2);
            w.rate=int(rd32(chunk.data()+4));bits=rd16(chunk.data()+14);
        }
        if(std::memcmp(h,"data",4)==0)data=std::move(chunk);
    }
    if(w.channels<1||w.channels>2||w.rate<8000||w.rate>192000||data.empty())return false;
    if(!(format==1 && (bits==16 || bits==24)) && !(format==3 && bits==32))return false;
    const auto stride=bits/8;
    if(data.size() % (size_t(stride)*w.channels))return false;
    w.samples.resize(data.size()/stride);
    for(size_t i=0;i<w.samples.size();++i) {
        if(format==1 && bits==16) w.samples[i]=int16_t(rd16(data.data()+i*2))/32768.f;
        else if(format==1 && bits==24) {
            const unsigned char* b=data.data()+i*3;
            int32_t v=int32_t(uint32_t(b[0]) | uint32_t(b[1])<<8 | uint32_t(b[2])<<16);
            if(v & 0x800000) v |= ~int32_t(0xffffff);
            w.samples[i]=float(v)/8388608.0f;
        } else {float x; std::memcpy(&x,data.data()+i*4,4);w.samples[i]=std::isfinite(x)?x:0.0f;}
    }
    return true;
}
static bool save(const std::string& path,const Wav& w) {
    std::ofstream f(path,std::ios::binary);if(!f)return false;
    const uint32_t dataSize=uint32_t(w.samples.size()*2);
    f.write("RIFF",4);wr32(f,36+dataSize);f.write("WAVEfmt ",8);wr32(f,16);
    wr16(f,1);wr16(f,uint16_t(w.channels));wr32(f,uint32_t(w.rate));
    wr32(f,uint32_t(w.rate*w.channels*2));wr16(f,uint16_t(w.channels*2));wr16(f,16);
    f.write("data",4);wr32(f,dataSize);
    for (float x:w.samples) {
        const auto v=int16_t(std::lround(std::clamp(x,-1.0f,0.999969f)*32768.0f));
        wr16(f,uint16_t(v));
    }
    return bool(f);
}

// Experimental demonstration only. Wet PSOLA has period-dependent historical
// source age and must not be substituted for a fixed 3ms live shifter.
int main(int argc,char** argv) {
    if(argc<4){std::cerr<<"Usage: cycle_render input.wav output.wav pitch_ratio (0.67..1.5)\n";return 2;}
    Wav w;
    if(!load(argv[1],w)){std::cerr<<"Unsupported WAV\n";return 1;}
    const double ratio=std::stod(argv[3]);
    if(ratio<0.67||ratio>1.5)return 2;
    skelly::CausalCycleSynth shifter;shifter.prepare(w.rate,w.channels);
    skelly::CausalPitchTracker tracker;tracker.prepare(w.rate);
    Wav dst; dst.rate=w.rate;dst.channels=w.channels;dst.samples.resize(w.samples.size());
    float hz=0.0f;bool voiced=false; double voicedBlend=0.0;
    // A 35ms delayed dry signal provides a safer unvoiced demo reference.
    // Not truly latency aligned with the variable source cycle age.
    const int dryDelay=int(w.rate*.035);
    const int length=int(w.samples.size()/w.channels);
    for(int i=0;i<length;++i){
        const float x0=w.samples[size_t(i*w.channels)],
                    x1=w.channels==2?w.samples[size_t(i*w.channels+1)]:0.0f;
        skelly::PitchEstimate est;
        if(tracker.push(w.channels==2?.5f*(x0+x1):x0,est)){
            if(est.valid && est.confidence>.70f){hz=est.hertz;voiced=true;}
            else voiced=false;
        }
        const float in[2]{x0,x1};float out[2]{};
        shifter.processSample(in,out,hz>65.0f?double(w.rate)/hz:0.0,ratio,voiced);
        const bool active=voiced && hz>65.0f;
        const double blendStep=1.0/(w.rate*.006);
        voicedBlend += std::clamp((active?1.0:0.0)-voicedBlend,-blendStep,blendStep);
        for(int c=0;c<w.channels;++c){
            const float dry=i>=dryDelay?w.samples[size_t((i-dryDelay)*w.channels+c)]:0.0f;
            const float sample=float(voicedBlend*out[c]+(1.0-voicedBlend)*dry);
            dst.samples[size_t(i*w.channels+c)]=std::isfinite(sample)?std::clamp(sample,-1.0f,1.0f):0.0f;
        }
    }
    const auto status=shifter.status();
    std::cout<<"Experimental cycle render: input="<<length<<" samples, ratio="<<ratio
        <<" grains="<<status.spawned<<" dropped="<<status.dropped
        <<" minimumSourceAge="<<status.minimumSourceAge<<" samples; input latency varies by pitch period\n";
    return save(argv[2],dst)?0:1;
}
