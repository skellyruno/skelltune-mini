#include "CausalCycleSynth.hpp"
#include <chrono>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>
static constexpr double pi=3.1415926535897932384626433832795;
int main() {
    constexpr int fs=48000,n=fs*5;
    std::vector<float> audio(n);
    for(int i=0;i<n;++i){
        const double t=i/double(fs);
        audio[size_t(i)]=float(.28*std::sin(2*pi*95*t)+.13*std::sin(2*pi*190*t)+.10*std::sin(2*pi*380*t));
    }
    for(int mode=0;mode<2;++mode){
        double times[3]{}; volatile float checksum=0;
        for(int trial=0;trial<3;++trial) {
            skelly::CausalCycleSynth s;s.prepare(fs,1);
            s.setWidePhaseReferenceEnabled(mode!=0);
            float in[2]{},out[2]{};
            const auto start=std::chrono::steady_clock::now();
            for(int i=0;i<n;++i) {
                in[0]=audio[size_t(i)];s.processSample(in,out,double(fs)/95.0,1.12,true);
                checksum+=out[0]*0.000001f;
            }
            times[trial]=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        }
        std::sort(std::begin(times),std::end(times));
        std::printf("%s, median_seconds=%.5f, realtime_speed=%.1fx, checksum=%.4f\n",
            mode?"v1.9 wide matcher":"v1.8 short matcher",times[1],5.0/times[1],float(checksum));
    }
}
