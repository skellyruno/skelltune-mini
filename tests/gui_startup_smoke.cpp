#include "../Source/PluginProcessor.h"
#include "../dsp/ScaleMask.hpp" // real effectiveMask() under mode-switch smoke
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <memory>
#include <exception>
#include <cmath>

// A successful VST3 build alone cannot detect an editor constructor crash.
// This executable exercises the same processor and editor used inside FL Studio.
int main() {
    try {
        juce::ScopedJuceInitialiser_GUI initialise;
        std::cerr << "STEP: JUCE init ok\n" << std::flush;
        SkellyTuneRealtimeProcessor processor;
        std::cerr << "STEP: processor ok\n" << std::flush;
        if (processor.getNumPrograms() != 1 || processor.getProgramName(0).trim().isEmpty()) {
            std::cerr << "FAIL: VST3 default program must have a non-empty name\n";
            return 28;
        }
        std::cerr << "STEP: VST3 default program name ok\n" << std::flush;
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
        if (processor.getName() != "SkellyTune META Lab" ||
            processor.parameters().getParameter("meta") == nullptr) {
            std::cerr << "FAIL: META Lab identity or algorithm parameter missing\n";
            return 29;
        }
#else
        if (processor.parameters().getParameter("meta") != nullptr) {
            std::cerr << "FAIL: release build unexpectedly exposes META\n";
            return 30;
        }
#endif
        // Receiver must not assume any SkellKey value until an authenticated
        // packet is received. Joining the local hub needs no Mini control.
        if (processor.linkedMusicalKey().valid()) {
            std::cerr << "FAIL: invented tuning before SkellKey sent\n";
            return 19;
        }
        const auto incoming = skellcore::world::makeMusicalTuning(7, 1, 442.4);
        if (!processor.applySkellKeyTuning(incoming) ||
            std::abs(processor.parameters().getRawParameterValue("key")->load() - 7.0f) > 0.01f ||
            std::abs(processor.parameters().getRawParameterValue("scale")->load() - 2.0f) > 0.01f ||
            std::abs(processor.parameters().getRawParameterValue("a4")->load() - 442.4f) > 0.06f) {
            std::cerr << "FAIL: SkellKey did not synchronize Key, Scale and A4\n";
            return 19;
        }
        auto malformed = incoming;
        malformed.root = 12;
        if (processor.applySkellKeyTuning(malformed) ||
            std::abs(processor.parameters().getRawParameterValue("key")->load() - 7.0f) > 0.01f) {
            std::cerr << "FAIL: accepted invalid external key\n";
            return 21;
        }
        std::cerr << "STEP: verified SkellKey key/scale/A4 receive + malformed rejection\n"
                  << std::flush;
        // Theme is UI-only, stored in a non-automatable property. Old plugin
        // parameter IDs remain intact after a state roundtrip.
        processor.setSkullSkin(skellcore::ui::SkullSkin::Ossuary);
        juce::MemoryBlock savedSkin;
        processor.getStateInformation(savedSkin);
        processor.setSkullSkin(skellcore::ui::SkullSkin::Neon);
        processor.setStateInformation(savedSkin.getData(),static_cast<int>(savedSkin.getSize()));
        if(processor.getSkullSkin()!=skellcore::ui::SkullSkin::Ossuary)return 20;
        processor.setSkullSkin(skellcore::ui::SkullSkin::Neon);

        // Out-of-order 64-bit host probe must never spin in a zero-stride loop.
        juce::AudioBuffer<double> earlyAudio(2, 64);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 64; ++i) earlyAudio.setSample(c, i, 0.125);
        juce::MidiBuffer earlyMidi;
        processor.processBlock(earlyAudio, earlyMidi);
        if (earlyAudio.getSample(0, 17) != 0.125 ||
            earlyAudio.getSample(1, 46) != 0.125) return 3;
        std::cerr << "STEP: unprepared 64-bit probe passthrough ok\n" << std::flush;
        processor.prepareToPlay(48000.0, 256);
        std::cerr << "STEP: prepare ok\n" << std::flush;
        std::cerr << "STEP: before editor construction\n" << std::flush;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        if (!processor.worldLink.requested() || !processor.followingSkellKey()) {
            std::cerr << "FAIL: Mini did not start automatic SkellKey listener\n";
            return 22;
        }
        std::cerr << "STEP: editor constructed and automatic listener started\n"
                  << std::flush;
        if (!editor || editor->getWidth() < 400 || editor->getHeight() < 400)
            return 2;
        editor->resized();
        const auto childrenInside = [&] {
            for (int i = 0; i < editor->getNumChildComponents(); ++i) {
                const auto* c = editor->getChildComponent(i);
                if (c != nullptr && c->isVisible() &&
                    !editor->getLocalBounds().contains(c->getBounds())) {
                    std::cerr << "FAIL: editor child outside bounds: "
                              << c->getName() << '\n';
                    return false;
                }
            }
            return true;
        };
        const auto validVisual = [&] {
            // Capture the whole component hierarchy (not just paint() of
            // the background), including children, knob and green note rings.
            const juce::Image snapshot =
                editor->createComponentSnapshot(editor->getLocalBounds(), false, 1.0f);
            if (!snapshot.isValid() || snapshot.getWidth() != editor->getWidth()
                || snapshot.getHeight() != editor->getHeight()) return false;
            int greenPixels = 0;
            for (int y=0; y<snapshot.getHeight(); y+=3)
                for (int x=0; x<snapshot.getWidth(); x+=3) {
                    const auto c = snapshot.getPixelAt(x,y);
                    if (c.getGreen() > c.getRed()+30 &&
                        c.getGreen() > c.getBlue()+35 &&
                        c.getGreen() > 105) ++greenPixels;
                }
            return greenPixels > 20; // neon accents must actually render
        };
        const auto savePreview = [&] (const juce::String& name) {
            const auto snapshot =
                editor->createComponentSnapshot(editor->getLocalBounds(), false, 1.0f);
            const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(name);
            auto stream = file.createOutputStream();
            return stream != nullptr &&
                   juce::PNGImageFormat().writeImageToStream(snapshot, *stream);
        };
        if (!childrenInside() || !validVisual()) return 4;
        if (!savePreview("SkellyTuneMini_UI_432.png")) return 14;
        std::cerr << "STEP: default 432x414 layout + neon rendering ok\n" << std::flush;
        editor->setSize(720, 690);
        editor->resized();
        if (!childrenInside() || !validVisual()) return 5;
        if (!savePreview("SkellyTuneMini_UI_720.png")) return 15;
        std::cerr << "STEP: full 720x690 layout ok\n" << std::flush;
        editor->setSize(1008, 966);
        editor->resized();
        if (!childrenInside() || !validVisual()) return 6;
        if (!savePreview("SkellyTuneMini_UI_1008.png")) return 16;
        std::cerr << "STEP: large 1008x966 layout ok\n" << std::flush;
        editor->setSize(432, 414);
        editor->resized();
        if (!childrenInside() || !validVisual()) return 7;
        std::cerr << "STEP: compact return layout ok\n" << std::flush;
        if (!editor->findChildWithID("skelly.algorithm")) {
            std::cerr << "FAIL: algorithm selector missing\n";
            return 31;
        }
        // The optional analyzer must expand the real editor, preserve
        // original child hit targets and collapse back to the exact old
        // 432x414 window with no APVTS or plugin identity changes.
        auto* analyzer = dynamic_cast<juce::TextButton*>(
            editor->findChildWithID("skelly.analyzerToggle"));
        if (!analyzer) return 46;
        analyzer->onClick();
        // JUCE's Button::triggerClick() may queue an asynchronous
        // command when no message loop is running in this smoke process.
        // Call the installed actual button callback synchronously, then
        // verify the real editor size, layout and captured pixels.
        if (editor->getWidth()!=432 || editor->getHeight()!=620 ||
            !childrenInside() || !validVisual() ||
            !savePreview("SkellyTuneMini_Analyzer_UI_432.png")) {
            std::cerr << "FAIL: analyzer expand expected 432x620, got "
                      << editor->getWidth() << "x" << editor->getHeight() << "\n";
            return 47;
        }
        // The note editor is real state, not merely painted handles:
        // save, restore and clear it in the actual JUCE host processor.
        auto* clearEdits=dynamic_cast<juce::TextButton*>(
            editor->findChildWithID("skelly.clearPitchEdits"));
        auto* undoEdits=dynamic_cast<juce::TextButton*>(
            editor->findChildWithID("skelly.undoPitchEdits"));
        if(!clearEdits || !clearEdits->isVisible() ||
           !undoEdits || !undoEdits->isVisible())return 60;
        if(processor.pitchEdits().add({150,500,66})<0)return 61;
        juce::MemoryBlock editedState;
        processor.getStateInformation(editedState);
        processor.pitchEdits().clear();
        processor.setStateInformation(editedState.getData(),
                                     static_cast<int>(editedState.getSize()));
        if(processor.pitchEdits().targetAt(200)!=66)return 62;
        if(processor.pitchEdits().add({550,700,68})<0)return 64;
        undoEdits->onClick();
        if(processor.pitchEdits().targetAt(600)!=-1 ||
           processor.pitchEdits().targetAt(200)!=66)return 65;
        clearEdits->onClick();
        if(processor.pitchEdits().targetAt(200)!=-1)return 63;
        analyzer->onClick();
        if (editor->getWidth()!=432 || editor->getHeight()!=414 ||
            !childrenInside() || !validVisual()) return 48;
        std::cerr << "STEP: collapsible live pitch analyzer geometry verified\n"
                  << std::flush;
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
        // Exercise the actual opt-in processor path and save a true JUCE META
        // editor frame. This is for visual inspection, not a release artifact.
        if (auto* meta = processor.parameters().getParameter("meta")) {
            meta->beginChangeGesture();
            meta->setValueNotifyingHost(1.0f);
            meta->endChangeGesture();
        }
        juce::Thread::sleep(80);
        juce::Timer::callPendingTimersSynchronously();
        if (processor.parameters().getRawParameterValue("meta")->load() < 0.5f ||
            !savePreview("SkellyTuneMini_META_Lab_UI_432.png")) {
            std::cerr << "FAIL: META test selector activation or UI capture\n";
            return 32;
        }
        std::cerr << "STEP: META Lab selected and actual editor captured\n" << std::flush;
#endif
        juce::Image frame(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics graphics(frame);
        editor->paint(graphics);
        std::cerr << "STEP: paint ok\n" << std::flush;
        auto* speed = dynamic_cast<juce::Slider*>(editor->findChildWithID("skelly.speed"));
        auto* a4 = dynamic_cast<juce::Slider*>(editor->findChildWithID("skelly.a4"));
        auto* key = dynamic_cast<juce::ComboBox*>(editor->findChildWithID("skelly.key"));
        auto* scale = dynamic_cast<juce::ComboBox*>(editor->findChildWithID("skelly.scale"));
        auto* note = dynamic_cast<juce::Button*>(editor->findChildWithID("skelly.note.1"));
        if (!speed || !a4 || !key || !scale || !note ||
            editor->findChildWithID("skelly.bypass") ||
            editor->findChildWithID("skelly.more") ||
            editor->findChildWithID("skelly.amount") ||
            editor->findChildWithID("skelly.mix")) {
            std::cerr << "FAIL: mini editor controls missing or extra controls remain\n";
            return 8;
        }
        // Host "speed" remains 0..200ms for old projects. The new Mini
        // dial provides a TRUE 0..100ms range (100 slow to 0 hard) while
        // rotating clockwise/upwards increases correction speed.
        const auto readSpeed = [&] {
            return processor.parameters().getRawParameterValue("speed")->load();
        };
        if (std::abs(speed->getValue() - 75.0) > 0.02 ||
            std::abs(readSpeed() - 25.0f) > 0.02f) {
            std::cerr << "FAIL: old 25ms state did not map to inverse 0..100 dial\n";
            return 36;
        }
        speed->setValue(0.0, juce::sendNotificationSync); // 100 shown = 100ms
        if (std::abs(readSpeed() - 100.0f) > 0.02f ||
            !savePreview("SkellyTuneMini_Knob_100ms_UI_432.png")) return 34;
        speed->setValue(100.0, juce::sendNotificationSync); // 0 shown = fastest
        if (std::abs(readSpeed()) > 0.02f ||
            !savePreview("SkellyTuneMini_Knob_0ms_UI_432.png")) return 35;
        speed->setValue(50.0, juce::sendNotificationSync); // midpoint 50 shown
        if (std::abs(readSpeed() - 50.0f) > 0.02f) return 37;
        speed->setValue(93.0, juce::sendNotificationSync); // 7ms engine
        if (std::abs(readSpeed() - 7.0f) > 0.02f) return 9;
        // Host automation updates the *existing* APVTS parameter and feeds
        // back to the reversed dial without recursively writing the host.
        if (auto* parameter = processor.parameters().getParameter("speed"))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(60.0f));
        if (std::abs(readSpeed() - 60.0f) > 0.02f ||
            std::abs(speed->getValue() - 40.0) > 0.02) return 38;
        // Old host-saved values in the previous 0..200ms range are preserved
        // when recalled, rather than being silently rewritten by the UI.
        if (auto* parameter = processor.parameters().getParameter("speed"))
            parameter->setValueNotifyingHost(parameter->convertTo0to1(175.0f));
        if (std::abs(readSpeed() - 175.0f) > 0.02f ||
            std::abs(speed->getValue()) > 0.02) return 39;
        speed->setValue(50.0, juce::sendNotificationSync); // user opts into the new 100ms range
        if (std::abs(readSpeed() - 50.0f) > 0.02f) return 40;
        speed->setValue(75.0, juce::sendNotificationSync); // restore 25ms
        key->setSelectedId(8, juce::sendNotificationSync); // G (zero-based 7)
        if (std::abs(processor.parameters().getRawParameterValue("key")->load() - 7.0f) > 0.01f)
            return 10;
        key->setSelectedId(1, juce::sendNotificationSync);
        a4->setValue(442.0, juce::sendNotificationSync);
        if (std::abs(processor.parameters().getRawParameterValue("a4")->load() - 442.0f) > 0.02f)
            return 11;
        a4->setValue(440.0, juce::sendNotificationSync);
        scale->setSelectedId(2, juce::sendNotificationSync);
        if (std::abs(processor.parameters().getRawParameterValue("scale")->load() - 1.0f) > 0.01f)
            return 12;
        scale->setSelectedId(1, juce::sendNotificationSync);
        // Synchronous toggle notification exercises the piano's real onClick
        // and DAW automation path without requiring a nested message loop.
        note->setToggleState(!note->getToggleState(), juce::sendNotificationSync);
        if (processor.parameters().getRawParameterValue("noteMask")->load() < 1.0f)
            return 13;
        std::cerr << "STEP: retune, A4, key, scale and piano update DAW state\n"
                  << std::flush;
        juce::AudioBuffer<float> audio(2, 256);
        audio.clear();
        juce::MidiBuffer midi;
        processor.processBlock(audio, midi);
        std::cerr << "STEP: processBlock ok\n" << std::flush;

        // Feed real harmonic samples through the *actual processor* with the
        // editor open. A# input in C-major should light A# as INPUT and a
        // distinct enabled target note green, not invent an animation.
        auto* maskParam = processor.parameters().getParameter("noteMask");
        auto* scaleParam = processor.parameters().getParameter("scale");
        if (!maskParam || !scaleParam) return 41;
        maskParam->setValueNotifyingHost(maskParam->convertTo0to1(0.0f));
        scaleParam->setValueNotifyingHost(scaleParam->convertTo0to1(1.0f));
        auto* testParam = processor.parameters().getParameter("testTune");
        if (testParam) testParam->setValueNotifyingHost(0.0f);
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
        auto* metaParam = processor.parameters().getParameter("meta");
        if (metaParam) metaParam->setValueNotifyingHost(0.0f);
#endif
        auto renderNoteFeedback = [&] (const juce::String& filename) {
            double phase = 0.0;
            for (int block = 0; block < 480; ++block) {
                for (int n=0;n<128;++n) {
                    phase += juce::MathConstants<double>::twoPi * 235.8 / 48000.0;
                    float v = 0.0f;
                    for (int h=1;h<=6;++h)
                        v += float(0.18 * std::sin(h*phase + h*0.31) / double(h));
                    audio.setSample(0,n,v);
                    audio.setSample(1,n,v);
                }
                // 256 sample buffer is prepared; use the complete capacity
                // with a second half carrying the same voiced signal.
                for (int n=128;n<256;++n) {
                    phase += juce::MathConstants<double>::twoPi * 235.8 / 48000.0;
                    float v = 0.0f;
                    for (int h=1;h<=6;++h)
                        v += float(0.18 * std::sin(h*phase + h*0.31) / double(h));
                    audio.setSample(0,n,v);
                    audio.setSample(1,n,v);
                }
                processor.processBlock(audio, midi);
            }
            // Timer callbacks are intentionally asynchronous; a single
            // synchronous flush after sleeping may deliver just one trace
            // point. Advance at least three genuine message-thread ticks.
            for (int tick = 0; tick < 3; ++tick) {
                juce::Thread::sleep(40);
                juce::Timer::callPendingTimersSynchronously();
            }
            if (processor.displayedPitchHz() < 175.0f ||
                processor.displayedTargetNote() < 0) return false;
            const int inputPc = juce::roundToInt(
                69.0 + 12.0 * std::log2(double(processor.displayedPitchHz()) / 440.0)) % 12;
            const int targetPc = processor.displayedTargetNote() % 12;
            auto* inputKey = editor->findChildWithID("skelly.note." + juce::String(inputPc));
            auto* targetKey = editor->findChildWithID("skelly.note." + juce::String(targetPc));
            if (!inputKey || !targetKey) return false;
            const double inputLamp = double(inputKey->getProperties()["liveInput"]);
            const double targetLamp = double(targetKey->getProperties()["liveTarget"]);
            if (inputLamp < 0.10 || targetLamp < 0.10) return false;
            auto* inputMeter = editor->findChildWithID("skelly.inputReadout");
            auto* targetMeter = editor->findChildWithID("skelly.targetReadout");
            if (!inputMeter || !targetMeter ||
                double(inputMeter->getProperties()["pitchConfidence"]) < 0.55 ||
                double(targetMeter->getProperties()["pitchConfidence"]) < 0.55)
                return false;
            auto* speedControl = editor->findChildWithID("skelly.speed");
            if (!speedControl ||
                int(speedControl->getProperties()["traceSamples"]) < 2 ||
                speedControl->getProperties()["liveStatus"].toString() == "LISTENING")
                return false;
            return savePreview(filename);
        };
        if (!renderNoteFeedback("SkellyTuneMini_REGULAR_LiveNotes_UI_432.png")) return 41;
        analyzer->onClick();
        if (editor->getHeight()!=620 ||
            int(analyzer->getProperties()["analyzerSamples"]) < 3 ||
            !savePreview("SkellyTuneMini_Analyzer_Active_UI_432.png")) return 49;
        analyzer->onClick();
        if (testParam) testParam->setValueNotifyingHost(1.0f);
        if (!renderNoteFeedback("SkellyTuneMini_TESTTUNE_LiveNotes_UI_432.png")) return 42;
#if defined(SKELLY_META_TEST) && SKELLY_META_TEST
        if (testParam) testParam->setValueNotifyingHost(0.0f);
        if (metaParam) metaParam->setValueNotifyingHost(1.0f);
        if (!renderNoteFeedback("SkellyTuneMini_META_LiveNotes_UI_432.png")) return 43;
        if (metaParam) metaParam->setValueNotifyingHost(0.0f);

        // META -> REGULAR / TESTTUNE previously processed one whole block
        // with factory defaults because engine_.reset() ran AFTER the
        // APVTS values were applied. Exercise both 32-bit and 64-bit
        // host switching with non-default speed and G-major note mask.
        auto* speedForSwitch = processor.parameters().getParameter("speed");
        if (!speedForSwitch || !metaParam) return 44;
        speedForSwitch->setValueNotifyingHost(
            speedForSwitch->convertTo0to1(7.0f));
        maskParam->setValueNotifyingHost(
            maskParam->convertTo0to1(0.0f));
        key->setSelectedId(8, juce::sendNotificationSync); // G
        scaleParam->setValueNotifyingHost(
            scaleParam->convertTo0to1(1.0f)); // major
        const int expectedMask = skelly::effectiveMask(7, 1, 0);
        metaParam->setValueNotifyingHost(1.0f);
        processor.processBlock(audio, midi);
        metaParam->setValueNotifyingHost(0.0f);
        processor.processBlock(audio, midi);
        auto snapshot = processor.regularEngineParametersForLabTests();
        if (std::abs(snapshot.speedMs - 7.0f) > 0.02f ||
            snapshot.pitchClasses != expectedMask) {
            std::cerr << "FAIL: META -> REGULAR reset lost host tuning (32-bit)\n";
            return 44;
        }
        // Exercise the third leg of the 3-way selector as well:
        // META -> TESTTUNE must reapply both the algorithm and saved
        // retune/key settings on the FIRST 64-bit callback.
        if (testParam) testParam->setValueNotifyingHost(1.0f);
        juce::AudioBuffer<double> doubleAudio(2, 256);
        doubleAudio.clear();
        metaParam->setValueNotifyingHost(1.0f);
        processor.processBlock(doubleAudio, midi);
        metaParam->setValueNotifyingHost(0.0f);
        processor.processBlock(doubleAudio, midi);
        snapshot = processor.regularEngineParametersForLabTests();
        if (std::abs(snapshot.speedMs - 7.0f) > 0.02f ||
            snapshot.pitchClasses != expectedMask ||
            !snapshot.testTune) {
            std::cerr << "FAIL: META -> TESTTUNE reset lost host tuning (64-bit)\n";
            return 45;
        }
        if (testParam) testParam->setValueNotifyingHost(0.0f);
        speedForSwitch->setValueNotifyingHost(
            speedForSwitch->convertTo0to1(25.0f));
        key->setSelectedId(1, juce::sendNotificationSync);
#endif
        // Restore the original C Chromatic/A4 440 fixture before the
        // independent SkellKey IPC test: our live-light scene explicitly
        // selected C Major, but connecting to the bus must NOT change the
        // original tuning before the sender deliberately publishes it.
        scaleParam->setValueNotifyingHost(scaleParam->convertTo0to1(0.0f));
        maskParam->setValueNotifyingHost(maskParam->convertTo0to1(0.0f));
        if (testParam) testParam->setValueNotifyingHost(0.0f);
        std::cerr << "STEP: live sung-note and target-note feedback captured for each algorithm\n"
                  << std::flush;

        if (juce::SystemStats::getEnvironmentVariable("SKELL_LOCAL_IPC_TEST", {}) == "1") {
            // End-to-end link: local shared-memory bus + WorldLink receiver + deliberately
            // opted-in SkellKey sender, without Hub or Mini UI controls.
            skellcore::world::WorldLink sender("skell.key", "Skell Key smoke sender");
            if (!sender.join()) {
                std::cerr << "FAIL: sender could not open local tuning bus\n"; return 23;
            }
            for (int i=0; i<100 &&
                 (!sender.online() || !processor.worldLink.online()); ++i) {
                juce::Thread::sleep(50);
                juce::Timer::callPendingTimersSynchronously();
            }
            if (!sender.online() || !processor.worldLink.online()) {
                std::cerr << "FAIL: sender/receiver could not open local tuning bus\n";
                return 24;
            }
            // Merely connecting must not change Key, Scale or A4:
            // publisher action is explicit and must come first.
            if (std::abs(processor.parameters().getRawParameterValue("key")->load()) > 0.01f ||
                std::abs(processor.parameters().getRawParameterValue("scale")->load()) > 0.01f ||
                std::abs(processor.parameters().getRawParameterValue("a4")->load() - 440.0f) > 0.05f) {
                std::cerr << "FAIL: tuning changed before Send to Tune\n";
                return 25;
            }
            juce::Thread::sleep(50); // two independent plugin objects
            if (!sender.publishMusicalTuning(
                    skellcore::world::makeMusicalTuning(4, 1, 432.5))) {
                std::cerr << "FAIL: hubless Send to Tune could not publish\n";
                return 26;
            }
            bool received = false;
            for (int i=0;i<100;++i) {
                juce::Thread::sleep(50);
                juce::Timer::callPendingTimersSynchronously();
                const auto& state = processor.parameters();
                if (std::abs(state.getRawParameterValue("key")->load()-4.0f)<0.01f &&
                    std::abs(state.getRawParameterValue("scale")->load()-2.0f)<0.01f &&
                    std::abs(state.getRawParameterValue("a4")->load()-432.5f)<0.06f) {
                    received=true; break;
                }
            }
            sender.leave();
            if (!received) {
                std::cerr << "FAIL: Mini did not receive Key=E, Minor, A4=432.5\n";
                return 27;
            }
            std::cerr << "STEP: HUB-FREE cross-plugin Send to Tune -> Key/Scale/A4 without Mini controls\n"
                      << std::flush;
        }
        editor.reset();
        processor.releaseResources();
        std::cout << "PASS: processor startup, editor construction/layout/paint, audio callback, editor teardown\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "FAIL: unknown exception\n";
        return 1;
    }
}
