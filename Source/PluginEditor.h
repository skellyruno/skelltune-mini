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
        skellcore::ui::SkullPalette palette=skellcore::ui::skullPalette(skellcore::ui::SkullSkin::Neon);
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float,
                              float, float, juce::Slider&) override;
        juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override {
            return juce::Font(juce::FontOptions(
                juce::jlimit(11.0f, 23.0f, float(buttonHeight) * 0.49f), juce::Font::bold));
        }
        // JUCE creates slider value readouts as Labels. Scale their font
        // with the editor instead of leaving tiny fixed-size values at 2x.
        juce::Font getLabelFont(juce::Label& label) override {
            return juce::Font(juce::FontOptions(
                juce::jlimit(11.0f, 25.0f, float(label.getHeight()) * 0.60f),
                juce::Font::bold));
        }
        juce::Font getComboBoxFont(juce::ComboBox& box) override {
            return juce::Font(juce::FontOptions(
                juce::jlimit(12.0f, 21.0f, float(box.getHeight()) * 0.50f)));
        }
    } theme_;
    struct PitchButton final : juce::Button {
        explicit PitchButton(const juce::String& text, const skellcore::ui::SkullPalette& skin)
            : juce::Button(text), palette_(&skin) { setButtonText(text); }
        const skellcore::ui::SkullPalette* palette_;
        void paintButton(juce::Graphics&, bool hover, bool down) override;
    };
    void timerCallback() override;
    void refreshNotes();
    void togglePitchClass(int);
    void resetManualNotes();
    void openAdvancedControls();
    void applySkullTheme();

    SkellyTuneRealtimeProcessor& processor_;
    juce::AudioProcessorValueTreeState& parameters_;
    skellcore::ui::SkullPalette palette_=skellcore::ui::skullPalette(skellcore::ui::SkullSkin::Neon);
    skellcore::ui::SkullSkin lastSkin_=skellcore::ui::SkullSkin::Neon;
    struct SkullHitButton final : juce::Button {
        SkullHitButton() : juce::Button("Cycle SKELL skin") {}
        void paintButton(juce::Graphics&, bool, bool) override {}
    } skullHit_;
    juce::ComboBox key_, scale_;
    juce::Slider speed_, amount_, mix_;
    juce::TextButton bypass_{"BYPASS"}, more_{"MORE"}, presetNotes_{"RESET NOTES"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttach_, scaleAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> speedAttach_, amountAttach_, mixAttach_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttach_;
    std::array<std::unique_ptr<PitchButton>, 12> notes_;
    float lastDisplayHz_ = 0.0f, lastConfidence_ = 0.0f, lastCorrectionCents_ = 0.0f;
    float lastReferenceA_ = 440.0f;
    int lastTargetNote_ = -1;
    bool lastTargetPending_ = false;
    int lastMask_ = -1;
    bool lastCustomMask_ = false;
    bool lastBypassed_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyTuneEditor)
};
