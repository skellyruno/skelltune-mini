#include "RealtimeTuner.hpp"
#include "NoteDecision.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>
using skelly_research::NoteDecision;
static void require(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL " << message << '\n'; std::exit(1); }
}
int main() {
    constexpr unsigned mask = (1u<<0)|(1u<<2)|(1u<<4)|(1u<<6)|(1u<<7)|(1u<<9)|(1u<<11);
    for (int rate : {44100,48000,96000}) {
        NoteDecision fast, conservative;
        fast.prepare(rate); conservative.prepare(rate);
        const unsigned hop=unsigned(std::round(rate*0.005));
        auto step=[&](double f,double confidence=.97,double onset=0.) {
            const auto a=fast.update(f,confidence,onset,hop,mask,440.,true);
            const auto b=conservative.update(f,confidence,onset,hop,mask,440.,false);
            return std::pair{a,b};
        };
        auto start=step(220.);
        require(start.first.targetMidi==57 && start.second.targetMidi==57,"G-major A3 anchor");
        int fastSwitch=999,slowSwitch=999;
        for(int i=0;i<18;++i){
            auto x=step(293.664768);
            if(x.first.targetMidi==62 && fastSwitch==999) fastSwitch=(i+1)*5;
            if(x.second.targetMidi==62 && slowSwitch==999) slowSwitch=(i+1)*5;
        }
        std::cout << rate << " note A3->D4 fast=" << fastSwitch << "ms normal=" << slowSwitch << "ms\n";
        require(fastSwitch<=15 && slowSwitch>=60,"large confident leap commits much faster");
        fast.reset();conservative.reset();step(220.);
        for(int i=0;i<4;++i){auto x=step(246.94165,0.70);
            require(x.first.targetMidi==57,"uncertain short passing B3 must be rejected"); }
        fast.reset();conservative.reset();step(220.);
        for(int i=0;i<60;++i){
            const double cents=85*std::sin(2*skelly::kPi*6*i*0.005);
            const auto x=step(220*std::pow(2.,cents/1200.));
            require(x.first.targetMidi==57,"85c 6Hz vibrato stays A3");
        }
        // A G4 vibrato excursion crosses the F#4/G4 nearest-note boundary.
        // It must not flip the target during sustained expressive vibrato.
        for (int rateHz : {4,6}) {
            fast.reset(); conservative.reset(); step(392.0);
            for (int i=0; i<240; ++i) {
                const double cents=-85*std::sin(2*skelly::kPi*rateHz*i*0.005);
                const auto r=step(392.0*std::pow(2.0,cents/1200.0));
                require(r.first.targetMidi==67,"G4/F#4 vibrato must not flip notes");
            }
        }
        // G-major B3->C4 is an adjacent semitone: accept after 95ms,
        // not 12ms (limits vibrato-triggered switches).
        fast.reset();conservative.reset();step(246.94165);
        for(int i=0;i<21;++i) step(261.625565);
        require(fast.update(261.625565,.99,0,hop,mask,440.,true).targetMidi==60,
                "adjacent sustained semitone eventually accepted");
        // The absolute fastest path must still keep the reported transport contract.
        skelly::RealtimeTuner dsp;
        dsp.prepare(rate,1);
        skelly::Parameters p;p.speedMs=1;p.mix=1;p.amount=1;
        p.pitchClasses=mask;p.flexCents=0;p.vibratoPreservation=0;
        dsp.setParameters(p);
        require(dsp.latencySamples()==int(std::lround(rate*.003)),"dry delay unchanged");
    }
    std::cout << "v2.7 note-decision and latency tests passed\n";
}
