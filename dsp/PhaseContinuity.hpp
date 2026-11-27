#pragma once
#include <algorithm>
#include <cmath>
#include <initializer_list>
namespace skelly {
// Gain-normalised waveform AND derivative continuity. Accumulated across
// stereo lanes, fixed stack storage. No correlation across unrelated takes,
// no FFT, allocation, or new transport delay.
struct PhaseContinuity {
    double xx=0.,yy=0.,xy=0.,dx=0.,dy=0.,dxy=0.;
    double px[2]{},py[2]{};bool started[2]{};
    void add(int lane,double x,double y) noexcept {
        xx+=x*x;yy+=y*y;xy+=x*y;
        if(started[lane]){
            const double a=x-px[lane],b=y-py[lane];
            dx+=a*a;dy+=b*b;dxy+=a*b;
        }
        px[lane]=x;py[lane]=y;started[lane]=true;
    }
    static double correlation(double a,double b,double ab) noexcept {
        if(a<1.e-14||b<1.e-14)return 0.;
        return std::clamp(ab/std::sqrt(a*b),-1.,1.);
    }
    double cost() const noexcept {
        if(xx<1.e-12||yy<1.e-12)return 2.;
        const double wave=correlation(xx,yy,xy);
        const double slope=(dx>1.e-14&&dy>1.e-14)?correlation(dx,dy,dxy):wave;
        // A level mismatch still matters, but should not steer to a
        // different waveform phase. The bounded term also rejects gaps.
        const double level=std::min(1.,std::abs(xx-yy)/(xx+yy));
        return .72*(1.-wave)+.23*(1.-slope)+.05*level;
    }
};
template<class Cost>
inline double refinePhase(double best,double nominal,double lo,double hi,Cost&& cost) noexcept {
    double chosen=best,score=cost(best)+.0001*std::abs(best-nominal);
    for(double delta:{-.5,.5,-.25,.25}){
        const double candidate=std::clamp(best+delta,lo,hi);
        // Fractional refinement cannot exceed the integer search bounds.
        const double c=cost(candidate)+.0001*std::abs(candidate-nominal);
        if(c<score){chosen=candidate;score=c;}
    }
    return chosen;
}
}
