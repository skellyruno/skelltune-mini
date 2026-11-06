#include "RealtimeTuner.hpp"
#include <cmath>
#include <iostream>
#include <algorithm>
#include <vector>
#include <array>

static int failures = 0;
static void check(bool condition, const char* message) {
    std::cout << (condition ? "PASS " : "FAIL ") << message << '\n';
    if (!condition) ++failures;
}
struct TrackingResult { int updates=0, accepted=0; double avgHz=0; };
static TrackingResult track(int fs,double hz, double level = 0.8, double additiveHz = 0.0, double additiveLevel=0.0) {
    skelly::CausalPitchTracker tracker;
    tracker.prepare(fs);
    TrackingResult r{};
    for (int i=0; i<fs*2; ++i) {
        const double t = double(i)/fs;
        const float sample=float(level*std::sin(2*skelly::kPi*hz*t) +
           additiveLevel*std::sin(2*skelly::kPi*additiveHz*t));
        skelly::PitchEstimate e;
        if(tracker.push(sample,e) && i>=fs) {
            ++r.updates;
            if(e.valid) {++r.accepted; r.avgHz+=e.hertz;}
        }
    }
    if(r.accepted) r.avgHz/=r.accepted;
    return r;
}
static void exerciseOutOfBandTonalGate() {
    for(const auto& cfg: {std::array<int,2>{48000,7000}, {96000,7000}, {96000,10500}}) {
        const auto a=track(cfg[0],cfg[1]);
        std::cout<<"REJECTION fs="<<cfg[0]<<" hz="<<cfg[1]<<" detected="<<a.accepted<<"/"<<a.updates<<'\n';
        check(a.updates>500 && a.accepted==0,"out-of-band sine does not produce false vocal notes");
    }
}
static void exercisePitchIntegrity() {
    for(const int fs: {44100,48000,96000}) {
        for(const double f: {85.0,110.0,220.0,440.0,780.0}) {
            const auto a=track(fs,f);
            const double error= a.avgHz > 0 ? std::abs(1200.0*std::log2(a.avgHz/f)) : 9999.;
            std::cout<<"VOICE fs="<<fs<<" f="<<f<<" mean="<<a.avgHz<<" error_cents="<<error
                <<" accepted="<<a.accepted<<"/"<<a.updates<<'\n';
            check(a.accepted>=a.updates*0.95 && error < 18.0,"voiced fundamental survives the anti-alias gate");
        }
    }
}
static void exerciseVoicedUnderHighFrequencyInterference() {
    for (int fs: {48000,96000}) {
        const auto a=track(fs,220.0,0.6,7000.0,0.5);
        const double cents=a.avgHz>0?1200.0*std::log2(a.avgHz/220.0):9999.;
        std::cout<<"MIXED fs="<<fs<<" valid="<<a.accepted<<"/"<<a.updates
                 <<" avgHz="<<a.avgHz<<" cents="<<cents<<'\n';
        check(a.accepted>a.updates*0.9 && std::abs(cents)<18,
            "voiced fundamental survives moderate high-frequency interference");
    }
}
static void exerciseRealtimeEngine() {
    const int fs=48000,n=fs*2;
    skelly::RealtimeTuner engine;
    engine.prepare(fs,1);
    skelly::Parameters p; p.speedMs=3;p.mix=1;p.amount=1;p.transientProtection=0;
    engine.setParameters(p);
    std::vector<float> input(n),out(n);
    for(int i=0;i<n;++i) input[i]=float(0.8*std::sin(2*skelly::kPi*7000*i/fs));
    const int block=137;
    for(int i=0;i<n;i+=block) {
        const float* in[]={input.data()+i};float* dst[]={out.data()+i};
        engine.process(in,dst,std::min(block,n-i),1);
    }
    double error=0., power=0.;const int latency=engine.latencySamples();
    for(int i=fs;i<n;++i) {
        const double dry=input[i-latency];
        error+=std::pow(double(out[i])-dry,2);power+=dry*dry;
    }
    std::cout<<"RENDER high-frequency-only delayed dry normalized error="<<std::sqrt(error/power)
            <<" targetNote="<<engine.currentTargetNote()<<" latency="<<latency<<'\n';
    check(engine.currentTargetNote()==-1,"out-of-band excitation cannot commit a tuning target");
    check(std::sqrt(error/power)<1e-6,"unvoiced high-frequency audio stays exactly dry after transport delay");
}
int main(){exerciseOutOfBandTonalGate();exercisePitchIntegrity();exerciseVoicedUnderHighFrequencyInterference();exerciseRealtimeEngine();return failures?1:0;}
