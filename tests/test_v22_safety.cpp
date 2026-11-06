// Short focused sanitizer smoke test of Studio Fast ring-buffer and clocked PSOLA reads.
#include "RealtimeTuner.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
int main(){
    int cases=0;
    for(int fs:{44100,48000,96000}) {
        skelly::CausalCycleSynth synth;
        synth.prepare(fs,2);
        synth.setClockLockedDelay(skelly::studioFastDelayContract(fs).reportedSamples);
        float in[2]{},out[2]{};
        bool valid=false;
        for(int k=0;k<fs/5;++k){
            const double hz=k<fs/10 ? 65.0:110.0;
            const double x=.3*std::sin(2*skelly::kPi*hz*k/fs);
            in[0]=float(x);in[1]=-in[0];
            synth.processSample(in,out,fs/hz,1.10,true);
            if(!std::isfinite(out[0])||!std::isfinite(out[1]))return 1;
            valid|=synth.hasOutputFrame();
        }
        if(!valid || synth.status().dropped!=0)return 2;
        skelly::RealtimeTuner tuner;
        tuner.prepare(fs,1,true,true);
        skelly::Parameters p;p.mix=1;p.amount=1;p.experimentalCycleSynthesis=true;
        tuner.setParameters(p);
        std::vector<float> input(size_t(fs/8),0),output(input.size());
        input[size_t(fs/100)]=.7f;
        const float* src[]{input.data()};float* dst[]{output.data()};
        tuner.process(src,dst,int(input.size()),1);
        if(output[size_t(fs/100+tuner.latencySamples())]!=.7f)return 3;
        ++cases;
    }
    std::printf("Fast Studio sanitizer smoke: %d sample rates passed\n",cases);
    return 0;
}
