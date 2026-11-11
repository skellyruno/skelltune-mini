#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <functional>
#include <memory>

class SkellyTuneEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit SkellyTuneEditor(SkellyTuneRealtimeProcessor&);
    ~SkellyTuneEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    struct SimpleTheme final : juce::LookAndFeel_V4 {
        skellcore::ui::SkullPalette palette =
            skellcore::ui::skullPalette(skellcore::ui::SkullSkin::Neon);
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float,
                              float, float, juce::Slider&) override;
        juce::Font getComboBoxFont(juce::ComboBox& b) override {
            return juce::Font(juce::FontOptions(
                juce::jlimit(12.0f, 22.0f, float(b.getHeight()) * 0.50f),
                juce::Font::bold));
        }
        juce::Font getLabelFont(juce::Label& l) override {
            return juce::Font(juce::FontOptions(
                juce::jlimit(12.0f, 24.0f, float(l.getHeight()) * 0.56f),
                juce::Font::bold));
        }
    } theme_;
    struct PitchButton final : juce::Button {
        PitchButton(const juce::String&, const skellcore::ui::SkullPalette&);
        void paintButton(juce::Graphics&, bool hover, bool down) override;
        void mouseDoubleClick(const juce::MouseEvent&) override;
        std::function<void()> onReset;
        const skellcore::ui::SkullPalette* palette_;
    };

    void timerCallback() override;
    void refreshNotes();
    void togglePitchClass(int index);
    void resetManualNotes();

    SkellyTuneRealtimeProcessor& processor_;
    juce::AudioProcessorValueTreeState& parameters_;
    skellcore::ui::SkullPalette palette_ =
        skellcore::ui::skullPalette(skellcore::ui::SkullSkin::Neon);
    juce::ComboBox key_, scale_;
    juce::Slider speed_, a4_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        keyAttachment_, scaleAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        speedAttachment_, a4Attachment_;
    std::array<std::unique_ptr<PitchButton>, 12> notes_;
    int lastMask_ = -1;
    bool lastCustomMask_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyTuneEditor)
};
