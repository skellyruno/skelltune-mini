// Two-mode output transient/PDC probe. No JUCE dependency; deterministic.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static void wav(const std::string& path, const std::vector<float>& a, int fs) {
    std::ofstream f(path,std::ios::binary);
    const uint32_t n=uint32_t(a.size()*2), total=36+n, rate=uint32_t(fs*2);
    auto w16=[&](uint16_t v){f.put(char(v&255));f.put(char(v>>8));};
    auto w32=[&](uint32_t v){w16(uint16_t(v&65535));w16(uint16_t(v>>16));};
    f.write("RIFF",4);w32(total);f.write("WAVEfmt ",8);w32(16);w16(1);w16(1);
    w32(uint32_t(fs));w32(rate);w16(2);w16(16);f.write("data",4);w32(n);
    for(float value:a){const int v=int(std::lrint(std::clamp(value,-1.f,1.f)*32767.f));w16(uint16_t(int16_t(v)));}
}
int main(int argc,char** argv){
    if(argc<2){std::fprintf(stderr,"Usage: measure_output_delay output_prefix [sample_rate]\n");return 2;}
    const std::string prefix=argv[1];const int fs=argc>2?std::atoi(argv[2]):48000;
    if(fs<8000||fs>192000)return 2;
    const int n=fs*1, impulse=fs/2;
    std::vector<float> x(size_t(n),0.f);
    for(int i=0;i<n;++i)x[size_t(i)]=float(.23*std::sin(2*skelly::kPi*225.0*i/fs));
    x[size_t(impulse)]+=.95f;
    wav(prefix+"_input.wav",x,fs);
    std::ofstream csv(prefix+"_measurements.csv");
    csv<<"mode,sample_rate,reported_samples,reported_ms,input_transient_index,output_transient_index,input_value,output_value_at_declared_delay,abs_error,protected_frames,fixed_wet_waveform_delay\n";
    for(int mode=0;mode<2;++mode){
        skelly::RealtimeTuner t;t.prepare(fs,1);
        skelly::Parameters p;p.speedMs=1;p.amount=1;p.mix=1;p.airPreservation=0;p.transientProtection=0;
        p.experimentalCycleSynthesis=(mode==1);t.setParameters(p);
        std::vector<float> out(x.size());
        const float* in[]={x.data()};float* op[]={out.data()};t.process(in,op,n,1);
        #ifdef SKELLY_V19_BASELINE
        const int reported=t.latencySamples();
        const int fixedWetWaveformDelay=0;
        const unsigned long long protectedFrames=0;
#else
        const auto c=t.outputDelayContract(mode==1);
        const int reported=c.reportedSamples;
        const int fixedWetWaveformDelay=int(c.fixedWetWaveformDelay);
        const unsigned long long protectedFrames=static_cast<unsigned long long>(t.preservedTransientSamples());
#endif
        const int at=impulse+reported;
        const double err=std::abs(double(x[size_t(impulse)]-out[size_t(at)]));
        const char* m=mode?"PSOLA":"Realtime";
        wav(prefix+(mode?"_psola.wav":"_realtime.wav"),out,fs);
        csv<<m<<","<<fs<<","<<reported<<","<<1000.*reported/fs<<","<<impulse
            <<","<<at<<","<<x[size_t(impulse)]<<","<<out[size_t(at)]<<","<<err<<","<<protectedFrames<<","<<fixedWetWaveformDelay<<"\n";
        std::printf("%s: reported=%d samples; transient at output sample %d; value=%.6f; error=%.8f; wet waveform constant delay=%s\n",
              m,reported,at,out[size_t(at)],err,fixedWetWaveformDelay?"YES":"NO");
    }
    return 0;
}
