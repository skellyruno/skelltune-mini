#pragma once
// Research candidate: causal, stereo-linked, level-normalised vowel restoration.
// No delay line, heap allocation, additional detector or FFT in the callback.
#include <algorithm>
#include <array>
#include <cmath>
namespace skelly {
class AdaptiveVowelGuard {
public:
    void prepare(double rate) noexcept {
        rate=std::clamp(rate,8000.,192000.);
        constexpr double edges[]{350.,750.,1450.,3000.,5500.};
        for(int k=0;k<5;++k)pole_[k]=std::exp(-6.283185307179586*std::min(edges[k],rate*.38)/rate);
        meter_=1.-std::exp(-1./(.024*rate));smooth_=1.-std::exp(-1./(.012*rate));
        enter_=1.-std::exp(-1./(.008*rate));leave_=1.-std::exp(-1./(.004*rate));
        reset();
    }
    void reset() noexcept {
        for(auto& x:dry_)x.fill(0.);
        for(auto& x:wet_)x.fill(0.);
        dp_.fill(0.);wp_.fill(0.);gain_.fill(1.);target_.fill(1.);
        phase_=0;activity_=0.;
    }
    void process(const float d[2],float w[2],int channels,double eligibility,double strength) noexcept {
        channels=std::clamp(channels,1,2);
        std::array<std::array<double,6>,2> wb{};
        std::array<double,6> dp{},wp{};
        for(int c=0;c<channels;++c){
            double x=d[c],y=w[c],a=0.,b=0.;
            for(int k=0;k<5;++k){
                dry_[c][k]=pole_[k]*dry_[c][k]+(1.-pole_[k])*x;
                wet_[c][k]=pole_[k]*wet_[c][k]+(1.-pole_[k])*y;
                const double u=dry_[c][k]-a,v=wet_[c][k]-b;
                a=dry_[c][k];b=wet_[c][k];dp[k]+=u*u;wp[k]+=v*v;wb[c][k]=v;
            }
            dp[5]+=(x-a)*(x-a);wp[5]+=(y-b)*(y-b);wb[c][5]=y-b;
        }
        for(int k=0;k<6;++k){
            dp_[k]+=meter_*(dp[k]/channels-dp_[k]);wp_[k]+=meter_*(wp[k]/channels-wp_[k]);
            if(dp_[k]<1.e-20)dp_[k]=0.;if(wp_[k]<1.e-20)wp_[k]=0.;
        }
        eligibility=std::clamp(eligibility,0.,1.);
        activity_+=(eligibility-activity_)*(eligibility>activity_?enter_:leave_);
        if(activity_<1.e-8)activity_=0.;
        if(phase_==0){
            double ds=0.,ws=0.;for(int k=0;k<6;++k){ds+=dp_[k];ws+=wp_[k];}
            target_.fill(1.);
            if(ds>1.e-7&&ws>1.e-7&&ws>ds*.15&&ws<ds*5.){
                // Compare relative band power, not raw gain. A volume dip must
                // not masquerade as a vowel change or raise the entire vocal.
                for(int k=1;k<=4;++k){
                    const double a=dp_[k]/ds,b=wp_[k]/ws;
                    if(a>.008&&b>.008){
                        const double ratio=std::sqrt(a/b);
                        target_[k]=1.+strength*(std::clamp(ratio,.71,1.41)-1.);
                    }
                }
            }
        }
        phase_=(phase_+1)&15;
        for(int k=1;k<=4;++k)gain_[k]+=smooth_*(target_[k]-gain_[k]);
        if(activity_==0.||eligibility==0.)return;
        // Preserve fundamental/air and the existing attack/mix dry anchor.
        // Linked residual headroom cap never introduces a new overload.
        double out[2]{},peak=0.,original=0.;
        for(int c=0;c<channels;++c){
            out[c]=w[c];original=std::max(original,std::abs(double(w[c])));
            for(int k=1;k<=4;++k)out[c]+=activity_*eligibility*(gain_[k]-1.)*wb[c][k];
            peak=std::max(peak,std::abs(out[c]));
        }
        const double cap=std::max(1.,original);
        const double safe=peak>cap?cap/peak:1.;
        for(int c=0;c<channels;++c)w[c]=float(out[c]*safe);
    }
private:
    std::array<double,5> pole_{};
    std::array<std::array<double,5>,2> dry_{},wet_{};
    std::array<double,6> dp_{},wp_{},gain_{},target_{};
    double meter_=0.,smooth_=0.,enter_=0.,leave_=0.,activity_=0.;
    unsigned phase_=0;
};
}
