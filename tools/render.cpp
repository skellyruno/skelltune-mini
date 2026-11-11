#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
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
// Emit IEEE float32 WAV, not a silently clipped/quantized PCM16 render.
// The engine may exceed +/-1.0 internally: retain those samples so DAW
// testing and diagnostic plots reveal the real peak rather than hiding it.
static bool save(const std::string& path,const Wav& w) {
    std::ofstream f(path,std::ios::binary);if(!f)return false;
    const uint32_t dataSize=uint32_t(w.samples.size()*4);
    const uint32_t frames=uint32_t(w.samples.size()/size_t(w.channels));
    f.write("RIFF",4);wr32(f,48+dataSize);f.write("WAVEfmt ",8);wr32(f,16);
    wr16(f,3);wr16(f,uint16_t(w.channels));wr32(f,uint32_t(w.rate));
    wr32(f,uint32_t(w.rate*w.channels*4));wr16(f,uint16_t(w.channels*4));wr16(f,32);
    f.write("fact",4);wr32(f,4);wr32(f,frames);
    f.write("data",4);wr32(f,dataSize);
    for (float value:w.samples) {
        if(!std::isfinite(value))value=0.0f;
        uint32_t bits=0;std::memcpy(&bits,&value,4);wr32(f,bits);
    }
    return bool(f);
}
static Wav demo() {
    Wav w;w.samples.resize(size_t(w.rate)*3);
    for(size_t i=0;i<w.samples.size();++i) {
        const double t=double(i)/w.rate;
        const double local=std::fmod(t,1.0);
        const double f=t<1?225.0:(t<2?255.0:195.0);
        const double amp=std::min(1.0,local*25.0)*std::min(1.0,(1.0-local)*25.0);
        w.samples[i]=float(amp*(.27*std::sin(2*skelly::kPi*f*local)
                   + .13*std::sin(4*skelly::kPi*f*local)
                   + .08*std::sin(6*skelly::kPi*f*local)));
    }
    return w;
}
static Wav process(const Wav& w, const skelly::Parameters& p) {
    Wav output;output.rate=w.rate;output.channels=w.channels;output.samples.resize(w.samples.size());
    skelly::RealtimeTuner dsp;dsp.prepare(w.rate,w.channels);dsp.setParameters(p);
    if(const char* confidence=std::getenv("SKELLY_V31_TRACKER_CONF"))
        dsp.setMinimumTrackerConfidence(float(std::atof(confidence)));
    if(const char* duration=std::getenv("SKELLY_V31_BRIDGE_MS"))
        dsp.setShortVoicedBridgeDurationMs(float(std::atof(duration)));
    if(const char* disable=std::getenv("SKELLY_V31_BRIDGE_OFF"))
        dsp.setShortVoicedBridgeEnabled(std::atoi(disable)==0);
    if (std::getenv("SKELLY_V33_ONSET_MEMORY_OFF"))
        dsp.setHardTuneOnsetMemoryEnabled(false);
    if (std::getenv("SKELLY_V30_PROVISIONAL")) dsp.setProvisionalTransitionEnabled(true);
    if (std::getenv("SKELLY_V30_GLOTTAL")) dsp.setGlottalCycleEvidenceEnabled(true);
    const size_t total=w.samples.size()/size_t(w.channels);
    constexpr int block=64;
    std::vector<float> inputA(block),inputB(block),outputA(block),outputB(block);
    std::ofstream trace;
    if(const char* name=std::getenv("SKELLY_TRACE")) {
        trace.open(name); trace << "time,f0,confidence,target,pending,cents,mix,source_age,onset,protected\n";
    }
    for(size_t offset=0;offset<total;offset+=block) {
        const int len=int(std::min<size_t>(block,total-offset));
        for(int i=0;i<len;++i) {
            inputA[i]=w.samples[(offset+i)*w.channels];
            if(w.channels==2)inputB[i]=w.samples[(offset+i)*w.channels+1];
        }
        const float* src[2]={inputA.data(),inputB.data()};
        float* dst[2]={outputA.data(),outputB.data()};
        dsp.process(src,dst,len,w.channels);
        if (trace && offset%256==0) {
            const auto est=dsp.lastPitch();
            trace << double(offset+len)/w.rate << ',' << est.hertz << ',' << est.confidence << ','
                  << dsp.currentTargetNote() << ',' << dsp.targetPending() << ','
                  << dsp.appliedCents() << ',' << dsp.effectiveWetMix() << ','
                  << dsp.currentReadDelaySamples() << ',' << dsp.currentOnsetScore() << ','
                  << dsp.preservedTransientSamples() << '\n';
        }
        for(int i=0;i<len;++i) {
            output.samples[(offset+i)*w.channels]=outputA[i];
            if(w.channels==2)output.samples[(offset+i)*w.channels+1]=outputB[i];
        }
    }
    float maxAbs=0.0f; size_t overFullScale=0;
    for(float sample:output.samples) {
        maxAbs=std::max(maxAbs,std::abs(sample));
        if(std::abs(sample)>1.0f) ++overFullScale;
    }
    std::cout << "DSP floating peak " << maxAbs << ", over full scale " << overFullScale << " samples; ";
    std::cout << "Sample rate: " << w.rate << ", channels: "<<w.channels
              << ", processed: "<<total<<" samples; nominal DSP latency "
              <<dsp.latencySamples()<<" samples; last detected pitch "
              <<dsp.lastPitch().hertz<<" Hz, target MIDI "<<dsp.currentTargetNote()
              << ", bridged tracker hops " << dsp.bridgedHops()
              << ", protected transient samples " << dsp.preservedTransientSamples()
              << ", remembered onset decisions " << dsp.onsetMemoryAppliedHops()
              << ", assisted note transitions " << dsp.onsetAssistedTransitions()
              << ", aligned jumps " << dsp.phaseAlignedJumpCount()
              << ", source-cycle mismatch reduction " << dsp.phaseMatchGainPercent() << "%"
              << ", experimental grains " << dsp.cycleSynthStatus().spawned
              << ", maximum formant-band gain " << dsp.maximumFormantGain() << "\n";
    return output;
}
int main(int argc,char**argv) {
    if(argc<3){
        std::cerr<<"Usage: skelly_render input.wav output.wav [speedMs] [amountPercent] [wetPercent] [vibratoPreservePercent] [flexCents] [pitchClassMask] [cycleStabilizationPercent] [synthesisMode:0=realtime,1=experimental-cycle] [airPreservationPercent:0..100] [formantPreservationPercent:0..100] [transientProtectionPercent:0..100]\n"
                <<"       skelly_render --demo output_prefix [speedMs] [amountPercent] [wetPercent]\n";
        return 2;
    }
    skelly::Parameters p;
    if(argc>3)p.speedMs=std::stof(argv[3]);
    if(argc>4)p.amount=std::stof(argv[4])/100.0f;
    if(argc>5)p.mix=std::stof(argv[5])/100.0f;
    if(argc>6)p.vibratoPreservation=std::stof(argv[6])/100.0f;
    if(argc>7)p.flexCents=std::stof(argv[7]);
    if(argc>8)p.pitchClasses=uint16_t(std::stoul(argv[8]));
    if(argc>9)p.cycleLevelStabilization=std::stof(argv[9])/100.0f;
    if(argc>10)p.experimentalCycleSynthesis=(std::stoi(argv[10])!=0);
    if(argc>11)p.airPreservation=std::clamp(std::stof(argv[11])/100.0f,0.0f,1.0f);
    if(argc>12)p.formantPreservation=std::clamp(std::stof(argv[12])/100.0f,0.0f,1.0f);
    if (argc>13) p.transientProtection=std::clamp(std::stof(argv[13])/100.0f, 0.0f, 1.0f);
    Wav source;
    if(std::string(argv[1])=="--demo") {
        source=demo();
        if(!save(std::string(argv[2])+"_raw.wav",source))return 1;
        if(!save(std::string(argv[2])+"_tuned.wav",process(source,p)))return 1;
        return 0;
    }
    if(!load(argv[1],source)){
        std::cerr<<"Could not read WAV. Supported: PCM16, PCM24 and IEEE float32 mono/stereo 8-192 kHz.\n";
        return 1;
    }
    return save(argv[2],process(source,p))?0:1;
}
