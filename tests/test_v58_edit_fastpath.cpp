#include "PitchEditTimeline.hpp"
#include <cstdlib>
#include <iostream>
static void require(bool v,const char* msg){if(!v){std::cerr<<"FAIL: "<<msg<<"\n";std::exit(1);}}
int main(){
    skelly::PitchEditTimeline t;
    for(int ms=0;ms<8000;ms+=11) {
        require(t.targetAt(ms)==-1,"empty timeline retuned audio");
        require(t.nextBoundaryAfter(ms)==-1,"empty timeline inserted boundary");
        require(t.framesUntilBoundary(ms,0,128,48000)==128,"empty timeline split host block");
    }
    require(t.add({100,300,60})>=0,"add");
    require(t.targetAt(100)==60&&t.nextBoundaryAfter(100)==300,"first note lost");
    require(t.add({150,200,64})>=0&&t.targetAt(175)==64,"priority");
    require(t.undoLast()&&t.targetAt(175)==60,"undo");
    require(t.eraseAt(160),"erase");
    require(t.targetAt(160)==-1&&t.nextBoundaryAfter(160)==-1,"zero-count not restored");
    for(int i=0;i<80;++i)require(t.add({i*100,i*100+60,36+i%36})>=0,"bounded insert");
    require(t.targetAt(7950)==36+79%36,"last overwrite");
    t.clear();
    require(t.targetAt(7950)==-1&&t.nextBoundaryAfter(7950)==-1,"clear fast path");
    std::cout<<"PASS: transparent unedited audio and bounded edits/undo/clear\n";
}
