// Render the same deterministic three-part pitch-transition sequence through
// any SkellyTune RealtimeTuner implementation. Emits planar float32 raw audio
// for independent analysis; WAV formatting is handled by Python outside DSP.
#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc, char** argv) {
    if(argc!=4) {
        std::cerr<<"Usage: transition_render input.f32 output.f32 sample_rate\n";
        return 1;
    }
    const int rate=std::atoi(argv[3]);
    if(rate!=44100 && rate!=48000 && rate!=96000) return 1;
    const int frames=rate*3;
    skelly::RealtimeTuner engine;engine.prepare(rate,1);
    skelly::Parameters params;params.speedMs=6;params.amount=params.mix=1;
    engine.setParameters(params);
    std::vector<float> raw(frames),tuned(frames);
    double phase=0;
    for(int i=0;i<frames;++i) {
        const double t=double(i)/rate;
        const bool passing=t>=0.9 && t<0.94;
        const bool newSyllable=t>=1.6;
        const double hz=passing||newSyllable ? 255.0 : 225.0;
        const double amplitude=newSyllable ? 0.38 : 0.085;
        phase+=2*skelly::kPi*hz/rate;
        raw[i]=float(amplitude*std::sin(phase)+0.28*amplitude*std::sin(2*phase));
    }
    for(int i=0;i<frames;i+=64) {
        const float* src[]={raw.data()+i};float* dst[]={tuned.data()+i};
        engine.process(src,dst,std::min(64,frames-i),1);
    }
    std::ofstream a(argv[1],std::ios::binary),b(argv[2],std::ios::binary);
    if(!a||!b) return 2;
    a.write(reinterpret_cast<const char*>(raw.data()),frames*sizeof(float));
    b.write(reinterpret_cast<const char*>(tuned.data()),frames*sizeof(float));
    return (a&&b)?0:3;
}
