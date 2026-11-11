#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/RealtimeTuner.hpp"
#include <SkellCore/WorldLink.h>
#include <SkellCore/SkullTheme.h>
#include <juce_events/juce_events.h>
#include <atomic>

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
#if defined(SKELLY_STUDIO_FAST_PROFILE) && SKELLY_STUDIO_FAST_PROFILE
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
    double getTailLengthSeconds() const override { return 0.01; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
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
private:
    void timerCallback() override;
    // Shared UI/theme and optional key-link state. Audio processing reads only
    // atomics and never touches a socket, mutex, GUI component or host params.
    std::atomic<int> skinIndex_{1};
    std::atomic<int> linkedKey_{0};
    std::atomic<std::uint64_t> linkedTuning_{0}; // all of scale + A4 + key in one atomic
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    skelly::Parameters currentParameters() const noexcept;
    juce::AudioBuffer<float> doubleScratch_;  // allocated ONLY in prepareToPlay
    juce::AudioProcessorValueTreeState state_;
    skelly::RealtimeTuner engine_;
    std::atomic<float> displayedPitchHz_{0.0f};
    std::atomic<float> displayedConfidence_{0.0f};
    std::atomic<int> displayedTargetNote_{-1};
    std::atomic<bool> displayedTargetPending_{false};
    std::atomic<float> displayedCorrectionCents_{0.0f};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyTuneRealtimeProcessor)
};
