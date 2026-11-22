#include "PitchEditTimeline.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>
using skelly::PitchEditTimeline;
static void check(bool good,const char* what){if(!good){std::cerr<<"FAIL "<<what<<"\n";std::exit(1);}}
int main(){
    PitchEditTimeline t;
    check(t.targetAt(250)==-1,"default must be unaffected");
    check(t.add({200,700,62})>=0,"first note edit");
    check(t.targetAt(199)==-1 && t.targetAt(200)==62 &&
          t.targetAt(699)==62 && t.targetAt(700)==-1,"edge sample");
    check(t.add({300,400,67})>=0 && t.targetAt(350)==67,"overlap priority");
    check(t.eraseAt(350)&&t.targetAt(350)==62,"erase highest edit");
    check(t.add({-1,300,60})==-1,"negative transport");
    check(t.add({800,800,60})==-1,"empty interval");
    check(t.add({800,900,129})==-1,"bad note");
    // Redrawing an older interval must supersede a newer overlapping
    // slot even after an erase creates a hole at a low index.
    check(t.add({300,400,68})>=0 && t.targetAt(350)==68,"redrawn note priority");
    check(t.add({300,400,69})>=0 && t.targetAt(350)==69,"latest overlap priority");
    t.clear();check(t.targetAt(350)==-1,"reset");
    for(int i=0;i<PitchEditTimeline::kCapacity+5;++i)
        check(t.add({100,200,48+i%30})>=0,"edit capacity");
    check(t.targetAt(150)==48+(PitchEditTimeline::kCapacity+4)%30,
          "over-capacity note must remain the newest");
    t.clear(); // isolate concurrent publication from filled-history test
    std::atomic<bool> done{false};
    std::thread writer([&]{for(int i=0;i<40000;++i)t.set(0,{100,200,i%2?57:72});done.store(true);});
    while(!done.load()){
        const int note=t.targetAt(150);
        check(note==-1||note==57||note==72,"torn published edit");
    }
    writer.join();check(t.targetAt(150)==57||t.targetAt(150)==72,"final live note");
    std::cout<<"PASS: time-bound pitch edit, concurrency, erase, guard\n";
}
