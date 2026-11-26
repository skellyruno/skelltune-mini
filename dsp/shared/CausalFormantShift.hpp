#pragma once
// Original causal spectral-envelope warp. No audio FIFO, FFT or lookahead.
// Band energies are shared across stereo; the output retains each lane's
// waveform and fundamental. This is a bounded approximation, not LPC synthesis.
#include <algorithm>
#include <array>
#include <cmath>
namespace skell::research {
class CausalFormantShift {
public:
    static constexpr int extraLatency=0, bands=14;
    void prepare(double sampleRate,bool enabled=true) noexcept {
        rate_=std::clamp(std::isfinite(sampleRate)?sampleRate:48000.,8000.,192000.);
        enabled_=enabled; target_=0.f; tick_=0;
        hop_=std::max(1,int(std::lround(rate_*.004)));
        powerStep_=1.-std::exp(-1./(.025*rate_));
        gainStep_=1.-std::exp(-1./(.012*rate_));
        active_=0;
        for(int k=0;k<bands;++k){
            const double hz=250.*std::exp2(k/3.);
            if(hz>=rate_*.40)break;
            const double w=6.283185307179586*hz/rate_,alpha=std::sin(w)/2.2;
            coeff_[k]={alpha/(1.+alpha),-2.*std::cos(w)/(1.+alpha),(1.-alpha)/(1.+alpha)};
            ++active_;
        }
        power_.fill(0.); gain_.fill(0.); goal_.fill(0.);
        for(auto& lane:state_)lane.fill({});
    }
    bool enabled() const noexcept {return enabled_;}
    int latency() const noexcept {return 0;}
    void setSemitones(float st) noexcept {target_=std::isfinite(st)?std::clamp(st,-12.f,12.f):0.f;}
    std::array<float,2> process(float left,float right,bool bypass=false) noexcept {
        const double input[2]={std::isfinite(left)?left:0.,std::isfinite(right)?right:0.};
        double out[2]={input[0],input[1]};
        for(int k=0;k<active_;++k){
            double y[2]{};
            for(int c=0;c<2;++c){
                auto& s=state_[c][k];const auto& b=coeff_[k];
                y[c]=b.b*input[c]+s.z1;
                s.z1=-b.a1*y[c]+s.z2;
                s.z2=-b.b*input[c]-b.a2*y[c];
                if(std::abs(s.z1)<1.e-20)s.z1=0.;
                if(std::abs(s.z2)<1.e-20)s.z2=0.;
            }
            power_[k]+=powerStep_*(.5*(y[0]*y[0]+y[1]*y[1])-power_[k]);
            gain_[k]+=gainStep_*(goal_[k]-gain_[k]);
            if(std::abs(gain_[k])<1.e-9&&goal_[k]==0.)gain_[k]=0.;
            for(int c=0;c<2;++c)out[c]+=gain_[k]*y[c];
        }
        if(++tick_>=hop_){tick_=0;update();}
        if(bypass||!enabled_)return {float(input[0]),float(input[1])};
        return {float(std::isfinite(out[0])?out[0]:input[0]),float(std::isfinite(out[1])?out[1]:input[1])};
    }
private:
    void update() noexcept {
        double maxPower=0.;for(int k=0;k<active_;++k)maxPower=std::max(maxPower,power_[k]);
        const double floor=std::max(1.e-12,maxPower*.008);
        for(int k=0;k<active_;++k){
            if(std::abs(target_)<1.e-5||maxPower<1.e-9){goal_[k]=0.;continue;}
            // Three bands per octave: positive shift moves existing peaks up.
            const double src=std::clamp(k-double(target_)/4.,0.,double(active_-1));
            const int lo=int(src),hi=std::min(lo+1,active_-1);
            const double a=src-lo;
            const double logWanted=(1.-a)*std::log(power_[lo]+floor)+a*std::log(power_[hi]+floor);
            const double ratio=std::exp(.5*(logWanted-std::log(power_[k]+floor)));
            // Broad overlapping bands: bounded residual, attenuated at edges.
            const double edge=std::min({1.,(k+1)*.5,(active_-k)*.5});
            goal_[k]=.42*edge*(std::clamp(ratio,.5,2.)-1.);
        }
    }
    struct Coeff{double b=0.,a1=0.,a2=0.;};
    struct State{double z1=0.,z2=0.;};
    std::array<Coeff,bands> coeff_{};
    std::array<std::array<State,bands>,2> state_{};
    std::array<double,bands> power_{},gain_{},goal_{};
    double rate_=48000.,powerStep_=0.,gainStep_=0.;
    float target_=0.f;int active_=0,tick_=0,hop_=1;bool enabled_=true;
};
} // namespace skell::research
