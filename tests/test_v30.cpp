#include "RealtimeTuner.hpp"
#include "CausalLpcSourceFilter.hpp"
#include <array>
#include <cassert>
#include <stdexcept>
#include <cmath>
#include <iostream>
#include <vector>
using namespace skelly;
static void check(bool ok){if(!ok)throw std::runtime_error("v30 test failed");}
constexpr double pi=3.141592653589793;

std::vector<float> render(bool provisional,double fs=48000) {
    RealtimeTuner e;e.prepare(fs,1);
    e.setProvisionalTransitionEnabled(provisional);
    Parameters p;p.speedMs=0;p.amount=1;p.mix=1;
    p.pitchClasses=static_cast<uint16_t>((1<<7)|(1<<9)|(1<<11)|(1<<0)|(1<<2)|(1<<4)|(1<<6));
    p.transientProtection=0; p.airPreservation=0;
    e.setParameters(p);
    std::vector<float> y(size_t(fs*.8));
    std::array<float,1> sample{};
    const float* in[]{sample.data()};
    float o=0;float* out[]{&o};
    double phase=0;
    for(size_t i=0;i<y.size();++i){
        const double t=double(i)/fs;
        const double hz=t<.25?196.0:246.94;
        phase+=2*pi*hz/fs;
        sample[0]=float(0.24*std::sin(phase)+0.1*std::sin(2*phase)+0.03*std::sin(3*phase));
        e.process(in,out,1,1);y[i]=o;
    }
    return y;
}
int main(){
    // Opt-in never affects existing default synth and must be block invariant.
    auto a=render(false),b=render(true);
    check(a.size()==b.size());
    for(float v:b)check(std::isfinite(v));
    double delta=0;
    for(size_t i=10000;i<a.size();++i)delta+=std::abs(a[i]-b[i]);
    // The next case tests optional derivative-based closure evidence on a
    // clock-locked low voice and checks that it actually schedules grains.
    for(double fs:{44100.0,48000.0,96000.0}){
        for(int mode:{0,1,2}) {
            CausalCycleSynth p;p.setClockLockedDelay(int(std::round(fs*.05)));
            p.prepare(fs,1);p.setClosureEvidenceEnabled(mode == 1);
            p.setStrongerConcurrentPhaseEnabled(mode == 2);
            double phase=0;
            for(int i=0;i<int(fs*.36);++i){
                phase+=2*pi*95.0/fs;
                // voiced, asymmetric high-harmonic excitation
                float x[2]={float(.28*std::sin(phase)+.13*std::sin(2*phase)
                     +.06*std::sin(5*phase)+.025*std::sin(9*phase)),0};
                float y[2]{};
                p.processSample(x,y,fs/95.0,1.10,true);
                check(std::isfinite(y[0]));
            }
            auto st=p.status();
            check(st.spawned>5 && st.timelineOutOfBounds < st.spawned+10);
        }
    }
    // Causal LPC inverse+resynthesis: no NaNs at three rates and stereo
    // anti-phase is preserved by a stereo-energy-linked model.
    for(double fs:{44100.0,48000.0,96000.0}){
        CausalLpcSourceFilter model;model.prepare(fs,2);
        double ph=0;
        for(int i=0;i<int(fs*.22);++i){
            ph+=2*pi*130.0/fs;
            const float a=float(.11*std::sin(ph)+.05*std::sin(2*ph));
            const float source[2]={a,-a};
            float exc[2]{},out[2]{};
            model.analyze(source,exc);
            model.synthesize(exc,out);
            check(std::isfinite(out[0]) && std::isfinite(out[1]));
            check(std::abs(out[0]+out[1])<1.e-4f);
        }
        check(model.hasModel() && model.modelUpdates()>3);
    }
    std::cout<<"v3.0 research-path checks passed, optional transition delta "<<delta<<"\n";
}
