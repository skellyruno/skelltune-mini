#include "PluginEditor.h"
#include "../dsp/ScaleMask.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
const auto bg       = juce::Colour::fromRGB(8, 10, 14);
const auto panel    = juce::Colour::fromRGB(17, 21, 27);
const auto cyan     = juce::Colour::fromRGB(72, 227, 235);
const auto dimText  = juce::Colour::fromRGB(124, 133, 148);
const auto light    = juce::Colour::fromRGB(231, 238, 247);
const auto stroke   = juce::Colour::fromRGB(38, 52, 62);
const char* const noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
using State = juce::AudioProcessorValueTreeState;

void styleSelector(juce::ComboBox& b) {
    b.setColour(juce::ComboBox::backgroundColourId, panel);
    b.setColour(juce::ComboBox::outlineColourId, cyan.withAlpha(0.40f));
    b.setColour(juce::ComboBox::textColourId, light);
    b.setColour(juce::ComboBox::arrowColourId, cyan);
    b.setJustificationType(juce::Justification::centred);
}

void styleLinear(juce::Slider& s, const juce::String& suffix) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 77, 21);
    s.setColour(juce::Slider::trackColourId, cyan);
    s.setColour(juce::Slider::backgroundColourId, stroke);
    s.setColour(juce::Slider::thumbColourId, light);
    s.setColour(juce::Slider::textBoxTextColourId, light);
    s.setColour(juce::Slider::textBoxBackgroundColourId, bg);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setTextValueSuffix(suffix);
}

class AdvancedControls final : public juce::Component {
public:
    explicit AdvancedControls(State& state) : state_(state) {
        const std::array<const char*, 8> ids {
            "transient", "air", "formantPreserve", "vibratoPreserve", "flex", "cycleStabilize", "a4", "synthesisMode"
        };
        const std::array<const char*, 8> labels {
            "TRANSIENT PROTECT", "CONSONANT AIR", "FORMANT ENVELOPE", "VIBRATO PRESERVE", "FLEX TUNE",
            "CYCLE LEVEL", "A4 REFERENCE", "SYNTHESIS MODE"
        };
        for (int i = 0; i < 7; ++i) {
            auto label = std::make_unique<juce::Label>();
            label->setText(labels[size_t(i)], juce::dontSendNotification);
            label->setColour(juce::Label::textColourId, dimText);
            label->setFont(juce::Font(juce::FontOptions(11.0f)));
            addAndMakeVisible(*label);
            labels_.push_back(std::move(label));
            auto slider = std::make_unique<juce::Slider>();
            styleLinear(*slider, i == 4 ? " ct" : (i == 6 ? " Hz" : "%"));
            addAndMakeVisible(*slider);
            sliderAttachments_.push_back(std::make_unique<State::SliderAttachment>(state_, ids[size_t(i)], *slider));
            sliders_.push_back(std::move(slider));
        }
        auto modeLabel = std::make_unique<juce::Label>();
        modeLabel->setText(labels[7], juce::dontSendNotification);
        modeLabel->setColour(juce::Label::textColourId, dimText);
        modeLabel->setFont(juce::Font(juce::FontOptions(11.0f)));
        addAndMakeVisible(*modeLabel);
        labels_.push_back(std::move(modeLabel));
#if (defined(SKELLY_STUDIO_PROFILE) && SKELLY_STUDIO_PROFILE) || (defined(SKELLY_STUDIO_FAST_PROFILE) && SKELLY_STUDIO_FAST_PROFILE)
        mode_.addItem("Studio Clock-Locked", 1);
        mode_.addItem("Studio Clock-Locked (fixed)", 2);
        mode_.setEnabled(false); // This plugin has one fixed PDC / DSP contract.
#else
        mode_.addItem("Realtime", 1);
        mode_.addItem("Cycle PSOLA (experimental)", 2);
#endif
        styleSelector(mode_);
        addAndMakeVisible(mode_);
        modeAttachment_ = std::make_unique<State::ComboBoxAttachment>(state_, "synthesisMode", mode_);
        phase_.setButtonText("Phase-aligned cycle jumps");
        phase_.setColour(juce::ToggleButton::textColourId, light);
        phase_.setColour(juce::ToggleButton::tickColourId, cyan);
        addAndMakeVisible(phase_);
        phaseAttachment_ = std::make_unique<State::ButtonAttachment>(state_, "phaseAlign", phase_);
        setSize(375, 530);
    }
    void paint(juce::Graphics& g) override {
        g.fillAll(bg);
        g.setColour(light);
        g.setFont(juce::Font(juce::FontOptions(18.0f, juce::Font::bold)));
        g.drawText("ADVANCED TUNING", 18, 10, getWidth()-36, 31, juce::Justification::centredLeft);
        g.setColour(dimText);
        g.setFont(11.0f);
        g.drawText("These controls remain automatable in your DAW", 18, 39, getWidth()-36, 19, juce::Justification::centredLeft);
    }
    void resized() override {
        int y = 64;
        for (size_t i=0; i<sliders_.size(); ++i) {
            labels_[i]->setBounds(18, y, getWidth()-36, 16);
            sliders_[i]->setBounds(18, y+18, getWidth()-36, 28);
            y += 53;
        }
        labels_[7]->setBounds(18, y, getWidth()-36, 16);
        mode_.setBounds(18, y+20, getWidth()-36, 32);
        phase_.setBounds(18, y+61, getWidth()-36, 29);
    }
private:
    State& state_;
    std::vector<std::unique_ptr<juce::Label>> labels_;
    std::vector<std::unique_ptr<juce::Slider>> sliders_;
    std::vector<std::unique_ptr<State::SliderAttachment>> sliderAttachments_;
    juce::ComboBox mode_;
    std::unique_ptr<State::ComboBoxAttachment> modeAttachment_;
    juce::ToggleButton phase_;
    std::unique_ptr<State::ButtonAttachment> phaseAttachment_;
};

juce::String detectedNoteName(float hz) {
    if (!std::isfinite(hz) || hz < 45.0f) return "—";
    const auto note = juce::roundToInt(69.0 + 12.0 * std::log2(double(hz) / 440.0));
    return juce::String(noteNames[(note % 12 + 12) % 12]) + juce::String(note / 12 - 1);
}
} // namespace

void SkellyTuneEditor::SimpleTheme::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                    float pos, float start, float end, juce::Slider& slider) {
    auto bounds = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(18.0f);
    const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float r = radius - 5.0f;
    juce::Path outer, active;
    outer.addCentredArc(cx, cy, r, r, 0.0f, start, end, true);
    active.addCentredArc(cx, cy, r, r, 0.0f, start, start + pos*(end-start), true);
    g.setColour(juce::Colour::fromRGB(37, 46, 56));
    g.strokePath(outer, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(cyan.withAlpha(0.20f));
    g.strokePath(active, juce::PathStrokeType(15.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(cyan);
    g.strokePath(active, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(juce::Colour::fromRGB(3, 6, 12));
    g.fillEllipse(cx-r+20.0f, cy-r+20.0f, 2.0f*r-40.0f, 2.0f*r-40.0f);
    g.setColour(stroke);
    g.drawEllipse(cx-r+20.0f, cy-r+20.0f, 2.0f*r-40.0f, 2.0f*r-40.0f, 3.0f);
    g.setColour(cyan);
    g.setFont(juce::Font(juce::FontOptions(41.0f, juce::Font::bold)));
    // 0 ms is a real DSP setting now. Do not round 0.1–0.9 ms down to a
    // visually misleading zero on the main control.
    const double speedValue = slider.getValue();
    const juce::String speedLabel = (speedValue < 0.005)
        ? juce::String("0 ms")
        : (speedValue < 1.0)
            ? juce::String(speedValue, 2) + " ms"
            : (speedValue < 10.0)
                ? juce::String(speedValue, 1) + " ms"
                : juce::String(juce::roundToInt(speedValue)) + " ms";
    g.drawText(speedLabel,
               juce::Rectangle<int>(int(cx-110),int(cy-34),220,68),juce::Justification::centred);
}

void SkellyTuneEditor::PitchButton::paintButton(juce::Graphics& g, bool hover, bool down) {
    const auto active = getToggleState();
    const auto r = getLocalBounds().toFloat().reduced(2.0f);
    if (active) {
        g.setColour(cyan.withAlpha(down ? 0.38f : 0.20f));
        g.fillEllipse(r);
    } else if (hover) {
        g.setColour(stroke.withAlpha(0.6f));
        g.fillEllipse(r);
    }
    g.setColour(active ? cyan : stroke.brighter(0.4f));
    g.drawEllipse(r, 1.8f);
    g.setColour(active ? light : dimText);
    g.setFont(juce::Font(juce::FontOptions(12.5f, active ? juce::Font::bold : juce::Font::plain)));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred);
}

SkellyTuneEditor::SkellyTuneEditor(SkellyTuneRealtimeProcessor& p)
    : juce::AudioProcessorEditor(p), processor_(p), parameters_(p.parameters()) {
    setSize(720, 690);
    setResizable(false, false);
    speed_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    speed_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    speed_.setRotaryParameters(juce::MathConstants<float>::pi * 1.23f,
                               juce::MathConstants<float>::pi * 2.77f, true);
    speed_.setLookAndFeel(&theme_);
    addAndMakeVisible(speed_);
    styleLinear(amount_, "%"); styleLinear(mix_, "%");
    addAndMakeVisible(amount_); addAndMakeVisible(mix_);
    const juce::StringArray keys {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    for (int i=0; i<keys.size(); ++i) key_.addItem(keys[i], i+1);
    scale_.addItem("Chromatic", 1); scale_.addItem("Major", 2); scale_.addItem("Minor", 3);
    styleSelector(key_); styleSelector(scale_);
    addAndMakeVisible(key_); addAndMakeVisible(scale_);
    keyAttach_ = std::make_unique<State::ComboBoxAttachment>(parameters_, "key", key_);
    scaleAttach_ = std::make_unique<State::ComboBoxAttachment>(parameters_, "scale", scale_);
    speedAttach_ = std::make_unique<State::SliderAttachment>(parameters_, "speed", speed_);
    amountAttach_ = std::make_unique<State::SliderAttachment>(parameters_, "amount", amount_);
    mixAttach_ = std::make_unique<State::SliderAttachment>(parameters_, "mix", mix_);
    bypassAttach_ = std::make_unique<State::ButtonAttachment>(parameters_, "bypass", bypass_);
    // Do not erase a saved custom note mask when a host restores/automates
    // key or scale or the ComboBox attachment initializes asynchronously.
    // RESET NOTES explicitly returns to the selected key/scale preset.
    for (int i=0; i<12; ++i) {
        notes_[size_t(i)] = std::make_unique<PitchButton>(noteNames[i]);
        addAndMakeVisible(*notes_[size_t(i)]);
        notes_[size_t(i)]->onClick = [this, i] { togglePitchClass(i); };
    }
    for (auto* btn : {&bypass_, &more_, &presetNotes_}) {
        btn->setColour(juce::TextButton::buttonColourId, panel);
        btn->setColour(juce::TextButton::buttonOnColourId, cyan.withAlpha(0.20f));
        btn->setColour(juce::TextButton::textColourOffId, dimText);
        btn->setColour(juce::TextButton::textColourOnId, cyan);
        addAndMakeVisible(*btn);
    }
    bypass_.setClickingTogglesState(true);
    more_.onClick = [this] { openAdvancedControls(); };
    presetNotes_.onClick = [this] { resetManualNotes(); };
    refreshNotes();
    startTimerHz(25);
}

SkellyTuneEditor::~SkellyTuneEditor() { speed_.setLookAndFeel(nullptr); }

void SkellyTuneEditor::resized() {
    key_.setBounds(445, 23, 85, 40);
    scale_.setBounds(540, 23, 158, 40);
    speed_.setBounds(209, 161, 302, 302);
    constexpr int natural[] = {0, 2, 4, 5, 7, 9, 11};
    constexpr int sharp[]   = {1, 3, 6, 8, 10};
    constexpr int upperX[]  = {205, 264, 382, 441, 500};
    for (int i=0; i<7; ++i) notes_[size_t(natural[i])]->setBounds(160+i*59, 529, 47, 47);
    for (int i=0; i<5; ++i) notes_[size_t(sharp[i])]->setBounds(upperX[i], 481, 47, 47);
    amount_.setBounds(125, 609, 197, 28);
    mix_.setBounds(431, 609, 197, 28);
    bypass_.setBounds(591, 651, 105, 29);
    presetNotes_.setBounds(27, 651, 115, 29);
    more_.setBounds(303, 652, 115, 27);
}

void SkellyTuneEditor::paint(juce::Graphics& g) {
    g.fillAll(bg);
    auto root = getLocalBounds().toFloat().reduced(5.0f);
    g.setColour(stroke);
    g.drawRoundedRectangle(root, 13.0f, 1.2f);
    g.setColour(juce::Colour::fromRGB(11, 14, 19));
    g.fillRoundedRectangle(juce::Rectangle<float>(8.0f,77.0f,704.0f,603.0f), 11.0f);
    g.setColour(stroke);
    g.drawLine(8.0f,75.0f,712.0f,75.0f,1.3f);
    // Original waveform icon: only vector drawing, no external logos/assets.
    for (int j=0; j<3; ++j) {
        juce::Path wave;
        wave.startNewSubPath(24.0f, 54.0f);
        wave.cubicTo(40.0f,53.0f,35.0f,16.0f+j*5.0f,47.0f,36.0f+j*2.0f);
        wave.cubicTo(58.0f,54.0f,61.0f,30.0f+j*4.0f,78.0f,53.0f);
        g.setColour(light.withAlpha(0.50f+j*0.20f));
        g.strokePath(wave, juce::PathStrokeType(1.35f));
    }
    g.setColour(light);
    g.setFont(juce::Font(juce::FontOptions(27.0f, juce::Font::bold)));
    g.drawText("SKELLYTUNE", 93, 24, 238, 40, juce::Justification::centredLeft);
    g.setColour(dimText);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("LIVE", 310, 38, 45, 21, juce::Justification::centredLeft);
    g.setFont(12.0f);
    g.drawText("KEY", 408, 33, 36, 22, juce::Justification::centredLeft);
    // Input pitch meter: live DSP estimate, not fabricated visual motion.
    g.setColour(dimText);
    g.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    g.drawText("INPUT", 29, 97, 65, 24, juce::Justification::centredLeft);
    if (lastDisplayHz_ > 0.0f) {
        g.setColour(cyan);
        g.drawText(detectedNoteName(lastDisplayHz_), 89, 96, 83, 25, juce::Justification::centredLeft);
        g.setColour(dimText);
        g.drawText(juce::String(lastDisplayHz_, 1) + " Hz", 160, 96, 92, 25, juce::Justification::centredLeft);
    } else {
        g.drawText("WAITING FOR VOICE", 97, 96, 160, 25, juce::Justification::centredLeft);
    }
    // Feedback comes from actual DSP state, not a decorative animation.
    // A target can be held briefly through unvoiced consonants, so only
    // display it as actively tuning when a current input pitch is available.
    g.setColour(dimText);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText("TARGET", 29, 124, 63, 22, juce::Justification::centredLeft);
    if (lastTargetNote_ >= 0 && lastDisplayHz_ > 0.0f && !lastTargetPending_) {
        g.setColour(cyan);
        g.drawText(juce::String(noteNames[(lastTargetNote_%12+12)%12])
                     + juce::String(lastTargetNote_/12-1), 89, 123, 76, 24,
                   juce::Justification::centredLeft);
        g.setColour(light);
        g.drawText(juce::String(lastCorrectionCents_ >= 0.0f ? "+" : "")
                   + juce::String(lastCorrectionCents_, 1) + " ct",
                   160, 123, 109, 24, juce::Justification::centredLeft);
    } else {
        g.setColour(dimText);
        g.drawText(lastTargetPending_ ? "CHECKING" : "—", 89, 123, 86, 24,
                   juce::Justification::centredLeft);
    }
    const int active = (lastDisplayHz_ > 0.0f) ? (juce::roundToInt(69.0 + 12.0*std::log2(double(lastDisplayHz_)/440.0))%12+12)%12 : -1;
    for (int i=0; i<12; ++i) {
        auto b = juce::Rectangle<float>(341.0f+i*28.0f, 103.0f, 21.0f, 12.0f);
        g.setColour(i == active ? cyan : juce::Colour::fromRGB(32, 38, 49));
        g.fillRoundedRectangle(b, 2.0f);
    }
    g.setColour(dimText);
    g.setFont(juce::Font(juce::FontOptions(15.0f)));
    g.drawText("RETUNE SPEED", 245, 448, 230, 30, juce::Justification::centred);
    const bool customNotes = parameters_.getRawParameterValue("noteMask")->load() > 0.5f;
    g.drawText(customNotes ? "CUSTOM NOTES" : "ENABLED NOTES", 268, 579, 185, 24,
               juce::Justification::centred);
    g.setColour(dimText);
    g.setFont(12.0f);
    g.drawText("AMOUNT", 39, 611, 85, 23, juce::Justification::centredRight);
    g.drawText("MIX", 378, 611, 50, 23, juce::Justification::centredRight);
}

void SkellyTuneEditor::togglePitchClass(int index) {
    const auto root = juce::roundToInt(parameters_.getRawParameterValue("key")->load());
    const auto scale = juce::roundToInt(parameters_.getRawParameterValue("scale")->load());
    const auto raw = juce::roundToInt(parameters_.getRawParameterValue("noteMask")->load());
    const auto previous = skelly::effectiveMask(root,scale,raw);
    const auto next = skelly::toggleNote(previous, index);
    if (next == previous) return;
    if (auto* param = parameters_.getParameter("noteMask")) {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(float(next)));
        param->endChangeGesture();
    }
    refreshNotes();
}

void SkellyTuneEditor::resetManualNotes() {
    if (auto* param = parameters_.getParameter("noteMask")) {
        if (parameters_.getRawParameterValue("noteMask")->load() != 0.0f) {
            param->beginChangeGesture();
            param->setValueNotifyingHost(param->convertTo0to1(0.0f));
            param->endChangeGesture();
        }
    }
    refreshNotes();
}

void SkellyTuneEditor::refreshNotes() {
    const int root = juce::roundToInt(parameters_.getRawParameterValue("key")->load());
    const int scale = juce::roundToInt(parameters_.getRawParameterValue("scale")->load());
    const int overrideMask = juce::roundToInt(parameters_.getRawParameterValue("noteMask")->load());
    const int mask = skelly::effectiveMask(root,scale,overrideMask);
    if (mask == lastMask_) return;
    lastMask_ = mask;
    for (int i=0; i<12; ++i)
        notes_[size_t(i)]->setToggleState((mask & (1<<i)) != 0, juce::dontSendNotification);
}

void SkellyTuneEditor::timerCallback() {
    refreshNotes();
    const auto hz = processor_.displayedPitchHz();
    const auto confidence = processor_.displayedConfidence();
    const auto target = processor_.displayedTargetNote();
    const auto pending = processor_.displayedTargetPending();
    const auto cents = processor_.displayedCorrectionCents();
    if (std::abs(hz-lastDisplayHz_) > 0.1f || std::abs(confidence-lastConfidence_) > 0.05f ||
        target != lastTargetNote_ || pending != lastTargetPending_ ||
        std::abs(cents-lastCorrectionCents_) > 0.5f) {
        lastDisplayHz_ = hz;
        lastConfidence_ = confidence;
        lastTargetNote_ = target;
        lastTargetPending_ = pending;
        lastCorrectionCents_ = cents;
        repaint(juce::Rectangle<int>(24, 91, 680, 60));
    }
}

void SkellyTuneEditor::openAdvancedControls() {
    auto advanced = std::make_unique<AdvancedControls>(parameters_);
    juce::CallOutBox::launchAsynchronously(std::move(advanced), more_.getScreenBounds(), nullptr);
}
