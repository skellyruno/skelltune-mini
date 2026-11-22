#include "PluginEditor.h"
#include "../dsp/ScaleMask.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {
using State = juce::AudioProcessorValueTreeState;
const char* const noteNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
// Keep the 0..200ms APVTS parameter and automation IDs unchanged for old
// FL Studio projects. New GUI interactions cover *only* 0..100ms, 1:1.
// Clockwise/up = faster pitch correction = 0ms at the right endpoint.
// Old saved values 100..200ms are preserved untouched until the knob moves.
constexpr double kRetuneMaximumMs = 100.0;
constexpr double kDialMaximum = 100.0;
double clockwiseFastnessForMs(float ms) noexcept {
    return kDialMaximum - juce::jlimit(0.0, kRetuneMaximumMs, double(ms));
}
float millisecondsForClockwiseFastness(double fastness) noexcept {
    return float(kDialMaximum - juce::jlimit(0.0, kDialMaximum, fastness));
}

juce::String noteLabel(int midi) {
    if (midi < 0) return "--";
    return juce::String(noteNames[midi % 12]) + juce::String(midi / 12 - 1);   // MIDI 60 = C4
}
juce::String signedCents(float cents) {
    const int rounded = juce::roundToInt(cents);
    return (rounded > 0 ? "+" : rounded < 0 ? "-" : "") + juce::String(std::abs(rounded)) + " ct";
}
void setBoolParameter(juce::AudioProcessorValueTreeState& state, const char* id, bool value) {
    if (auto* parameter = state.getParameter(id)) {
        if ((parameter->getValue() > 0.5f) != value) {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(value ? 1.0f : 0.0f);
            parameter->endChangeGesture();
        }
    }
}

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
    const float uiScale = std::max(1.0f, std::min(float(w), float(h)) / 178.0f);
    const float cx = rect.getCentreX(), cy = rect.getCentreY();
    const float radius = std::min(rect.getWidth(), rect.getHeight()) * 0.5f;
    const float line = std::max(3.5f, radius * 0.085f);
    const float r = radius - line * 1.45f;
    const float live = juce::jlimit(0.0f, 1.0f, activity);
    // position=0 is SLOW (100), position=1 is HARD (0). Clockwise
    // rotation and upward dragging now actually shorten retune time.
    const float fastness = juce::jlimit(0.0f, 1.0f, position);
    const float needleAngle = start + fastness * (end - start);
    const auto endpoint = [&](float angle, float distance) {
        return juce::Point<float>(cx + distance * std::sin(angle),
                                  cy - distance * std::cos(angle));
    };

    // A restrained dual-surface knob. These highlights are painted only by
    // JUCE's editor and never touch the audio thread.
    g.setColour(palette.accent.withAlpha(0.035f + 0.07f * live));
    g.fillEllipse(cx - radius + 1.0f, cy - radius + 1.0f,
                  2.0f * (radius - 1.0f), 2.0f * (radius - 1.0f));

    juce::Path track, progress;
    track.addCentredArc(cx, cy, r, r, 0.0f, start, end, true);
    if (fastness > 0.0001f)
        progress.addCentredArc(cx, cy, r, r, 0.0f, start, needleAngle, true);
    g.setColour(palette.line);
    g.strokePath(track, juce::PathStrokeType(line,
        juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (fastness > 0.0001f) {
        g.setColour(palette.accent.withAlpha(0.12f + 0.19f * live));
        g.strokePath(progress, juce::PathStrokeType(line * 1.9f,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(palette.accent);
        g.strokePath(progress, juce::PathStrokeType(line,
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Fine reference marks: 100 (slow) on the left to 0 (fast) on the right.
    for (int i = 0; i <= 10; ++i) {
        const float t = float(i) / 10.0f;
        const float angle = start + t * (end - start);
        const auto inner = endpoint(angle, r + line * 1.4f);
        const auto outer = endpoint(angle, r + line * (i % 5 == 0 ? 2.05f : 1.75f));
        g.setColour((t <= fastness ? palette.accent : palette.boneDim)
            .withAlpha(t <= fastness ? 0.55f : 0.25f));
        g.drawLine(inner.x, inner.y, outer.x, outer.y,
                   std::max(0.9f, line * 0.16f));
    }

    const auto marker = endpoint(needleAngle, r);
    const float markerRadius = std::max(3.0f, line * 0.66f);
    g.setColour(palette.accent.withAlpha(0.18f + 0.11f * live));
    g.fillEllipse(marker.x - markerRadius * 1.65f,
                  marker.y - markerRadius * 1.65f,
                  markerRadius * 3.3f, markerRadius * 3.3f);
    g.setColour(palette.accent);
    g.fillEllipse(marker.x - markerRadius, marker.y - markerRadius,
                  markerRadius * 2.0f, markerRadius * 2.0f);

    const auto centre = juce::Rectangle<float>(cx-r+line*1.3f, cy-r+line*1.3f,
        2.0f*(r-line*1.3f), 2.0f*(r-line*1.3f));
    g.setGradientFill(juce::ColourGradient(
        palette.panel.brighter(0.10f), cx, cy - r,
        palette.background, cx, cy + r, false));
    g.fillEllipse(centre);
    g.setColour(palette.line.brighter(0.11f));
    g.drawEllipse(centre, std::max(1.0f, uiScale * 0.8f));

    g.setColour(palette.boneDim);
    g.setFont(juce::Font(juce::FontOptions(juce::jlimit(10.0f, 30.0f,
        float(w) * 0.062f), juce::Font::bold)));
    const auto p0 = endpoint(start, r), p1 = endpoint(end, r);
    const int labelHeight = juce::roundToInt(14.0f * uiScale);
    const int labelOffset = juce::roundToInt(6.0f * uiScale);
    const int zeroHalf = juce::roundToInt(18.0f * uiScale);
    const int endHalf = juce::roundToInt(21.0f * uiScale);
    g.drawText("100", juce::Rectangle<int>(
        int(p0.x) - endHalf, int(p0.y) + labelOffset,
        endHalf * 2, labelHeight), juce::Justification::centred);
    g.drawText("0", juce::Rectangle<int>(
        int(p1.x) - zeroHalf, int(p1.y) + labelOffset,
        zeroHalf * 2, labelHeight), juce::Justification::centred);

    // 100 on the face is now exactly 100ms. Older projects may contain
    // >100ms from the original parameter range: do not silently clamp the
    // saved value; the centre shows "100+" and a truthful legacy readout.
    const bool legacyValue = hostSpeedMs > 100.01f;
    const double shownRetune = 100.0 - slider.getValue();
    const int shown = juce::roundToInt(shownRetune);
    const float milliseconds = legacyValue ? hostSpeedMs
        : millisecondsForClockwiseFastness(slider.getValue());
    const juce::String response = legacyValue ? "LEGACY"
        : shown <= 0 ? "HARD" : shown <= 20 ? "FAST"
        : shown <= 60 ? "NATURAL" : "SLOW";

    const int innerMargin = juce::roundToInt(8.0f * uiScale);
    const int textWidth = int(radius * 2.0f) - innerMargin * 2;
    g.setColour(palette.accent);
    g.setFont(juce::Font(juce::FontOptions(
        juce::jlimit(24.0f, 86.0f, float(w) * 0.20f), juce::Font::bold)));
    g.drawText(legacyValue ? juce::String("100+") : juce::String(shown),
        juce::Rectangle<int>(
        int(cx-radius)+innerMargin, int(cy)-juce::roundToInt(30.0f * uiScale),
        textWidth, juce::roundToInt(43.0f * uiScale)),
        juce::Justification::centred, true);

    g.setColour(palette.boneDim);
    g.setFont(juce::Font(juce::FontOptions(
        juce::jlimit(10.0f, 33.0f, float(w) * 0.068f), juce::Font::bold)));
    const juce::String detail = juce::String(juce::roundToInt(milliseconds))
        + " ms  /  " + response;
    g.drawText(detail, juce::Rectangle<int>(
        int(cx-radius)+innerMargin, int(cy)+juce::roundToInt(15.0f * uiScale),
        textWidth, juce::roundToInt(20.0f * uiScale)),
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
    const bool enabled = getToggleState();
    const float scale = float(getHeight()) / 35.0f;
    const auto area = getLocalBounds().toFloat().reduced(1.7f * scale);
    const auto centre = area.getCentre();
    const float glowValue = juce::jlimit(0.0f, 1.0f, glow);
    const float lift = down ? -0.05f : hover ? 0.10f : 0.0f;

    // A precise, dimensional, playable note pad rather than an empty ring:
    // selected tones look active, disabled notes stay legible, live targets
    // have a subtle glow, and hover/press feedback is consistent at all sizes.
    if (enabled || glowValue > 0.015f) {
        const float haloAlpha = std::min(0.25f, 0.06f +
            (enabled ? 0.055f : 0.0f) + glowValue * 0.14f);
        g.setColour(p.accent.withAlpha(haloAlpha));
        g.fillEllipse(area.expanded(2.4f * scale));
    }
    const auto top = enabled ? p.panel.brighter(0.18f + lift)
                             : p.panel.brighter(0.05f + lift);
    const auto bottom = enabled ? p.background.brighter(0.09f)
                                : p.background;
    g.setGradientFill(juce::ColourGradient(
        top, centre.x, area.getY(),
        bottom, centre.x, area.getBottom(), false));
    g.fillEllipse(area);

    // Outer selection ring, then an extremely restrained inner specular rim.
    g.setColour(enabled ? p.accent.withAlpha(down ? 0.82f : 0.94f)
                        : (hover ? p.boneDim.brighter(0.55f) : p.line.brighter(0.32f)));
    g.drawEllipse(area, std::max(1.4f, 1.65f * scale));
    g.setColour(enabled ? p.accent.withAlpha(0.19f + 0.25f * glowValue)
                        : p.boneDim.withAlpha(0.07f));
    g.drawEllipse(area.reduced(3.2f * scale), std::max(0.7f, 0.85f * scale));

    // A short low-key reflection makes pads feel like controls, not outlines.
    const float y = area.getY() + area.getHeight() * 0.20f;
    g.setColour(p.bone.withAlpha(enabled ? 0.16f : 0.07f));
    g.drawLine(centre.x - area.getWidth() * 0.13f, y,
               centre.x + area.getWidth() * 0.13f, y,
               std::max(0.75f, 0.95f * scale));
    if (glowValue > 0.015f) {
        g.setColour(p.accent.withAlpha(0.16f + 0.45f * glowValue));
        g.drawEllipse(area.expanded(1.5f * scale), std::max(1.2f, 1.55f * scale));
    }

    g.setColour(enabled ? p.bone : p.boneDim.withAlpha(0.88f));
    g.setFont(juce::Font(juce::FontOptions(
        juce::jlimit(10.0f, 29.0f, float(getHeight()) * 0.38f),
        enabled ? juce::Font::bold : juce::Font::plain)));
    g.drawText(getButtonText(), getLocalBounds().reduced(2),
               juce::Justification::centred, true);
}
void SkellyTuneEditor::PitchButton::mouseDoubleClick(const juce::MouseEvent&) {
    if (onReset) onReset();
}

void SkellyTuneEditor::LiveReadout::paint(juce::Graphics& g) {
    const auto& p = *palette_;
    const float w = float(getWidth()), sc = w / 100.0f;
    const float a = juce::jlimit(0.0f, 1.0f, alpha_);
    g.setColour(p.boneDim);
    g.setFont(juce::Font(juce::FontOptions(10.5f * sc, juce::Font::bold)));
    g.drawText(input_ ? "INPUT" : "TARGET", 0, 0, int(w), int(15.0f * sc), juce::Justification::centred);

    g.setColour((active_ > 0.15f ? p.accent : p.bone).withAlpha(0.22f + 0.78f * a));
    g.setFont(juce::Font(juce::FontOptions(32.0f * sc, juce::Font::bold)));
    g.drawText(note_, 0, int(17.0f * sc), int(w), int(40.0f * sc), juce::Justification::centred);

    g.setColour(p.boneDim.withAlpha(0.30f + 0.70f * a));
    g.setFont(juce::Font(juce::FontOptions(12.5f * sc, juce::Font::plain)));
    g.drawText(a > 0.05f ? detail_ : juce::String("--"), 0, int(58.0f * sc), int(w), int(18.0f * sc),
               juce::Justification::centred);

    // Horizontal meter: centre tick = in tune. INPUT shows a dot (how far you are from the target);
    // TARGET shows a bar (how far, and which way, the tuner is moving you).
    const float y = 92.0f * sc, x0 = 8.0f * sc, x1 = w - 8.0f * sc, cx = 0.5f * (x0 + x1);
    g.setColour(p.line);
    g.drawLine(x0, y, x1, y, 2.0f * sc);
    g.setColour(p.line.brighter(0.5f));
    g.drawLine(cx, y - 6.0f * sc, cx, y + 6.0f * sc, 1.5f * sc);
    const float mx = cx + juce::jlimit(-1.0f, 1.0f, meter_) * (x1 - x0) * 0.5f;
    if (input_) {
        g.setColour(p.accent.withAlpha(0.14f * a));
        g.fillEllipse(mx - 8.0f * sc, y - 8.0f * sc, 16.0f * sc, 16.0f * sc);
        g.setColour(p.accent.withAlpha(0.15f + 0.85f * a));
        g.fillEllipse(mx - 4.5f * sc, y - 4.5f * sc, 9.0f * sc, 9.0f * sc);
    } else {
        const float left = std::min(cx, mx), width = std::max(2.0f * sc, std::abs(mx - cx));
        g.setColour(p.accent.withAlpha(0.22f * a * (0.4f + 0.6f * active_)));
        g.fillRoundedRectangle(left, y - 6.0f * sc, width, 12.0f * sc, 3.0f * sc);
        g.setColour(p.accent.withAlpha(0.20f + 0.80f * a));
        g.fillRoundedRectangle(left, y - 3.0f * sc, width, 6.0f * sc, 3.0f * sc);
    }
    g.setColour(p.boneDim.withAlpha(0.75f));
    g.setFont(juce::Font(juce::FontOptions(9.0f * sc, juce::Font::plain)));
    g.drawText(input_ ? "FLAT" : "LOWER", int(x0), int(y + 9.0f * sc), int(w * 0.5f - x0), int(12.0f * sc),
               juce::Justification::centredLeft);
    g.drawText(input_ ? "SHARP" : "RAISE", int(w * 0.5f), int(y + 9.0f * sc), int(w * 0.5f - 8.0f * sc),
               int(12.0f * sc), juce::Justification::centredRight);
}

void SkellyTuneEditor::AlgorithmSelector::paint(juce::Graphics& g) {
    const auto& p = *palette_;
    const auto b = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(p.panel);
    g.fillRoundedRectangle(b, 7.0f);
    g.setColour(p.line.brighter(0.25f));
    g.drawRoundedRectangle(b, 7.0f, 1.0f);
    const int n = std::max(1, labels_.size());
    const float segmentWidth = b.getWidth() / float(n);
    g.setFont(juce::Font(juce::FontOptions(juce::jlimit(9.0f, 15.0f, b.getHeight() * 0.36f), juce::Font::bold)));
    for (int i = 0; i < labels_.size(); ++i) {
        const auto cell = juce::Rectangle<float>(b.getX() + float(i) * segmentWidth, b.getY(),
                                                 segmentWidth, b.getHeight()).reduced(2.5f);
        const bool on = i == selected_;
        if (on) {
            g.setColour(p.accent.withAlpha(0.20f));
            g.fillRoundedRectangle(cell, 5.0f);
            g.setColour(p.accent);
            g.drawRoundedRectangle(cell, 5.0f, 1.2f);
        } else if (i == hover_) {
            g.setColour(p.line.brighter(0.2f).withAlpha(0.6f));
            g.fillRoundedRectangle(cell, 5.0f);
        }
        g.setColour(on ? p.accent : p.boneDim);
        g.drawText(labels_[i], cell.toNearestInt(), juce::Justification::centred, false);
    }
}
void SkellyTuneEditor::AlgorithmSelector::mouseDown(const juce::MouseEvent& e) {
    const int index = segmentAt(e.position.x);
    if (index != selected_) {
        selected_ = index;
        repaint();
        if (onSelect) onSelect(index);
    }
}
void SkellyTuneEditor::AlgorithmSelector::mouseMove(const juce::MouseEvent& e) {
    const int index = segmentAt(e.position.x);
    if (index != hover_) { hover_ = index; repaint(); }
}

SkellyTuneEditor::SkellyTuneEditor(SkellyTuneRealtimeProcessor& p)
    : juce::AudioProcessorEditor(p), processor_(p), parameters_(p.parameters()) {
    speed_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    // Physical control increases clockwise/up. Visible 100..0 maps
    // one-to-one to new GUI 100..0ms; old 101..200ms states are preserved.
    speed_.setRange(0.0, 100.0, 0.01);
    speed_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    speed_.setRotaryParameters(juce::MathConstants<float>::pi * 1.23f,
                              juce::MathConstants<float>::pi * 2.77f, true);
    speed_.setName("Retune Speed");
    speed_.setComponentID("skelly.speed");
    speed_.setTooltip("Retune time: 100 = 100ms (slowest normal setting); "
                      "0 = 0ms (fastest hard tune). Clockwise/up moves toward 0. "
                      "Older 101-200ms project values remain intact and show 100+ LEGACY.");
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
    // The built-in SliderAttachment would wire clockwise increase to
    // MORE ms (= slower pitch correction). ParameterAttachment keeps the
    // original host APVTS parameter and supports an inverted GUI mapping
    // with host automation, gestures and preset recall.
    auto* retuneParameter = parameters_.getParameter("speed");
    jassert(retuneParameter != nullptr);
    speedAttachment_ = std::make_unique<juce::ParameterAttachment>(
        *retuneParameter, [this](float savedMs) {
            theme_.hostSpeedMs = savedMs;
            speed_.setValue(clockwiseFastnessForMs(savedMs), juce::dontSendNotification);
            speed_.repaint();
        });
    speed_.onDragStart = [this] {
        speedDragging_ = true;
        speedAttachment_->beginGesture();
    };
    speed_.onValueChange = [this] {
        if (!speedAttachment_) return;
        const auto ms = millisecondsForClockwiseFastness(speed_.getValue());
        theme_.hostSpeedMs = ms;
        speed_.repaint();
        if (speedDragging_) speedAttachment_->setValueAsPartOfGesture(ms);
        else speedAttachment_->setValueAsCompleteGesture(ms);
    };
    speed_.onDragEnd = [this] {
        speedAttachment_->endGesture();
        speedDragging_ = false;
    };
    speedAttachment_->sendInitialUpdate();
    a4Attachment_ = std::make_unique<State::SliderAttachment>(
        parameters_, "a4", a4_);

    // Algorithm switch. REGULAR = the standard engine. TESTTUNE = stateless nearest-note snapping in the
    // period domain. META (test builds only) = the Meta engine.
    algorithm_.setName("Algorithm");
    algorithm_.setComponentID("skelly.algorithm");
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    algorithm_.setSegments(
        {"REGULAR", "TESTTUNE", "META"},
        {"Regular: the standard algorithm.",
         "TestTune: snaps to the nearest in-scale note (measured in samples per cycle) with no hysteresis.",
         "Meta (TEST BUILD ONLY): period-table snap + smoother + grain resampler."});
#else
    algorithm_.setSegments(
        {"REGULAR", "TESTTUNE"},
        {"Regular: the standard algorithm.",
         "TestTune: snaps to the nearest in-scale note (measured in samples per cycle) with no hysteresis."});
#endif
    algorithm_.onSelect = [this](int index) { chooseAlgorithm(index); };
    algorithm_.setSelected(currentAlgorithm());
    addAndMakeVisible(algorithm_);
    // Live feedback: what you are singing (left) and what the tuner is moving it to (right).
    addAndMakeVisible(inputReadout_);
    addAndMakeVisible(targetReadout_);

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
    startTimerHz(30);   // smooth live feedback; refreshNotes() returns early when nothing changed
}
SkellyTuneEditor::~SkellyTuneEditor() {
    speed_.onValueChange = nullptr;
    speed_.onDragStart = nullptr;
    speed_.onDragEnd = nullptr;
    speedAttachment_.reset();
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
    // Proportionally grow the editable A4 readout with the rest of the
    // editor. Without this the +/- buttons filled most of the wide layout.
    a4_.setTextBoxStyle(juce::Slider::TextBoxLeft, false,
                        std::max(83, juce::roundToInt(83.0f*sx)),
                        std::max(30, juce::roundToInt(30.0f*sy)));
    place(algorithm_, 208, 19, 208, 28);            // header row, right of the title
    place(inputReadout_, 19, 150, 100, 130);        // left of the dial
    place(targetReadout_, 313, 150, 100, 130);      // right of the dial
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
    g.setFont(juce::Font(juce::FontOptions(9.5f,juce::Font::bold)));
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    g.drawText("MINI / META LAB",60,46,135,14,juce::Justification::centredLeft);
#else
    g.drawText("MINI",60,46,60,14,juce::Justification::centredLeft);
#endif
    g.setColour(palette_.line);
    g.drawLine(16.0f,63.0f,416.0f,63.0f,1.0f);

    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(11.5f,juce::Font::bold)));
    g.drawText("KEY",20,76,89,16,juce::Justification::centredLeft);
    g.drawText("SCALE",121,76,145,16,juce::Justification::centredLeft);
    g.drawText("A4",292,76,110,16,juce::Justification::centredLeft);

    // Keep Mini's approved dark/neon identity, but soften the instrument
    // surface with gentle gradient depth and a restrained top highlight.
    const auto panelBounds = juce::Rectangle<float>(13,137,406,178);
    g.setGradientFill(juce::ColourGradient(
        panel.brighter(0.065f), 13.0f, 137.0f,
        panel.darker(0.075f), 419.0f, 315.0f, false));
    g.fillRoundedRectangle(panelBounds,9.0f);
    g.setColour(palette_.line.withAlpha(0.72f));
    g.drawRoundedRectangle(panelBounds,9.0f,0.9f);
    g.setColour(accent.withAlpha(0.18f));
    g.drawLine(24.0f, 138.5f, 408.0f, 138.5f, 0.8f);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(12.0f,juce::Font::bold)));
    g.drawText("RETUNE SPEED",137,314,158,19,juce::Justification::centred);

    // Slightly recessed note-bank surface anchors the two chromatic rows
    // without changing their hit targets or affecting click automation.
    const auto noteBank = juce::Rectangle<float>(13,333,406,76);
    g.setGradientFill(juce::ColourGradient(
        panel.brighter(0.015f), 13.0f,333.0f,
        bg, 13.0f,409.0f,false));
    g.fillRoundedRectangle(noteBank,8.0f);
    g.setColour(palette_.line.withAlpha(0.32f));
    g.drawRoundedRectangle(noteBank.reduced(0.5f),8.0f,0.8f);

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
void SkellyTuneEditor::timerCallback() {
    refreshNotes();
    updateLiveFeedback();
}
int SkellyTuneEditor::currentAlgorithm() const {
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    if (parameters_.getRawParameterValue("meta")->load() > 0.5f) return 2;
#endif
    return parameters_.getRawParameterValue("testTune")->load() > 0.5f ? 1 : 0;
}
void SkellyTuneEditor::chooseAlgorithm(int index) {
    // Exactly one algorithm is active: REGULAR = neither flag set.
    setBoolParameter(parameters_, "testTune", index == 1);
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    setBoolParameter(parameters_, "meta", index == 2);
#endif
}
void SkellyTuneEditor::updateLiveFeedback() {
    // Follow automation / preset recall.
    algorithm_.setSelected(currentAlgorithm());

    const float hz = processor_.displayedPitchHz();
    const int target = processor_.displayedTargetNote();
    const float correction = processor_.displayedCorrectionCents();
    const bool voiced = hz > 40.0f && processor_.displayedConfidence() >= 0.55f && target >= 0;

    voicedFade_ += ((voiced ? 1.0f : 0.0f) - voicedFade_) * (voiced ? 0.55f : 0.18f);
    if (voiced) {
        const double a4 = parameters_.getRawParameterValue("a4")->load();
        const double inputMidi = 69.0 + 12.0 * std::log2(double(hz) / a4);
        const float deviation = float(juce::jlimit(-100.0, 100.0, (inputMidi - double(target)) * 100.0));
        dispDeviation_ += (deviation - dispDeviation_) * 0.5f;
        dispCorrection_ += (juce::jlimit(-100.0f, 100.0f, correction) - dispCorrection_) * 0.5f;
        lastInputName_ = noteLabel(juce::roundToInt(inputMidi));
        lastTargetName_ = noteLabel(target);
        lastTarget_ = target;
    }
    const float activityGoal = voiced ? juce::jlimit(0.0f, 1.0f, std::abs(correction) / 25.0f) : 0.0f;
    activity_ += (activityGoal - activity_) * (activityGoal > activity_ ? 0.5f : 0.15f);

    inputReadout_.setState(voicedFade_, lastInputName_, signedCents(dispDeviation_),
                           dispDeviation_ / 100.0f, activity_);
    targetReadout_.setState(voicedFade_, lastTargetName_, signedCents(dispCorrection_),
                            dispCorrection_ / 100.0f, activity_);

    // Dial glow follows how hard the tuner is working.
    if (std::abs(theme_.activity - activity_) > 0.015f) {
        theme_.activity = activity_;
        speed_.repaint();
    }
    // The live target note lights up on the keyboard: dim while locked, bright while correcting.
    for (int i = 0; i < 12; ++i) {
        const bool isTarget = voicedFade_ > 0.05f && lastTarget_ >= 0 && (lastTarget_ % 12) == i;
        const float goal = isTarget ? voicedFade_ * (0.35f + 0.65f * activity_) : 0.0f;
        keyGlow_[size_t(i)] += (goal - keyGlow_[size_t(i)]) * 0.35f;
        if (keyGlow_[size_t(i)] < 0.01f) keyGlow_[size_t(i)] = 0.0f;
        if (std::abs(keyGlow_[size_t(i)] - notes_[size_t(i)]->glow) > 0.02f ||
            (keyGlow_[size_t(i)] == 0.0f && notes_[size_t(i)]->glow != 0.0f))
            notes_[size_t(i)]->setGlow(keyGlow_[size_t(i)]);
    }
}
