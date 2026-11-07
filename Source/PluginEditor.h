#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <memory>

class SkellyTuneEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SkellyTuneEditor(SkellyTuneRealtimeProcessor&);
    ~SkellyTuneEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct SimpleTheme : juce::LookAndFeel_V4 {
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float,
                              float, float, juce::Slider&) override;
        juce::Font getComboBoxFont(juce::ComboBox& box) override {
            return juce::Font(juce::FontOptions(
                juce::jlimit(11.0f, 19.0f, float(box.getHeight()) * 0.46f)));
        }
    } theme_;
    struct PitchButton final : juce::Button {
        explicit PitchButton(const juce::String& text) : juce::Button(text) { setButtonText(text); }
        void paintButton(juce::Graphics&, bool hover, bool down) override;
    };
    void timerCallback() override;
    void refreshNotes();
    void togglePitchClass(int);
    void resetManualNotes();
    void openAdvancedControls();

    SkellyTuneRealtimeProcessor& processor_;
    juce::AudioProcessorValueTreeState& parameters_;
    juce::ComboBox key_, scale_;
    juce::Slider speed_, amount_, mix_;
    juce::TextButton bypass_{"BYPASS"}, more_{"MORE"}, presetNotes_{"RESET NOTES"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttach_, scaleAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttach_, amountAttach_, mixAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach_;
    std::array<std::unique_ptr<PitchButton>, 12> notes_;
    float lastDisplayHz_ = 0.0f, lastConfidence_ = 0.0f, lastCorrectionCents_ = 0.0f;
    int lastTargetNote_ = -1;
    bool lastTargetPending_ = false;
    int lastMask_ = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyTuneEditor)
};
