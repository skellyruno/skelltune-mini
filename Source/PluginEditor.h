#pragma once
#include "PluginProcessor.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <algorithm>
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
        float activity = 0.0f;   // 0..1, pitch correction strength
        float confidence = 0.0f, voiceAlpha = 0.0f, deviationCents = 0.0f;
        std::array<float, 32> pitchTrail{};
        int trailHead = 0, trailCount = 0;
        float hostSpeedMs = 25.0f; // true saved ms, including legacy values above the new 100ms dial max
        void drawRotarySlider(juce::Graphics&, int, int, int, int, float,
                              float, float, juce::Slider&) override;
        juce::Font getTextButtonFont(juce::TextButton&, int height) override {
            return juce::Font(juce::FontOptions(
                juce::jlimit(12.0f, 26.0f, float(height) * 0.43f),
                juce::Font::bold));
        }
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
        float glow = 0.0f;       // 0..1, target pitch (neon green)
        float inputGlow = 0.0f;  // 0..1, detected sung pitch (white/soft mint)
        void setLiveFeedback(float input, float target) {
            inputGlow = input;
            glow = target;
            // Expose factual message-thread state for real editor smoke
            // tests. These are UI properties, not host parameters.
            getProperties().set("liveInput", double(input));
            getProperties().set("liveTarget", double(target));
            repaint();
        }
    };

    // INPUT / TARGET readout shown on either side of the dial. Draws only; never takes mouse input.
    class LiveReadout final : public juce::Component {
    public:
        LiveReadout(const skellcore::ui::SkullPalette& skin, bool isInput) : palette_(&skin), input_(isInput) {
            setInterceptsMouseClicks(false, false);
        }
        // alpha: 0 = nothing sung, 1 = voiced. meter: -1..1. active: 0..1 correction strength.
        void setState(float alpha, const juce::String& note, const juce::String& detail,
                      float meter, float active, float confidence) {
            alpha_ = alpha; note_ = note; detail_ = detail; meter_ = meter;
            active_ = active; confidence_ = confidence;
            getProperties().set("pitchConfidence", double(confidence));
            repaint();
        }
        void paint(juce::Graphics&) override;
    private:
        const skellcore::ui::SkullPalette* palette_;
        bool input_;
        float alpha_ = 0.0f, meter_ = 0.0f, active_ = 0.0f, confidence_ = 0.0f;
        juce::String note_ = "--", detail_;
    };

    // Segmented algorithm switch: exactly one segment is selected at a time.
    class AlgorithmSelector final : public juce::Component, public juce::SettableTooltipClient {
    public:
        explicit AlgorithmSelector(const skellcore::ui::SkullPalette& skin) : palette_(&skin) {}
        void setSegments(const juce::StringArray& labels, const juce::StringArray& tips) {
            labels_ = labels; tips_ = tips; repaint();
        }
        void setSelected(int index) { if (index != selected_) { selected_ = index; repaint(); } }
        int selected() const noexcept { return selected_; }
        std::function<void(int)> onSelect;
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseMove(const juce::MouseEvent&) override;
        void mouseExit(const juce::MouseEvent&) override { if (hover_ != -1) { hover_ = -1; repaint(); } }
        juce::String getTooltip() override {
            return hover_ >= 0 && hover_ < tips_.size() ? tips_[hover_] : juce::String();
        }
    private:
        int segmentAt(float x) const noexcept {
            const int n = std::max(1, labels_.size());
            return juce::jlimit(0, n - 1, int(x / (float(getWidth()) / float(n))));
        }
        const skellcore::ui::SkullPalette* palette_;
        juce::StringArray labels_, tips_;
        int selected_ = 0, hover_ = -1;
    };

    void timerCallback() override;
    void updateLiveFeedback();
    int currentAlgorithm() const;
    void chooseAlgorithm(int index);
    void refreshNotes();
    void togglePitchClass(int index);
    void resetManualNotes();
    void toggleAnalyzer();

    SkellyTuneRealtimeProcessor& processor_;
    juce::AudioProcessorValueTreeState& parameters_;
    skellcore::ui::SkullPalette palette_ =
        skellcore::ui::skullPalette(skellcore::ui::SkullSkin::Neon);
    juce::ComboBox key_, scale_;
    juce::Slider speed_, a4_;
    juce::TextButton analyzerToggle_{"ANALYZE"};
    bool analyzerExpanded_ = false;
    static constexpr int kAnalyzerHistory = 90;
    std::array<float, kAnalyzerHistory> analyzerPitch_{}, analyzerCorrection_{};
    int analyzerHead_ = 0, analyzerSamples_ = 0;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        keyAttachment_, scaleAttachment_;
    // UI is a clockwise fastness controller (0..100), while the existing
    // saved/automated APVTS "speed" parameter stays 0..200 milliseconds.
    std::unique_ptr<juce::ParameterAttachment> speedAttachment_;
    bool speedDragging_ = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> a4Attachment_;
    // Algorithm switch (REGULAR / TESTTUNE / META). Drives the APVTS ids "testTune" and (test builds) "meta".
    AlgorithmSelector algorithm_{palette_};
    LiveReadout inputReadout_{palette_, true}, targetReadout_{palette_, false};
    // Smoothed live-feedback state (message thread only).
    float voicedFade_ = 0.0f, activity_ = 0.0f, dispDeviation_ = 0.0f, dispCorrection_ = 0.0f;
    float liveConfidence_ = 0.0f;
    juce::String liveStatus_ = "LISTENING";
    int lastTarget_ = -1, lastInput_ = -1;
    juce::String lastInputName_ = "--", lastTargetName_ = "--";
    std::array<float, 12> keyGlow_{}, inputKeyGlow_{};
    std::array<std::unique_ptr<PitchButton>, 12> notes_;
    int lastMask_ = -1;
    bool lastCustomMask_ = false;
    // Every control sets a tooltip (including the note double-click hint), but a
    // component only shows one if a TooltipWindow exists. Declared last so it is
    // destroyed before the controls it describes.
    juce::TooltipWindow tooltipWindow_{this, 600};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkellyTuneEditor)
};
