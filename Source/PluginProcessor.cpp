#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../dsp/ScaleMask.hpp"
#include "../dsp/SafePresetValue.hpp"
#include <array>
#include <algorithm>

SkellyTuneRealtimeProcessor::SkellyTuneRealtimeProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      state_(*this, nullptr, "STATE", createLayout()) {
    // JUCE Timer callbacks run on the message thread, not the audio thread.
    // Starting here also covers instances whose editor is never opened.
    startTimerHz(5);
}

SkellyTuneRealtimeProcessor::~SkellyTuneRealtimeProcessor() { leaveWorld(); }

// Called ONLY on the JUCE message thread after WorldLink has validated the
// publisher's SkellKey product ID and complete key/scale/A4 payload.
bool SkellyTuneRealtimeProcessor::applySkellKeyTuning(
    const skellcore::world::MusicalTuning& tuning) {
    if (!tuning.valid()) return false;
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Persist visible Key, Scale and A4 values via their existing APVTS IDs.
    // No new automation IDs, custom masks or plugin state fields are added.
    // Change notifications happen only when the value differs.
    const auto syncParameter = [this](const char* id, float value) {
        auto* parameter = state_.getParameter(id);
        const auto* current = state_.getRawParameterValue(id);
        if (parameter == nullptr || current == nullptr) return;
        const float epsilon = juce::String(id) == "a4" ? 0.051f : 0.01f;
        if (std::abs(current->load(std::memory_order_relaxed) - value) <= epsilon)
            return;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        parameter->endChangeGesture();
    };
    syncParameter("key", float(tuning.root));
    syncParameter("scale", float(tuning.mode + 1)); // major=1, natural minor=2
    syncParameter("a4", float(std::round(tuning.a4Hz() * 10.0) / 10.0));

    // Publish a coherent 64-bit snapshot for the realtime audio callback.
    linkedTuning_.store(skellcore::world::packMusicalTuning(tuning,1),
                        std::memory_order_release);
    linkedKey_.store(skellcore::world::MusicalKey{tuning.root,tuning.mode}.packed(),
                     std::memory_order_release);
    return true;
}

void SkellyTuneRealtimeProcessor::timerCallback() {
    // The listener is automatic. Its worker retries 127.0.0.1 without ever
    // blocking audio or requesting a receiver-side JOIN/FOLLOW button.
    if (!joinWorld()) return;
    if (!worldLink.online()) {
        linkedKey_.store(0, std::memory_order_release);
        linkedTuning_.store(0, std::memory_order_release);
        return;
    }
    skellcore::world::MusicalTuning tuning;
    std::uint32_t tuningVersion = 0;
    // Full tuning packet takes priority so Key, Scale and A4 arrive together.
    if (worldLink.latestMusicalTuning(tuning, tuningVersion)) {
        applySkellKeyTuning(tuning);
        return;
    }
    linkedTuning_.store(0, std::memory_order_release);
    // Older SkellKey builds may send only musical.key. A4 stays manual.
    skellcore::world::MusicalKey key;
    std::uint64_t version = 0;
    if (worldLink.latestMusicalKey(key, version)) {
        linkedKey_.store(key.packed(), std::memory_order_release);
        const auto sync = [this](const char* id, float value) {
            auto* p = state_.getParameter(id);
            auto* current = state_.getRawParameterValue(id);
            if (p != nullptr && current != nullptr
                && std::abs(current->load(std::memory_order_relaxed)-value)>0.01f) {
                p->beginChangeGesture();
                p->setValueNotifyingHost(p->convertTo0to1(value));
                p->endChangeGesture();
            }
        };
        sync("key", float(key.root));
        sync("scale", float(key.mode + 1));
    } else {
        linkedKey_.store(0, std::memory_order_release);
    }
}
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
    // Alternate algorithm for A/B testing: nearest in-scale note in the PERIOD domain, no
    // hysteresis. Off by default; the standard algorithm is unchanged when this is off.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"testTune", 1},
        "TestTune Algorithm", false));
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    // TEST-ONLY: the Meta engine. Not persisted in saved sessions (not in the saved-state allow-list).
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"meta", 1},
        "Meta Algorithm (test only)", false));
#endif
    return layout;
}

void SkellyTuneRealtimeProcessor::prepareToPlay(double sampleRate, int maxBlockSize) {
    preparedAudioRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ?
        sampleRate : 0.0;
    doubleScratch_.setSize(2, juce::jlimit(1, 16384, maxBlockSize), false, true, false);
#if defined(SKELLY_STUDIO_FAST_PROFILE) && SKELLY_STUDIO_FAST_PROFILE
    engine_.prepare(sampleRate, getTotalNumInputChannels(), true, true);
#elif defined(SKELLY_STUDIO_PROFILE) && SKELLY_STUDIO_PROFILE
    engine_.prepare(sampleRate, getTotalNumInputChannels(), true);
#else
    // 1.5ms fixed PDC in this Mini VST3, set before processing starts.
    // Existing 40/50ms Studio products and legacy portable DSP defaults
    // retain their own contracts; no realtime latency automation.
    engine_.prepare(sampleRate, getTotalNumInputChannels(),
                    false, false, 1.5);
#endif
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    // META retains its own grain-source history, but the dry/transient
    // transport must match this same host PDC in every selected algorithm.
    meta_.prepare(sampleRate, 1.5);
    metaWasActive_ = false;
#endif
    // Both modes share the same sample-accurate dry/transient transport delay.
    // Wet tonal cycles are nonlinearly time-warped; their source age is NOT PDC.
    // Mode automation must not trigger a host latency change on the audio thread.
    const auto realtime = engine_.outputDelayContract(false);
    const auto cycle = engine_.outputDelayContract(true);
    jassert(realtime.reportedSamples == cycle.reportedSamples);
    setLatencySamples(realtime.reportedSamples);
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    jassert(meta_.latencySamples() == realtime.reportedSamples);
#endif
}
void SkellyTuneRealtimeProcessor::releaseResources() {
    preparedAudioRate_ = 0.0;
    engine_.reset();
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    meta_.reset();
#endif
}
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
    p.testTune = state_.getRawParameterValue("testTune")->load() > 0.5f;
#if (defined(SKELLY_STUDIO_PROFILE) && SKELLY_STUDIO_PROFILE) || (defined(SKELLY_STUDIO_FAST_PROFILE) && SKELLY_STUDIO_FAST_PROFILE)
    p.experimentalCycleSynthesis = true; // Separate instance, locked timebase.
#else
    p.experimentalCycleSynthesis = state_.getRawParameterValue("synthesisMode")->load() > 0.5f;
#endif
    int key = juce::jlimit(0, 11, int(std::lround(state_.getRawParameterValue("key")->load())));
    int scale = juce::jlimit(0, 2, int(std::lround(state_.getRawParameterValue("scale")->load())));
    const int manualMask = juce::jlimit(0, 4095, int(std::lround(state_.getRawParameterValue("noteMask")->load())));
    // One atomic read; no socket, locks, host notifications or allocations.
    // An explicit user opt-in temporarily overrides tuning scale only.
    // Saved key/scale parameters and any custom note mask remain untouched.
    if (followingSkellKey() && manualMask==0) {
        const auto world=skellcore::world::unpackMusicalKey(linkedKey_.load(std::memory_order_relaxed));
        if(world.valid()) {
            key=world.root;
            scale=world.mode==0 ? 1 : 2;
        }
    }
    p.pitchClasses = skelly::effectiveMask(key, scale, manualMask);
    if (followingSkellKey() && manualMask == 0) {
        skellcore::world::MusicalTuning tuning;
        std::uint32_t version=0;
        const auto snapshot=linkedTuning_.load(std::memory_order_acquire);
        if (skellcore::world::unpackMusicalTuning(snapshot,tuning,version)) {
            // Never alter host-automatable A4/key/scale. Override only the
            // DSP's transient effective values, after explicit user opt-in.
            p.pitchClasses = int(tuning.mask);
            p.referenceA = static_cast<float>(tuning.a4Hz());
        }
    }
    // The engine's dry path retains the plugin's declared latency. Bypass
    // smoothly drops the wet mix while the detector remains warmed up.
    if (state_.getRawParameterValue("bypass")->load() > 0.5f) p.mix = 0.0f;
    return p;
}
void SkellyTuneRealtimeProcessor::updateTimelinePosition() noexcept {
    int64_t positionMs=-1;
    const double rate=preparedAudioRate_;
    if(rate>0.0) {
        if(auto* playhead=getPlayHead()) {
            const auto pos=playhead->getPosition();
            if(pos) {
                if(const auto samples=pos->getTimeInSamples())
                    positionMs=int64_t(std::llround(double(*samples)*1000.0/rate));
                else if(const auto seconds=pos->getTimeInSeconds()) {
                    // FL Studio/other VST3 hosts may supply seconds but
                    // omit sample position. Never fabricate a clock.
                    if(std::isfinite(*seconds) && *seconds>=0.0 &&
                       *seconds<=2147483.0)
                        positionMs=int64_t(std::llround(*seconds*1000.0));
                }
            }
        }
    }
    displayedHostMs_.store(positionMs,std::memory_order_relaxed);
}
void SkellyTuneRealtimeProcessor::processEditedAudio(
    float* const* io,int channels,int frames,int blockOffset,bool useMeta) {
    // JUCE standalone smoke calls prepareToPlay directly without a host
    // rate. Never discard the whole audio callback because
    // AudioProcessor::getSampleRate() still reports zero.
    if(frames<=0 || !(preparedAudioRate_>0.0))return;
    const double rate=preparedAudioRate_;
    const auto blockMs=displayedHostMs_.load(std::memory_order_relaxed);
    for(int offset=0;offset<frames;) {
        const int absolute=blockOffset+offset;
        const auto atMs=skelly::PitchEditTimeline::frameMs(blockMs,absolute,rate);
        const int target=pitchEdits_.targetAt(atMs);
        const int count=pitchEdits_.framesUntilBoundary(blockMs,absolute,
                                                        frames-offset,rate);
        float* segment[2]={io[0]+offset,
                           channels==2?io[1]+offset:nullptr};
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
        if(useMeta) {
            meta_.setManualTargetMidi(target);
            runMeta(segment,channels,count);
        } else
#endif
        {
            (void)useMeta;
            engine_.setManualTargetMidi(target);
            const float* in[2]={segment[0],segment[1]};
            engine_.process(in,segment,count,channels);
        }
        offset+=count;
    }
}
void SkellyTuneRealtimeProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    const int ch = std::min(2, b.getNumChannels());
    if (ch == 0 || b.getNumSamples() <= 0) return;
    const auto params = currentParameters();
    updateTimelinePosition();
    engine_.setParameters(params);
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    if (state_.getRawParameterValue("meta")->load() > 0.5f) {
        if (!metaWasActive_) { meta_.reset(); metaWasActive_ = true; }
        float* io[2] = { b.getWritePointer(0), ch == 2 ? b.getWritePointer(1) : nullptr };
        processEditedAudio(io,ch,b.getNumSamples(),0,true);
        return;
    }
    if (metaWasActive_) {
        // reset() intentionally clears cached engine Parameters. The old
        // order applied APVTS values BEFORE the reset and processed the
        // first switched block with factory defaults: audible A/B glitch.
        engine_.reset();
        engine_.setParameters(params);
        metaWasActive_ = false;
    }
#endif
    float* io[2]={b.getWritePointer(0),ch==2?b.getWritePointer(1):nullptr};
    processEditedAudio(io,ch,b.getNumSamples(),0,false);
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
    const auto params = currentParameters();
    updateTimelinePosition();
    engine_.setParameters(params);
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
        float* dst[2] = {doubleScratch_.getWritePointer(0),
                         ch == 2 ? doubleScratch_.getWritePointer(1) : nullptr};
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
        if (state_.getRawParameterValue("meta")->load() > 0.5f) {
            if (!metaWasActive_) { meta_.reset(); metaWasActive_ = true; }
            processEditedAudio(dst,ch,n,offset,true);
        } else {
            if (metaWasActive_) {
                engine_.reset();
                engine_.setParameters(params); // persist host state in first returned 64-bit block
                metaWasActive_ = false;
            }
            processEditedAudio(dst,ch,n,offset,false);
        }
#else
        processEditedAudio(dst,ch,n,offset,false);
#endif
        for (int c = 0; c < ch; ++c) {
            double* dest = b.getWritePointer(c, offset);
            const float* frame = doubleScratch_.getReadPointer(c);
            for (int i = 0; i < n; ++i) dest[i] = double(frame[i]);
        }
    }
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
    if (metaWasActive_) return;   // runMeta() already published the meter values
#endif
    const auto pitch = engine_.lastPitch();
    displayedPitchHz_.store(pitch.valid ? pitch.hertz : 0.0f, std::memory_order_relaxed);
    displayedConfidence_.store(pitch.valid ? pitch.confidence : 0.0f, std::memory_order_relaxed);
    displayedTargetNote_.store(engine_.currentTargetNote(), std::memory_order_relaxed);
    displayedTargetPending_.store(engine_.targetPending(), std::memory_order_relaxed);
    displayedCorrectionCents_.store(float(engine_.appliedCents()), std::memory_order_relaxed);
}
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
void SkellyTuneRealtimeProcessor::runMeta(float* const* io, int channels, int frames) {
    const auto p = currentParameters();
    // True 0-200ms retune time constant, consistent with REGULAR/TESTTUNE.
    // META research must respect the existing host Mix control as well
    // as Amount. Previously Mix=0 still emitted fully retuned META grains.
    // The experimental engine handles Amount=0 as sample-exact 3ms dry.
    meta_.setParameters(uint16_t(p.pitchClasses), double(p.referenceA),
                        double(p.amount * p.mix), double(p.speedMs));
    const bool bypass = state_.getRawParameterValue("bypass")->load() > 0.5f;
    // processEditedAudio() already supplies the note for THIS sub-block.
    // Resetting it to the host block's first-note target here silently
    // discarded manual pitch edits starting or ending mid-buffer.
    meta_.process(io, channels, frames, bypass);
    const double hz = meta_.detectedHz(), targetHz = meta_.targetHz();
    const bool voiced = hz > 0.0 && targetHz > 0.0;
    displayedPitchHz_.store(voiced ? float(hz) : 0.0f, std::memory_order_relaxed);
    displayedConfidence_.store(voiced ? float(meta_.lastConfidence()) : 0.0f, std::memory_order_relaxed);
    displayedTargetNote_.store(voiced ? int(std::lround(69.0 + 12.0 * std::log2(targetHz / double(p.referenceA)))) : -1,
                               std::memory_order_relaxed);
    displayedTargetPending_.store(false, std::memory_order_relaxed);
    displayedCorrectionCents_.store(voiced ? float(meta_.shiftCents()) : 0.0f, std::memory_order_relaxed);
}
#endif
juce::AudioProcessorEditor* SkellyTuneRealtimeProcessor::createEditor() {
    // Connect immediately for a visible editor, but do not require that
    // editor to remain open: processor timer keeps the listener running.
    joinWorld();
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
    // Saved XML extends the old APVTS state rather than changing any
    // published parameter IDs, numeric ranges, or plugin identities.
    auto safe=state_.copyState();
    juce::ValueTree edits("PITCH_EDITS");
    for(int i=0;i<skelly::PitchEditTimeline::kCapacity;++i) {
        const auto r=pitchEdits_.get(i);
        if(!r.valid())continue;
        juce::ValueTree e("EDIT");
        e.setProperty("slot",i,nullptr);
        e.setProperty("startMs",static_cast<juce::int64>(r.startMs),nullptr);
        e.setProperty("endMs",static_cast<juce::int64>(r.endMs),nullptr);
        e.setProperty("midi",r.targetMidi,nullptr);
        edits.addChild(e,-1,nullptr);
    }
    if(edits.getNumChildren()>0)safe.addChild(edits,-1,nullptr);
    if(const auto xml=safe.createXml())copyXmlToBinary(*xml,block);
    else block.reset();
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
    // Explicitly allow one bounded UI-only property. Old presets remain valid,
    // and no new automatable parameters or DSP preset fields are introduced.
    bool restoredSkin=false;
    if (incoming.hasProperty(juce::Identifier("skellSkullSkin"))) {
        const auto candidate=decodeSavedNumber(incoming.getProperty("skellSkullSkin"));
        if(candidate && std::isfinite(*candidate) && *candidate >= 0 && *candidate <= 3 &&
           std::floor(*candidate)==*candidate) {
            const auto selected=static_cast<int>(*candidate);
            safeState.setProperty("skellSkullSkin",selected,nullptr);
            skinIndex_.store(selected,std::memory_order_release);
            restoredSkin=true;
        }
    }
    if (restored > 0 || restoredSkin) state_.replaceState(safeState);
    // A legacy preset contains no edits: no stale manual note is kept.
    // Validate every untrusted host XML field independently.
    pitchEdits_.clear();
    const auto edits=incoming.getChildWithName("PITCH_EDITS");
    if(edits.isValid()) {
        const int count=std::min(edits.getNumChildren(),
                                 skelly::PitchEditTimeline::kCapacity);
        for(int i=0;i<count;++i) {
            const auto e=edits.getChild(i);
            if(!e.hasType("EDIT"))continue;
            const auto slot=decodeSavedNumber(e.getProperty("slot"));
            const auto first=decodeSavedNumber(e.getProperty("startMs"));
            const auto last=decodeSavedNumber(e.getProperty("endMs"));
            const auto midi=decodeSavedNumber(e.getProperty("midi"));
            if(!slot||!first||!last||!midi)continue;
            if(!std::isfinite(*slot)||!std::isfinite(*first)||
               !std::isfinite(*last)||!std::isfinite(*midi))continue;
            if(*slot<0||*slot>=skelly::PitchEditTimeline::kCapacity||
               *first<0||*last>2147483647.0||*midi<0||*midi>127||
               std::floor(*slot)!=*slot||std::floor(*first)!=*first||
               std::floor(*last)!=*last||std::floor(*midi)!=*midi)continue;
            pitchEdits_.set(int(*slot),{int64_t(*first),int64_t(*last),int(*midi)});
        }
    }
    // Invalid/unknown chunks do not damage the current valid state.
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SkellyTuneRealtimeProcessor();
}
