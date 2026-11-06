#include "RealtimeTuner.hpp"
#include "NoteDecision.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
using namespace skelly;
using skelly_research::NoteDecision;
static void expect(bool ok, const char* msg) { if(!ok) { std::cerr<<"FAIL: "<<msg<<'\n';std::abort(); } }
static void zeroRetuneTest(double rate) {
    RealtimeTuner zero, one;
    zero.prepare(rate,1); one.prepare(rate,1);
    Parameters p; p.pitchClasses=1u<<9; p.speedMs=0.0f;
    p.amount=1; p.mix=1; p.vibratoPreservation=0; p.flexCents=0;
    zero.setParameters(p); p.speedMs=1.0f; one.setParameters(p);
    constexpr int block=64;
    float srcBuffer[block]{},zeroBuffer[block]{},oneBuffer[block]{};
    const float* src[]={srcBuffer};float* dstZero[]={zeroBuffer};float* dstOne[]={oneBuffer};
    bool observed=false;
    for (int n=0; n<int(rate*.5);n+=block) {
        for(int j=0;j<block;++j)
            srcBuffer[j]=0.32f*float(std::sin(2*kPi*225.0*(n+j)/rate));
        zero.process(src,dstZero,block,1);one.process(src,dstOne,block,1);
        if (!observed && std::abs(zero.appliedCents())>15.0) {
            // Zero-slew must reach the requested cent target within the same
            // detector hop; 1 ms mode intentionally retains exponential slew.
            const double expected=1200.0*std::log2(220.0 / zero.lastPitch().hertz);
            expect(std::abs(zero.appliedCents()-expected)<1e-3,"0ms snaps to full correction immediately");
            expect(std::abs(one.appliedCents()) < std::abs(zero.appliedCents())-0.01,"1ms still has a nonzero slew");
            observed=true;
        }
        for(int j=0;j<block;++j) {
            expect(std::isfinite(zeroBuffer[j]) && std::isfinite(oneBuffer[j]),"zero retune audio finite");
            expect(std::abs(zeroBuffer[j])<3.0f,"zero retune bounded audio");
        }
    }
    expect(observed,"pitch detector produced a correction");
    expect(zero.latencySamples()==int(std::lround(rate*0.003)),"unchanged 3ms host delay");
}
static void adjacentAndVibrato(double rate) {
    NoteDecision notes;notes.prepare(rate);
    const uint16_t mask = 0x0fff;
    const auto hop=uint32_t(std::lround(rate*0.005));
    auto push=[&](double f){return notes.update(f,.98,0,hop,mask,440.,true);};
    expect(push(246.9416506).targetMidi==59,"B3 anchor");
    int changedAt=1000;
    for(int n=1;n<15;++n)
        if(push(261.625565).targetMidi==60){changedAt=n*5;break;}
    expect(changedAt<=25,"stable semitone center commits quickly at max retune");
    notes.reset();expect(push(392.).targetMidi==67,"G4 vibrato anchor");
    for(int i=0;i<180;++i) {
        const double cents=-85.0*std::sin(2*kPi*4*i*.005);
        const auto d=push(392.0*std::pow(2.0,cents/1200.0));
        expect(d.targetMidi==67,"85 cent vibrato cannot flip G4 to F#4");
    }
}
static void lowVoiceFreshOnset(double rate) {
    CausalPitchTracker tracker;tracker.prepare(rate);
    PitchEstimate e{}; int valid=0,correct=0;
    for(int i=0;i<int(rate*.45);++i) {
        double t=double(i)/rate;
        const double f=95.0;
        const float x=float(0.04*std::sin(2*kPi*f*t)+0.4*std::sin(4*kPi*f*t)
            +0.3*std::sin(6*kPi*f*t));
        if(tracker.push(x,e)&&e.valid&&i>int(rate*.10)) {
            ++valid;
            if(std::abs(1200.0*std::log2(double(e.hertz)/f))<25) ++correct;
        }
    }
    expect(valid>60 && correct>int(.90*valid),"low weak-fundamental retains full-window accuracy");
}
int main() {
    for(double rate:{44100.0,48000.0,96000.0}){
        zeroRetuneTest(rate);
        adjacentAndVibrato(rate);
        lowVoiceFreshOnset(rate);
    }
    std::cout<<"v2.8 zero-ms/adjacent-centering/low-vocal cases passed at three sample rates\n";
}
