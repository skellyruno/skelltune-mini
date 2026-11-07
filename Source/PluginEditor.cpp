#include "PluginEditor.h"
#include "../dsp/ScaleMask.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
const auto bg       = juce::Colour::fromRGB(8, 13, 10);
const auto panel    = juce::Colour::fromRGB(18, 27, 20);
const auto neon     = juce::Colour::fromRGB(144, 255, 68);
const auto dimText  = juce::Colour::fromRGB(157, 173, 156);
const auto light    = juce::Colour::fromRGB(236, 248, 235);
const auto stroke   = juce::Colour::fromRGB(47, 67, 48);
const char* const noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
using State = juce::AudioProcessorValueTreeState;

void styleSelector(juce::ComboBox& b) {
    b.setColour(juce::ComboBox::backgroundColourId, panel);
    b.setColour(juce::ComboBox::outlineColourId, neon.withAlpha(0.40f));
    b.setColour(juce::ComboBox::textColourId, light);
    b.setColour(juce::ComboBox::arrowColourId, neon);
    b.setJustificationType(juce::Justification::centred);
}

void styleLinear(juce::Slider& s, const juce::String& suffix) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 77, 21);
    s.setColour(juce::Slider::trackColourId, neon);
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
        phase_.setColour(juce::ToggleButton::tickColourId, neon);
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
    if (!std::isfinite(hz) || hz < 45.0f) return "--";
    const auto note = juce::roundToInt(69.0 + 12.0 * std::log2(double(hz) / 440.0));
    return juce::String(noteNames[(note % 12 + 12) % 12]) + juce::String(note / 12 - 1);
}
} // namespace

void SkellyTuneEditor::SimpleTheme::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h,
                                    float pos, float start, float end, juce::Slider& slider) {
    const auto rect = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(5.0f);
    const float cx = rect.getCentreX(), cy = rect.getCentreY();
    const float radius = 0.5f * std::min(rect.getWidth(), rect.getHeight());
    const float ring = std::max(3.0f, radius * 0.075f);
    const float r = radius - ring * 1.9f;
    juce::Path track, active;
    track.addCentredArc(cx, cy, r, r, 0.0f, start, end, true);
    active.addCentredArc(cx, cy, r, r, 0.0f, start, start + pos * (end - start), true);
    g.setColour(juce::Colour::fromRGB(40, 60, 43));
    g.strokePath(track, juce::PathStrokeType(ring, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    g.setColour(neon.withAlpha(0.18f));
    g.strokePath(active, juce::PathStrokeType(ring * 2.1f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour(neon);
    g.strokePath(active, juce::PathStrokeType(ring, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    auto center = juce::Rectangle<float>(cx-r+ring*1.7f, cy-r+ring*1.7f,
                                          2.0f*(r-ring*1.7f), 2.0f*(r-ring*1.7f));
    g.setColour(juce::Colour::fromRGB(5, 10, 7));
    g.fillEllipse(center);
    g.setColour(stroke);
    g.drawEllipse(center, 1.4f);
    // The centre remains readable when the editor is at its 432x414 default.
    const double value = slider.getValue();
    const juce::String speedLabel = value < 0.005 ? "0 ms" :
        (value < 1.0 ? juce::String(value, 2) + " ms" :
        (value < 10.0 ? juce::String(value, 1) + " ms" :
                       juce::String(juce::roundToInt(value)) + " ms"));
    g.setColour(neon);
    g.setFont(juce::Font(juce::FontOptions(
        juce::jlimit(21.0f, 41.0f, float(w) * 0.24f), juce::Font::bold)));
    g.drawText(speedLabel,
        juce::Rectangle<int>(int(cx-radius+8), int(cy-27), int(radius*2-16), 54),
        juce::Justification::centred, true);
}

void SkellyTuneEditor::PitchButton::paintButton(juce::Graphics& g, bool hover, bool down) {
    const bool active = getToggleState();
    const auto area = getLocalBounds().toFloat().reduced(1.8f);
    g.setColour(active ? neon.withAlpha(down ? 0.28f : 0.16f) :
                           (hover ? stroke.withAlpha(0.7f) : panel));
    g.fillEllipse(area);
    g.setColour(active ? neon : stroke.brighter(0.55f));
    g.drawEllipse(area, std::max(1.2f, getHeight() * 0.05f));
    g.setColour(active ? light : dimText);
    g.setFont(juce::Font(juce::FontOptions(
        juce::jlimit(11.0f, 18.0f, float(getHeight()) * 0.39f),
        active ? juce::Font::bold : juce::Font::plain)));
    g.drawText(getButtonText(), getLocalBounds(), juce::Justification::centred, true);
}

SkellyTuneEditor::SkellyTuneEditor(SkellyTuneRealtimeProcessor& p)
    : juce::AudioProcessorEditor(p), processor_(p), parameters_(p.parameters()) {
    setResizable(true, true);
    setResizeLimits(432, 414, 1008, 966);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(432.0 / 414.0);
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
    key_.setLookAndFeel(&theme_);
    scale_.setLookAndFeel(&theme_);
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
        btn->setColour(juce::TextButton::buttonOnColourId, neon.withAlpha(0.20f));
        btn->setColour(juce::TextButton::textColourOffId, dimText);
        btn->setColour(juce::TextButton::textColourOnId, neon);
        addAndMakeVisible(*btn);
    }
    bypass_.setClickingTogglesState(true);
    more_.onClick = [this] { openAdvancedControls(); };
    presetNotes_.onClick = [this] { resetManualNotes(); };
    refreshNotes();
    // IMPORTANT: setSize synchronously invokes resized(). All 12 notes, sliders
    // and buttons must already exist before resized() dereferences them.
    setSize(432, 414);
    startTimerHz(25);
}

SkellyTuneEditor::~SkellyTuneEditor() {
    speed_.setLookAndFeel(nullptr);
    key_.setLookAndFeel(nullptr);
    scale_.setLookAndFeel(nullptr);
}

void SkellyTuneEditor::resized() {
    // Draw and place children in a 432 x 414 logical design space.
    // Fixed aspect ratio avoids distorted typography and hit targets.
    const float sx = float(getWidth()) / 432.0f;
    const float sy = float(getHeight()) / 414.0f;
    const auto place = [sx, sy](juce::Component& child, float x, float y, float w, float h) {
        child.setBounds(juce::roundToInt(x*sx), juce::roundToInt(y*sy),
                        std::max(1, juce::roundToInt(w*sx)),
                        std::max(1, juce::roundToInt(h*sy)));
    };
    place(key_, 254, 28, 69, 27);
    place(scale_, 331, 28, 92, 27);
    place(speed_, 143, 123, 146, 146);
    // Match the position of the black keys on a seven-white-key row.
    constexpr float naturalX[] = {22, 77, 132, 187, 242, 297, 352};
    constexpr float sharpX[] = {51, 106, 216, 271, 326};
    constexpr int naturalNotes[] = {0, 2, 4, 5, 7, 9, 11};
    constexpr int sharpNotes[] = {1, 3, 6, 8, 10};
    for (int n=0; n<7; ++n)
        place(*notes_[size_t(naturalNotes[n])], naturalX[n], 320, 34, 34);
    for (int n=0; n<5; ++n)
        place(*notes_[size_t(sharpNotes[n])], sharpX[n], 289, 31, 31);
    amount_.setTextBoxStyle(juce::Slider::TextBoxRight, false,
        std::max(48, juce::roundToInt(53*sx)), std::max(18, juce::roundToInt(20*sy)));
    mix_.setTextBoxStyle(juce::Slider::TextBoxRight, false,
        std::max(48, juce::roundToInt(53*sx)), std::max(18, juce::roundToInt(20*sy)));
    place(amount_, 72, 357, 133, 24);
    place(mix_, 258, 357, 155, 24);
    place(presetNotes_, 11, 386, 112, 23);
    place(more_, 170, 386, 92, 23);
    place(bypass_, 327, 386, 96, 23);
}

void SkellyTuneEditor::paint(juce::Graphics& g) {
    g.fillAll(bg);
    juce::Graphics::ScopedSaveState saved(g);
    g.addTransform(juce::AffineTransform::scale(
        float(getWidth()) / 432.0f, float(getHeight()) / 414.0f));

    g.setColour(stroke);
    g.drawRoundedRectangle(juce::Rectangle<float>(5, 5, 422, 404), 10.0f, 1.0f);
    g.setColour(panel);
    g.fillRoundedRectangle(juce::Rectangle<float>(8, 68, 416, 64), 7.0f);
    g.fillRoundedRectangle(juce::Rectangle<float>(9, 138, 414, 141), 9.0f);

    // Minimal vector waveform mark, not a raster image.
    g.setColour(neon);
    juce::Path wave;
    wave.startNewSubPath(15.0f, 40.0f);
    wave.cubicTo(21, 40, 19, 18, 26, 22);
    wave.cubicTo(33, 26, 32, 38, 43, 40);
    g.strokePath(wave, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    g.setColour(light);
    g.setFont(juce::Font(juce::FontOptions(20.0f, juce::Font::bold)));
    g.drawText("SKELLYTUNE", 49, 12, 190, 27, juce::Justification::centredLeft, true);
    g.setColour(neon);
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText("MINI  /  LIVE", 49, 39, 128, 16, juce::Justification::centredLeft);
    g.setColour(dimText);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText("KEY", 254, 10, 66, 15, juce::Justification::centredLeft);
    g.drawText("SCALE", 331, 10, 90, 15, juce::Justification::centredLeft);
    g.setColour(stroke);
    g.drawLine(9.0f, 62.0f, 423.0f, 62.0f, 1.0f);

    // Live detector and controller state; never animate guessed pitch.
    const auto targetLabel = (lastTargetNote_ >= 0 && lastDisplayHz_ > 0 &&
                              !lastTargetPending_)
        ? juce::String(noteNames[(lastTargetNote_%12+12)%12])
            + juce::String(lastTargetNote_/12-1)
        : juce::String(lastTargetPending_ ? "CHECKING" : "--");
    const auto inputLabel = lastDisplayHz_ > 0.0f
        ? detectedNoteName(lastDisplayHz_) : juce::String("WAITING");
    const auto centsLabel = lastDisplayHz_ > 0.0f && !lastTargetPending_
        ? juce::String(lastCorrectionCents_ >= 0.0f ? "+" : "")
            + juce::String(lastCorrectionCents_, 1) + " ct"
        : juce::String("--");

    g.setColour(dimText);
    g.setFont(juce::Font(juce::FontOptions(10.0f, juce::Font::bold)));
    g.drawText("INPUT", 17, 73, 100, 17, juce::Justification::centredLeft);
    g.drawText("TARGET", 168, 73, 100, 17, juce::Justification::centredLeft);
    g.drawText("CORRECTION", 306, 73, 111, 17, juce::Justification::centredLeft);
    g.setColour(neon);
    g.setFont(juce::Font(juce::FontOptions(16.0f, juce::Font::bold)));
    g.drawText(inputLabel, 17, 88, 128, 23, juce::Justification::centredLeft, true);
    g.drawText(targetLabel, 168, 88, 120, 23, juce::Justification::centredLeft, true);
    g.drawText(centsLabel, 306, 88, 108, 23, juce::Justification::centredLeft, true);
    g.setColour(dimText);
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(lastDisplayHz_ > 0.0f ? juce::String(lastDisplayHz_, 1) + " Hz"
                                    : juce::String("No voiced input"),
               17, 110, 137, 16, juce::Justification::centredLeft);

    // Tiny note-class status strip, separate from the 12 playable note buttons.
    const int active = lastDisplayHz_ > 0.0f
        ? (juce::roundToInt(69.0 + 12.0*std::log2(double(lastDisplayHz_)/440.0))%12+12)%12
        : -1;
    for (int n=0; n<12; ++n) {
        g.setColour(n==active ? neon : stroke);
        g.fillRoundedRectangle(juce::Rectangle<float>(170.0f+n*20.0f, 117.0f, 13.0f, 4.0f), 2.0f);
    }
    g.setColour(dimText);
    g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
    g.drawText("RETUNE SPEED", 133, 269, 165, 19, juce::Justification::centred);
    const bool custom = parameters_.getRawParameterValue("noteMask")->load() > 0.5f;
    g.setFont(juce::Font(juce::FontOptions(9.0f, juce::Font::bold)));
    g.drawText(custom ? "CUSTOM NOTES" : "ENABLED NOTES", 13, 295, 109, 14,
               juce::Justification::centredLeft, true);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText("AMOUNT", 13, 360, 62, 17, juce::Justification::centredLeft);
    g.drawText("MIX", 222, 360, 35, 17, juce::Justification::centredLeft);
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
        const auto sy = float(getHeight()) / 414.0f;
        repaint(juce::Rectangle<int>(0, juce::roundToInt(67.0f * sy),
                                       getWidth(), juce::roundToInt(66.0f * sy)));
    }
}

void SkellyTuneEditor::openAdvancedControls() {
    auto advanced = std::make_unique<AdvancedControls>(parameters_);
    juce::CallOutBox::launchAsynchronously(std::move(advanced), more_.getScreenBounds(), nullptr);
}
