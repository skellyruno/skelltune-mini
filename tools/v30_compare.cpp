#include "RealtimeTuner.hpp"
#include "CausalCycleSynth.hpp"
#include "CausalLpcSourceFilter.hpp"
#include <vector>
#include <cmath>
#include <fstream>
#include <string>
#include <iostream>
#include <algorithm>
#include <chrono>

static constexpr double pi=3.14159265358979323846;
static void save(const std::string& file,const std::vector<float>& data) {
    std::ofstream stream(file, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(data.data()),long(data.size()*sizeof(float)));
}
static std::vector<float> load(const std::string& file) {
    std::ifstream stream(file,std::ios::binary|std::ios::ate);
    if(!stream)return {};
    auto size=stream.tellg(); stream.seekg(0);
    std::vector<float> result(size_t(size/4),0);
    stream.read(reinterpret_cast<char*>(result.data()),long(result.size()*4));
    return result;
}
static std::vector<float> vowel(int fs,double frequency,int formantSet) {
    const int n=fs*2;
    std::vector<float> out(static_cast<size_t>(n),0.0f);
    const double F1= formantSet==0? 650.0 : (formantSet==1? 390.0 : 810.0);
    const double F2= formantSet==0? 1100.0 : (formantSet==1? 2050.0 : 1350.0);
    double phase=0;
    for(int i=0;i<n;++i) {
        const double t=double(i)/fs;
        const double f=frequency*(1.0+0.009*std::sin(2*pi*5.1*t));
        phase+=2*pi*f/fs;
        double value=0;
        for(int h=1;h<45;++h) {
            const double freq=f*h;
            if(freq>fs*0.45)break;
            const auto bell=[&](double center,double width) { const double x=(freq-center)/width;return std::exp(-.5*x*x); };
            const double envelope=0.035+bell(F1,130.0)+0.8*bell(F2,210.0)+0.23*bell(2800,460);
            const double amp=envelope/std::sqrt(double(h));
            value+=amp*std::sin(h*phase+0.17*std::sin(h*0.23+2*pi*0.8*t));
        }
        const double attack=std::clamp(t*20.0,0.0,1.0);
        const double release=std::clamp((2.0-t)*20.0,0.0,1.0);
        const double modulation=0.72+0.12*std::sin(2*pi*7.0*t)+0.10*std::sin(2*pi*12.1*t);
        out[size_t(i)]=float(std::clamp(value*.085*modulation*attack*release,-0.95,0.95));
    }
    return out;
}
static std::vector<float> synthDirect(const std::vector<float>& input,int fs,double f0,int mode) {
    skelly::CausalCycleSynth synth;
    synth.prepare(fs,1); synth.setClockLockedDelay(int(std::lround(fs*.05)));
    synth.setOverlapCoherenceEnabled(true);
    synth.setClosureEvidenceEnabled(mode == 1);
    synth.setStrongerConcurrentPhaseEnabled(mode == 3);
    skelly::CausalLpcSourceFilter lpc;lpc.prepare(fs,1);
    std::vector<float> out(input.size(),0.0f);
    const auto started=std::chrono::steady_clock::now();
    float in[2]{},wet[2]{};
    for(size_t i=0;i<input.size();++i) {
        in[0]=input[i];
        const double t=double(i)/fs;
        const double f=f0*(1.0+0.009*std::sin(2*pi*5.1*t));
        float residual[2]{},filtered[2]{};
        if(mode==2) {
            lpc.analyze(in,residual);
            synth.processSample(residual,wet,fs/f,1.12,true);
            lpc.synthesize(wet,filtered);
            out[i]=filtered[0];
        } else {
            synth.processSample(in,wet,fs/f,1.12,true);
            out[i]=wet[0];
        }
    }
    const auto stats=synth.status();
    const double elapsedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    std::cout<<"direct "<<f0<<" mode="<<mode<<" grains="<<stats.spawned<<" rejected="<<stats.dropped
        <<" sourceAgeMs="<<1000.0*stats.meanWeightedSourceAge/fs<<" elapsedMs="<<elapsedMs
        <<" concurrentCorr="<<stats.meanConcurrentPhaseAgreement<<" negativeGrains="<<stats.negativeConcurrentAgreementGrains<<"\n";
    return out;
}
static std::vector<float> integrated(const std::vector<float>& input,int fs,bool overlap) {
    skelly::RealtimeTuner tuner;
    tuner.prepare(fs,1,true,false); tuner.setCycleOverlapCoherenceEnabled(overlap);
    skelly::Parameters p;
    p.experimentalCycleSynthesis=true;p.speedMs=6;p.airPreservation=.65f;
    p.amount=1;p.mix=1;p.formantPreservation=0;
    tuner.setParameters(p);
    std::vector<float> output(input.size());
    constexpr int chunk=127;
    for(size_t index=0;index<input.size();index+=chunk) {
        const int frames=int(std::min(size_t(chunk),input.size()-index));
        const float* in[]={input.data()+index}; float* out[]={output.data()+index};
        tuner.process(in,out,frames,1);
    }
    return output;
}
int main(int argc,char**argv) {
    if(argc<2) return 2;
    const std::string directory=argv[1];
    int fs=48000;
    for(int v=0;v<3;++v)for(double f0:{75.,95.,110.,155.,220.}) {
        auto x=vowel(fs,f0,v);
        const auto old=synthDirect(x,fs,f0,0);
        const auto improved=synthDirect(x,fs,f0,1);
        const auto lpc=synthDirect(x,fs,f0,2);
        const auto phase=synthDirect(x,fs,f0,3);
        const std::string id=directory+"/vowel"+std::to_string(v)+"_"+std::to_string(int(f0));
        save(id+"_input.f32",x); save(id+"_old.f32",old);
        save(id+"_new.f32",improved);save(id+"_lpc.f32",lpc);save(id+"_phase.f32",phase);
    }
    if(argc>=3){auto voice=load(argv[2]);
        if(!voice.empty()){
            save(directory+"/real_old.f32",integrated(voice,fs,false));
            save(directory+"/real_new.f32",integrated(voice,fs,true));
            save(directory+"/real_input.f32",voice);
        }
    }
    return 0;
}
