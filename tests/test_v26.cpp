#include "NoteDecision.hpp"
#include <cmath>
#include <iostream>
#include <cstdlib>
using skelly_research::NoteDecision;
static void must(bool a, const char* message) {
 if(!a){std::cerr << "FAIL " << message << "\n";std::exit(1);}
}
int main() {
 for(int rate:{44100,48000,96000}) {
  NoteDecision notes;notes.prepare(rate);
  const auto hop=static_cast<unsigned>(std::round(rate*.005));
  auto push=[&](double hz,double confidence=.95,double onset=0.) {
   return notes.update(hz,confidence,onset,hop,0x0fff,440.0);
  };
  must(push(220).targetMidi==57,"initial A3");
  // A short silence should not erase established musical context.
  for(int i=0;i<2;++i)push(0,0);
  auto brief=push(246.94165);
  must(brief.targetMidi==57 && brief.pending,"10ms consonant protects short passing note");
  // Explicit re-entry after >=20ms of unvoiced evidence.
  notes.reset();must(push(220).targetMidi==57,"initial A3 reset");
  for(int i=0;i<6;++i)push(0,0);
  auto returned=push(246.94165,.95);
  must(returned.targetMidi==59 && !returned.pending,"30ms gap commits confident B3 immediately");
  // Ambiguous pitch after the same gap must still be confirmed normally.
  notes.reset();must(push(220).targetMidi==57,"initial A3 second reset");
  for(int i=0;i<6;++i)push(0,0);
  auto uncertain=push(246.94165,.65);
  must(uncertain.targetMidi==57 && uncertain.pending,"uncertain re-entry stays pending");
  // 85c vibrato without an unvoiced gap remains a variation of A3.
  notes.reset();must(push(220).targetMidi==57,"initial A3 third reset");
  for(int i=0;i<16;++i) {
   const double freq=220*std::pow(2.0,(i%2?85.0:-85.0)/1200.0);
   const auto r=push(freq,.95);
   must(r.targetMidi==57,"vibrato must not become a new target");
  }
 }
 std::cout << "v2.6 3-rate note re-entry, gap safety, confidence and vibrato tests passed\n";
}
