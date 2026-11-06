#include "CausalCycleSynth.hpp"
#include "RealtimeTuner.hpp"
#include <vector>
#include <fstream>
#include <cmath>
#include <iostream>
#include <string>
#include <algorithm>
static constexpr double pi=3.14159265358979323846;
static void save(const std::string& filename,const std::vector<float>& data) {
    std::ofstream f(filename,std::ios::binary);
    f.write(reinterpret_cast<const char*>(data.data()),long(data.size()*sizeof(float)));
}
static std::vector<float> makeVowel(int rate,double f0) {
    const int n=rate*5;
    std::vector<float> x(static_cast<size_t>(n),0.f);
    double phase=0;
    for(int i=0;i<n;++i) {
        const double t=double(i)/rate;
        phase+=2*pi*f0/rate;
        const double amplitude=0.63+0.17*std::sin(2*pi*1.73*t+.1)+
            0.13*std::sin(2*pi*3.21*t+.7)+0.08*std::sin(2*pi*5.17*t+1.4);
        double v=0.0;
        for(int k=1;k<25;++k) {
            const double f=k*f0;
            if(f>rate*0.45)break;
            const auto bell=[&](double center,double width){const double d=(f-center)/width;return std::exp(-0.5*d*d);};
            const double a=(0.05+1.3*bell(550,170)+bell(1250,260)+0.4*bell(2450,400))/std::sqrt(double(k));
            v+=a*std::sin(k*phase);
        }
        x[size_t(i)]=float(std::clamp(amplitude*v*.09,-.95,.95));
    }
    return x;
}
static std::vector<float> shift(const std::vector<float>& src,int rate,double f0,int delay,double ratio) {
    skelly::CausalCycleSynth s;
    s.prepare(rate,1);s.setClockLockedDelay(delay);
    std::vector<float> output(src.size(),0.0f);
    float input[2]{},wet[2]{};
    for(size_t i=0;i<src.size();++i){
        input[0]=src[i];s.processSample(input,wet,rate/f0,ratio,true);
        output[i]=wet[0];
    }
    const auto stats=s.status();
    std::cout << "F0="<<f0<<" ratio="<<ratio<<" declaredTimeline="<<double(delay)/rate*1000
              <<"ms meanSourceAge="<<stats.meanWeightedSourceAge/rate*1000
              <<"ms maxMarkOffset="<<stats.maximumTimelineOffset/rate*1000
              <<"ms dropped="<<stats.dropped<<"\n";
    return output;
}
static std::vector<float> integrated(const std::vector<float>& source,int fs,int mode) {
    skelly::RealtimeTuner tuner;
    tuner.prepare(fs,1,mode!=0,mode==40);
    skelly::Parameters p;
    p.experimentalCycleSynthesis=true; p.speedMs=4.0f;
    p.amount=1.0f; p.mix=1.0f; p.airPreservation=0;
    p.transientProtection=0; p.formantPreservation=0;
    tuner.setParameters(p);
    std::vector<float> out(source.size(),0.0f);
    for(size_t i=0;i<source.size();i+=127) {
        const int n=int(std::min(size_t(127),source.size()-i));
        const float* input[]={source.data()+i}; float* output[]={out.data()+i};
        tuner.process(input,output,n,1);
    }
    const auto status=tuner.cycleSynthStatus();
    std::cout<<"INTEGRATED mode="<<mode<<" hostDelaySamples="<<tuner.latencySamples()
             <<" grains="<<status.spawned<<" wetSourceAgeMs="
             <<status.meanWeightedSourceAge/fs*1000.0<<"\n";
    return out;
}
int main(int argc,char**argv) {
    if(argc!=2)return 2;
    const std::string dir=argv[1];
    for(double f0:{95.0,225.0}) {
        const int fs=48000;
        const auto source=makeVowel(fs,f0);
        const std::string prefix=dir+"/envelope_"+std::to_string(int(f0));
        save(prefix+"_dry.f32",source);
        for(int ms:{0,40,50}){
            const auto output=shift(source,fs,f0,int(std::round(ms*fs/1000.0)),1.12);
            save(prefix+"_mode"+std::to_string(ms)+".f32",output);
            const auto live=integrated(source,fs,ms);
            save(prefix+"_integrated"+std::to_string(ms)+".f32",live);
        }
    }
}
