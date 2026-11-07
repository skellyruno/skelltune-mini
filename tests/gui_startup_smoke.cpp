#include "../Source/PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <memory>
#include <exception>

// A successful VST3 build alone cannot detect an editor constructor crash.
// This executable exercises the same processor and editor used inside FL Studio.
int main() {
    try {
        juce::ScopedJuceInitialiser_GUI initialise;
        std::cerr << "STEP: JUCE init ok\n" << std::flush;
        SkellyTuneRealtimeProcessor processor;
        std::cerr << "STEP: processor ok\n" << std::flush;
        processor.prepareToPlay(48000.0, 256);
        std::cerr << "STEP: prepare ok\n" << std::flush;
        std::cerr << "STEP: before editor construction\n" << std::flush;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        std::cerr << "STEP: editor constructed\n" << std::flush;
        if (!editor || editor->getWidth() < 400 || editor->getHeight() < 400)
            return 2;
        editor->resized();
        std::cerr << "STEP: resized ok\n" << std::flush;
        juce::Image frame(juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
        juce::Graphics graphics(frame);
        editor->paint(graphics);
        std::cerr << "STEP: paint ok\n" << std::flush;
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
