#include "RealtimeTuner.hpp"
#include <vector>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <cstdio>
#include <string>
#include <cstdlib>

static constexpr double PI=3.1415926535897932384626433832795;
static void writeRaw(const std::string& name,const std::vector<float>& samples) {
    std::ofstream f(name,std::ios::binary);
    f.write(reinterpret_cast<const char*>(samples.data()),std::streamsize(samples.size()*sizeof(float)));
}
// A vowel-like harmonic source with time-varying fundamental and spectral resonances.
static std::vector<float> makeVowel(int fs,double seconds,double f0) {
    const int n=int(fs*seconds);
    std::vector<float> x(static_cast<size_t>(n), 0.0f);
    double phase=0.0;
    uint32_t rand=0x531A7E9D;
    for(int i=0;i<n;++i){
        double t=i/double(fs);
        double currentF0=f0*(1.0+0.009*std::sin(2*PI*4.7*t));
        phase+=2*PI*currentF0/fs;
        double formant1=580+80*std::sin(2*PI*.9*t);
        double formant2=1250+120*std::cos(2*PI*.57*t);
        double vocal=0;
        for(int h=1;h<=40;++h) {
            double freq=h*currentF0;
            if(freq>=fs*.47)break;
            auto bell=[&](double center,double width) {
                double v=(freq-center)/width;
                return std::exp(-.5*v*v);
            };
            double amp=(.05 + 1.1*bell(formant1,160)+.8*bell(formant2,230)+.5*bell(2550,430))/std::sqrt(double(h));
            // Variation in harmonic phase models non-identical consecutive cycles.
            double ph=h*phase + .09*std::sin(.5*h + 2*PI*1.3*t);
            vocal+=amp*std::sin(ph);
        }
        rand^=rand<<13;rand^=rand>>17;rand^=rand<<5;
        double white=double(rand)/4294967295.0*2.0-1.0;
        double env=std::min(1.0,t*12.0)*std::min(1.0,(seconds-t)*12.0);
        x[size_t(i)]=float(std::clamp((.11*vocal+.004*white)*env,-1.0,1.0));
    }
    return x;
}
static std::vector<float> renderDirect(skelly::CausalCycleSynth& synth,int fs,double freq,double ratio,bool wide,std::vector<float>& source,skelly::CausalCycleSynth::Status& st) {
    synth.prepare(fs,1);
    synth.setWidePhaseReferenceEnabled(wide);
    source=makeVowel(fs,2.3,freq);
    std::vector<float> out(source.size());
    float in[2]{},o[2]{};
    for(size_t i=0;i<source.size();++i) {
        in[0]=source[i];
        synth.processSample(in,o,fs/(freq*(1+.009*std::sin(2*PI*4.7*i/fs))),ratio,true);
        out[i]=o[0];
    }
    st=synth.status();
    return out;
}
int main(int argc,char**argv){
    if(argc<2){std::fprintf(stderr,"Usage: wet_path_probe output_prefix [fs] [f0] [ratio]\n");return 2;}
    const std::string prefix=argv[1];
    const int fs=argc>2?std::atoi(argv[2]):48000;
    const double f0=argc>3?std::atof(argv[3]):95.0;
    const double ratio=argc>4?std::atof(argv[4]):1.12;
    skelly::CausalCycleSynth synth;
    skelly::CausalCycleSynth::Status oldSt{},newSt{};
    std::vector<float> source,sourceNew;
    const auto old=renderDirect(synth,fs,f0,ratio,false,source,oldSt);
    const auto upgraded=renderDirect(synth,fs,f0,ratio,true,sourceNew,newSt);
    writeRaw(prefix+"_source.f32",source);
    writeRaw(prefix+"_old.f32",old);
    writeRaw(prefix+"_new.f32",upgraded);
    std::printf("fs=%d f0=%.3f ratio=%.4f dry_reported=%.3fms\n",fs,f0,ratio,1000.0*std::lround(fs*.003)/fs);
    std::printf("version,spawned,drop,mean_weighted_age_ms,min_age_ms,max_age_ms,valid_frames,mark_refinements\n");
    auto line=[&](const char* name,skelly::CausalCycleSynth::Status st){
        std::printf("%s,%llu,%llu,%.4f,%.4f,%.4f,%llu,%llu\n",name,
            (unsigned long long)st.spawned,(unsigned long long)st.dropped,
            1000.0*st.meanWeightedSourceAge/fs,1000.0*st.minimumSourceAge/fs,
            1000.0*st.maximumSourceAge/fs,(unsigned long long)st.validFrames,
            (unsigned long long)st.refinedPitchMarks);
    };
    line("v1.8",oldSt);line("v1.9",newSt);
    // Independent sample-exact impulse test of reported dry transport delay.
    skelly::RealtimeTuner dry;
    dry.prepare(fs,1);
    skelly::Parameters p;p.amount=0.0f;dry.setParameters(p);
    std::vector<float> impulse(size_t(fs)/5,0.f),y(impulse.size());
    impulse[130]=1.f;
    const float* ip[]={impulse.data()};float* op[]={y.data()};
    dry.process(ip,op,int(y.size()),1);
    const int peak=int(std::max_element(y.begin(),y.end())-y.begin());
    std::printf("DRY_IMPULSE measured=%d samples declared=%d\n",peak-130,dry.latencySamples());
    // Full live engine, not just an isolated grain generator. Output-specific
    // source ages remain state-dependent and should never be interpreted as
    // a universal reported VST3 latency.
    skelly::RealtimeTuner integrated;integrated.prepare(fs,1);
    skelly::Parameters live;live.speedMs=5.0f;live.transientProtection=0;
    live.experimentalCycleSynthesis=true;live.airPreservation=0;
    integrated.setParameters(live);
    std::vector<float> processed(source.size());
    const float* sourcePtr[]={source.data()};float* processedPtr[]={processed.data()};
    integrated.process(sourcePtr,processedPtr,int(source.size()),1);
    const auto integratedStatus=integrated.cycleSynthStatus();
    std::printf("INTEGRATED mode=PSOLA grain_count=%llu mean_src_age_ms=%.3f "
                "min_src_age_ms=%.3f max_src_age_ms=%.3f "
                "baseline_wet_source_age_min_ms=%.3f "
                "baseline_wet_source_age_max_ms=%.3f\n",
                (unsigned long long)integratedStatus.spawned,
                1000.*integratedStatus.meanWeightedSourceAge/fs,
                1000.*integratedStatus.minimumSourceAge/fs,
                1000.*integratedStatus.maximumSourceAge/fs,
                1000.*integrated.minimumWetSourceAgeSamples()/fs,
                1000.*integrated.maximumWetSourceAgeSamples()/fs);
    return 0;
}
