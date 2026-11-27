#pragma once
// TEST ONLY: this is a comparison adapter, not a replacement for any standalone engine.
#if !defined(SKELLY_META_TEST) || !SKELLY_META_TEST
#error "Pitch Playground requires the research-only Mini build"
#endif
#include "RealtimeTuner.hpp"
#include "MetaTest.hpp"
#include "CoherentMixGuard.hpp"
#include "WetLevelGuard.hpp"
#include "FormantEnvelopeGuard.hpp"
#include "AdaptiveVowelGuard.hpp"
#include "ChallengerDynamics.hpp"
#include "FinalListeningPolicy.hpp"
#include "SnapCore.h"
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
#include "SkellyFusionEngine.h"
#endif
#include <memory>
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

namespace skelly::playground {
// Exactly five active engines (four in the portable build). Keep the old host
// choice range for automation/state compatibility; retired slots are aliases only.
inline constexpr std::array<const char*,5> names{{"TESTTUNE", "META", "CLASSIC", "CRYSTAL", "METATUNE 2.5"}};
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
inline constexpr int modeCount=5, legacySlotCount=7;
#else
inline constexpr int modeCount=4, legacySlotCount=5;
#endif
inline constexpr std::array<int,5> legacySlots{{1,2,3,4,6}};
inline constexpr std::array<const char*,7> legacyLabels{{"TESTTUNE (legacy slot)", "TESTTUNE", "META", "CLASSIC", "CRYSTAL", "CLASSIC (legacy slot)", "METATUNE 2.5"}};
inline constexpr int fromLegacySlot(int slot) noexcept {
    return slot==2?1:slot==3||slot==5?2:slot==4?3:slot==6&&modeCount==5?4:0;
}
inline constexpr int toLegacySlot(int mode) noexcept {
    return mode>=0&&mode<modeCount?legacySlots[size_t(mode)]:1;
}
// Matched mode is the old common-delay comparison. Other profiles lock the
// prepared engine and report its real minimum plus any requested padding.
// Zero is a requested minimum, NOT a claim of zero physical delay.
// Keep numeric IDs for existing research states/render commands. Profile 0
// is a legacy offline comparison, not an interactive latency choice.
inline constexpr std::array<const char*,6> latencyNames{{"Legacy matched", "0 ms (Zero)", "1 ms (Ultra)", "2.5 ms (Low)", "5 ms (Bass / Baritone)", "10 ms (Stable)"}};
inline constexpr int interactiveLatencyProfile(int value) noexcept {
    return value<=0?3:std::min(value,int(latencyNames.size())-1);
}
inline constexpr std::array<double,6> latencyTargets{{-1.,0.,1.,2.5,5.,10.}};
inline bool graphEditsSupported(int mode) noexcept { return mode>=0 && mode<2; }
struct Telemetry { float hz=0, confidence=0, cents=0; int target=-1; bool pending=false; };

class Bank {
public:
    // Set before prepare. Offline A/B keeps the complete previous engine policy.
    // Preparation-time A/B: preserve legacy low latency paths and native IDs.
    void setRecordingResponseEnabled(bool enabled) noexcept { recordingResponse_=enabled; }
    void setNativeQualityEnabled(bool enabled) noexcept { nativeQuality_=enabled; }
    void setOvershootBalanceEnabled(bool enabled) noexcept { overshootBalance_=enabled; }
    void setWetLevelRecoveryEnabled(bool enabled) noexcept { wetLevelRecovery_=enabled; }
    void setChallengerExperiments(bool enabled) noexcept { challengers_=enabled; }
    void setEnvelopeCandidate(bool enabled) noexcept { envelopeCandidate_=enabled; }
    void setAdaptiveVowelCandidate(double strength) noexcept { adaptiveStrength_=std::clamp(strength,0.,1.); }
    // The real plugin always enables the measured listening policy. Offline
    // reference renders can deliberately leave it off for reproducible A/B.
    void setListeningPromotion(bool enabled) noexcept { listeningPromotion_=enabled; }
    void setMeasuredGrainPromotion(bool enabled) noexcept { measuredGrainPromotion_=enabled; }
    void setFinalListeningPolicy(bool enabled) noexcept { finalListeningPolicy_=enabled; }
    void setFinalTrial(unsigned flags) noexcept { finalTrial_=int(flags&15u); }
    static float finalMotionSpeed(float hostSpeed,float existing,int mode) noexcept {
        if(mode==2||mode==3)return existing;
        const float weight=std::clamp(std::min((hostSpeed-2.f)/8.f,(85.f-hostSpeed)/35.f),0.f,1.f);
        const float factor=mode==0?.70f:mode==1?.80f:.72f;
        return existing*(1.f-weight*(1.f-factor));
    }
    static double measuredMetaGrain(float hostSpeed) noexcept {
        return std::max(std::clamp(1.-double(hostSpeed)/8.,0.,1.),std::clamp((double(hostSpeed)-50.)/50.,0.,1.));
    }
    void setSynthesisCandidate(bool enabled) noexcept { synthesisCandidate_=enabled; }
    void setPhaseContinuityCandidate(bool enabled) noexcept { phaseContinuityCandidate_=enabled; }
    void setMotionCandidate(bool enabled) noexcept { motionCandidate_=enabled; }
    void setDynamicsCandidate(bool enabled) noexcept { dynamicsCandidate_=enabled; }
    void prepare(double rate,int channels,int maxBlockSize=128,int profile=0,int selectedEngine=0) {
        rate_=std::clamp(std::isfinite(rate)?rate:48000.,8000.,192000.);
        channels_=std::clamp(channels,1,2);
        profile_=std::clamp(profile,0,int(latencyNames.size())-1);
        lockedMode_=profile_==0?-1:std::clamp(selectedEngine,0,modeCount-1);
        const double requestedMs=latencyTargets[size_t(profile_)];
        const bool extended=nativeQuality_ && profile_>=4;
        const double miniMs=profile_==0?1.5:std::clamp(requestedMs,1.5,extended?10.0:3.0);
        snapMs_=profile_==0?2.5:requestedMs;
        testTune_=std::make_unique<RealtimeTuner>();testTune_->prepare(rate_,channels_,false,false,miniMs,extended);
        meta_=std::make_unique<MetaTest>();meta_->prepare(rate_,miniMs,extended);
        meta_->setFastRatioResponseEnabled(recordingResponse_ && profile_>=1 && profile_<=3);
        snap_=std::make_unique<snap::Engine>();snap_->prepare(rate_,channels_);
        snap::Params sp;sp.latencyMs=snapMs_;snap_->setParams(sp);snap_->reset();
        native_[0]=testTune_->latencySamples();native_[1]=meta_->latencySamples();
        native_[2]=native_[3]=snap_->actualDelaySamples();
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
        (void)maxBlockSize;
        inspired_=std::make_unique<autotune::SkellyFusionEngine>();inspired_->prepare(rate_,70.f,1000.f);
        inspiredQuality_=nativeQuality_ && profile_==5 && lockedMode_==4;
        autotune::Settings ms;ms.lowLatency=!inspiredQuality_;ms.ultraLowLatency=ms.lowLatency;ms.strictRealtime=true;
        inspired_->setSettings(ms);inspired_->reset();native_[4]=inspired_->latencySamples();
#else
        (void)maxBlockSize;
#endif
        common_=profile_==0?*std::max_element(native_.begin(),native_.begin()+modeCount)
            :std::max(native_[size_t(lockedMode_)],int(std::lround(rate_*requestedMs*.001)));
        capacity_=common_+1;
        for(int c=0;c<2;++c){dry_[c].assign(size_t(capacity_),0);wet_[c].assign(size_t(capacity_),0);}
        active_=-1;cursor_=0;fadePosition_=0;mixGain_=0.;warm_=0;telemetry_={};
        fadeSamples_=std::max(1,int(std::lround(rate_*.006)));
        fadeStep_=1./double(fadeSamples_);
        wetLevelGuard_.prepare(rate_);
        envelopeGuard_.prepare(rate_,channels_);
        adaptiveVowel_.prepare(rate_);
        challengerDynamics_.prepare(rate_);
        mixGuard_.prepare(rate_);partialBlend_=false;
        attackMarks_.assign(size_t(capacity_),0);
        pitchValidHistory_.assign(size_t(capacity_),0);
        attackPrevious_.fill(0.);attackPower_=attackDiffPower_=attackBlend_=0.;attackHold_=0;
        attackHoldSamples_=std::max(1,int(std::lround(rate_*.002)));
        attackAdvance_=std::min(common_,std::max(1,int(std::lround(rate_*.00075))));
        attackMeter_=1.-std::exp(-1./(rate_*.03));
        attackEntry_=1.-std::exp(-1./(rate_*.00012));
        attackRelease_=std::exp(-1./(rate_*.003));
    }
    int latencySamples() const noexcept {return common_;}
    int latencyProfile() const noexcept {return profile_;}
    int lockedMode() const noexcept {return lockedMode_;}
    int nativeLatency(int mode) const noexcept {return mode>=0&&mode<modeCount?native_[size_t(mode)]:-1;}
    int activeMode() const noexcept {return active_;}
    Telemetry telemetry() const noexcept {return telemetry_;}
    // Only the selected engine runs. Changes fade to the fixed-PDC dry path,
    // reset the new engine, prime its history, then fade in. No prepare() or
    // buffer resize on the callback, and no dynamic host PDC notifications.
    void process(float* const* io,int channels,int frames,const Parameters& input,
                 int requested,int manual=-1,bool bypass=false) {
        if(!testTune_||!io||frames<=0||channels<1||channels>channels_)return;
        for(int c=0;c<channels;++c)if(!io[c])return;
        requested=lockedMode_>=0?lockedMode_:std::clamp(requested,0,modeCount-1);
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
            const double protection=std::clamp(std::isfinite(p.transientProtection)?double(p.transientProtection):1.,0.,1.);
            const bool manualHere=graphEditsSupported(active_)&&manual>=24&&manual<=107;
            const bool dryOnly=bypass||p.amount==0.f||p.mix==0.f||(!p.pitchClasses&&!manualHere);
            std::array<uint8_t,128> pitchValid{};
            render(q,channels,n,p,manualHere?manual:-1,pitchValid.data());
            const int pad=common_-native_[size_t(active_)];
            for(int i=0;i<n;++i){
                if(requested!=active_||warm_>0)fadePosition_=std::max(0,fadePosition_-1);
                else fadePosition_=std::min(fadeSamples_,fadePosition_+1);
                if(warm_>0)--warm_;
                const double mixTarget=dryOnly?0.:p.mix;
                mixGain_+=std::clamp(mixTarget-mixGain_,-fadeStep_,fadeStep_);
                // The delay ring has exactly common_+1 slots. Avoid three dynamic
                // modulo divisions per audio sample, preserving every tap index.
                const int dr=cursor_+1==capacity_?0:cursor_+1;
                const int wetIndex=cursor_-pad;
                const int wr=wetIndex<0?wetIndex+capacity_:wetIndex;
                const int ahead=dr+attackAdvance_;
                const int look= ahead>=capacity_?ahead-capacity_:ahead;
                // Mark only isolated, high-contrast attacks in the ORIGINAL input.
                // The common transport already provides the history needed to
                // fade in before the delayed attack; no extra PDC or allocations.
                double power=0.,derivative=0.;
                for(int c=0;c<channels;++c){
                    const double x=raw[size_t(c)][size_t(i)];
                    power=std::max(power,x*x);
                    derivative=std::max(derivative,std::abs(x-attackPrevious_[size_t(c)]));
                    attackPrevious_[size_t(c)]=x;
                }
                const double diff=derivative*derivative;
                // Relative contrast detects quiet as well as loud attacks. The
                // absolute floor is 0.005 amplitude (~-46 dBFS), not 0.25.
                // Keep both energy gates to avoid treating sustained signal
                // or ordinary noise as an attack.
                const bool sharp=diff>std::max({.000025,3.*attackPower_,64.*attackDiffPower_});
                attackPower_+=attackMeter_*(std::min(power,4.)-attackPower_);
                attackDiffPower_+=attackMeter_*(std::min(diff,1.)-attackDiffPower_);
                if(sharp)attackHold_=attackHoldSamples_;
                attackMarks_[size_t(cursor_)]=attackHold_>0?1:0;
                if(attackHold_>0)--attackHold_;
                if(attackMarks_[size_t(dr)])attackBlend_=1.;
                else if(attackAdvance_>0&&attackMarks_[size_t(look)])
                    attackBlend_+=(1.-attackBlend_)*attackEntry_;
                else attackBlend_*=attackRelease_;
                if(attackBlend_<1.e-9)attackBlend_=0.;
                const float blend=float(double(fadePosition_)/fadeSamples_*mixGain_*(1.-protection*attackBlend_));
                float alignedDry[2]{},alignedWet[2]{};
                for(int c=0;c<channels;++c){
                    dry_[c][size_t(cursor_)]=raw[size_t(c)][size_t(i)];
                    wet_[c][size_t(cursor_)]=std::isfinite(q[c][i])?q[c][i]:0.f;
                    alignedDry[c]=dry_[c][size_t(dr)];alignedWet[c]=wet_[c][size_t(wr)];
                }
                // Classic and Crystal are immutable listening references.
                if(active_!=2 && active_!=3) pitchValidHistory_[size_t(cursor_)]=pitchValid[size_t(i)];
                if((dynamicsCandidate_ || (listeningPromotion_ && profile_==3 && (active_==0||active_==1||active_==4))) && active_!=2 && active_!=3){
                    const double strength=active_==1?1.:.75;
                    if(!dynamicsCandidate_ && active_==4){
                        float old[2]{alignedWet[0],alignedWet[1]};
                        wetLevelGuard_.process(alignedDry,old,channels,!dryOnly&&pitchValidHistory_[size_t(wr)]!=0,mixGain_,blend,false);
                        challengerDynamics_.process(alignedDry,alignedWet,channels,!dryOnly&&pitchValidHistory_[size_t(wr)]!=0,blend,strength);
                        const float weight=skelly::inspiredPromotionWeight(p.speedMs);
                        if(weight==0.f)for(int c=0;c<channels;++c)alignedWet[c]=old[c];
                        else if(weight<1.f)for(int c=0;c<channels;++c)alignedWet[c]=old[c]+weight*(alignedWet[c]-old[c]);
                    }else challengerDynamics_.process(alignedDry,alignedWet,channels,!dryOnly&&pitchValidHistory_[size_t(wr)]!=0,blend,strength);
                }else if(wetLevelRecovery_ && active_!=2 && active_!=3){
                    wetLevelGuard_.process(alignedDry,alignedWet,channels,!dryOnly&&pitchValidHistory_[size_t(wr)]!=0,mixGain_,blend,overshootBalance_ && (active_==1));
                }
                // Candidate only: aligned dry envelope guides the corrected wet signal.
                // Never touch Classic/Crystal, bypass, or zero Amount/Mix.
                if(envelopeCandidate_ && active_!=2 && active_!=3){
                    float corrected[2]{};
                    const double strength=!dryOnly && pitchValidHistory_[size_t(wr)]!=0 ? .65 : 0.;
                    envelopeGuard_.process(alignedDry,alignedWet,corrected,channels,strength,strength>0.);
                    // Preserve the old experimental shape but prohibit new overload.
                    double peak=0.,original=0.;for(int c=0;c<channels;++c){peak=std::max(peak,std::abs(double(corrected[c])));original=std::max(original,std::abs(double(alignedWet[c])));}
                    const double cap=std::max(1.,original),safe=peak>cap?cap/peak:1.;
                    for(int c=0;c<channels;++c)alignedWet[c]=float(corrected[c]*safe);
                }
                const double vowelStrength=(finalFlags()&8u)?1.:adaptiveStrength_;
                if(vowelStrength>0. && active_!=2 && active_!=3){
                    const double voiced=!dryOnly&&pitchValidHistory_[size_t(wr)]!=0?1.:0.;
                    const double fullWet=std::clamp((double(blend)-.90)*10.,0.,1.);
                    adaptiveVowel_.process(alignedDry,alignedWet,channels,voiced*fullWet,vowelStrength);
                }
                // Keep the compensation envelope alive through a protected
                // attack. Previously it reset to unity at the first marker,
                // then took the full release plus reacquisition to recover.
                // Smoothly taper only the compensation, not the dry anchor.
                const bool partial=mixGain_>0.075&&mixGain_<0.925;
                if(partial&&!partialBlend_)mixGuard_.reset();
                partialBlend_=partial;
                if(blend==0.f||blend==1.f){
                    // Preserve exact endpoints and avoid needless arithmetic.
                    if(partial)mixGuard_.process(alignedDry,alignedWet,channels,mixGain_,true);
                    for(int c=0;c<channels;++c)q[c][i]=blend==0.f?alignedDry[c]:alignedWet[c];
                }else{
                    float mixed[2]{},mixPeak=0.f;
                    for(int c=0;c<channels;++c){
                        mixed[c]=alignedDry[c]+(alignedWet[c]-alignedDry[c])*blend;
                        mixPeak=std::max(mixPeak,std::abs(mixed[c]));
                    }
                    double safeGain=1.;
                    if(partial){
                        const double gain=mixGuard_.process(alignedDry,alignedWet,channels,mixGain_,true);
                        const double mixEligibility=std::clamp((mixGain_-.075)/.15,0.,1.)*
                            std::clamp((.925-mixGain_)/.15,0.,1.);
                        const double audibleFraction=std::clamp(double(blend)/mixGain_,0.,1.);
                        const double headroom=std::clamp(2.*(1.-double(mixPeak)),0.,1.);
                        safeGain=1.+(gain-1.)*headroom*mixEligibility*audibleFraction;
                    }
                    for(int c=0;c<channels;++c)q[c][i]=float(mixed[c]*safeGain);
                }
                if(++cursor_==capacity_)cursor_=0;
            }
            telemetry_.cents*=float(double(fadePosition_)/fadeSamples_*mixGain_*(1.-protection*attackBlend_));
            if(bypass||(!p.pitchClasses&&!manualHere)){telemetry_={};}
            off+=n;
        }
    }
private:
    void activate(int mode) {
        wetLevelGuard_.reset();
        envelopeGuard_.reset();
        adaptiveVowel_.reset();
        challengerDynamics_.reset();
        std::fill(pitchValidHistory_.begin(),pitchValidHistory_.end(),uint8_t(0));
        mixGuard_.reset();partialBlend_=false;
        active_=mode;fadePosition_=0;warm_=common_+int(rate_*.04);telemetry_={};
        for(auto& v:wet_)std::fill(v.begin(),v.end(),0.f);
        if(mode==0)testTune_->reset();else if(mode==1)meta_->reset();
        else if(mode<4)snap_->reset();
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
        else inspired_->reset();
#endif
    }
    unsigned finalFlags() const noexcept {
        if(profile_!=3||active_<0||active_==2||active_==3)return 0;
        return finalTrial_>=0?unsigned(finalTrial_):finalListeningPolicy_?finalListeningFlags[size_t(active_)]:0u;
    }
    void render(float* const* io,int channels,int frames,Parameters p,int manual,uint8_t* pitchValid) {
        const unsigned flags=finalFlags();
        const bool finalFast=profile_==3&&(finalListeningPolicy_||finalTrial_>=0);
        const double fastFade=std::clamp((25.-double(p.speedMs))/15.,0.,1.);
        const double hardFade=std::clamp(1.-double(p.speedMs)/8.,0.,1.);
        const bool phase=phaseContinuityCandidate_||(flags&1u);
        const bool compact=synthesisCandidate_||(flags&2u);
        const float hostSpeed=p.speedMs;
        if(motionCandidate_&&active_!=2&&active_!=3)p.speedMs=skelly::challengerSpeed(p.speedMs,active_);
        else if(listeningPromotion_&&profile_==3)p.speedMs=skelly::listeningSpeed(p.speedMs,active_);
        if(flags&4u)p.speedMs=finalMotionSpeed(hostSpeed,p.speedMs,active_);
        else if(finalFast&&active_==0&&fastFade>0.)p.speedMs=float(p.speedMs+fastFade*(finalMotionSpeed(hostSpeed,p.speedMs,active_)-p.speedMs));
        if(active_==0){p.testTune=true;p.mix=1.f;p.experimentalCycleSynthesis=false;
            testTune_->setPlaygroundTransitionExperiment(challengers_);testTune_->setCompactWrapCandidate(compact);testTune_->setPhaseContinuityCandidate(phase);testTune_->setTrajectoryCandidate(finalFast?(.65+.65*hardFade)*fastFade:0.);testTune_->setParameters(p);testTune_->setManualTargetMidi(manual);
            const float* in[2]={io[0],channels==2?io[1]:nullptr};testTune_->process(in,io,frames,channels,pitchValid);
            const auto e=testTune_->lastPitch();telemetry_={e.valid?e.hertz:0.f,e.valid?e.confidence:0.f,float(testTune_->appliedCents()),testTune_->currentTargetNote(),testTune_->targetPending()};return;}
        if(active_==1){meta_->setCompactGrainWeight(compact?1.:(measuredGrainPromotion_&&profile_==3?measuredMetaGrain(hostSpeed):0.));meta_->setPhaseContinuityCandidate(phase);meta_->setTrajectoryCandidate(finalFast?1.3*hardFade:0.);meta_->setParameters(p.pitchClasses,p.referenceA,p.amount,p.speedMs,0.,1.);meta_->setManualTargetMidi(manual);meta_->process(io,channels,frames,false,pitchValid);
            const double target=meta_->targetHz();telemetry_={float(meta_->detectedHz()),float(meta_->lastConfidence()),float(meta_->estimatedOutputCorrectionCents()),target>0?int(std::lround(69+12*std::log2(target/p.referenceA))):-1,false};return;}
        if(active_<4){snap::Params s;s.mode=active_-2;s.latencyMs=snapMs_;s.mask=p.pitchClasses;s.refHz=p.referenceA;s.amount=p.amount;s.retuneMs=p.speedMs;s.minHz=80.;
            snap_->setParams(s);snap_->process(io,channels,frames);const auto t=snap_->telemetry();telemetry_={t.voiced?t.inHz:0.f,t.conf,float(1200*std::log2(std::max(1e-6,snap_->currentRatio()))),t.voiced?t.targetNote:-1,false};return;}
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
        std::array<bool,12> mask{};for(int i=0;i<12;++i)mask[size_t(i)]=(p.pitchClasses&(1u<<i))!=0;
        autotune::Settings s;s.key=0;s.scale=autotune::Scale::Custom;s.customScale=mask;s.referenceHz=p.referenceA;s.retuneMs=p.speedMs;s.strength=p.amount;
        s.lowLatency=!inspiredQuality_;s.ultraLowLatency=s.lowLatency;s.strictRealtime=true;s.snapLive=false;s.noteConfirmMs=motionCandidate_&&p.speedMs>2.f&&p.speedMs<45.f?5.f:10.f;
        if(!motionCandidate_&&listeningPromotion_&&profile_==3)s.noteConfirmMs=10.f-5.f*skelly::inspiredPromotionWeight(hostSpeed);
        if(compact&&profile_<=3)s.noteConfirmMs=std::min(s.noteConfirmMs,6.f);
        if(finalFast&&active_==4)s.noteConfirmMs=std::min(s.noteConfirmMs,float(10.-4.*hardFade));
        // The pinned native adapter actually maps snapLive to its own
        // confidence and hysteresis controller. Its legacy follow field is ignored.
        if(phase&&profile_<=3)s.snapLive=true;
        s.noteStabilizerMs=0.;s.follow=.85f;s.voicingThreshold=.68f;
        // Restore consonants sooner at intermediate retune rates without
        // changing hard tune, slow retune or the higher-latency quality path.
        if(recordingResponse_ && profile_>=1 && profile_<=3){
            const float weight=std::clamp(std::min((p.speedMs-5.f)/5.f,(50.f-p.speedMs)/25.f),0.f,1.f);
            s.consonantReleaseMs=8.f-4.f*weight;
        }
        inspired_->setSettings(s);inspired_->processChannels(io,channels,frames);
        const float midi=inspired_->getDetectedMidi();telemetry_={midi>0?p.referenceA*std::exp2((midi-69.f)/12.f):0.f,inspired_->getConfidence(),100.f*inspired_->getCorrectionSemitones(),inspired_->getTargetMidi(),false};
        std::fill_n(pitchValid,frames,uint8_t(telemetry_.hz>0?1:0));
#endif
    }
    std::unique_ptr<RealtimeTuner> testTune_;
    std::unique_ptr<MetaTest> meta_;
    std::unique_ptr<snap::Engine> snap_;
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
    std::unique_ptr<autotune::SkellyFusionEngine> inspired_;
#endif
    int profile_=0,lockedMode_=-1;double snapMs_=2.5;
    bool recordingResponse_=true;
    bool challengers_=true;
    bool wetLevelRecovery_=true;
    bool nativeQuality_=true,overshootBalance_=true,inspiredQuality_=false;
    WetLevelGuard wetLevelGuard_;
    skelly::FormantEnvelopeGuard envelopeGuard_;
    bool envelopeCandidate_=false;
    skelly::AdaptiveVowelGuard adaptiveVowel_;
    double adaptiveStrength_=0.;
    skelly::ChallengerDynamics challengerDynamics_;
    bool motionCandidate_=false,dynamicsCandidate_=false,listeningPromotion_=false,synthesisCandidate_=false,phaseContinuityCandidate_=false,measuredGrainPromotion_=false;
    bool finalListeningPolicy_=false;
    int finalTrial_=-1;
    std::array<int,modeCount> native_{};
    std::array<std::vector<float>,2> dry_,wet_;
    CoherentMixGuard mixGuard_;bool partialBlend_=false;
    std::vector<uint8_t> attackMarks_,pitchValidHistory_;
    std::array<double,2> attackPrevious_{};
    double attackPower_=0.,attackDiffPower_=0.,attackBlend_=0.;
    double attackMeter_=0.,attackEntry_=0.,attackRelease_=0.;
    int attackHold_=0,attackHoldSamples_=1,attackAdvance_=0;
    Telemetry telemetry_{};double rate_=48000.,mixGain_=0.,fadeStep_=.004;
    int fadeSamples_=1,fadePosition_=0;
    int channels_=0,common_=0,capacity_=1,cursor_=0,active_=-1,warm_=0;
};
} // namespace skelly::playground
