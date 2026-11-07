// v3.5: stable adjacent semitone acquisition without vibrato target flips.
#include "NoteDecision.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <cstdlib>
#include <limits>

static constexpr double pi = 3.14159265358979323846;
static void check(bool ok, const char* why) {
    if (!ok) { std::cerr << "FAIL: " << why << '\n'; std::exit(1); }
}
static double midiHz(int note, double a4=440.0) { return a4 * std::pow(2.0, (note-69)/12.0); }
static double centsOffset(double hz, double cents) { return hz * std::pow(2.0, cents/1200.0); }

static void stableAdjacent(double rate) {
    skelly_research::NoteDecision fast, normal;
    fast.prepare(rate); normal.prepare(rate);
    const uint32_t hop = uint32_t(std::lround(rate*0.005));
    check(fast.update(midiHz(59),0.99,0,hop,0x0fff,440,true).targetMidi==59,"B3 initial target");
    check(normal.update(midiHz(59),0.99,0,hop,0x0fff,440,false).targetMidi==59,"normal B3 initial target");
    int fastTime=-1, normalTime=-1;
    for(int i=1;i<=24;++i) {
        // Incoming new C4 is consistently 25 cents sharp, not note-center perfect.
        const double hz=centsOffset(midiHz(60),25.0);
        auto f=fast.update(hz,0.99,0,hop,0x0fff,440,true);
        auto n=normal.update(hz,0.99,0,hop,0x0fff,440,false);
        if(f.targetMidi==60 && fastTime<0)fastTime=i*5;
        if(n.targetMidi==60 && normalTime<0)normalTime=i*5;
    }
    std::cout << rate << "Hz: steady off-center semitone fast=" << fastTime << " ms normal=" << normalTime << " ms\n";
    check(fastTime>=30 && fastTime<=40,"off-center high-confidence fast-lock latency");
    check(normalTime>=65,"normal retune target delay unchanged");
}

static void vibratoNoFalseSwitch(double rate, double depth, double hz) {
    skelly_research::NoteDecision d;
    d.prepare(rate);
    const uint32_t hop = uint32_t(std::lround(rate*0.005));
    check(d.update(midiHz(67),0.99,0,hop,0x0fff,440,true).targetMidi==67,"G4 initial target");
    for (int n=0;n<500;++n) {
        // Cross the F#4/G4 boundary, including vibrato precisely centered on F#.
        const double t=n*.005;
        const double f0=centsOffset(midiHz(67),-depth*std::sin(2.0*pi*hz*t));
        const auto r=d.update(f0,0.98,0,hop,0x0fff,440,true);
        if (r.targetMidi!=67) {
            std::cerr << "Vibrato flip: depth=" << depth << " freq=" << hz
                      << " t=" << t << " note=" << r.targetMidi << '\n';
            check(false,"vibrato briefly mistaken for adjacent note");
        }
    }
}

static void existingFastPaths(double rate) {
    skelly_research::NoteDecision d;
    d.prepare(rate);
    const uint32_t hop=uint32_t(std::lround(rate*.005));
    d.update(midiHz(59),.99,0,hop,0x0fff,440,true);
    int centered=-1;
    for(int i=1;i<10;++i) if (d.update(midiHz(60),.99,0,hop,0x0fff,440,true).targetMidi==60){centered=i*5;break;}
    check(centered>0 && centered<=20,"exact-center adjacent lock remains fast");
    d.reset();d.update(midiHz(57),.99,0,hop,0x0fff,440,true);
    int leap=-1;
    for(int i=1;i<10;++i) if (d.update(midiHz(62),.99,0,hop,0x0fff,440,true).targetMidi==62){leap=i*5;break;}
    check(leap>0 && leap<=15,"high-confidence major leap still commits rapidly");
    d.reset();d.update(midiHz(59),.99,0,hop,0x0fff,440,true);
    for(int i=0;i<4;++i)
        check(d.update(midiHz(60),0.70,0,hop,0x0fff,440,true).targetMidi==59,
            "low-confidence passing note must not fast-lock");
    // Invalid detector input must not preserve a pending note across a gap.
    d.update(midiHz(60),.99,0,hop,0x0fff,440,true);
    d.update(std::numeric_limits<double>::quiet_NaN(),.99,0,hop,0x0fff,440,true);
    check(d.update(midiHz(59),.99,0,hop,0x0fff,440,true).targetMidi==59,
        "invalid pitch cancels pending candidate");
}
static void ratioEquivalence() {
    // The production signal-path ratio uses exp2 instead of the generic pow.
    // Compare the full allowed correction range at sub-cent resolution.
    for (int i=-120000;i<=120000;i+=7) {
        const double cents=i/100.0;
        const double reference=std::fmax(0.5,std::fmin(2.0,std::pow(2.0,cents/1200.0)));
        const double optimized=std::fmax(0.5,std::fmin(2.0,std::exp2(cents/1200.0)));
        check(std::abs(reference-optimized)<1.0e-12,
            "exp2 pitch-ratio result must match reference pow");
    }
}
static void phraseReentry(double rate) {
    skelly_research::NoteDecision d;
    d.prepare(rate);
    const uint32_t hop=uint32_t(std::lround(rate*.005));
    check(d.update(midiHz(59),.99,0,hop,0x0fff,440,true).targetMidi==59,
          "initial phrase target");
    for (int i=0;i<6;++i)
        d.update(0.0,0,0,hop,0x0fff,440,true);
    check(d.update(midiHz(60),.99,0,hop,0x0fff,440,true).targetMidi==60,
          "new phrase re-entry must not inherit stale note");
    d.reset();
    check(d.update(midiHz(59),.99,0,hop,0x0fff,440,true).targetMidi==59,
          "target after reset");
    check(d.update(midiHz(59),.99,0,hop,0x0fffu & ~(1u<<11),440,true).targetMidi!=59,
          "disallowed note should be dropped immediately");
}
static void transientThresholdEquivalence() {
    // Match the original sqrt threshold across a deterministic stream of
    // low-level samples and bursts, updating the same running noise floor.
    uint32_t seed=0x432a937u;
    double power=0.0, previous=0.0;
    const double step=1.0-std::exp(-1.0/(48000.0*.030));
    int events=0;
    for (int i=0;i<2000000;++i) {
        seed=1664525u*seed+1013904223u;
        const double noise=(double((seed>>8)&65535)/65535.0-0.5)*0.006;
        const double input=noise+((i%10003==0)?0.32:0.0);
        const double delta=std::abs(input-previous); previous=input;
        const bool original=delta>std::max(0.075,5.0*std::sqrt(std::max(0.0,power)));
        const bool optimized=delta*delta>std::max(0.005625,25.0*std::max(0.0,power));
        check(original==optimized,"transient protection event changed");
        if(original)++events;
        power+=step*(std::min(delta*delta,1.0)-power);
    }
    check(events>0,"transient threshold regression must exercise attacks");
}
int main(){
    for(double rate : {44100.0,48000.0,96000.0}) {
        stableAdjacent(rate);
        for(double depth : {70.0,85.0,100.0})
            for(double freq : {4.0,5.0,6.0})
                vibratoNoFalseSwitch(rate,depth,freq);
        existingFastPaths(rate);
        phraseReentry(rate);
    }
    ratioEquivalence();
    transientThresholdEquivalence();
    std::cout << "PASS: v3.5 stable note lock / vibrato / legacy fast paths\n";
}
