#include "GraphViewport.hpp"
#include "PitchEditTimeline.hpp"
#include "ScaleMask.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace skelly;
static void check(bool ok,const char* msg){
    if(!ok){std::cerr<<"FAIL "<<msg<<"\n";std::exit(1);}
}
int main(){
    constexpr float l=54,r=405;
    check(std::abs(graphViewportX(0,3,2000,3000,l,r)-288.0f)<1e-5f,
          "2s old note has incorrect x");
    check(std::abs(graphViewportX(1,3,2500,3000,l,r)-346.5f)<1e-5f,
          "uneven DAW times drawn with uniform index");
    check(graphViewportX(2,3,3000,3000,l,r)==r,
          "playhead sample not at right");
    check(graphViewportTimeMs(l,3000,l,r)==0 &&
          graphViewportTimeMs(r,3000,l,r)==3000,
          "graph timeline inverse did not preserve endpoints");
    check(graphViewportTimeMs(graphViewportTimeX(2500,3000,l,r),
                              3000,l,r)==2500,
          "right-click erased nearest GUI frame instead of actual host time");
    check(graphViewportTimeMs(l-20,5000,l,r)==2000 &&
          graphViewportTimeMs(r+20,5000,l,r)==5000,
          "right-click timestamp escaped three-second viewport");
    check(graphViewportTimeMs(std::numeric_limits<float>::quiet_NaN(),
                              5000,l,r)==-1,
          "NaN pointer position fabricated a timeline edit");
    check(graphViewportTimeX(0,3000,l,r)==l &&
          graphViewportTimeX(4000,3000,l,r)==r,
          "old/future pitch edit line unclipped");
    check(graphViewportX(1,3,-1,-1,l,r)==(l+r)*0.5f,
          "live note view without sync lost old fallback");
    check(graphDiscontinuity(3000,2900) &&
          graphDiscontinuity(3000,3200) &&
          !graphDiscontinuity(3000,3100),
          "seek or dropped frames joined");
    check(graphWheelPan(60,0.5f)==64.0f &&
          graphWheelPan(60,-0.25f)==58.0f &&
          graphWheelPan(95,4)==96.0f &&
          graphWheelPan(37,-4)==36.0f,
          "wheel pan direction or range");
    // A snapshot stays visually stable while the live ring moves and wraps.
    std::array<float,4> in{{62.0f,63.0f,60.0f,61.0f}};
    std::array<float,4> out{{62.5f,63.5f,60.5f,61.5f}};
    std::array<int64_t,4> times{{300,400,100,200}};
    PitchGraphGestureSnapshot<4> gesture;
    gesture.freeze(2,4,in,out,times); // ring chronology: 100,200,300,400
    check(gesture.active() && gesture.count()==4 &&
          gesture.newestMs()==400 && gesture.inputAt(0)==60.0f &&
          gesture.outputAt(3)==63.5f && gesture.timeAt(2)==300,
          "wrapped live drag snapshot order");
    in.fill(90.0f);out.fill(99.0f);times.fill(1000);
    check(gesture.inputAt(0)==60.0f && gesture.outputAt(3)==63.5f &&
          gesture.newestMs()==400,"live capture shifted selected note");
    gesture.release();
    check(!gesture.active()&&gesture.count()==0&&gesture.newestMs()==-1,
          "drag release did not restore live following");
    gesture.freeze(1,0,in,out,times);
    check(!gesture.active(),"empty graph snapshot incorrectly armed");
    PitchEditTimeline edits;
    check(edits.framesUntilBoundary(-1,0,256,48000.0)==256,
          "empty timeline split buffer");
    check(edits.add({500,1000,60})>=0 &&
          edits.framesUntilBoundary(498,0,256,48000.0)==96,
          "edited timeline boundary wrong");
    check(edits.add({500,1000,69})==PitchEditTimeline::kCapacity-1 &&
          edits.add({500,1000,69})==PitchEditTimeline::kCapacity-1 &&
          edits.targetAt(600)==69,
          "latest edit broke");
    check(toggleNote(0xffffu,1)==(kChromaticMask^(1u<<1)),
          "illegal bits in note mask");
    std::cout<<"PASS: DAW clock graph, scroll, edit, mask\n";
}
