#include "PluginEditor.h"
#include "../dsp/ScaleMask.hpp"
#include <algorithm>
#include <cmath>

namespace {
using State = juce::AudioProcessorValueTreeState;
const char* const noteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

void styleCombo(juce::ComboBox& c, const skellcore::ui::SkullPalette& p) {
    c.setColour(juce::ComboBox::backgroundColourId, p.panel);
    c.setColour(juce::ComboBox::outlineColourId, p.line.brighter(0.25f));
    c.setColour(juce::ComboBox::textColourId, p.bone);
    c.setColour(juce::ComboBox::arrowColourId, p.accent);
    c.setJustificationType(juce::Justification::centred);
}
} // namespace

void SkellyTuneEditor::SimpleTheme::drawRotarySlider(
        juce::Graphics& g, int x, int y, int w, int h, float position,
        float start, float end, juce::Slider& slider) {
    const auto rect = juce::Rectangle<float>(float(x), float(y), float(w), float(h)).reduced(7.0f);
    const float cx = rect.getCentreX(), cy = rect.getCentreY();
    const float radius = std::min(rect.getWidth(), rect.getHeight()) * 0.5f;
    const float line = std::max(3.5f, radius * 0.085f);
    const float r = radius - line * 1.45f;
    juce::Path track, progress;
    track.addCentredArc(cx, cy, r, r, 0.0f, start, end, true);
    progress.addCentredArc(cx, cy, r, r, 0.0f, start,
                          start + position * (end-start), true);
    g.setColour(palette.line);
    g.strokePath(track, juce::PathStrokeType(line,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(palette.accent.withAlpha(0.14f));
    g.strokePath(progress, juce::PathStrokeType(line * 1.75f,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(palette.accent);
    g.strokePath(progress, juce::PathStrokeType(line,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const auto centre = juce::Rectangle<float>(cx-r+line*1.3f, cy-r+line*1.3f,
        2.0f*(r-line*1.3f), 2.0f*(r-line*1.3f));
    g.setColour(palette.background);
    g.fillEllipse(centre);
    g.setColour(palette.line);
    g.drawEllipse(centre, 1.15f);

    const double speed = slider.getValue();
    const auto displayed = speed < 0.005 ? juce::String("0 ms")
        : speed < 1.0 ? juce::String(speed, 2) + " ms"
        : speed < 10.0 ? juce::String(speed, 1) + " ms"
                       : juce::String(juce::roundToInt(speed)) + " ms";
    g.setColour(palette.accent);
    g.setFont(juce::Font(juce::FontOptions(
        juce::jlimit(24.0f, 44.0f, float(w) * 0.235f), juce::Font::bold)));
    g.drawText(displayed, juce::Rectangle<int>(
        int(cx-radius)+9, int(cy)-26, int(radius*2)-18, 52),
        juce::Justification::centred, true);
}

SkellyTuneEditor::PitchButton::PitchButton(
        const juce::String& name, const skellcore::ui::SkullPalette& skin)
        : juce::Button(name), palette_(&skin) {
    setButtonText(name);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setTooltip("Click to toggle this note. Double-click to restore Key / Scale notes.");
}
void SkellyTuneEditor::PitchButton::paintButton(
        juce::Graphics& g, bool hover, bool down) {
    const auto& p = *palette_;
    const bool selected = getToggleState();
    const auto area = getLocalBounds().toFloat().reduced(1.5f);
    g.setColour(selected ? p.accent.withAlpha(down ? 0.28f : 0.14f)
                         : (hover ? p.line.brighter(0.24f) : p.panel));
    g.fillEllipse(area);
    g.setColour(selected ? p.accent : p.line.brighter(0.55f));
    g.drawEllipse(area, std::max(1.25f, float(getHeight()) * 0.045f));
    g.setColour(selected ? p.bone : p.boneDim);
    g.setFont(juce::Font(juce::FontOptions(
        juce::jlimit(11.0f, 18.0f, float(getHeight()) * 0.39f),
        selected ? juce::Font::bold : juce::Font::plain)));
    g.drawText(getButtonText(), getLocalBounds(),
               juce::Justification::centred, true);
}
void SkellyTuneEditor::PitchButton::mouseDoubleClick(const juce::MouseEvent&) {
    if (onReset) onReset();
}

SkellyTuneEditor::SkellyTuneEditor(SkellyTuneRealtimeProcessor& p)
    : juce::AudioProcessorEditor(p), processor_(p), parameters_(p.parameters()) {
    speed_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    speed_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    speed_.setRotaryParameters(juce::MathConstants<float>::pi * 1.23f,
                              juce::MathConstants<float>::pi * 2.77f, true);
    speed_.setName("Retune Speed");
    speed_.setComponentID("skelly.speed");
    speed_.setTooltip("0 ms means immediate note correction, not zero detection latency.");
    speed_.setLookAndFeel(&theme_);
    addAndMakeVisible(speed_);

    // One editable A4 value with compact increment/decrement controls.
    a4_.setSliderStyle(juce::Slider::IncDecButtons);
    a4_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 83, 30);
    a4_.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical);
    a4_.setTextValueSuffix(" Hz");
    a4_.setColour(juce::Slider::textBoxTextColourId, palette_.bone);
    a4_.setColour(juce::Slider::textBoxBackgroundColourId, palette_.panel);
    a4_.setColour(juce::Slider::textBoxOutlineColourId, palette_.line);
    a4_.setColour(juce::Slider::textBoxHighlightColourId, palette_.accent.withAlpha(0.30f));
    a4_.setName("A4 tuning reference");
    a4_.setComponentID("skelly.a4");
    a4_.setTooltip("Concert pitch reference, normally 440 Hz. Click the value to type.");
    a4_.setLookAndFeel(&theme_);
    addAndMakeVisible(a4_);

    const juce::StringArray keys{
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    for (int i=0; i<keys.size(); ++i) key_.addItem(keys[i], i+1);
    scale_.addItem("Chromatic", 1);
    scale_.addItem("Major", 2);
    scale_.addItem("Minor", 3);
    styleCombo(key_, palette_);
    styleCombo(scale_, palette_);
    key_.setName("Key");
    scale_.setName("Scale");
    key_.setComponentID("skelly.key");
    scale_.setComponentID("skelly.scale");
    key_.setLookAndFeel(&theme_);
    scale_.setLookAndFeel(&theme_);
    addAndMakeVisible(key_);
    addAndMakeVisible(scale_);

    // Existing APVTS identifiers stay unchanged for automation and preset recall.
    keyAttachment_ = std::make_unique<State::ComboBoxAttachment>(
        parameters_, "key", key_);
    scaleAttachment_ = std::make_unique<State::ComboBoxAttachment>(
        parameters_, "scale", scale_);
    speedAttachment_ = std::make_unique<State::SliderAttachment>(
        parameters_, "speed", speed_);
    a4Attachment_ = std::make_unique<State::SliderAttachment>(
        parameters_, "a4", a4_);

    for (int i=0; i<12; ++i) {
        notes_[size_t(i)] =
            std::make_unique<PitchButton>(noteNames[i], palette_);
        auto& note = *notes_[size_t(i)];
        note.setComponentID("skelly.note." + juce::String(i));
        note.onClick = [this, i] { togglePitchClass(i); };
        note.onReset = [this] { resetManualNotes(); };
        addAndMakeVisible(note);
    }
    refreshNotes();
    // No hidden advanced page or bottom bar: just five primary workflows.
    setResizable(true, true);
    setResizeLimits(432, 414, 1008, 966);
    if (auto* constraint = getConstrainer())
        constraint->setFixedAspectRatio(432.0 / 414.0);
    setSize(432, 414);
    startTimerHz(12);
}
SkellyTuneEditor::~SkellyTuneEditor() {
    speed_.setLookAndFeel(nullptr);
    a4_.setLookAndFeel(nullptr);
    key_.setLookAndFeel(nullptr);
    scale_.setLookAndFeel(nullptr);
}
void SkellyTuneEditor::resized() {
    const float sx = float(getWidth()) / 432.0f;
    const float sy = float(getHeight()) / 414.0f;
    const auto place = [sx,sy](juce::Component& c, float x, float y, float w, float h) {
        c.setBounds(juce::roundToInt(x*sx), juce::roundToInt(y*sy),
            std::max(1,juce::roundToInt(w*sx)), std::max(1,juce::roundToInt(h*sy)));
    };
    place(key_, 18, 95, 90, 33);
    place(scale_, 119, 95, 151, 33);
    place(a4_, 290, 95, 122, 33);
    place(speed_, 127, 137, 178, 178);
    constexpr int naturals[] = {0,2,4,5,7,9,11};
    constexpr int accidentals[] = {1,3,6,8,10};
    constexpr float normalX[] = {22,77,132,187,242,297,352};
    constexpr float blackX[]  = {51,106,216,271,326};
    for (int i=0; i<7; ++i)
        place(*notes_[size_t(naturals[i])], normalX[i], 369, 35, 35);
    for (int i=0; i<5; ++i)
        place(*notes_[size_t(accidentals[i])], blackX[i], 337, 31, 31);
}
void SkellyTuneEditor::paint(juce::Graphics& g) {
    const auto bg=palette_.background, panel=palette_.panel;
    const auto accent=palette_.accent, text=palette_.bone, muted=palette_.boneDim;
    g.fillAll(bg);
    juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::scale(
        float(getWidth())/432.0f, float(getHeight())/414.0f));
    g.setColour(palette_.line);
    g.drawRoundedRectangle(juce::Rectangle<float>(5,5,422,404),10.0f,1.0f);

    // The shared SkellCore skull is branding only, not another control.
    {
        juce::Graphics::ScopedSaveState logo(g);
        g.addTransform(juce::AffineTransform::scale(0.46f).translated(13.0f,17.0f));
        skellcore::ui::drawSkullKeyMark(g,0.f,0.f,text,bg);
    }
    g.setFont(juce::Font(juce::FontOptions(20.0f,juce::Font::bold)));
    g.setColour(text);
    g.drawText("SKELLY",57,17,95,31,juce::Justification::centredLeft,true);
    g.setColour(accent);
    g.drawText("TUNE",149,17,77,31,juce::Justification::centredLeft,true);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(11.0f,juce::Font::bold)));
    g.drawText("MINI",234,23,53,23,juce::Justification::centredLeft);
    g.setColour(palette_.line);
    g.drawLine(16.0f,63.0f,416.0f,63.0f,1.0f);

    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(11.5f,juce::Font::bold)));
    g.drawText("KEY",20,76,89,16,juce::Justification::centredLeft);
    g.drawText("SCALE",121,76,145,16,juce::Justification::centredLeft);
    g.drawText("A4",292,76,110,16,juce::Justification::centredLeft);

    g.setColour(panel);
    g.fillRoundedRectangle(juce::Rectangle<float>(13,137,406,178),9.0f);
    g.setColour(palette_.line.withAlpha(0.65f));
    g.drawRoundedRectangle(juce::Rectangle<float>(13,137,406,178),9.0f,0.9f);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));
    g.drawText("RETUNE SPEED",137,314,158,19,juce::Justification::centred);

    const bool custom = parameters_.getRawParameterValue("noteMask")->load() > 0.5f;
    g.setFont(juce::Font(juce::FontOptions(10.0f)));
    g.drawText(custom ? "CUSTOM NOTES" : "ENABLED NOTES", 16,318,117,15,
               juce::Justification::centredLeft);
    if (custom)
        g.drawText("DOUBLE-CLICK TO RESET",299,318,119,15,
                   juce::Justification::centredRight,true);
}
void SkellyTuneEditor::togglePitchClass(int index) {
    const int root = juce::roundToInt(parameters_.getRawParameterValue("key")->load());
    const int scale = juce::roundToInt(parameters_.getRawParameterValue("scale")->load());
    const int savedMask = juce::roundToInt(parameters_.getRawParameterValue("noteMask")->load());
    const auto previous = skelly::effectiveMask(root,scale,savedMask);
    const auto next = skelly::toggleNote(previous,index);
    if (next == previous) return;
    if (auto* parameter = parameters_.getParameter("noteMask")) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(float(next)));
        parameter->endChangeGesture();
    }
    refreshNotes();
}
void SkellyTuneEditor::resetManualNotes() {
    if (auto* parameter = parameters_.getParameter("noteMask")) {
        if (parameters_.getRawParameterValue("noteMask")->load() != 0.0f) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(parameter->convertTo0to1(0.0f));
            parameter->endChangeGesture();
        }
    }
    refreshNotes();
}
void SkellyTuneEditor::refreshNotes() {
    const auto linked = processor_.linkedMusicalKey();
    const bool useLinked = processor_.followingSkellKey() && linked.valid();
    const int root = useLinked ? linked.root :
        juce::roundToInt(parameters_.getRawParameterValue("key")->load());
    const int scale = useLinked ? (linked.mode == 0 ? 1 : 2) :
        juce::roundToInt(parameters_.getRawParameterValue("scale")->load());
    const int raw = juce::roundToInt(parameters_.getRawParameterValue("noteMask")->load());
    const int mask = skelly::effectiveMask(root,scale,raw);
    const bool custom = raw != 0;
    if (mask == lastMask_ && custom == lastCustomMask_) return;
    lastMask_ = mask;
    lastCustomMask_ = custom;
    repaint();
    for (int i=0; i<12; ++i)
        notes_[size_t(i)]->setToggleState((mask & (1<<i))!=0,
                                          juce::dontSendNotification);
}
void SkellyTuneEditor::timerCallback() { refreshNotes(); }
