#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../dsp/ScaleMask.hpp"
#include "../dsp/SafePresetValue.hpp"
#include <array>
#include <algorithm>

SkellyTuneRealtimeProcessor::SkellyTuneRealtimeProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "STATE", createLayout()) {}

juce::AudioProcessorValueTreeState::ParameterLayout SkellyTuneRealtimeProcessor::createLayout() {
    using F = juce::AudioParameterFloat;
    using C = juce::AudioParameterChoice;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<F>(juce::ParameterID{"speed", 1}, "Retune Speed (ms)",
        juce::NormalisableRange<float>(0.0f, 200.0f, 0.01f, 0.40f), 25.0f));
    layout.add(std::make_unique<F>(juce::ParameterID{"amount", 1}, "Correction Amount (%)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
    layout.add(std::make_unique<F>(juce::ParameterID{"mix", 1}, "Wet Mix (%)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
    layout.add(std::make_unique<F>(juce::ParameterID{"transient", 1}, "Transient Protection (%)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f));
    layout.add(std::make_unique<F>(juce::ParameterID{"air", 1}, "Consonant Air Preservation (%)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 65.0f));
    layout.add(std::make_unique<F>(juce::ParameterID{"vibratoPreserve", 1}, "Vibrato Preservation (%)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<F>(juce::ParameterID{"flex", 1}, "Flex Tune (cents)",
        juce::NormalisableRange<float>(0.0f, 60.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<F>(juce::ParameterID{"formantPreserve", 1}, "Formant Envelope Preservation (%)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 0.0f));
    layout.add(std::make_unique<F>(juce::ParameterID{"cycleStabilize", 1}, "Cycle Level Stabilization (%)",
        juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 80.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"phaseAlign", 1},
        "Phase-Aligned Cycle Jumps", true));
    layout.add(std::make_unique<F>(juce::ParameterID{"a4", 1}, "A4 Reference (Hz)",
        juce::NormalisableRange<float>(400.0f, 480.0f, 0.1f), 440.0f));
    layout.add(std::make_unique<C>(juce::ParameterID{"synthesisMode", 1}, "Synthesis (Cycle Experimental)",
#if (defined(SKELLY_STUDIO_PROFILE) && SKELLY_STUDIO_PROFILE) || (defined(SKELLY_STUDIO_FAST_PROFILE) && SKELLY_STUDIO_FAST_PROFILE)
        juce::StringArray{"Studio Clock-Locked", "Studio Clock-Locked (fixed)"}, 0));
#else
        juce::StringArray{"Realtime (default)", "Cycle PSOLA (experimental)"}, 0));
#endif
    layout.add(std::make_unique<C>(juce::ParameterID{"key", 1}, "Root Note",
        juce::StringArray{"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"}, 0));
    layout.add(std::make_unique<C>(juce::ParameterID{"scale", 1}, "Scale",
        juce::StringArray{"Chromatic","Major","Natural Minor"}, 0));
    // A value of zero follows the selected root/scale preset. 1..4095 are
    // absolute C..B pitch-class masks that the editor can set one note at a time.
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"noteMask", 1},
        "Enabled Notes Mask (0 = follow scale)", 0, 4095, 0));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"bypass", 1},
        "Bypass", false));
    return layout;
}

void SkellyTuneRealtimeProcessor::prepareToPlay(double sampleRate, int maxBlockSize) {
    doubleScratch_.setSize(2, juce::jlimit(1, 16384, maxBlockSize), false, true, false);
#if defined(SKELLY_STUDIO_FAST_PROFILE) && SKELLY_STUDIO_FAST_PROFILE
    engine_.prepare(sampleRate, getTotalNumInputChannels(), true, true);
#elif defined(SKELLY_STUDIO_PROFILE) && SKELLY_STUDIO_PROFILE
    engine_.prepare(sampleRate, getTotalNumInputChannels(), true);
#else
    engine_.prepare(sampleRate, getTotalNumInputChannels());
#endif
    // Both modes share the same sample-accurate dry/transient transport delay.
    // Wet tonal cycles are nonlinearly time-warped; their source age is NOT PDC.
    // Mode automation must not trigger a host latency change on the audio thread.
    const auto realtime = engine_.outputDelayContract(false);
    const auto cycle = engine_.outputDelayContract(true);
    jassert(realtime.reportedSamples == cycle.reportedSamples);
    setLatencySamples(realtime.reportedSamples);
}
void SkellyTuneRealtimeProcessor::releaseResources() { engine_.reset(); }
bool SkellyTuneRealtimeProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    return l.getMainInputChannelSet() == l.getMainOutputChannelSet() &&
       (l.getMainOutputChannelSet() == juce::AudioChannelSet::mono() ||
        l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo());
}
skelly::Parameters SkellyTuneRealtimeProcessor::currentParameters() const noexcept {
    skelly::Parameters p;
    p.speedMs = state_.getRawParameterValue("speed")->load();
    p.amount = state_.getRawParameterValue("amount")->load() / 100.0f;
    p.mix = state_.getRawParameterValue("mix")->load() / 100.0f;
    p.transientProtection = state_.getRawParameterValue("transient")->load() / 100.0f;
    p.airPreservation = state_.getRawParameterValue("air")->load() / 100.0f;
    p.vibratoPreservation = state_.getRawParameterValue("vibratoPreserve")->load() / 100.0f;
    p.flexCents = state_.getRawParameterValue("flex")->load();
    p.formantPreservation = state_.getRawParameterValue("formantPreserve")->load()/100.0f;
    p.cycleLevelStabilization = state_.getRawParameterValue("cycleStabilize")->load()/100.0f;
    p.phaseAlignedJumps = state_.getRawParameterValue("phaseAlign")->load() > 0.5f;
    p.referenceA = state_.getRawParameterValue("a4")->load();
#if (defined(SKELLY_STUDIO_PROFILE) && SKELLY_STUDIO_PROFILE) || (defined(SKELLY_STUDIO_FAST_PROFILE) && SKELLY_STUDIO_FAST_PROFILE)
    p.experimentalCycleSynthesis = true; // Separate instance, locked timebase.
#else
    p.experimentalCycleSynthesis = state_.getRawParameterValue("synthesisMode")->load() > 0.5f;
#endif
    const int key = juce::jlimit(0, 11, int(std::lround(state_.getRawParameterValue("key")->load())));
    const int scale = juce::jlimit(0, 2, int(std::lround(state_.getRawParameterValue("scale")->load())));
    const int manualMask = juce::jlimit(0, 4095, int(std::lround(state_.getRawParameterValue("noteMask")->load())));
    p.pitchClasses = skelly::effectiveMask(key, scale, manualMask);
    // The engine's dry path retains the plugin's declared latency. Bypass
    // smoothly drops the wet mix while the detector remains warmed up.
    if (state_.getRawParameterValue("bypass")->load() > 0.5f) p.mix = 0.0f;
    return p;
}
void SkellyTuneRealtimeProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    const int ch = std::min(2, b.getNumChannels());
    if (ch == 0 || b.getNumSamples() <= 0) return;
    engine_.setParameters(currentParameters());
    std::array<const float*, 2> in { b.getReadPointer(0), ch == 2 ? b.getReadPointer(1) : nullptr };
    std::array<float*, 2> out { b.getWritePointer(0), ch == 2 ? b.getWritePointer(1) : nullptr };
    engine_.process(in.data(), out.data(), b.getNumSamples(), ch);
    const auto pitch = engine_.lastPitch();
    displayedPitchHz_.store(pitch.valid ? pitch.hertz : 0.0f, std::memory_order_relaxed);
    displayedConfidence_.store(pitch.valid ? pitch.confidence : 0.0f, std::memory_order_relaxed);
    displayedTargetNote_.store(engine_.currentTargetNote(), std::memory_order_relaxed);
    displayedTargetPending_.store(engine_.targetPending(), std::memory_order_relaxed);
    displayedCorrectionCents_.store(float(engine_.appliedCents()), std::memory_order_relaxed);
}
void SkellyTuneRealtimeProcessor::processBlock(juce::AudioBuffer<double>& b, juce::MidiBuffer&) {
    // v0.3 silently zeroed a session whenever a host selected 64-bit processing.
    // Convert in preallocated chunks; no audio-thread allocations or loss of audio.
    juce::ScopedNoDenormals guard;
    const int ch = std::min(2, b.getNumChannels());
    if (ch == 0 || b.getNumSamples() == 0) return;
    engine_.setParameters(currentParameters());
    const int capacity = doubleScratch_.getNumSamples();
    // Some hosts can probe a processor before prepareToPlay; never enter a
    // zero-stride loop or allocate on the realtime thread. Match the float
    // path's unprepared behavior by passing the original buffer through.
    if (capacity <= 0) return;
    for (int offset = 0; offset < b.getNumSamples(); offset += capacity) {
        const int n = std::min(capacity, b.getNumSamples() - offset);
        for (int c = 0; c < ch; ++c) {
            const double* src = b.getReadPointer(c, offset);
            float* dst = doubleScratch_.getWritePointer(c);
            for (int i = 0; i < n; ++i) dst[i] = std::isfinite(src[i]) ? float(src[i]) : 0.0f;
        }
        const float* src[2] = {doubleScratch_.getReadPointer(0),
                                ch == 2 ? doubleScratch_.getReadPointer(1) : nullptr};
        float* dst[2] = {doubleScratch_.getWritePointer(0),
                         ch == 2 ? doubleScratch_.getWritePointer(1) : nullptr};
        engine_.process(src, dst, n, ch);
        for (int c = 0; c < ch; ++c) {
            double* dest = b.getWritePointer(c, offset);
            const float* frame = doubleScratch_.getReadPointer(c);
            for (int i = 0; i < n; ++i) dest[i] = double(frame[i]);
        }
    }
    const auto pitch = engine_.lastPitch();
    displayedPitchHz_.store(pitch.valid ? pitch.hertz : 0.0f, std::memory_order_relaxed);
    displayedConfidence_.store(pitch.valid ? pitch.confidence : 0.0f, std::memory_order_relaxed);
    displayedTargetNote_.store(engine_.currentTargetNote(), std::memory_order_relaxed);
    displayedTargetPending_.store(engine_.targetPending(), std::memory_order_relaxed);
    displayedCorrectionCents_.store(float(engine_.appliedCents()), std::memory_order_relaxed);
}
juce::AudioProcessorEditor* SkellyTuneRealtimeProcessor::createEditor() {
    return new SkellyTuneEditor(*this);
}

namespace {
constexpr int kMaxSavedStateBytes = 256 * 1024;

// Reject invalid saved values before installing host-supplied state.
std::optional<double> decodeSavedNumber(const juce::var& raw) {
    const auto text = raw.toString().trim();
    if (text.isEmpty() || text.getNumBytesAsUTF8() > 63) return std::nullopt;
    return skelly::parseSavedNumber(text.toRawUTF8());
}
} // namespace

void SkellyTuneRealtimeProcessor::getStateInformation(juce::MemoryBlock& block) {
    if (const auto xml = state_.copyState().createXml())
        copyXmlToBinary(*xml, block);
    else
        block.reset();
}

void SkellyTuneRealtimeProcessor::setStateInformation(const void* data, int bytes) {
    // FL Studio can restore an older parameter layout. Restrict input size,
    // XML root, IDs, and numeric values instead of replacing APVTS blindly.
    if (data == nullptr || bytes <= 0 || bytes > kMaxSavedStateBytes) return;
    const auto xml = getXmlFromBinary(data, bytes);
    if (xml == nullptr || !xml->hasTagName(state_.state.getType())) return;

    const auto incoming = juce::ValueTree::fromXml(*xml);
    if (!incoming.isValid()) return;
    auto safeState = state_.copyState();
    int restored = 0;
    for (int i = 0; i < incoming.getNumChildren(); ++i) {
        const auto source = incoming.getChild(i);
        if (!source.hasType(juce::Identifier("PARAM"))) continue;
        const auto id = source.getProperty(juce::Identifier("id")).toString();
        const auto* spec = skelly::findSavedParameter(id.toRawUTF8());
        if (spec == nullptr || !source.hasProperty(juce::Identifier("value"))) continue;
        const auto rawValue = decodeSavedNumber(source.getProperty(juce::Identifier("value")));
        if (!rawValue.has_value()) continue;
        const auto cleanValue = skelly::sanitizedSavedValue(spec->id, *rawValue);
        if (!cleanValue.has_value()) continue;
        auto target = safeState.getChildWithProperty(juce::Identifier("id"), id);
        if (!target.isValid()) continue;
        target.setProperty(juce::Identifier("value"), *cleanValue, nullptr);
        ++restored;
    }
    if (restored > 0) state_.replaceState(safeState);
    // Invalid/unknown chunks do not damage the current valid state.
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SkellyTuneRealtimeProcessor();
}
