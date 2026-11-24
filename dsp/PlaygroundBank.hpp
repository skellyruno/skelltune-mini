#pragma once
// TEST ONLY: this is a comparison adapter, not a replacement for any standalone engine.
#if !defined(SKELLY_META_TEST) || !SKELLY_META_TEST
#error "Pitch Playground requires the research-only Mini build"
#endif
#include "RealtimeTuner.hpp"
#include "MetaTest.hpp"
#include "SnapCore.h"
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
#include "AutoTuneEngine.h"
#include "SkellyFusionEngine.h"
#endif
#include <memory>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

namespace skelly::playground {
// Stable IDs: do not reorder. META is not MetaTune Inspired.
inline constexpr std::array<const char*,7> names{{"REGULAR", "TESTTUNE", "META", "CLASSIC", "CRYSTAL", "OG SKELLYTUNE", "METATUNE 2.5"}};
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
inline constexpr int modeCount=7;
#else
inline constexpr int modeCount=5;
#endif
inline bool graphEditsSupported(int mode) noexcept { return mode>=0 && mode<3; }
struct Telemetry { float hz=0, confidence=0, cents=0; int target=-1; bool pending=false; };

class Bank {
public:
    void prepare(double rate,int channels,int maxBlockSize=128) {
        rate_=std::clamp(std::isfinite(rate)?rate:48000.,8000.,192000.);
        channels_=std::clamp(channels,1,2);
        regular_=std::make_unique<RealtimeTuner>();regular_->prepare(rate_,channels_,false,false,1.5);
        meta_=std::make_unique<MetaTest>();meta_->prepare(rate_,1.5);
        snap_=std::make_unique<snap::Engine>();snap_->prepare(rate_,channels_);
        snap::Params sp;sp.latencyMs=2.5;snap_->setParams(sp);snap_->reset();
        native_[0]=native_[1]=regular_->latencySamples();native_[2]=meta_->latencySamples();
        native_[3]=native_[4]=snap_->latencySamples();
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
        og_=std::make_unique<humtune::AutoTuneEngine>();og_->prepare(rate_,std::max(128,maxBlockSize));
        og_->setLowLatencyMode(false);og_->setVoiceRange(1);og_->setFormantPreserve(true);
        native_[5]=og_->getLatencySamples();
        inspired_=std::make_unique<autotune::SkellyFusionEngine>();inspired_->prepare(rate_,70.f,1000.f);
        autotune::Settings ms;ms.lowLatency=true;ms.ultraLowLatency=true;ms.strictRealtime=true;
        inspired_->setSettings(ms);inspired_->reset();native_[6]=inspired_->latencySamples();
#else
        (void)maxBlockSize;
#endif
        common_=*std::max_element(native_.begin(),native_.begin()+modeCount);
        capacity_=common_+1;
        for(int c=0;c<2;++c){dry_[c].assign(size_t(capacity_),0);wet_[c].assign(size_t(capacity_),0);}
        active_=-1;cursor_=0;fadePosition_=0;mixGain_=0.;warm_=0;telemetry_={};
        fadeSamples_=std::max(1,int(std::lround(rate_*.006)));
        fadeStep_=1./double(fadeSamples_);
    }
    int latencySamples() const noexcept {return common_;}
    int nativeLatency(int mode) const noexcept {return mode>=0&&mode<modeCount?native_[size_t(mode)]:-1;}
    int activeMode() const noexcept {return active_;}
    Telemetry telemetry() const noexcept {return telemetry_;}
    // Only the selected engine runs. Changes fade to the fixed-PDC dry path,
    // reset the new engine, prime its history, then fade in. No prepare() or
    // buffer resize on the callback, and no dynamic host PDC notifications.
    void process(float* const* io,int channels,int frames,const Parameters& input,
                 int requested,int manual=-1,bool bypass=false) {
        if(!regular_||!io||frames<=0||channels<1||channels>channels_)return;
        for(int c=0;c<channels;++c)if(!io[c])return;
        requested=std::clamp(requested,0,modeCount-1);
        for(int off=0;off<frames;){
            if(active_<0 || (requested!=active_ && fadePosition_==0))activate(requested);
            int n=std::min(128,frames-off);
            // Finish a switch at the exact fade boundary, not at the next
            // host/sub-block boundary. This also makes the warm-up start
            // sample deterministic for odd host callback sizes.
            if(requested!=active_ && fadePosition_>0)n=std::min(n,fadePosition_);
            float* q[2]={io[0]+off,channels==2?io[1]+off:nullptr};
            std::array<std::array<float,128>,2> raw{};
            for(int c=0;c<channels;++c)for(int i=0;i<n;++i){const float x=q[c][i];q[c][i]=raw[size_t(c)][size_t(i)]=std::isfinite(x)?std::clamp(x,-32.f,32.f):0.f;}
            Parameters p=input;p.speedMs=std::clamp(std::isfinite(p.speedMs)?p.speedMs:25.f,0.f,200.f);
            p.referenceA=std::clamp(std::isfinite(p.referenceA)?p.referenceA:440.f,400.f,480.f);
            p.amount=std::clamp(std::isfinite(p.amount)?p.amount:1.f,0.f,1.f);
            p.mix=std::clamp(std::isfinite(p.mix)?p.mix:1.f,0.f,1.f);
            p.pitchClasses&=0xfffu;
            const bool manualHere=graphEditsSupported(active_)&&manual>=24&&manual<=107;
            const bool dryOnly=bypass||p.amount==0.f||p.mix==0.f||(!p.pitchClasses&&!manualHere);
            render(q,channels,n,p,manualHere?manual:-1);
            const int pad=common_-native_[size_t(active_)];
            for(int i=0;i<n;++i){
                if(requested!=active_||warm_>0)fadePosition_=std::max(0,fadePosition_-1);
                else fadePosition_=std::min(fadeSamples_,fadePosition_+1);
                if(warm_>0)--warm_;
                const double mixTarget=dryOnly?0.:p.mix;
                mixGain_+=std::clamp(mixTarget-mixGain_,-fadeStep_,fadeStep_);
                const float blend=float(double(fadePosition_)/fadeSamples_*mixGain_);
                const int dr=(cursor_+capacity_-common_)%capacity_,wr=(cursor_+capacity_-pad)%capacity_;
                for(int c=0;c<channels;++c){
                    dry_[c][size_t(cursor_)]=raw[size_t(c)][size_t(i)];
                    wet_[c][size_t(cursor_)]=std::isfinite(q[c][i])?q[c][i]:0.f;
                    const float d=dry_[c][size_t(dr)],w=wet_[c][size_t(wr)];
                    q[c][i]=blend==1.f?w:d+(w-d)*blend;
                }
                if(++cursor_==capacity_)cursor_=0;
            }
            telemetry_.cents*=float(double(fadePosition_)/fadeSamples_*mixGain_);
            if(bypass||(!p.pitchClasses&&!manualHere)){telemetry_={};}
            off+=n;
        }
    }
private:
    void activate(int mode) {
        active_=mode;fadePosition_=0;warm_=common_+int(rate_*.04);telemetry_={};
        for(auto& v:wet_)std::fill(v.begin(),v.end(),0.f);
        if(mode<2)regular_->reset();else if(mode==2)meta_->reset();
        else if(mode<5)snap_->reset();
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
        else if(mode==5){og_->setEnabled(false);og_->setEnabled(true);}
        else inspired_->reset();
#endif
    }
    void render(float* const* io,int channels,int frames,Parameters p,int manual) {
        if(active_<2){p.testTune=active_==1;p.mix=1.f;p.experimentalCycleSynthesis=false;
            regular_->setParameters(p);regular_->setManualTargetMidi(manual);
            const float* in[2]={io[0],channels==2?io[1]:nullptr};regular_->process(in,io,frames,channels);
            const auto e=regular_->lastPitch();telemetry_={e.valid?e.hertz:0.f,e.valid?e.confidence:0.f,float(regular_->appliedCents()),regular_->currentTargetNote(),regular_->targetPending()};return;}
        if(active_==2){meta_->setParameters(p.pitchClasses,p.referenceA,p.amount,p.speedMs,0.,1.);meta_->setManualTargetMidi(manual);meta_->process(io,channels,frames);
            const double target=meta_->targetHz();telemetry_={float(meta_->detectedHz()),float(meta_->lastConfidence()),float(meta_->estimatedOutputCorrectionCents()),target>0?int(std::lround(69+12*std::log2(target/p.referenceA))):-1,false};return;}
        if(active_<5){snap::Params s;s.mode=active_-3;s.latencyMs=2.5;s.mask=p.pitchClasses;s.refHz=p.referenceA;s.amount=p.amount;s.retuneMs=p.speedMs;s.minHz=80.;
            snap_->setParams(s);snap_->process(io,channels,frames);const auto t=snap_->telemetry();telemetry_={t.voiced?t.inHz:0.f,t.conf,float(1200*std::log2(std::max(1e-6,snap_->currentRatio()))),t.voiced?t.targetNote:-1,false};return;}
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
        std::array<bool,12> mask{};for(int i=0;i<12;++i)mask[size_t(i)]=(p.pitchClasses&(1u<<i))!=0;
        if(active_==5){
            // OG's native speed is nonlinear, not a literal millisecond control.
            // Map to its nearest supported time constant; >100ms saturates.
            const float ms=p.speedMs;
            const float native=ms<.35f?0.f:ms<3.f?.099f:ms<25.5f?std::clamp((ms-.5f)/50.f,.1f,.4999f):ms<42.f?.4999f:std::clamp((ms-20.f)/80.f,.5f,1.f);
            og_->setRetuneSpeed(native);og_->setAmount(p.amount);og_->setReferenceFreq(p.referenceA);og_->setCustomScale(mask);og_->setHumanize(0);og_->setNoteStabilizer(false);og_->setFormantPreserve(true);
            juce::AudioBuffer<float> view(io,channels,frames);og_->process(view);
            telemetry_={og_->getDetectedHz(),og_->getConfidence(),float(1200*std::log2(std::max(1e-6f,og_->getAppliedPitchRatio()))),og_->getTargetMidiNote(),false};return;
        }
        autotune::Settings s;s.key=0;s.scale=autotune::Scale::Custom;s.customScale=mask;s.referenceHz=p.referenceA;s.retuneMs=p.speedMs;s.strength=p.amount;
        s.lowLatency=true;s.ultraLowLatency=true;s.strictRealtime=true;s.snapLive=false;s.noteConfirmMs=10.;s.noteStabilizerMs=0.;s.follow=.85f;s.voicingThreshold=.68f;
        inspired_->setSettings(s);inspired_->processChannels(io,channels,frames);
        const float midi=inspired_->getDetectedMidi();telemetry_={midi>0?p.referenceA*std::exp2((midi-69.f)/12.f):0.f,inspired_->getConfidence(),100.f*inspired_->getCorrectionSemitones(),inspired_->getTargetMidi(),false};
#endif
    }
    std::unique_ptr<RealtimeTuner> regular_;
    std::unique_ptr<MetaTest> meta_;
    std::unique_ptr<snap::Engine> snap_;
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
    std::unique_ptr<humtune::AutoTuneEngine> og_;
    std::unique_ptr<autotune::SkellyFusionEngine> inspired_;
#endif
    std::array<int,7> native_{};
    std::array<std::vector<float>,2> dry_,wet_;
    Telemetry telemetry_{};double rate_=48000.,mixGain_=0.,fadeStep_=.004;
    int fadeSamples_=1,fadePosition_=0;
    int channels_=0,common_=0,capacity_=1,cursor_=0,active_=-1,warm_=0;
};
} // namespace skelly::playground
