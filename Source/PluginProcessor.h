#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/RealtimeTuner.hpp"
#include "../dsp/PitchEditTimeline.hpp"
#include <SkellCore/WorldLink.h>
#include <SkellCore/SkullTheme.h>
#include <juce_events/juce_events.h>
#include <atomic>
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
#include "../dsp/MetaTest.hpp"   // TEST-ONLY engine. Never ship a build that defines SKELLY_META_TEST.
#endif

#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
#include "../dsp/PlaygroundBank.hpp"
#endif

class SkellyTuneRealtimeProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    SkellyTuneRealtimeProcessor();
    ~SkellyTuneRealtimeProcessor() override;
    void prepareToPlay(double sampleRate, int maxBlockSize) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock(juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override {
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
        return skelly::playground::modeCount==7?"SkellyTune Playground":"SkellyTune Playground 5";
#elif defined(SKELLY_META_TEST) && SKELLY_META_TEST
        return "SkellyTune META Lab";
#elif defined(SKELLY_STUDIO_FAST_PROFILE) && SKELLY_STUDIO_FAST_PROFILE
        return "SkellyTune Studio Fast";
#elif defined(SKELLY_STUDIO_PROFILE) && SKELLY_STUDIO_PROFILE
        return "SkellyTune Studio";
#else
        return "SkellyTune Realtime";
#endif
    }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override {
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
        return .1;
#else
        return .01;
#endif
    }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int index) override { return index == 0 ? "Default" : juce::String{}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;
    // Receiver connects automatically on the JUCE message thread; only a
    // deliberate SEND TO TUNE action in Skell Key publishes a tuning packet.
    // Socket traffic stays in WorldLink's worker; processBlock reads atomics.
    skellcore::world::WorldLink worldLink{"skell.tune","SkellyTune"};
    bool joinWorld() {
        if (worldLink.requested()) return true;
        return worldLink.join();
    }
    void leaveWorld() {
        stopTimer();
        linkedKey_.store(0, std::memory_order_release);
        linkedTuning_.store(0, std::memory_order_release);
        worldLink.leave();
    }
    bool followingSkellKey() const noexcept { return worldLink.requested(); }
    // Message-thread-only handler for an authenticated musical.tuning packet.
    // Exposed to the host-independent JUCE smoke test as well as timerCallback.
    bool applySkellKeyTuning(const skellcore::world::MusicalTuning& tuning);
    skellcore::world::MusicalKey linkedMusicalKey() const noexcept {
        if(!followingSkellKey() || !worldLink.online())return {};
        return skellcore::world::unpackMusicalKey(linkedKey_.load(std::memory_order_acquire));
    }
    skellcore::ui::SkullSkin getSkullSkin() const noexcept {
        return skellcore::ui::skinFromIndex(skinIndex_.load(std::memory_order_acquire));
    }
    void setSkullSkin(skellcore::ui::SkullSkin skin) {
        const auto next=static_cast<int>(skin);
        skinIndex_.store(next,std::memory_order_release);
        state_.state.setProperty("skellSkullSkin",next,nullptr);
    }
    juce::AudioProcessorValueTreeState& parameters() noexcept { return state_; }
    const juce::AudioProcessorValueTreeState& parameters() const noexcept { return state_; }
    float displayedPitchHz() const noexcept { return displayedPitchHz_.load(std::memory_order_relaxed); }
    float displayedConfidence() const noexcept { return displayedConfidence_.load(std::memory_order_relaxed); }
    int displayedTargetNote() const noexcept { return displayedTargetNote_.load(std::memory_order_relaxed); }
    bool displayedTargetPending() const noexcept { return displayedTargetPending_.load(std::memory_order_relaxed); }
    float displayedCorrectionCents() const noexcept { return displayedCorrectionCents_.load(std::memory_order_relaxed); }
    // Displays follow the SAME transient A4 as DSP, including SkellKey linked
    // concert-pitch overrides (without mutating the saved APVTS A4 value).
    float displayedReferenceA() const noexcept { return displayedReferenceA_.load(std::memory_order_relaxed); }
    int64_t displayedHostMs() const noexcept {
        return displayedHostMs_.load(std::memory_order_relaxed);
    }
    bool graphEditsSupported() const noexcept {
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
        return skelly::playground::graphEditsSupported(playgroundRenderMode());
#else
        return true;
#endif
    }
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    int playgroundMode() const noexcept {
        return juce::jlimit(0,skelly::playground::modeCount-1,
            int(std::lround(state_.getRawParameterValue("playgroundEngine")->load())));
    }
    int playgroundProfile() const noexcept {
        return juce::jlimit(0,int(skelly::playground::latencyNames.size())-1,
            int(std::lround(state_.getRawParameterValue("playgroundLatencyProfile")->load())));
    }
    int playgroundAppliedProfile() const noexcept { return playgroundAppliedProfile_.load(); }
    int playgroundRenderMode() const noexcept {
        const int locked=playgroundLockedMode_.load();
        return locked>=0?locked:playgroundMode();
    }
    bool playgroundNeedsApply() const noexcept {
        return playgroundProfile()!=playgroundAppliedProfile() ||
            (playgroundAppliedProfile()!=0 && playgroundMode()!=playgroundRenderMode());
    }
    bool playgroundPlaying() const noexcept { return playgroundPlaying_.load(); }
    int playgroundHostDelay() const noexcept { return playgroundHostDelay_.load(); }
    bool applyPlaygroundLatency(); // explicit message-thread action, transport stopped
    int playgroundActiveMode() const noexcept { return playgroundActive_.load(std::memory_order_relaxed); }
    int playgroundNativeDelay(int mode) const noexcept {
        return mode>=0&&mode<skelly::playground::modeCount?playgroundNative_[size_t(mode)].load():0;
    }
#endif
    skelly::PitchEditTimeline& pitchEdits() noexcept { return pitchEdits_; }
    const skelly::PitchEditTimeline& pitchEdits() const noexcept { return pitchEdits_; }
    void pitchEditStateChanged() {
        updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
    }
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    skelly::Parameters regularEngineParametersForLabTests() const noexcept {
        return engine_.parameterSnapshotForLabTests();
    }
#endif
private:
    void timerCallback() override;
    void updateTimelinePosition() noexcept;
    // Each call is a prepared, planar float audio segment. Splits only at
    // edited note boundaries; ordinary sessions remain a single DSP call.
    void processEditedAudio(float* const* io,int channels,int frames,
                            int blockOffset,bool useMeta);
    // Shared UI/theme and optional key-link state. Audio processing reads only
    // atomics and never touches a socket, mutex, GUI component or host params.
    std::atomic<int> skinIndex_{1};
    std::atomic<int> linkedKey_{0};
    std::atomic<std::uint64_t> linkedTuning_{0}; // all of scale + A4 + key in one atomic
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    skelly::Parameters currentParameters() const noexcept;
    // JUCE standalone GUI smoke invokes prepareToPlay directly; JUCE's
    // host-managed getSampleRate() may still be zero. Always use the
    // actual rate passed to prepareToPlay for DSP and edit boundaries.
    double preparedAudioRate_ = 0.0;
    juce::AudioBuffer<float> doubleScratch_;  // allocated ONLY in prepareToPlay
    juce::AudioProcessorValueTreeState state_;
    skelly::RealtimeTuner engine_;
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    skelly::MetaTest meta_;
    bool metaWasActive_ = false;   // audio thread only
    void runMeta(float* const* io, int channels, int frames);
#endif
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    skelly::playground::Bank playground_;
    std::atomic<int> playgroundActive_{-1};
    std::atomic<int> playgroundAppliedProfile_{0},playgroundLockedMode_{-1},playgroundHostDelay_{0};
    std::atomic<bool> playgroundPlaying_{false};
    std::atomic<unsigned> playgroundGeneration_{0};
    void publishPlaygroundConfig();
    std::array<std::atomic<int>,7> playgroundNative_{};
    void processPlayground(float* const* io,int channels,int frames,int blockOffset,const skelly::Parameters& p);
#endif
    skelly::PitchEditTimeline pitchEdits_;
    std::atomic<int64_t> displayedHostMs_{-1};
    std::atomic<float> displayedPitchHz_{0.0f};
    std::atomic<float> displayedConfidence_{0.0f};
    std::atomic<int> displayedTargetNote_{-1};
    std::atomic<bool> displayedTargetPending_{false};
    std::atomic<float> displayedCorrectionCents_{0.0f};
    std::atomic<float> displayedReferenceA_{440.0f};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyTuneRealtimeProcessor)
};
