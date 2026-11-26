#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../dsp/ScaleMask.hpp"
#include "../dsp/SafePresetValue.hpp"
#include <array>
#include <algorithm>
#include <limits>

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

    // Preserve local custom masks. A non-diatonic remote mask can be
    // persisted only when there is no existing local custom selection.
    if (tuning.mask != skellcore::world::diatonicMask(tuning.root,tuning.mode) &&
        state_.getRawParameterValue("noteMask")->load() == 0.f)
        syncParameter("noteMask", float(tuning.mask));
    // Linked key is informational only; APVTS is the tuning source of truth.
    linkedKey_.store(skellcore::world::MusicalKey{tuning.root,tuning.mode}.packed(),
                     std::memory_order_release);
    return true;
}

void SkellyTuneRealtimeProcessor::timerCallback() {
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    // Preset recall and host parameter edits also work without an open editor.
    // Never allocate or announce changing PDC from processBlock.
    if(playgroundNeedsApply())applyPlaygroundLatency();
#endif
    // The listener is automatic. Its worker retries 127.0.0.1 without ever
    // blocking audio or requesting a receiver-side JOIN/FOLLOW button.
    if (!joinWorld()) return;
    if (!worldLink.online()) {
        linkedKey_.store(0, std::memory_order_release);
        tuningInbox_.reset();
        return;
    }
    skellcore::world::MusicalTuning tuning;
    std::uint32_t tuningVersion = 0;
    // Full tuning packet takes priority so Key, Scale and A4 arrive together.
    if (worldLink.latestMusicalTuning(tuning, tuningVersion)) {
        const auto key = skellcore::world::MusicalKey{tuning.root,tuning.mode}.packed();
        // Ignore timestamp changes for an unchanged payload. Manual edits,
        // automation and preset recall must not be overwritten by heartbeats.
        if (tuningInbox_.acceptFull(skellcore::world::packMusicalTuning(tuning,1),key))
            applySkellKeyTuning(tuning);
        return;
    }
    // Older SkellKey builds may send only musical.key. A4 stays manual.
    skellcore::world::MusicalKey key;
    std::uint64_t version = 0;
    if (worldLink.latestMusicalKey(key, version)) {
        linkedKey_.store(key.packed(), std::memory_order_release);
        if (!tuningInbox_.acceptKey(key.packed())) return;
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
        tuningInbox_.reset(); // observed withdrawal permits a fresh Send
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
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    juce::StringArray engines;
    for(int i=0;i<skelly::playground::legacySlotCount;++i)engines.add(skelly::playground::legacyLabels[size_t(i)]);
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"playgroundEngine",1},"Playground Engine",engines,1));
    juce::StringArray profiles;
    for(const auto* name:skelly::playground::latencyNames)profiles.add(name);
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"playgroundLatencyProfile",1},"Latency",profiles,3,
        juce::AudioParameterChoiceAttributes{}.withAutomatable(false)));
    // Append research-only controls; never change old parameter IDs or latency defaults.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"playgroundFormantEnabled",1},
        "Formant Audition (no added delay)",false,juce::AudioParameterBoolAttributes{}.withAutomatable(false)));
    layout.add(std::make_unique<F>(juce::ParameterID{"playgroundFormantShift",1}, "Formant Shift (semitones)",
        juce::NormalisableRange<float>(-12.f,12.f,0.1f),0.f));
#endif
    return layout;
}

void SkellyTuneRealtimeProcessor::prepareToPlay(double sampleRate, int maxBlockSize) {
    preparedAudioRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0) ?
        sampleRate : 0.0;
    doubleScratch_.setSize(2, juce::jlimit(1, 16384, maxBlockSize), false, true, false);
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    playground_.setListeningPromotion(true);
    playground_.setMeasuredGrainPromotion(true);
    playground_.setFinalListeningPolicy(true);
    playground_.prepare(sampleRate,std::max(1,getTotalNumInputChannels()),maxBlockSize,playgroundProfile(),playgroundMode());
    playgroundFormant_.prepare(sampleRate,true);
    playgroundPlaying_.store(false);
    publishPlaygroundConfig();
    return;
#endif
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
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
void SkellyTuneRealtimeProcessor::publishPlaygroundConfig() {
    for(int i=0;i<skelly::playground::modeCount;++i)playgroundNative_[size_t(i)].store(playground_.nativeLatency(i));
    playgroundLockedMode_.store(playground_.lockedMode());
    playgroundAppliedProfile_.store(playground_.latencyProfile());
    playgroundFormantApplied_.store(playgroundFormantRequested());
    const int totalDelay=playground_.latencySamples();
    playgroundHostDelay_.store(totalDelay);
    playgroundActive_.store(-1);
    displayedPitchHz_.store(0);displayedConfidence_.store(0);
    displayedTargetNote_.store(-1);displayedCorrectionCents_.store(0);
    displayedTargetPending_.store(false);
    ++playgroundGeneration_;
    setLatencySamples(totalDelay);
}
bool SkellyTuneRealtimeProcessor::applyPlaygroundLatency() {
    auto* mm=juce::MessageManager::getInstanceWithoutCreating();
    if(!mm||!mm->isThisTheMessageThread())return false;
    if(!playgroundNeedsApply())return true;
    double rate=0.;int channels=0,block=0,profile=0,mode=0;unsigned generation=0;
    {
        const juce::ScopedLock lock(getCallbackLock());
        rate=preparedAudioRate_;channels=getTotalNumInputChannels();block=doubleScratch_.getNumSamples();
        profile=playgroundProfile();mode=playgroundMode();generation=playgroundGeneration_.load();
    }
    if(!(rate>0.)||channels<1||block<1)return false;
    // Allocate and prepare the replacement while the current engine remains
    // untouched. The callback lock is held only for the replacement swap;
    // publishPlaygroundConfig announces actual latency through JUCE to the host.
    // A transport change is not a reason to reject the selection.
    skelly::playground::Bank next;
    next.setListeningPromotion(true);
    next.setMeasuredGrainPromotion(true);
    next.setFinalListeningPolicy(true);
    try { next.prepare(rate,channels,block,profile,mode); }
    catch(...) { return false; }
    {
        const juce::ScopedLock lock(getCallbackLock());
        if(generation!=playgroundGeneration_.load()||
           profile!=playgroundProfile()||mode!=playgroundMode())return false;
        const bool wasSuspended=isSuspended();
        suspendProcessing(true);
        std::swap(playground_,next);
        playgroundFormant_.prepare(rate,true);
        publishPlaygroundConfig();
        suspendProcessing(wasSuspended);
    }
    return true;
}
#endif
void SkellyTuneRealtimeProcessor::releaseResources() {
    preparedAudioRate_ = 0.0;
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    playgroundActive_.store(-1);playgroundPlaying_.store(false);++playgroundGeneration_;
    return;
#endif
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
    // Send to Tune updates APVTS once; manual controls and DSP share it.
    p.pitchClasses = skelly::effectiveMask(key, scale, manualMask);
    // The engine's dry path retains the plugin's declared latency. Bypass
    // smoothly drops the wet mix while the detector remains warmed up.
    if (state_.getRawParameterValue("bypass")->load() > 0.5f) p.mix = 0.0f;
    return p;
}
void SkellyTuneRealtimeProcessor::updateTimelinePosition() noexcept {
    int64_t positionMs=-1;
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    bool playing=false;
#endif
    const double rate=preparedAudioRate_;
    if(rate>0.0) {
        if(auto* playhead=getPlayHead()) {
            const auto pos=playhead->getPosition();
            if(pos) {
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
                playing=pos->getIsPlaying()||pos->getIsRecording();
#endif
                if(const auto samples=pos->getTimeInSamples()) {
                    // Ignore negative preroll and out-of-range host samples
                    // before llround, avoiding undefined signed overflow.
                    if(*samples>=0) {
                        const double ms=double(*samples)*1000.0/rate;
                        if(std::isfinite(ms) && ms>=0.0 && ms<=2147483647.0)
                            positionMs=int64_t(std::llround(ms));
                    }
                }
                else if(const auto seconds=pos->getTimeInSeconds()) {
                    // FL Studio/other VST3 hosts may supply seconds but
                    // omit sample position. Never fabricate a clock.
                    if(std::isfinite(*seconds) && *seconds>=0.0 &&
                       *seconds<=2147483.647)
                        positionMs=int64_t(std::llround(*seconds*1000.0));
                }
            }
        }
    }
    displayedHostMs_.store(positionMs,std::memory_order_relaxed);
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    playgroundPlaying_.store(playing);
#endif
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
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
void SkellyTuneRealtimeProcessor::processPlayground(float* const* io,int channels,int frames,int blockOffset,const skelly::Parameters& p) {
    if(!(preparedAudioRate_>0.0)||frames<=0)return;
    const int mode=playgroundRenderMode();
    const auto blockMs=displayedHostMs_.load(std::memory_order_relaxed);
    for(int offset=0;offset<frames;){
        const int absolute=blockOffset+offset;
        const auto atMs=skelly::PitchEditTimeline::frameMs(blockMs,absolute,preparedAudioRate_);
        const bool edit=skelly::playground::graphEditsSupported(mode);
        const int target=edit?pitchEdits_.targetAt(atMs):-1;
        const int count=edit?pitchEdits_.framesUntilBoundary(blockMs,absolute,frames-offset,preparedAudioRate_):frames-offset;
        float* segment[2]={io[0]+offset,channels==2?io[1]+offset:nullptr};
        playground_.process(segment,channels,count,p,mode,target);
        offset+=count;
    }
    {
        playgroundFormant_.setSemitones(playgroundFormantRequested()?state_.getRawParameterValue("playgroundFormantShift")->load():0.f);
        const bool bypass=state_.getRawParameterValue("bypass")->load()>0.5f || !playgroundFormantRequested();
        for(int i=0;i<frames;++i){
            const float l=io[0][i],r=channels==2?io[1][i]:l;
            const auto y=playgroundFormant_.process(l,r,bypass);
            io[0][i]=y[0];if(channels==2)io[1][i]=y[1];
        }
    }
    const auto t=playground_.telemetry();
    playgroundActive_.store(playground_.activeMode(),std::memory_order_relaxed);
    displayedPitchHz_.store(t.hz,std::memory_order_relaxed);
    displayedConfidence_.store(t.confidence,std::memory_order_relaxed);
    displayedTargetNote_.store(t.target,std::memory_order_relaxed);
    displayedTargetPending_.store(t.pending,std::memory_order_relaxed);
    displayedCorrectionCents_.store(t.cents,std::memory_order_relaxed);
}
#endif
void SkellyTuneRealtimeProcessor::processBlock(juce::AudioBuffer<float>& b, juce::MidiBuffer&) {
    juce::ScopedNoDenormals guard;
    const int ch = std::min(2, b.getNumChannels());
    if (ch == 0 || b.getNumSamples() <= 0) return;
    const auto params = currentParameters();
    displayedReferenceA_.store(params.referenceA,std::memory_order_relaxed);
    updateTimelinePosition();
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    float* labIO[2]={b.getWritePointer(0),ch==2?b.getWritePointer(1):nullptr};
    processPlayground(labIO,ch,b.getNumSamples(),0,params);
    return;
#endif
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
    displayedReferenceA_.store(params.referenceA,std::memory_order_relaxed);
    updateTimelinePosition();
#if !defined(SKELLY_PITCH_PLAYGROUND) || !SKELLY_PITCH_PLAYGROUND
    engine_.setParameters(params);
#endif
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
            for (int i = 0; i < n; ++i) {
                const double sample=src[i];
                // Extremely large but finite double audio must not
                // become an infinity inside float scratch.
                dst[i]=(std::isfinite(sample) &&
                        std::abs(sample)<=double(std::numeric_limits<float>::max()))
                    ?float(sample):0.0f;
            }
        }
        float* dst[2] = {doubleScratch_.getWritePointer(0),
                         ch == 2 ? doubleScratch_.getWritePointer(1) : nullptr};
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
        processPlayground(dst,ch,n,offset,params);
#elif defined(SKELLY_META_TEST) && SKELLY_META_TEST
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
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    return; // processPlayground already published the actual selected engine telemetry.
#elif defined(SKELLY_META_TEST) && SKELLY_META_TEST
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
    // Match the other engines: Amount controls pitch correction in cents;
    // Mix independently controls the blend of the processed and dry voices.
    // Zero Amount or Mix retains the declared fixed dry transport delay.
    meta_.setParameters(uint16_t(p.pitchClasses), double(p.referenceA),
                        double(p.amount), double(p.speedMs), 0.0, double(p.mix));
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
    displayedCorrectionCents_.store(voiced ?
        float(meta_.estimatedOutputCorrectionCents()) : 0.0f,
        std::memory_order_relaxed);
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
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    bool seenFormantEnabled=false,seenFormantShift=false;
#endif
    for (int i = 0; i < incoming.getNumChildren(); ++i) {
        const auto source = incoming.getChild(i);
        if (!source.hasType(juce::Identifier("PARAM"))) continue;
        const auto id = source.getProperty(juce::Identifier("id")).toString();
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
        if(id=="playgroundFormantEnabled"||id=="playgroundFormantShift"){
            const bool enabled=id=="playgroundFormantEnabled";
            if(enabled)seenFormantEnabled=true;else seenFormantShift=true;
            const auto value=decodeSavedNumber(source.getProperty("value"));
            auto target=safeState.getChildWithProperty(juce::Identifier("id"),id);
            if(value&&target.isValid()&&(enabled?(*value==0.||*value==1.):(*value>=-12.&&*value<=12.))){
                target.setProperty("value",*value,nullptr);++restored;
            }
            continue;
        }
        if(id=="playgroundLatencyProfile"){
            const auto value=decodeSavedNumber(source.getProperty("value"));
            auto target=safeState.getChildWithProperty(juce::Identifier("id"),id);
            if(value&&target.isValid()&&std::floor(*value)==*value&&*value>=0&&*value<double(skelly::playground::latencyNames.size())){
                // Old matched sessions migrate to Snap's default 2.5ms.
                target.setProperty("value",skelly::playground::interactiveLatencyProfile(int(*value)),nullptr);++restored;
            }
            continue;
        }
        if(id=="playgroundEngine"){
            const auto value=decodeSavedNumber(source.getProperty("value"));
            auto target=safeState.getChildWithProperty(juce::Identifier("id"),id);
            if(value&&target.isValid()&&std::floor(*value)==*value&&*value>=0&&*value<skelly::playground::legacySlotCount){
                target.setProperty("value",skelly::playground::toLegacySlot(skelly::playground::fromLegacySlot(int(*value))),nullptr);++restored;
            }
            continue;
        }
#endif
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
    const auto edits=incoming.getChildWithName("PITCH_EDITS");
    // An XML chunk with no known fields is not a valid user project;
    // it must not silently wipe existing manual pitch corrections.
    if(restored==0 && !restoredSkin && !edits.isValid())return;
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    // An older valid project must not inherit the newly enabled colour stage.
    if(restored>0){
        if(!seenFormantEnabled)safeState.getChildWithProperty("id","playgroundFormantEnabled").setProperty("value",0.,nullptr);
        if(!seenFormantShift)safeState.getChildWithProperty("id","playgroundFormantShift").setProperty("value",0.,nullptr);
    }
#endif
    if (restored > 0 || restoredSkin) state_.replaceState(safeState);
    // Valid legacy presets have no manual edits, so clear them on recall.
    pitchEdits_.clear();
    if(edits.isValid()) {
        std::array<bool,skelly::PitchEditTimeline::kCapacity> seen{};
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
            const int index=int(*slot);
            if(seen[std::size_t(index)])continue; // first valid slot wins
            seen[std::size_t(index)]=true;
            pitchEdits_.set(index,{int64_t(*first),int64_t(*last),int(*midi)});
        }
    }
#if defined(SKELLY_PITCH_PLAYGROUND) && SKELLY_PITCH_PLAYGROUND
    // On other threads the processor timer performs the same application.
    if(playgroundNeedsApply())applyPlaygroundLatency();
#endif
    // Invalid/unknown chunks do not damage the current valid state.
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new SkellyTuneRealtimeProcessor();
}
