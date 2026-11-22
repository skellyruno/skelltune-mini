// META analysis must not be left-channel-only. Keep the full stereo
// synthesized signal untouched while recovering right-only/antiphase F0.
#include "MetaTest.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
static void check(bool ok,const char* reason) {
    if(!ok) {std::cerr<<"FAIL "<<reason<<"\n";std::exit(1);}
}
int main() {
    constexpr double pi=3.14159265358979323846;
    for(int type=0;type<3;++type) {
        skelly::MetaTest m; m.prepare(48000.0);
        m.setParameters(0x0fff,440.0,1.0,0.0);
        double phase=0;double peak=0;
        for(int i=0;i<48000;++i) {
            phase+=2*pi*225.0/48000.0;
            const float voice=float(0.17*std::sin(phase)+0.035*std::sin(2*phase));
            float left=type==0?0.0f:voice;
            float right=type==1?0.0f:(type==2?-voice:voice);
            float* channels[2]={&left,&right};
            m.process(channels,2,1);
            check(std::isfinite(left)&&std::isfinite(right),"nonfinite stereo output");
            peak=std::max({peak,std::abs(double(left)),std::abs(double(right))});
        }
        const double detected=m.detectedHz(),target=m.targetHz();
        std::cout<<"type="<<type<<" detected="<<detected
                 <<" target="<<target<<" peak="<<peak<<"\n";
        check(detected>200&&detected<250,"META missed stereo voiced pitch");
        check(target>205&&target<235,"META lost A3 note target");
        check(peak<1.5,"META unexpected level after stereo rescue");
    }
    std::cout<<"PASS: META right-only/left-only/antiphase stereo tracking\n";
}