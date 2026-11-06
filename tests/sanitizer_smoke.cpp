#include "RealtimeTuner.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

int main() {
    for (int rate : {44100, 48000, 96000, 192000}) {
        for (int channels : {1,2}) {
            skelly::RealtimeTuner tuner;
            tuner.prepare(rate, channels);
            skelly::Parameters params;
            params.speedMs = 6.0f;
            tuner.setParameters(params);
            constexpr int maxBlock=257;
            std::array<std::vector<float>,2> source = {std::vector<float>(maxBlock),std::vector<float>(maxBlock)};
            std::array<std::vector<float>,2> result = {std::vector<float>(maxBlock),std::vector<float>(maxBlock)};
            const int total = rate / 6;
            for(int offset=0;offset<total;offset+=maxBlock) {
                const int n=std::min(maxBlock,total-offset);
                for(int i=0;i<n;++i) {
                    const double time=double(offset+i)/rate;
                    const double f = time<0.07 ? 225.0 : 255.0;
                    source[0][i]=float(.25*std::sin(2*skelly::kPi*f*time));
                    source[1][i]=-source[0][i];
                }
                const float* in[]={source[0].data(),source[1].data()};
                float* out[]={result[0].data(),result[1].data()};
                tuner.process(in,out,n,channels);
                for(int c=0;c<channels;++c)
                    for(int i=0;i<n;++i)
                        if(!std::isfinite(out[c][i])) return 1;
            }
        }
    }
    std::puts("PASS: ASan/UBSan smoke: mono/stereo, 44.1/48/96/192 kHz");
}
