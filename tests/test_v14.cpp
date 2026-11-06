#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {
constexpr double pi=3.1415926535897932384626433832795;
int failures=0;
void test(bool ok,const char* description){std::printf("%s: %s\n",ok?"PASS":"FAIL",description);failures+=!ok;}
struct Render {std::vector<float> left,right; double blend=0; unsigned long long grains=0;};
Render run(int fs, int block, float air, bool cycle, bool noisy, bool invertedStereo=false) {
    const int n=fs*15/10;
    skelly::RealtimeTuner tuner; tuner.prepare(fs,2);
    skelly::Parameters p; p.speedMs=5.0f; p.mix=1.0f; p.amount=1.0f;
    p.airPreservation=air; p.transientProtection=0; p.experimentalCycleSynthesis=cycle;
    tuner.setParameters(p);
    std::vector<float> l(n),r(n),ol(n),orr(n);
    uint32_t noiseSeed=0x31415926;
    for(int i=0;i<n;++i){
        const double t=i/double(fs);
        const double low = .34*std::sin(2*pi*225*t)+.1*std::sin(4*pi*225*t+0.3)+.06*std::sin(6*pi*225*t+0.9);
        noiseSeed^=noiseSeed<<13;noiseSeed^=noiseSeed>>17;noiseSeed^=noiseSeed<<5;
        const double white = 2.0*(double(noiseSeed)/4294967295.0)-1.0;
        const double airSignal=noisy?(.17*std::sin(2*pi*6300*t)+.035*white):0.0;
        l[i]=float(low+airSignal);r[i]=invertedStereo?-l[i]:l[i];
    }
    for(int j=0;j<n;j+=block){
        const int count=std::min(block,n-j);
        const float* in[]={l.data()+j,r.data()+j}; float* out[]={ol.data()+j,orr.data()+j};
        tuner.process(in,out,count,2);
    }
    auto status=tuner.cycleSynthStatus();
    Render re;re.left=std::move(ol);re.right=std::move(orr);
    re.grains=status.spawned; re.blend=tuner.currentCycleBlend();
    return re;
}
// Target a highband component which would otherwise undergo tonal cycle
// reconstruction. The unmodified delayed signal is the phase reference.
double toneProjection(const std::vector<float>& x,int fs,double freq){
    double a=0,b=0;
    for (int i=fs/2;i<int(x.size());++i){
        const double phase=2*pi*freq*i/fs;
        a+=x[size_t(i)]*std::cos(phase);
        b+=x[size_t(i)]*std::sin(phase);
    }
    const double samples=int(x.size())-fs/2;
    return 2.0*std::hypot(a,b)/samples;
}
void testAir(int fs) {
    auto none=run(fs,64,0,true,true);
    auto restored=run(fs,64,1,true,true);
    const double low1=toneProjection(none.left,fs,220);
    const double low2=toneProjection(restored.left,fs,220);
    const double hf1=toneProjection(none.left,fs,6300);
    const double hf2=toneProjection(restored.left,fs,6300);
    double maxStereo=0;
    for(size_t i=0;i<none.left.size();++i){
        maxStereo=std::max(maxStereo,std::abs(double(restored.left[i]-restored.right[i])));
    }
    std::printf("fs%d cycle %.4f grain %llu: 220Hz %.4f->%.4f, 6300Hz %.4f->%.4f\n",fs,restored.blend,restored.grains,low1,low2,hf1,hf2);
    test(restored.grains>100 && restored.blend>0.9,"PSOLA mode engages on noisy voiced input");
    test(hf2>hf1*1.1,"High-frequency articulation is restored by Air control");
    test(low2>low1*0.75 && low2<low1*1.25,"Low-frequency corrected tone remains stable");
    test(maxStereo<1e-6,"Linked stereo processing remains symmetric");
}
void testCleanVoiced() {
    const int fs=48000;
    const auto noAir=run(fs,64,0,true,false);
    const auto fullAir=run(fs,64,1,true,false);
    double maximum=0;
    for(size_t i=0;i<noAir.left.size();++i)
        maximum=std::max(maximum,std::abs(double(noAir.left[i]-fullAir.left[i])));
    std::printf("CLEAN VOICED: maximum Air on/off sample difference %.8g\n",maximum);
    test(maximum<1.e-5,"Clean harmonic vocals are unaffected when no broadband air is detected");
}
void testBlock(int fs){
    auto a=run(fs,1,0.8f,true,true,true);
    auto b=run(fs,257,0.8f,true,true,true);
    double maxError=0, anti=0;
    for(size_t i=0;i<a.left.size();++i){
        maxError=std::max(maxError,std::abs(double(a.left[i]-b.left[i])));
        anti=std::max(anti,std::abs(double(a.left[i]+a.right[i])));
    }
    test(maxError==0,"1-sample and 257-sample blocks remain sample identical");
    test(anti<1e-5,"Air preservation honors opposite-polarity stereo");
}
}
int main(){for (int fs:{44100,48000,96000})testAir(fs);testCleanVoiced();testBlock(48000);return failures?1:0;}
