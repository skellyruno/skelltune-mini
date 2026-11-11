#include "../Source/PluginProcessor.h"
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
        speed->setValue(7.0, juce::sendNotificationSync);
        if (std::abs(processor.parameters().getRawParameterValue("speed")->load() - 7.0f) > 0.02f)
            return 9;
        speed->setValue(25.0, juce::sendNotificationSync);
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
