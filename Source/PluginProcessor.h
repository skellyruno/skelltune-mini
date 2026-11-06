#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/RealtimeTuner.hpp"
#include <atomic>

class SkellyTuneRealtimeProcessor final : public juce::AudioProcessor {
public:
    SkellyTuneRealtimeProcessor();
    ~SkellyTuneRealtimeProcessor() override = default;
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
    juce::AudioProcessorValueTreeState& parameters() noexcept { return state_; }
    const juce::AudioProcessorValueTreeState& parameters() const noexcept { return state_; }
    float displayedPitchHz() const noexcept { return displayedPitchHz_.load(std::memory_order_relaxed); }
    float displayedConfidence() const noexcept { return displayedConfidence_.load(std::memory_order_relaxed); }
    int displayedTargetNote() const noexcept { return displayedTargetNote_.load(std::memory_order_relaxed); }
    bool displayedTargetPending() const noexcept { return displayedTargetPending_.load(std::memory_order_relaxed); }
    float displayedCorrectionCents() const noexcept { return displayedCorrectionCents_.load(std::memory_order_relaxed); }
private:
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
