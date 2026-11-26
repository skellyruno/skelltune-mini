#pragma once
#include <algorithm>
#include <cmath>
namespace skelly {
// Opt-in research controller. Wet-only, stereo linked, causal. Match the
// existing aligned dry short-term energy without chasing quiet/noisy gaps.
class ChallengerDynamics {
public:
    void prepare(double fs) noexcept {
        fs=std::clamp(fs,8000.,192000.);
        meter_=1.-std::exp(-1./(.008*fs));
        up_=1.-std::exp(-1./(.006*fs));
        down_=1.-std::exp(-1./(.003*fs));
        gateStep_=1.-std::exp(-1./(.003*fs));reset();
    }
    void reset() noexcept {dp_=wp_=0.;gain_=target_=1.;gate_=0.;phase_=0;}
    void process(const float d[2],float w[2],int ch,bool voiced,double blend,double strength) noexcept {
        double a=0.,b=0.,peak=0.;
        for(int c=0;c<ch;++c){a+=double(d[c])*d[c];b+=double(w[c])*w[c];peak=std::max(peak,std::abs(double(w[c])));}
        dp_+=meter_*(a/ch-dp_);wp_+=meter_*(b/ch-wp_);
        if(dp_<1.e-20)dp_=0.;
        if(wp_<1.e-20)wp_=0.;
        const double eligible=voiced?std::clamp((blend-.90)*10.,0.,1.):0.;
        gate_+=gateStep_*(eligible-gate_);if(gate_<1.e-9)gate_=0.;
        if(phase_==0){
            target_=1.;
            if(eligible>0.&&dp_>2.e-6&&wp_>dp_*.16&&wp_<dp_*5.){
                const double ratio=std::sqrt(dp_/wp_);
                // Moderate attenuation as well as recovery. A 1.5x limit
                // cannot resurrect a missing cycle; leave those for synthesis.
                target_=1.+strength*(std::clamp(ratio,.76,1.5)-1.);
            }
        }
        phase_=(phase_+1)&7;
        gain_+=(target_-gain_)*(target_>gain_?up_:down_);
        if(eligible==0.||peak>=1.||strength<=0.)return;
        const double g=1.+eligible*gate_*(gain_-1.);
        const double safe=g>1.&&peak*g>1.?1./peak:g;
        for(int c=0;c<ch;++c)w[c]=float(w[c]*safe);
    }
private:
    double dp_=0.,wp_=0.,gain_=1.,target_=1.,gate_=0.;
    double meter_=0.,up_=0.,down_=0.,gateStep_=0.;unsigned phase_=0;
};
// Candidate transition calibration, not a change to the host speed parameter.
// Blend out at hard/slow endpoints. Each contender has its own curve.
inline float challengerSpeed(float ms,int mode) noexcept {
    if(mode==2||mode==3||ms<=2.f||ms>=80.f)return ms;
    const float influence=std::clamp(std::min((ms-2.f)/8.f,(80.f-ms)/35.f),0.f,1.f);
    const float reduction=mode==0?.42f:mode==1?.18f:.25f;
    return ms*(1.f-reduction*influence);
}
// Promote the measured 2.5ms results. TESTTUNE at 25ms lost envelope
// accuracy and voiced coverage, so return to its old motion there.
inline float inspiredPromotionWeight(float ms) noexcept {return std::clamp(1.f-std::abs(ms-25.f)/10.f,0.f,1.f);}
inline float listeningSpeed(float ms,int mode) noexcept {
    if(mode==1)return challengerSpeed(ms,mode);
    if(mode==4){const float weight=std::clamp(1.f-std::abs(ms-25.f)/10.f,0.f,1.f);return ms+weight*(challengerSpeed(ms,mode)-ms);}
    if(mode!=0)return ms;
    const float weight=std::clamp((25.f-ms)/15.f,0.f,1.f);
    return ms+weight*(challengerSpeed(ms,mode)-ms);
}
}
