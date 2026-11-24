#include "PluginEditor.h"
#include "../dsp/ScaleMask.hpp"
#include "../dsp/GraphViewport.hpp"
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

    // An actual message-thread pitch trace lives inside the dial; it
    // represents recent detected cents from the chosen target, not a
    // fake waveform or audio-level animation. Both 432px and 1008px
    // editors preserve the same relative trace geometry.
    const float alpha = juce::jlimit(0.0f, 1.0f, voiceAlpha);
    const float conf = juce::jlimit(0.0f, 1.0f, confidence);
    const juce::String signal = alpha < 0.16f ? "LISTENING" :
        conf < 0.68f ? "ACQUIRING" :
        std::abs(deviationCents) <= 8.0f ? "ON NOTE" : "CORRECTING";
    g.setColour((alpha > 0.18f ? palette.accent : palette.boneDim)
                .withAlpha(alpha > 0.18f ? 0.92f : 0.84f));
    g.setFont(juce::Font(juce::FontOptions(
        juce::jlimit(9.8f, 24.0f, 10.5f * uiScale), juce::Font::bold)));
    g.drawText(signal, juce::Rectangle<int>(
        int(cx-radius)+innerMargin, int(cy)+juce::roundToInt(35.5f * uiScale),
        textWidth, juce::roundToInt(12.0f * uiScale)),
        juce::Justification::centred, true);

    const float left = cx - 42.0f * uiScale;
    const float right = cx + 42.0f * uiScale;
    const float traceY = cy + 56.0f * uiScale;
    const float traceH = 7.0f * uiScale;
    g.setColour(palette.line.withAlpha(0.65f));
    g.drawLine(left, traceY, right, traceY, std::max(0.8f, uiScale * 0.7f));
    if (trailCount >= 2) {
        juce::Path trace;
        for (int i = 0; i < 32; ++i) {
            const int index = (trailHead + i) % 32; // oldest to newest
            const float value = juce::jlimit(-1.0f, 1.0f, pitchTrail[size_t(index)]);
            const float px = left + (right-left)*float(i)/31.0f;
            const float py = traceY - value*traceH;
            if (i == 0) trace.startNewSubPath(px, py);
            else trace.lineTo(px, py);
        }
        g.setColour(palette.bone.withAlpha(0.18f + 0.66f * alpha));
        g.strokePath(trace, juce::PathStrokeType(
            std::max(1.1f, 1.35f * uiScale),
            juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const int mostRecent = (trailHead + 31) % 32;
        const float markerY = traceY -
            juce::jlimit(-1.0f, 1.0f, pitchTrail[size_t(mostRecent)])*traceH;
        g.setColour(palette.accent.withAlpha(0.22f + 0.72f*alpha));
        g.fillEllipse(right - 1.75f*uiScale, markerY - 1.75f*uiScale,
                      3.5f*uiScale, 3.5f*uiScale);
    }
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

    // A precise, dimensional, playable note pad rather than an empty ring.
    // Live-input and live-target states must stay plainly visible at the
    // minimum 432px Mini editor size, not only on a high-DPI display.
    const float inputLight = juce::jlimit(0.0f, 1.0f, inputGlow);
    if (enabled || glowValue > 0.015f || inputLight > 0.015f) {
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
    // Paint a clearly lit central lens as well as the tiny LEDs.
    // Input = pale white/mint; corrected target = neon green.
    // Deliberately moderate alpha to keep note text legible and the
    // approved dark aesthetic intact, even when both lights coincide.
    if (glowValue > 0.015f) {
        g.setColour(p.accent.withAlpha(0.12f + 0.40f * glowValue));
        g.fillEllipse(area.reduced(3.5f * scale));
    }
    if (inputLight > 0.015f) {
        g.setColour(p.bone.withAlpha(0.09f + 0.31f * inputLight));
        g.fillEllipse(area.reduced(4.0f * scale));
    }

    // Outer selection ring, then an extremely restrained inner specular rim.
    const auto outerInk = glowValue > 0.03f ? p.accent :
        inputLight > 0.03f ? p.bone :
        enabled ? p.accent.withAlpha(down ? 0.82f : 0.94f) :
        (hover ? p.boneDim.brighter(0.55f) : p.line.brighter(0.32f));
    g.setColour(outerInk);
    g.drawEllipse(area, std::max(1.4f,
        (glowValue > 0.03f || inputLight > 0.03f ? 2.5f : 1.65f) * scale));
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
        // Green LED at the bottom: the *output target* of tuning.
        const float led = std::max(2.7f, 4.7f * scale);
        g.setColour(p.accent.withAlpha(0.55f + 0.45f * glowValue));
        g.fillEllipse(centre.x - led * 0.5f, area.getBottom() - 1.1f * led,
                      led, led);
    }
    if (inputLight > 0.015f) {
        // Pale inner rim and top LED: incoming pitch, distinct from
        // the green output target. Both show when they are the same note.
        g.setColour(p.bone.withAlpha(0.12f + 0.45f * inputLight));
        g.drawEllipse(area.reduced(1.8f * scale), std::max(1.1f, 1.4f * scale));
        const float led = std::max(2.5f, 4.4f * scale);
        g.setColour(p.bone.withAlpha(0.40f + 0.60f * inputLight));
        g.fillEllipse(centre.x - led * 0.5f, area.getY() + 1.0f * scale,
                      led, led);
    }

    g.setColour((enabled || inputLight > 0.04f || glowValue > 0.04f)
        ? p.bone : p.boneDim.withAlpha(0.88f));
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
    g.setFont(juce::Font(juce::FontOptions(11.2f * sc, juce::Font::bold)));
    g.drawText(input_ ? "INPUT" : "TARGET", 0, 0, int(w), int(15.0f * sc), juce::Justification::centred);

    g.setColour((active_ > 0.15f ? p.accent : p.bone).withAlpha(0.40f + 0.60f * a));
    g.setFont(juce::Font(juce::FontOptions(32.0f * sc, juce::Font::bold)));
    g.drawText(note_, 0, int(17.0f * sc), int(w), int(40.0f * sc), juce::Justification::centred);

    g.setColour(p.boneDim.withAlpha(0.48f + 0.52f * a));
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
    g.setColour(p.boneDim.brighter(0.13f).withAlpha(0.90f));
    g.setFont(juce::Font(juce::FontOptions(9.5f * sc, juce::Font::plain)));
    g.drawText(input_ ? "FLAT" : "LOWER", int(x0), int(y + 9.0f * sc), int(w * 0.5f - x0), int(12.0f * sc),
               juce::Justification::centredLeft);
    g.drawText(input_ ? "SHARP" : "RAISE", int(w * 0.5f), int(y + 9.0f * sc), int(w * 0.5f - 8.0f * sc),
               int(12.0f * sc), juce::Justification::centredRight);
    // Five detector-confidence LEDs under the cent deviation scale.
    // Actual processor meters drive them; no animation on the DSP thread.
    const float confidence = juce::jlimit(0.0f, 1.0f, confidence_);
    const float lampY = 118.0f * sc, lampW = 11.0f * sc, lampGap = 4.0f * sc;
    const float width = 5.0f * lampW + 4.0f * lampGap;
    for (int i = 0; i < 5; ++i) {
        const float x = (w - width) * 0.5f + float(i) * (lampW + lampGap);
        const float on = juce::jlimit(0.0f, 1.0f,
                                    confidence * 5.0f - float(i));
        g.setColour(p.line.withAlpha(0.8f));
        g.fillRoundedRectangle(x, lampY, lampW, 3.0f * sc, 1.5f * sc);
        if (on > 0.01f) {
            g.setColour((input_ ? p.bone : p.accent).withAlpha(
                0.18f + 0.78f * on * a));
            g.fillRoundedRectangle(x, lampY, lampW, 3.0f * sc, 1.5f * sc);
        }
    }
}

void SkellyTuneEditor::AlgorithmSelector::paint(juce::Graphics& g) {
    const auto& p = *palette_;
    const auto b = getLocalBounds().toFloat().reduced(0.5f);
    g.setGradientFill(juce::ColourGradient(
        p.panel.brighter(0.10f), b.getX(), b.getY(),
        p.panel.darker(0.14f), b.getRight(), b.getBottom(), false));
    g.fillRoundedRectangle(b, 7.0f);
    g.setColour(p.line.brighter(0.25f));
    g.drawRoundedRectangle(b, 7.0f, 1.0f);
    const int n = std::max(1, labels_.size());
    const float segmentWidth = b.getWidth() / float(n);
    g.setFont(juce::Font(juce::FontOptions(juce::jlimit(10.0f, 15.0f, b.getHeight() * 0.39f), juce::Font::bold)));
    for (int i = 0; i < labels_.size(); ++i) {
        const auto cell = juce::Rectangle<float>(b.getX() + float(i) * segmentWidth, b.getY(),
                                                 segmentWidth, b.getHeight()).reduced(2.5f);
        const bool on = i == selected_;
        if (on) {
            g.setGradientFill(juce::ColourGradient(
                p.accent.withAlpha(0.25f), cell.getX(), cell.getY(),
                p.accent.withAlpha(0.10f), cell.getX(), cell.getBottom(), false));
            g.fillRoundedRectangle(cell, 5.0f);
            g.setColour(p.accent.withAlpha(0.76f));
            g.drawRoundedRectangle(cell, 5.0f, 1.2f);
            g.setColour(p.accent);
            g.fillRoundedRectangle(cell.getX() + 6.0f,
                cell.getBottom() - 3.0f,
                std::max(1.0f, cell.getWidth() - 12.0f), 1.7f, 0.7f);
        } else if (i == hover_) {
            g.setColour(p.line.brighter(0.2f).withAlpha(0.6f));
            g.fillRoundedRectangle(cell, 5.0f);
        }
        g.setColour(on ? p.accent : p.boneDim.brighter(0.34f));
        g.drawText(labels_[i], cell.toNearestInt(), juce::Justification::centred, false);
    }
}
void SkellyTuneEditor::AlgorithmSelector::mouseDown(const juce::MouseEvent& e) {
    if(!e.mods.isLeftButtonDown())return; // context click does not change DSP
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
    // Double-click is a direct UI convenience; the saved host default
    // remains 25ms and the APVTS ID/range are untouched.
    speed_.setDoubleClickReturnValue(true, clockwiseFastnessForMs(25.0f));
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
                      "Older 101-200ms project values remain intact and show 100+ LEGACY. "
                      "Double-click the dial to restore the factory 25ms.");
    speed_.setLookAndFeel(&theme_);
    addAndMakeVisible(speed_);

    // One editable A4 value with compact increment/decrement controls.
    a4_.setSliderStyle(juce::Slider::IncDecButtons);
    a4_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 92, 30);
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
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    playgroundSelector_.setName("Research engine");
    playgroundSelector_.setComponentID("skelly.algorithm");
    playgroundSelector_.setLookAndFeel(&theme_);
    styleCombo(playgroundSelector_,palette_);
    for(int i=0;i<skelly::playground::modeCount;++i)
        playgroundSelector_.addItem(skelly::playground::names[size_t(i)],i+1);
    playgroundSelector_.setSelectedId(currentAlgorithm()+1,juce::dontSendNotification);
    playgroundSelector_.setTooltip("Select the original DSP engine. Common PDC for comparison; native delay shown below.");
    playgroundSelector_.onChange=[this]{chooseAlgorithm(playgroundSelector_.getSelectedId()-1);};
    addAndMakeVisible(playgroundSelector_);
#else
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
#endif
    // Live feedback: what you are singing (left) and what the tuner is moving it to (right).
    inputReadout_.setComponentID("skelly.inputReadout");
    targetReadout_.setComponentID("skelly.targetReadout");
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
    // Optional visually rich analyzer: collapsed by default so original
    // 432x414 Mini interface, host preset and automation remain identical.
    analyzerToggle_.setComponentID("skelly.analyzerToggle");
    analyzerToggle_.setName("Expand pitch graph");
    analyzerToggle_.setTooltip("Pitch editor: drag notes, mouse wheel pans pitches, "
                               "double-click follows; UNDO/CLEAR manage edits.");
    analyzerToggle_.setColour(juce::TextButton::buttonColourId,
                             palette_.panel.brighter(0.10f));
    analyzerToggle_.setColour(juce::TextButton::buttonOnColourId,
                             palette_.accent.withAlpha(0.25f));
    analyzerToggle_.setColour(juce::TextButton::textColourOffId, palette_.bone);
    analyzerToggle_.setColour(juce::TextButton::textColourOnId, palette_.accent);
    analyzerToggle_.setClickingTogglesState(false);
    analyzerToggle_.onClick = [this] { toggleAnalyzer(); };
    addAndMakeVisible(analyzerToggle_);
    clearPitchEdits_.setComponentID("skelly.clearPitchEdits");
    clearPitchEdits_.setName("Clear manual pitch edits");
    clearPitchEdits_.setLookAndFeel(&theme_);
    clearPitchEdits_.setTooltip("Delete all recorded note edits in this project.");
    clearPitchEdits_.setColour(juce::TextButton::buttonColourId,palette_.panel.brighter(0.08f));
    clearPitchEdits_.setColour(juce::TextButton::textColourOffId,palette_.boneDim.brighter(0.25f));
    clearPitchEdits_.onClick=[this] {
        if(!processor_.pitchEdits().hasEdits())return;
        processor_.pitchEdits().clear();
        processor_.pitchEditStateChanged();
        clearPitchEdits_.setEnabled(false);
        undoPitchEdits_.setEnabled(false);
        repaint();
    };
    addAndMakeVisible(clearPitchEdits_);
    clearPitchEdits_.setVisible(false);
    // One-click reversal is safer than clearing all pitch edits.
    undoPitchEdits_.setComponentID("skelly.undoPitchEdits");
    undoPitchEdits_.setName("Undo latest recorded pitch edit");
    undoPitchEdits_.setTooltip("Undo the last note edit; earlier edits remain.");
    undoPitchEdits_.setLookAndFeel(&theme_);
    undoPitchEdits_.setColour(juce::TextButton::buttonColourId,
                             palette_.panel.brighter(0.08f));
    undoPitchEdits_.setColour(juce::TextButton::textColourOffId,palette_.boneDim.brighter(0.25f));
    undoPitchEdits_.onClick=[this] {
        if(processor_.pitchEdits().undoLast())
            processor_.pitchEditStateChanged();
        const bool hasEdits=processor_.pitchEdits().hasEdits();
        undoPitchEdits_.setEnabled(hasEdits);
        clearPitchEdits_.setEnabled(hasEdits);
        repaint();
    };
    addAndMakeVisible(undoPitchEdits_);
    undoPitchEdits_.setVisible(false);
    clearPitchEdits_.setEnabled(processor_.pitchEdits().hasEdits());
    undoPitchEdits_.setEnabled(processor_.pitchEdits().hasEdits());
    setResizable(true, true);
    setResizeLimits(432, 414, 1008, 966);
    if (auto* constraint = getConstrainer())
        constraint->setFixedAspectRatio(432.0 / 414.0);
    setSize(432, 414);
    startTimerHz(30);   // smooth live feedback; refreshNotes() returns early when nothing changed
}
SkellyTuneEditor::~SkellyTuneEditor() {
    analyzerToggle_.onClick = nullptr;
    clearPitchEdits_.onClick = nullptr;
    clearPitchEdits_.setLookAndFeel(nullptr);
    undoPitchEdits_.onClick = nullptr;
    undoPitchEdits_.setLookAndFeel(nullptr);
    speed_.onValueChange = nullptr;
    speed_.onDragStart = nullptr;
    speed_.onDragEnd = nullptr;
    speedAttachment_.reset();
    speed_.setLookAndFeel(nullptr);
    a4_.setLookAndFeel(nullptr);
    key_.setLookAndFeel(nullptr);
    scale_.setLookAndFeel(nullptr);
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    playgroundSelector_.onChange=nullptr;
    playgroundSelector_.setLookAndFeel(nullptr);
#endif
}
void SkellyTuneEditor::toggleAnalyzer() {
    // A collapsed graph must not leave a stale pending drag.
    graphEditing_=false;
    graphDragging_=false;
    graphGesture_.release();
    analyzerExpanded_ = !analyzerExpanded_;
    clearPitchEdits_.setVisible(analyzerExpanded_);
    undoPitchEdits_.setVisible(analyzerExpanded_);
    analyzerToggle_.setButtonText(analyzerExpanded_ ? "COMPACT" : "ANALYZE");
    analyzerToggle_.setTooltip(analyzerExpanded_ ?
        "Drag notes while playing/recording: the selected phrase holds still "
        "while new pitch keeps tracking; release to store an edit for replay." :
        "Expand the live pitch monitor; supported engines can replay manual note edits.");
    // Change only editor dimensions. This setting isn't in saved APVTS
    // state and can never invalidate an old FL Studio project.
    const int referenceHeight = analyzerExpanded_ ? 700 : 414;
    const int originalWidth = getWidth();
    // JUCE's setResizeLimits() can immediately enforce the MINIMUM size.
    // Change the aspect ratio FIRST and preserve the original width from
    // BEFORE the new 540px minimum is applied. Otherwise the old 432:414
    // constraint widens the editor to 563x704 when opening the analyzer.
    if (auto* constraint = getConstrainer())
        constraint->setFixedAspectRatio(432.0 / double(referenceHeight));
    setResizeLimits(432, referenceHeight, 1008,
                    int(std::lround(1008.0 * referenceHeight / 432.0)));
    setSize(originalWidth, int(std::lround(
        double(originalWidth) * double(referenceHeight) / 432.0)));
    resized();
    repaint();
}

void SkellyTuneEditor::resized() {
    const float sx = float(getWidth()) / 432.0f;
    const float sy = float(getHeight()) / (analyzerExpanded_ ? 700.0f : 414.0f);
    const auto place = [sx,sy](juce::Component& c, float x, float y, float w, float h) {
        c.setBounds(juce::roundToInt(x*sx), juce::roundToInt(y*sy),
            std::max(1,juce::roundToInt(w*sx)), std::max(1,juce::roundToInt(h*sy)));
    };
    // Proportionally grow the editable A4 readout with the rest of the
    // editor. Without this the +/- buttons filled most of the wide layout.
    a4_.setTextBoxStyle(juce::Slider::TextBoxLeft, false,
                        std::max(92, juce::roundToInt(92.0f*sx)),
                        std::max(30, juce::roundToInt(30.0f*sy)));
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    place(playgroundSelector_, 208, 19, 208, 28);
#else
    place(algorithm_, 208, 19, 208, 28);
#endif
    place(analyzerToggle_, 139, 46, 64, 16);
    // Collapsed control must not retain an off-window hit target.
    if(analyzerExpanded_) {
        place(clearPitchEdits_,354,419,53,22);
        place(undoPitchEdits_,299,419,50,22);
    } else {
        clearPitchEdits_.setBounds(0,0,1,1);
        undoPitchEdits_.setBounds(0,0,1,1);
    }
    place(inputReadout_, 19, 150, 100, 130);        // left of the dial
    place(targetReadout_, 313, 150, 100, 130);      // right of the dial
    // 12px matched gutters for the complete tuning-control row.
    place(key_, 18, 95, 94, 33);
    place(scale_, 124, 95, 150, 33);
    place(a4_, 286, 95, 126, 33);
    place(speed_, 127, 137, 178, 178);
    constexpr int naturals[] = {0,2,4,5,7,9,11};
    constexpr int accidentals[] = {1,3,6,8,10};
    // Symmetric 33px/34px outer margins: previous bank drifted 11px left.
    // Accidentals remain centred between their natural-note neighbours.
    constexpr float normalX[] = {33,88,143,198,253,308,363};
    constexpr float blackX[]  = {61,116,226,281,336};
    for (int i=0; i<7; ++i)
        place(*notes_[size_t(naturals[i])], normalX[i], 369, 35, 35);
    for (int i=0; i<5; ++i)
        place(*notes_[size_t(accidentals[i])], blackX[i], 336, 33, 33);
}
void SkellyTuneEditor::paint(juce::Graphics& g) {
    const auto bg=palette_.background, panel=palette_.panel;
    const auto accent=palette_.accent, text=palette_.bone, muted=palette_.boneDim;
    g.fillAll(bg);
    juce::Graphics::ScopedSaveState save(g);
    g.addTransform(juce::AffineTransform::scale(
        float(getWidth())/432.0f,
        float(getHeight())/(analyzerExpanded_ ? 700.0f : 414.0f)));
    g.setColour(palette_.line);
    g.drawRoundedRectangle(juce::Rectangle<float>(5,5,422,
        analyzerExpanded_ ? 690.0f : 404.0f),10.0f,1.0f);

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
    // SKELLYTUNE reads as one word, while TUNE remains green.
    g.drawText("TUNE",131,17,77,31,juce::Justification::centredLeft,true);
    g.setColour(muted.brighter(0.24f));
    g.setFont(juce::Font(juce::FontOptions(10.5f,juce::Font::bold)));
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    g.drawText("PLAYGROUND / "+juce::String(skelly::playground::modeCount),60,46,145,14,juce::Justification::centredLeft);
    const double sr=processor_.getSampleRate();
    if(sr>0.0){
        const int active=processor_.playgroundActiveMode();
        const auto native=processor_.playgroundNativeDelay(active);
        const juce::String delay=active!=currentAlgorithm()?"SWITCHING":juce::String(1000.*native/sr,2)+"ms NATIVE";
        g.drawText(delay,208,46,123,14,juce::Justification::centredLeft);
    }
#elif defined(SKELLY_META_TEST) && SKELLY_META_TEST
    g.drawText("MINI / LAB",60,46,78,14,juce::Justification::centredLeft);
#else
    g.drawText("MINI",60,46,60,14,juce::Justification::centredLeft);
#endif
    // Compact signal feedback under the algorithm selector. The header
    // used to be inert; it now confirms audio tracking without taking
    // controls or keyboard hit targets away from the user.
    const bool signal = voicedFade_ > 0.25f && liveConfidence_ >= 0.55f;
    g.setColour((signal ? accent : muted).withAlpha(signal ? 0.95f : 0.73f));
    g.fillEllipse(336.0f,51.0f,5.5f,5.5f);
    g.setColour(signal ? text : muted.brighter(0.18f));
    g.setFont(juce::Font(juce::FontOptions(10.3f,juce::Font::bold)));
    g.drawText(signal ? "SIGNAL LOCK" : "NO SIGNAL",346,45,70,15,
               juce::Justification::centredRight,true);
    g.setColour(palette_.line);
    g.drawLine(16.0f,63.0f,416.0f,63.0f,1.0f);

    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(11.5f,juce::Font::bold)));
    g.drawText("KEY",20,76,92,16,juce::Justification::centredLeft);
    g.drawText("SCALE",126,76,148,16,juce::Justification::centredLeft);
    g.drawText("A4",288,76,120,16,juce::Justification::centredLeft);

    // Keep Mini's approved dark/neon identity, but soften the instrument
    // surface with gentle gradient depth and a restrained top highlight.
    const auto panelBounds = juce::Rectangle<float>(13,137,406,178);
    g.setGradientFill(juce::ColourGradient(
        panel.brighter(0.065f), 13.0f, 137.0f,
        panel.darker(0.075f), 419.0f, 315.0f, false));
    g.fillRoundedRectangle(panelBounds,9.0f);
    g.setColour(palette_.line.withAlpha(0.72f));
    g.drawRoundedRectangle(panelBounds,9.0f,0.9f);
    // Detector-driven top border gives instant, non-distracting signal
    // activity while preserving the Mini's restrained neon aesthetic.
    g.setColour(palette_.line.withAlpha(0.72f));
    g.drawLine(24.0f, 138.5f, 408.0f, 138.5f, 1.1f);
    g.setColour(accent.withAlpha(0.18f + liveConfidence_ * 0.54f));
    g.drawLine(24.0f, 138.5f,
               24.0f + 384.0f * juce::jlimit(0.0f,1.0f,liveConfidence_),
               138.5f, 1.4f);
    g.setColour(muted);
    g.setFont(juce::Font(juce::FontOptions(12.3f,juce::Font::bold)));
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
    // Set ink again after drawing the recessed note bank: its dim border
    // colour must not leak into the ENABLED/CUSTOM NOTES heading.
    g.setColour(muted.brighter(0.34f));
    g.setFont(juce::Font(juce::FontOptions(10.5f,juce::Font::bold)));
    g.drawText(custom ? "CUSTOM NOTES" : "ENABLED NOTES", 16,318,117,15,
               juce::Justification::centredLeft);
    // Small live-input/target legend. Accessible even when no note is
    // present; feedback is driven by the detector, not UI animation.
    if (custom) {
        g.drawText("DOUBLE-CLICK TO RESET",299,318,119,15,
                   juce::Justification::centredRight,true);
    } else {
        g.setColour(text.withAlpha(0.74f));
        g.fillEllipse(280.0f,324.0f,5.0f,5.0f);
        g.setColour(muted);
        g.drawText("IN",288,318,21,15,juce::Justification::centredLeft);
        g.setColour(accent.withAlpha(0.82f));
        g.fillEllipse(330.0f,324.0f,5.0f,5.0f);
        g.setColour(muted);
        g.drawText("TARGET",339,318,70,15,juce::Justification::centredLeft);
    }

    if (analyzerExpanded_) {
        const auto panelRect = juce::Rectangle<float>(13.0f,416.0f,406.0f,274.0f);
        g.setGradientFill(juce::ColourGradient(
            panel.brighter(0.12f),13.0f,416.0f,
            panel.darker(0.14f),419.0f,690.0f,false));
        g.fillRoundedRectangle(panelRect,9.0f);
        g.setColour(palette_.line.withAlpha(0.80f));
        g.drawRoundedRectangle(panelRect,9.0f,0.85f);
        g.setColour(text);
        g.setFont(juce::Font(juce::FontOptions(12.8f,juce::Font::bold)));
        g.drawText("LIVE PITCH GRAPH",24,421,190,19,
                   juce::Justification::centredLeft);
        g.setColour(muted.brighter(0.23f));
        g.setFont(juce::Font(juce::FontOptions(10.0f)));
        g.drawText(processor_.displayedHostMs()<0 ? "NO DAW CLOCK" :
                   (graphEditing_ ? "EDIT " + noteLabel(graphEditMidi_) :
                    juce::String(double(processor_.displayedHostMs())/1000.0,2)+"s"),
                   197,422,92,17,juce::Justification::centredRight);
        // Eight semitones of real note-space. Dragged voiced lines
        // record host-timed pitch edits, never change published APVTS IDs.
        constexpr float top=451.0f,bottom=649.0f;
        constexpr float left=54.0f,right=405.0f;
        const auto noteY=[this](float midi) noexcept {
            return 550.0f-(midi-graphCenterMidi_)*((649.0f-451.0f)/8.0f);
        };
        g.setColour(bg.withAlpha(0.70f));
        g.fillRoundedRectangle(juce::Rectangle<float>(49.0f,446.0f,360.0f,208.0f),5.0f);
        for(int second=1;second<3;++second) {
            const float x=54.0f+(405.0f-54.0f)*float(second)/3.0f;
            g.setColour(palette_.line.withAlpha(0.26f));
            g.drawVerticalLine(juce::roundToInt(x),451.0f,649.0f);
            g.setFont(juce::Font(juce::FontOptions(9.3f)));
            g.setColour(muted.brighter(0.21f).withAlpha(0.88f));
            g.drawText(juce::String(second-3)+"s",
                       juce::roundToInt(x)-16,448,32,11,
                       juce::Justification::centred);
        }
        const int lo=juce::jlimit(0,127,int(std::floor(graphCenterMidi_))-5);
        const int hi=juce::jlimit(0,127,int(std::ceil(graphCenterMidi_))+5);
        for(int note=lo;note<=hi;++note) {
            const float y=noteY(float(note));
            if(y<top||y>bottom)continue;
            const bool targetRow=voicedFade_>0.05f&&note==lastTarget_;
            const bool octaveRow=(note%12)==0;
            if(octaveRow) {
                const float y0=std::max(top,y-10.0f), y1=std::min(bottom,y+10.0f);
                g.setColour(palette_.line.withAlpha(0.14f));
                g.fillRect(left,y0,right-left,std::max(0.0f,y1-y0));
            }
            g.setColour(targetRow?accent.withAlpha(0.48f):
                        palette_.line.withAlpha(octaveRow?0.69f:0.41f));
            g.drawLine(left,y,right,y,targetRow?1.55f:(octaveRow?1.0f:0.65f));
            g.setColour(targetRow?accent:(octaveRow?text:muted.brighter(0.26f)));
            g.setFont(juce::Font(juce::FontOptions(10.5f,juce::Font::bold)));
            g.drawText(noteLabel(note),17,int(y-7.0f),34,14,
                       juce::Justification::centredRight);
        }
        if (analyzerSamples_ >= 2 || graphGesture_.active()) {
            juce::Path incoming,corrected;
            const bool pinned=graphGesture_.active();
            const int n=pinned?graphGesture_.count():analyzerSamples_;
            const int first=(analyzerHead_+kAnalyzerHistory-n)%kAnalyzerHistory;
            const int64_t newestMs=pinned?graphGesture_.newestMs():
                analyzerHostMs_[size_t(
                    (analyzerHead_+kAnalyzerHistory-1)%kAnalyzerHistory)];
            bool inputOpen=false,outputOpen=false;
            for(int i=0;i<n;++i) {
                const int index=(first+i)%kAnalyzerHistory;
                const int64_t stamp=pinned?graphGesture_.timeAt(i):
                    analyzerHostMs_[size_t(index)];
                const float x=skelly::graphViewportX(
                    i,n,stamp,newestMs,left,right);
                if(i>0) {
                    const int previous=(index+kAnalyzerHistory-1)%kAnalyzerHistory;
                    const int64_t prevStamp=pinned?graphGesture_.timeAt(i-1):
                        analyzerHostMs_[size_t(previous)];
                    if(skelly::graphDiscontinuity(prevStamp,stamp)) {
                        inputOpen=false;
                        outputOpen=false;
                    }
                }
                const float a=pinned?graphGesture_.inputAt(i):
                    analyzerInputMidi_[size_t(index)];
                const float b=pinned?graphGesture_.outputAt(i):
                    analyzerOutputMidi_[size_t(index)];
                if(a>=0.0f&&std::isfinite(a)) {
                    const float y=noteY(a);
                    if(!inputOpen)incoming.startNewSubPath(x,y);
                    else incoming.lineTo(x,y);
                    inputOpen=true;
                }else inputOpen=false;
                if(b>=0.0f&&std::isfinite(b)) {
                    const float y=noteY(b);
                    if(!outputOpen)corrected.startNewSubPath(x,y);
                    else corrected.lineTo(x,y);
                    outputOpen=true;
                }else outputOpen=false;
            }
            {
                juce::Graphics::ScopedSaveState clipping(g);
                g.reduceClipRegion(juce::Rectangle<int>(int(left),int(top),
                    int(right-left)+1,int(bottom-top)+1));
                g.setColour(text.withAlpha(0.84f));
                g.strokePath(incoming,juce::PathStrokeType(1.6f));
                g.setColour(accent.withAlpha(0.90f));
                g.strokePath(corrected,juce::PathStrokeType(2.0f));
                const int latest=(analyzerHead_+kAnalyzerHistory-1)%kAnalyzerHistory;
                const float nowIn=pinned?graphGesture_.inputAt(n-1):
                    analyzerInputMidi_[size_t(latest)];
                const float nowOut=pinned?graphGesture_.outputAt(n-1):
                    analyzerOutputMidi_[size_t(latest)];
                if(nowIn>=0.0f && std::isfinite(nowIn)) {
                    const float y=noteY(nowIn);
                    g.setColour(text);
                    g.fillEllipse(right-3.0f,y-3.0f,6.0f,6.0f);
                }
                if(nowOut>=0.0f && std::isfinite(nowOut)) {
                    const float y=noteY(nowOut);
                    g.setColour(accent);
                    g.fillEllipse(right-3.5f,y-3.5f,7.0f,7.0f);
                }
            }
        }
        // Edited notes are real absolute transport-time retune targets.
        // Render their MIDI lanes only where visible in the current 3s view.
        if(processor_.graphEditsSupported() && (analyzerSamples_>=1 || graphGesture_.active())) {
            const int64_t lastTime=graphGesture_.active()?
                graphGesture_.newestMs():analyzerHostMs_[size_t(
                (analyzerHead_+kAnalyzerHistory-1)%kAnalyzerHistory)];
            // Saved edits can begin BEFORE the first GUI note sample after
            // opening the editor; show the whole 3s time viewport, not only
            // timestamps already observed by the 30Hz callback.
            if(lastTime>=0) {
                const int64_t earliest=std::max<int64_t>(0,lastTime-3000);
                juce::Graphics::ScopedSaveState clipEdits(g);
                g.reduceClipRegion(juce::Rectangle<int>(54,451,352,199));
                for(int i=0;i<skelly::PitchEditTimeline::kCapacity;++i) {
                    const auto edit=processor_.pitchEdits().get(i);
                    if(!edit.valid()||edit.endMs<earliest||
                       edit.startMs>lastTime)continue;
                    const float x1=skelly::graphViewportTimeX(
                        edit.startMs,lastTime,left,right);
                    const float x2=skelly::graphViewportTimeX(
                        edit.endMs,lastTime,left,right);
                    const float y=noteY(float(edit.targetMidi));
                    g.setColour(accent.withAlpha(0.13f));
                    g.fillRoundedRectangle(x1,y-6.0f,std::max(3.0f,x2-x1),12.0f,3.0f);
                    g.setColour(accent.brighter(0.25f));
                    g.drawLine(x1,y,x2,y,2.2f);
                    g.fillEllipse(x1-3.0f,y-3.0f,6.0f,6.0f);
                }
            }
        }
        if(graphEditing_ && graphEditMidi_>=24 &&
           graphEditMidi_!=graphEditInitialMidi_ && analyzerSamples_>0) {
            // A note edit is a finite time span, not a three-second global
            // pitch change. Display exactly the interval that mouse-up saves.
            juce::Graphics::ScopedSaveState preview(g);
            g.reduceClipRegion(juce::Rectangle<int>(54,451,352,199));
            const int64_t newest=graphGesture_.active()?
                graphGesture_.newestMs():analyzerHostMs_[size_t(
                (analyzerHead_+kAnalyzerHistory-1)%kAnalyzerHistory)];
            if(newest>=0) {
                const float x1=skelly::graphViewportTimeX(
                    graphEditStartMs_,newest,left,right);
                const float x2=skelly::graphViewportTimeX(
                    graphEditEndMs_,newest,left,right);
                if(x2>x1) {
                    const float y=noteY(float(graphEditMidi_));
                    g.setColour(accent.withAlpha(0.20f));
                    g.fillRoundedRectangle(x1,y-7.0f,x2-x1,14.0f,4.0f);
                    g.setColour(accent.brighter(0.45f));
                    g.drawLine(x1,y,x2,y,2.2f);
                }
            }
        }
        g.setColour(text.withAlpha(0.90f));
        g.setFont(juce::Font(juce::FontOptions(10.0f,juce::Font::bold)));
        g.drawText("WHITE  INPUT",24,660,104,15,juce::Justification::centredLeft);
        g.setColour(accent);
        g.drawText("GREEN  EST. OUTPUT",129,660,159,15,juce::Justification::centredLeft);
        g.setColour(muted.brighter(0.27f));
        g.drawText("TARGET  LINE",301,660,103,15,juce::Justification::centredRight);
        g.setColour(muted.brighter(0.26f));
        g.setFont(juce::Font(juce::FontOptions(10.2f)));
        // Without host time, this is a live monitor but cannot save edits.
        g.drawText(!processor_.graphEditsSupported()?"MONITOR ONLY  |  NOTE EDIT ADAPTER NOT SUPPORTED":processor_.displayedHostMs()<0
                       ? "LIVE MONITOR  |  DAW CLOCK REQUIRED TO EDIT"
                       : "DRAG NOTE  |  SCROLL PITCH  |  RIGHT-CLICK TO ERASE",
                   20,676,390,13,juce::Justification::centred);

    }
}
bool SkellyTuneEditor::graphHit(juce::Point<float> position) const noexcept {
    if (!analyzerExpanded_ || getWidth() <= 0 || getHeight() <= 0) return false;
    const float x=position.x*432.0f/float(getWidth());
    const float y=position.y*700.0f/float(getHeight());
    // Only the plotted data rectangle is interactive. The previous
    // enlarged hit box made clicks on the axis border erase end notes.
    return x>=54.0f&&x<=405.0f&&y>=451.0f&&y<=649.0f;
}
int SkellyTuneEditor::graphPickHistoryIndex(float x) const noexcept {
    if(analyzerSamples_<=0)return -1;
    const int n=analyzerSamples_;
    const int first=(analyzerHead_+kAnalyzerHistory-n)%kAnalyzerHistory;
    const int64_t newest=analyzerHostMs_[size_t(
        (analyzerHead_+kAnalyzerHistory-1)%kAnalyzerHistory)];
    int closestIndex=-1;
    float closestDistance=std::numeric_limits<float>::max();
    for(int i=0;i<n;++i) {
        const int index=(first+i)%kAnalyzerHistory;
        const int64_t stamp=analyzerHostMs_[size_t(index)];
        const float at=skelly::graphViewportX(
            i,n,stamp,newest,54.0f,405.0f);
        if(!skelly::graphEditableSampleHit(x,at,stamp,newest))continue;
        const float distance=std::abs(x-at);
        if(distance<closestDistance) {
            closestDistance=distance;
            closestIndex=i;
        }
    }
    return closestIndex;
}
void SkellyTuneEditor::mouseWheelMove(
    const juce::MouseEvent& e,const juce::MouseWheelDetails& details) {
    if(!graphHit(e.position)) {
        juce::AudioProcessorEditor::mouseWheelMove(e,details);
        return;
    }
    if(graphEditing_ || graphDragging_ ||
       !std::isfinite(details.deltaY) || details.deltaY==0.0f)return;
    graphCenterMidi_=skelly::graphWheelPan(graphCenterMidi_,details.deltaY);
    graphFollow_=false;
    analyzerToggle_.getProperties().set("graphFollow",false);
    analyzerToggle_.getProperties().set("graphCenter",double(graphCenterMidi_));
    repaint();
}
bool SkellyTuneEditor::startGraphNoteEdit(juce::Point<float> position) {
    if(!processor_.graphEditsSupported()||!graphHit(position)||analyzerSamples_<1||
       processor_.displayedHostMs()<0)return false;
    const float x=position.x*432.0f/float(getWidth());
    const float y=position.y*700.0f/float(getHeight());
    const int newestIndex=(analyzerHead_+kAnalyzerHistory-1)%kAnalyzerHistory;
    const int64_t newest=analyzerHostMs_[size_t(newestIndex)];
    if(newest<0)return false;
    // Prefer an already saved green edit line over the older estimated
    // output trace. The stored edit can exist before any second GUI tick.
    const auto begin=[this,position](int note,int64_t from,int64_t to) {
        if(note<24||note>107||from<0||to<=from)return false;
        graphEditStartMs_=from;
        graphEditEndMs_=to;
        graphEditMidi_=note;
        graphEditInitialMidi_=note;
        graphDragging_=false;
        graphEditing_=true;
        graphFollow_=false;
        analyzerToggle_.getProperties().set("graphFollow",false);
        graphDragStartY_=position.y;
        graphDragStartMidi_=float(note);
        graphGesture_.freeze(analyzerHead_,analyzerSamples_,
                             analyzerInputMidi_,analyzerOutputMidi_,analyzerHostMs_);
        repaint();
        return true;
    };
    const int64_t clickMs=skelly::graphViewportTimeMs(
        x,newest,54.0f,405.0f);
    if(clickMs<0)return false;
    const auto prior=processor_.pitchEdits().latestAt(clickMs);
    if(prior.valid()) {
        const float priorY=550.0f-
            (float(prior.targetMidi)-graphCenterMidi_)*198.0f/8.0f;
        if(std::abs(y-priorY)<=10.0f)
            return begin(prior.targetMidi,prior.startMs,prior.endMs);
    }

    if(analyzerSamples_<2)return false;
    const int pick=graphPickHistoryIndex(x);
    if(pick<0)return false;
    const int count=analyzerSamples_;
    const int first=(analyzerHead_+kAnalyzerHistory-count)%kAnalyzerHistory;
    const int index=(first+pick)%kAnalyzerHistory;
    const float pitch=analyzerOutputMidi_[size_t(index)];
    const int64_t stamp=analyzerHostMs_[size_t(index)];
    if(!std::isfinite(pitch)||pitch<0.0f||stamp<0)return false;
    const float pitchY=550.0f-(pitch-graphCenterMidi_)*198.0f/8.0f;
    if(std::abs(y-pitchY)>10.0f)return false;
    const int note=analyzerTargetMidi_[size_t(index)];
    if(note<0)return false;
    // This is a contiguous detector note, not an unrelated syllable.
    int64_t firstMs=stamp,lastMs=stamp;
    for(int i=pick-1;i>=0;--i) {
        const int j=(first+i)%kAnalyzerHistory;
        if(analyzerTargetMidi_[size_t(j)]!=note ||
           analyzerHostMs_[size_t(j)]<0 ||
           firstMs-analyzerHostMs_[size_t(j)]>110)break;
        firstMs=analyzerHostMs_[size_t(j)];
    }
    for(int i=pick+1;i<count;++i) {
        const int j=(first+i)%kAnalyzerHistory;
        if(analyzerTargetMidi_[size_t(j)]!=note ||
           analyzerHostMs_[size_t(j)]<0 ||
           analyzerHostMs_[size_t(j)]-lastMs>110)break;
        lastMs=analyzerHostMs_[size_t(j)];
    }
    const int64_t from=std::max<int64_t>(0,firstMs-35);
    const int64_t to=std::max(from+100,lastMs+65);
    return begin(note,from,to);
}
void SkellyTuneEditor::mouseDown(const juce::MouseEvent& e) {
    if(!graphHit(e.position))return;
    const float x=e.position.x*432.0f/float(getWidth());
    if(e.mods.isRightButtonDown()) {
        if(!processor_.graphEditsSupported())return;
        if(analyzerSamples_>=1 && processor_.displayedHostMs()>=0) {
            const int64_t newest=analyzerHostMs_[size_t(
                (analyzerHead_+kAnalyzerHistory-1)%kAnalyzerHistory)];
            const auto ms=skelly::graphViewportTimeMs(
                x,newest,54.0f,405.0f);
            if(ms<0)return;
            // Right-click must hit the actual edited target line; clicking
            // blank piano-roll space must never delete an unrelated note.
            const int note=processor_.pitchEdits().targetAt(ms);
            const float y=e.position.y*700.0f/float(getHeight());
            const float targetY=550.0f-
                (float(note)-graphCenterMidi_)*198.0f/8.0f;
            if(note>=0 && std::abs(y-targetY)<=9.0f &&
               processor_.pitchEdits().eraseAt(ms)) {
                processor_.pitchEditStateChanged();
                const bool remaining=processor_.pitchEdits().hasEdits();
                clearPitchEdits_.setEnabled(remaining);
                undoPitchEdits_.setEnabled(remaining);
            }
            repaint();
        }
        return;
    }
    if(!e.mods.isLeftButtonDown())return;
    if(startGraphNoteEdit(e.position))return;
    graphDragging_=true;
    graphEditing_=false;
    graphFollow_=false;
    graphDragStartY_=e.position.y;
    graphDragStartMidi_=graphCenterMidi_;
    // Panning during recording holds the visible phrase in place too;
    // pitch acquisition keeps updating the underlying circular buffer.
    graphGesture_.freeze(analyzerHead_,analyzerSamples_,
                         analyzerInputMidi_,analyzerOutputMidi_,analyzerHostMs_);
    analyzerToggle_.getProperties().set("graphFollow",false);
    repaint();
}
void SkellyTuneEditor::mouseDrag(const juce::MouseEvent& e) {
    const float notePixels=std::max(1.0f,198.0f*float(getHeight())/700.0f);
    if(graphEditing_) {
        graphEditMidi_=juce::jlimit(24,107,juce::roundToInt(
            graphDragStartMidi_+(graphDragStartY_-e.position.y)*8.0f/notePixels));
        repaint();
        return;
    }
    if(!graphDragging_)return;
    const float dy=e.position.y-graphDragStartY_;
    graphCenterMidi_=juce::jlimit(36.0f,96.0f,
        graphDragStartMidi_+dy*8.0f/notePixels);
    analyzerToggle_.getProperties().set("graphCenter",double(graphCenterMidi_));
    repaint();
}
void SkellyTuneEditor::mouseUp(const juce::MouseEvent&) {
    // Just selecting an existing note must not silently overwrite it:
    // commit only if the pointer actually moved to a new MIDI note.
    if(processor_.graphEditsSupported() && graphEditing_ && graphEditMidi_>=24 &&
       graphEditMidi_!=graphEditInitialMidi_ &&
       graphEditEndMs_>graphEditStartMs_) {
        if(processor_.pitchEdits().add({
                graphEditStartMs_,graphEditEndMs_,graphEditMidi_})>=0) {
            processor_.pitchEditStateChanged();
            clearPitchEdits_.setEnabled(true);
            undoPitchEdits_.setEnabled(true);
        }
    }
    graphEditing_=false;
    graphDragging_=false;
    graphGesture_.release();
    repaint();
}
void SkellyTuneEditor::mouseDoubleClick(const juce::MouseEvent& e) {
    if(!graphHit(e.position))return;
    graphDragging_=false;
    graphEditing_=false;
    graphGesture_.release();
    graphFollow_=true;
    graphCenterMidi_=float(lastTarget_>=0?lastTarget_:60);
    analyzerToggle_.getProperties().set("graphFollow",true);
    analyzerToggle_.getProperties().set("graphCenter",double(graphCenterMidi_));
    repaint();
}
void SkellyTuneEditor::togglePitchClass(int index) {
    // Match the same externally followed scale displayed by refreshNotes().
    const auto linked=processor_.linkedMusicalKey();
    const bool follow=processor_.followingSkellKey() && linked.valid();
    const int root=follow?linked.root:
        juce::roundToInt(parameters_.getRawParameterValue("key")->load());
    const int scale=follow?(linked.mode==0?1:2):
        juce::roundToInt(parameters_.getRawParameterValue("scale")->load());
    const int savedMask=juce::roundToInt(parameters_.getRawParameterValue("noteMask")->load());
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
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    return processor_.playgroundMode();
#elif defined(SKELLY_META_TEST) && SKELLY_META_TEST
    if (parameters_.getRawParameterValue("meta")->load() > 0.5f) return 2;
#endif
    return parameters_.getRawParameterValue("testTune")->load() > 0.5f ? 1 : 0;
}
void SkellyTuneEditor::chooseAlgorithm(int index) {
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    if(index>=0&&index<skelly::playground::modeCount){
        if(auto* p=parameters_.getParameter("playgroundEngine")){
            p->beginChangeGesture();p->setValueNotifyingHost(p->convertTo0to1(float(index)));p->endChangeGesture();
        }
    }
    return;
#endif
    // Enter META before disabling TESTTUNE; no intermediate REGULAR block.
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    if(index==2) {
        setBoolParameter(parameters_,"meta",true);
        setBoolParameter(parameters_,"testTune",false);
        return;
    }
#endif
    setBoolParameter(parameters_,"testTune",index==1);
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    setBoolParameter(parameters_,"meta",false);
#endif
}
void SkellyTuneEditor::updateLiveFeedback() {
    // Follow automation / preset recall.
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    playgroundSelector_.setSelectedId(currentAlgorithm()+1,juce::dontSendNotification);
    styleCombo(playgroundSelector_,palette_);
    const int actual=processor_.playgroundActiveMode();
    if(actual!=lastPlaygroundMode_){
        lastPlaygroundMode_=actual;analyzerSamples_=0;analyzerHead_=0;
        graphEditing_=false;graphDragging_=false;graphGesture_.release();
    }
#else
    algorithm_.setSelected(currentAlgorithm());
#endif
    const bool anyEdits=processor_.graphEditsSupported()&&processor_.pitchEdits().hasEdits();
    if(clearPitchEdits_.isEnabled()!=anyEdits)clearPitchEdits_.setEnabled(anyEdits);
    if(undoPitchEdits_.isEnabled()!=anyEdits)undoPitchEdits_.setEnabled(anyEdits);

    const float hz = processor_.displayedPitchHz();
    const int target = processor_.displayedTargetNote();
    const float correction = processor_.displayedCorrectionCents();
    const double a4=double(processor_.displayedReferenceA());
    const bool voiced = std::isfinite(hz) && hz>40.0f && hz<2400.0f &&
        std::isfinite(correction) && std::isfinite(a4) && a4>0.0 &&
        processor_.displayedConfidence()>=0.55f &&
        target>=0 && target<=127;

    voicedFade_ += ((voiced ? 1.0f : 0.0f) - voicedFade_) * (voiced ? 0.55f : 0.18f);
    if (voiced) {
        const double inputMidi = 69.0 + 12.0 * std::log2(double(hz) / a4);
        const float deviation = float(juce::jlimit(-100.0, 100.0, (inputMidi - double(target)) * 100.0));
        dispDeviation_ += (deviation - dispDeviation_) * 0.5f;
        dispCorrection_ += (juce::jlimit(-100.0f, 100.0f, correction) - dispCorrection_) * 0.5f;
        lastInputName_ = noteLabel(juce::roundToInt(inputMidi));
        lastTargetName_ = noteLabel(target);
        lastTarget_ = target;
        lastInput_ = juce::roundToInt(inputMidi);
    }
    const float activityGoal = voiced ? juce::jlimit(0.0f, 1.0f, std::abs(correction) / 25.0f) : 0.0f;
    activity_ += (activityGoal - activity_) * (activityGoal > activity_ ? 0.5f : 0.15f);

    const float confidence = voiced ? juce::jlimit(0.0f, 1.0f,
        processor_.displayedConfidence()) : 0.0f;
    // Absolute note history for the graph. Use -1 for silence: a gap
    // breaks the polyline instead of drawing a spurious pitch plunge.
    const float inputMidi = voiced ? float(69.0 +
        12.0*std::log2(double(hz) /
            double(processor_.displayedReferenceA()))) : -1.0f;
    const int64_t hostMs=processor_.displayedHostMs();
    const int last=(analyzerHead_+kAnalyzerHistory-1)%kAnalyzerHistory;
    const int64_t previous=analyzerSamples_>0?analyzerHostMs_[size_t(last)]:-1;
    // A seek or loop restart is a new timeline fragment. Never draw
    // a connecting pitch line between unrelated audio locations.
    // Treat DAW-clock loss/recovery like a seek, never join separate timelines.
    if(analyzerSamples_>0 &&
       ((hostMs<0)!=(previous<0) ||
        (hostMs>=0 && previous>=0 &&
         (hostMs<previous || hostMs-previous>1200)))) {
        analyzerSamples_=0;
        // A transport seek/loop invalidates the position being dragged.
        // Cancel rather than silently write a note into the wrong take.
        graphEditing_=false;
        graphDragging_=false;
        graphGesture_.release();
    }
    // Preserve recorded notes when DAW transport stops. Without a
    // playhead, ordinary live graph display remains supported, but
    // editing is disabled rather than inventing a timestamp.
    const bool advancing=hostMs<0||analyzerSamples_==0||hostMs!=previous;
    // Capture keeps advancing even while the selected note is held under
    // the pointer. No extra work or allocation occurs in the audio callback.
    if(advancing) {
        analyzerInputMidi_[size_t(analyzerHead_)]=inputMidi;
        analyzerOutputMidi_[size_t(analyzerHead_)]=
            voiced ? inputMidi+correction*0.01f : -1.0f;
        analyzerHostMs_[size_t(analyzerHead_)]=hostMs;
        analyzerTargetMidi_[size_t(analyzerHead_)]=voiced?target:-1;
        analyzerHead_=(analyzerHead_+1)%kAnalyzerHistory;
        analyzerSamples_=std::min(kAnalyzerHistory,analyzerSamples_+1);
    }
    if (voiced && graphFollow_)
        graphCenterMidi_ += (float(target)-graphCenterMidi_) *
            (analyzerSamples_<=1?1.0f:0.27f);
    analyzerToggle_.getProperties().set("analyzerSamples",analyzerSamples_);
    analyzerToggle_.getProperties().set("graphFollow",graphFollow_);
    analyzerToggle_.getProperties().set("graphCenter",double(graphCenterMidi_));
    if (analyzerExpanded_)
        repaint(0, int(416.0f*float(getHeight())/700.0f),
                getWidth(),getHeight());
    const float oldConfidence = liveConfidence_;
    liveConfidence_ += (confidence - liveConfidence_) * (voiced ? 0.40f : 0.18f);
    const juce::String newStatus = !voiced ? "LISTENING" :
        confidence < 0.68f ? "ACQUIRING" :
        std::abs(dispDeviation_) <= 8.0f ? "ON NOTE" : "CORRECTING";
    if (newStatus != liveStatus_ ||
        std::abs(oldConfidence - liveConfidence_) > 0.02f) {
        liveStatus_ = newStatus;
        // Header and panel top line only; no full-editor 30Hz repaints.
        repaint(290, 44, 126, 19);
        repaint(22, 136, 389, 6);
    }
    inputReadout_.setState(voicedFade_, lastInputName_, signedCents(dispDeviation_),
                           dispDeviation_ / 100.0f, activity_, confidence);
    targetReadout_.setState(voicedFade_, lastTargetName_, signedCents(dispCorrection_),
                            dispCorrection_ / 100.0f, activity_, confidence);

    // The trace is held only on the JUCE message thread. Bounded fixed
    // storage (32 samples, ~1.1 seconds at 30Hz) and no audio callback
    // allocations. It follows real cents, regardless of the algorithm.
    theme_.pitchTrail[size_t(theme_.trailHead)] =
        voiced ? juce::jlimit(-1.0f, 1.0f, dispDeviation_/100.0f) : 0.0f;
    theme_.trailHead = (theme_.trailHead + 1) % 32;
    theme_.trailCount = std::min(32, theme_.trailCount + 1);
    theme_.activity = activity_;
    theme_.voiceAlpha = voicedFade_;
    theme_.confidence = confidence;
    theme_.deviationCents = dispDeviation_;
    speed_.getProperties().set("liveStatus", liveStatus_);
    speed_.getProperties().set("traceSamples", theme_.trailCount);
    speed_.repaint();
    // Two independently driven live note lights, smoothed only on
    // JUCE's message thread. White = incoming sung note; green = output
    // target. They can light the same pad, and decay promptly on silence.
    for (int i = 0; i < 12; ++i) {
        const bool input = voicedFade_ > 0.03f && lastInput_ >= 0 && lastInput_ % 12 == i;
        const bool targetIsNote = voicedFade_ > 0.03f && lastTarget_ >= 0 && lastTarget_ % 12 == i;
        const float inGoal = input ? voicedFade_ * 0.90f : 0.0f;
        const float targetGoal = targetIsNote ? voicedFade_ * (0.50f + 0.50f * activity_) : 0.0f;
        inputKeyGlow_[size_t(i)] += (inGoal - inputKeyGlow_[size_t(i)]) * 0.40f;
        keyGlow_[size_t(i)] += (targetGoal - keyGlow_[size_t(i)]) * 0.40f;
        auto& key = *notes_[size_t(i)];
        if (std::abs(inputKeyGlow_[size_t(i)] - key.inputGlow) > 0.02f ||
            std::abs(keyGlow_[size_t(i)] - key.glow) > 0.02f ||
            (inputKeyGlow_[size_t(i)] < 0.015f && key.inputGlow != 0.0f) ||
            (keyGlow_[size_t(i)] < 0.015f && key.glow != 0.0f)) {
            if (inputKeyGlow_[size_t(i)] < 0.015f) inputKeyGlow_[size_t(i)] = 0.0f;
            if (keyGlow_[size_t(i)] < 0.015f) keyGlow_[size_t(i)] = 0.0f;
            key.setLiveFeedback(inputKeyGlow_[size_t(i)], keyGlow_[size_t(i)]);
        }
    }
}
