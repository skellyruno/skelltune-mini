#pragma once
// Experimental source-filter formant-preserving excitation processor.
// Inputs are decomposed causally into LPC prediction residual plus slowly
// changing vocal-tract coefficients. Pitch shifting must be performed on the
// residual, not the full vowel, then resynthesized through the AR envelope.
// Isolated research module; NOT enabled in the production VST3 signal path.
// Copyright 2026 Skelly Audio. Original implementation.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace skelly {
class CausalLpcSourceFilter {
public:
    static constexpr int order=18;
    static constexpr int historySize=2048;
    void prepare(double fs,int channels=1) noexcept {
        fs_=std::clamp(fs,8000.0,192000.0);
        channels_=std::clamp(channels,1,2);
        updateEvery_=std::max(64,int(fs_*0.008));
        reset();
    }
    void reset() noexcept {
        analysis_.fill(0.0);
        for(auto &a: inputPast_)a.fill(0.0);
        for(auto &a: outputPast_)a.fill(0.0);
        coefficients_.fill(0.0);
        sampleIndex_=0;
        previousInput_.fill(0.0);
        previousOutput_.fill(0.0);
        level_=0.0; outputPower_=0.0; gain_=1.0;
        valid_=false;
        updates_=0;
    }
    // Caller uses residual for pitch-synchronous cycle reconstruction.
    // Individual channel histories are preserved; LPC analysis is channel-
    // energy linked rather than L+R summing, safe for anti-phase stereo.
    void analyze(const float input[2],float residual[2]) noexcept {
        const int idx=int(sampleIndex_ % historySize);
        for(int ch=0;ch<channels_;++ch) {
            const double x=std::isfinite(input[ch]) ? input[ch] : 0.0;
            const double pre=x-0.95*previousInput_[ch];
            previousInput_[ch]=x;
            analysis_[size_t(ch)*historySize+idx]=float(pre);
            double e=pre;
            for(int k=0;k<order;++k) e+=coefficients_[size_t(k)]*inputPast_[ch][size_t(k)];
            for(int k=order-1;k>0;--k)inputPast_[ch][size_t(k)]=inputPast_[ch][size_t(k-1)];
            inputPast_[ch][0]=pre;
            residual[ch]=float(std::clamp(e,-4.0,4.0));
        }
        double amplitude=0.0;
        for(int ch=0;ch<channels_;++ch) amplitude+=previousInput_[ch]*previousInput_[ch];
        level_ += 0.002*(amplitude/channels_-level_);
        ++sampleIndex_;
        if(sampleIndex_>=historySize && sampleIndex_%uint64_t(updateEvery_)==0) updateModel();
    }
    // Pitch-shifted residual -> restored resonant envelope. The bounded
    // excitation limit protects against unstable coefficients / NaNs, but
    // the module intentionally makes no claim of transparent formant quality.
    void synthesize(const float excitation[2],float output[2]) noexcept {
        for(int ch=0;ch<channels_;++ch) {
            double value=std::isfinite(excitation[ch])?excitation[ch]:0.0;
            for(int k=0;k<order;++k)
                value-=coefficients_[size_t(k)]*outputPast_[ch][size_t(k)];
            value=std::clamp(value,-8.0,8.0);
            for(int k=order-1;k>0;--k)outputPast_[ch][size_t(k)]=outputPast_[ch][size_t(k-1)];
            outputPast_[ch][0]=value;
            const double x=value+0.95*previousOutput_[ch];
            previousOutput_[ch]=std::clamp(x,-8.0,8.0);
            output[ch]=float(previousOutput_[ch]);
        }
        double energy=0.0;
        for(int ch=0;ch<channels_;++ch) energy+=double(output[ch])*output[ch];
        outputPower_+=0.002*(energy/channels_-outputPower_);
        // Synthesis gain is bounded and follows power over ~10ms. We do not
        // rely on hard clipping as a substitute for coefficient stability.
        const double desired=std::clamp(
            std::sqrt((level_+1.e-6)/(outputPower_+1.e-6)),0.20,2.0);
        gain_+=0.001*(desired-gain_);
        for(int ch=0;ch<channels_;++ch)
            output[ch]=float(std::clamp(double(output[ch])*gain_,-1.5,1.5));
    }
    bool hasModel() const noexcept {return valid_;}
    uint64_t modelUpdates() const noexcept{return updates_;}
    double coefficient(int i) const noexcept{return (i>=0&&i<order)?coefficients_[size_t(i)]:0.0;}
private:
    void updateModel() noexcept {
        std::array<double,order+1> r{};
        // Causal 35 ms window, accumulating stereo energy separately.
        const int frames=std::min(1536,int(fs_*.035));
        for(int ch=0;ch<channels_;++ch) {
            const size_t offset=size_t(ch)*historySize;
            for(int k=0;k<=order;++k) {
                double xy=0.0;
                for(int i=order;i<frames;++i) {
                    const int a=int((sampleIndex_-1-uint64_t(i))%historySize);
                    const int b=int((sampleIndex_-1-uint64_t(i-k))%historySize);
                    xy+=double(analysis_[offset+size_t(a)])*double(analysis_[offset+size_t(b)]);
                }
                r[size_t(k)]+=xy;
            }
        }
        if(r[0]<0.003) {valid_=false;return;}
        r[0]*=1.001;
        std::array<double,order+1> a{};
        double err=r[0];
        bool ok=true;
        for(int m=1;m<=order;++m) {
            double corr=r[size_t(m)];
            for(int j=1;j<m;++j)corr+=a[size_t(j)]*r[size_t(m-j)];
            const double reflection=std::clamp(-corr/std::max(err,1.e-10),-0.93,0.93);
            auto next=a;
            next[size_t(m)]=reflection;
            for(int j=1;j<m;++j)next[size_t(j)]=a[size_t(j)]+reflection*a[size_t(m-j)];
            a=next;
            err*=1.0-reflection*reflection;
            if(err<1.e-9||!std::isfinite(err)){ok=false;break;}
        }
        if(!ok)return;
        for(int k=0;k<order;++k) {
            // Slight bandwidth expansion to avoid high-Q time-varying ringing.
            const double desired=a[size_t(k+1)]*std::pow(0.90,k+1);
            coefficients_[size_t(k)]=0.80*coefficients_[size_t(k)]+0.20*desired;
        }
        valid_=true;
        ++updates_;
    }
    double fs_=48000;
    int channels_=1,updateEvery_=384;
    std::array<float,historySize*2> analysis_{};
    std::array<std::array<double,order>,2> inputPast_{},outputPast_{};
    std::array<double,order> coefficients_{};
    std::array<double,2> previousInput_{}, previousOutput_{};
    uint64_t sampleIndex_=0,updates_=0;
    double level_=0.0, outputPower_=0.0, gain_=1.0;
    bool valid_=false;
};
}
