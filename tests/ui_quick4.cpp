// Real SkellyTune Mini JUCE editor screenshots, built and run LOCALLY.
// This is not dsp/tools/render.cpp (that utility renders audio to WAV).
// Four native component snapshots, never mockups; no plugin/DAW code changes.
#include "../Source/PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>

namespace {
struct Scene {
    const char* name;
    int width, height, mode;
};
bool changeParameter(SkellyTuneRealtimeProcessor& processor,
                     const char* id, float value) {
    auto* p = processor.parameters().getParameter(id);
    if (p == nullptr) return false;
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1(value));
    p->endChangeGesture();
    return true;
}
bool savePng(const juce::Image& image, const juce::File& file) {
    if (!image.isValid()) return false;
    auto stream = file.createOutputStream();
    if (!stream) return false;
    const bool written = juce::PNGImageFormat().writeImageToStream(image, *stream);
    stream->flush();
    return written && file.getSize() > 1000;
}
int colourCount(const juce::Image& image) {
    std::set<juce::uint32> colours;
    for (int y=2; y<image.getHeight(); y+=7)
        for (int x=2; x<image.getWidth(); x+=7)
            colours.insert(image.getPixelAt(x,y).getARGB());
    return static_cast<int>(colours.size());
}
bool controlsInside(juce::Component& editor) {
    for (int i=0;i<editor.getNumChildComponents();++i) {
        auto* c=editor.getChildComponent(i);
        if (c && c->isVisible() &&
            !editor.getLocalBounds().contains(c->getBounds())) {
            std::fprintf(stderr,"Offscreen Mini control: %s\n",c->getComponentID().toRawUTF8());
            return false;
        }
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    juce::String folderName("ui-quick4");
    bool full=false;
    for (int i=1;i<argc;++i) {
        const juce::String arg(argv[i]);
        if (arg=="--output" && i+1<argc) folderName=juce::String(argv[++i]);
        else if (arg=="--full") full=true;
        else {std::fprintf(stderr,"Usage: SkellyTuneQuick4 [--output folder] [--full]\n");return 2;}
    }
    const juce::File folder = juce::File::isAbsolutePath(folderName)
        ? juce::File(folderName)
        : juce::File::getCurrentWorkingDirectory().getChildFile(folderName);
    if (folder.createDirectory().failed()) return 3;

    SkellyTuneRealtimeProcessor processor;
    processor.prepareToPlay(48000.0, 256);
    std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
    if (!editor || !editor->findChildWithID("skelly.speed")
        || !editor->findChildWithID("skelly.a4")
        || !editor->findChildWithID("skelly.key")
        || !editor->findChildWithID("skelly.scale")
        || !editor->findChildWithID("skelly.note.1")) {
        std::fprintf(stderr,"Mini: real editor or expected controls missing\n");
        return 4;
    }
    // Mini intentionally has ONE theme (neon green). Fourth shot shows a
    // different genuine note-selection state, never an invented colour skin.
    const std::array<Scene,8> scenes{{
        {"main_default_432",432,414,0},
        {"key_scale_selected_432",432,414,1},
        {"retune_active_720",720,690,2},
        {"custom_notes_432",432,414,3},
        {"minimum_recheck_432",432,414,4},
        {"medium_recheck_720",720,690,5},
        {"maximum_recheck_1008",1008,966,6},
        {"a4_alternate_432",432,414,7}
    }};
    const int count=full ? static_cast<int>(scenes.size()) : 4;
    juce::Array<juce::var> rows;
    juce::Image sheet(juce::Image::ARGB,864,856,true);
    juce::Graphics grid(sheet);
    grid.fillAll(juce::Colour(0xff0b100e));
    int failures=0;
    for (int i=0; i<count; ++i) {
        const Scene& sc=scenes[static_cast<size_t>(i)];
        // Reset real host parameters, not simulated controls.
        bool ok=changeParameter(processor,"key",0.0f)
            && changeParameter(processor,"scale",0.0f)
            && changeParameter(processor,"a4",440.0f)
            && changeParameter(processor,"speed",25.0f)
            && changeParameter(processor,"noteMask",0.0f);
        if (!ok) { ++failures; break; }
        if (sc.mode==1) {
            ok=changeParameter(processor,"key",7.0f)
                && changeParameter(processor,"scale",2.0f)
                && changeParameter(processor,"a4",442.0f);
        } else if (sc.mode==2) {
            ok=changeParameter(processor,"speed",0.0f)
                && changeParameter(processor,"key",4.0f)
                && changeParameter(processor,"scale",1.0f);
        } else if (sc.mode==3) {
            ok=changeParameter(processor,"noteMask",0x555)
                && changeParameter(processor,"speed",7.0f);
        } else if (sc.mode==7) {
            ok=changeParameter(processor,"a4",432.0f);
        }
        if (!ok) { ++failures; break; }
        editor->setSize(sc.width,sc.height);
        editor->resized();
        // Let Mini's own timer update the real piano state on the message thread.
        juce::MessageManager::getInstance()->runDispatchLoopUntil(110);
        auto shot=editor->createComponentSnapshot(editor->getLocalBounds(),true);
        if (!shot.isValid() || shot.getWidth()!=sc.width || shot.getHeight()!=sc.height
            || colourCount(shot)<12 || !controlsInside(*editor)) {
            std::fprintf(stderr,"Invalid Mini screenshot: %s\n",sc.name);
            ++failures; continue;
        }
        const juce::File png=folder.getChildFile(juce::String(sc.name)+".png");
        if (!savePng(shot,png)) { ++failures; continue; }
        juce::var record(new juce::DynamicObject());
        record.getDynamicObject()->setProperty("file",png.getFileName());
        record.getDynamicObject()->setProperty("width",sc.width);
        record.getDynamicObject()->setProperty("height",sc.height);
        record.getDynamicObject()->setProperty("sampledColours",colourCount(shot));
        rows.add(record);
        if(i<4) {
            const int cx=(i%2)*432,cy=(i/2)*428;
            grid.setColour(juce::Colours::white);
            grid.setFont(juce::Font(juce::FontOptions(15.f)));
            grid.drawText(juce::String(sc.name).replaceCharacter('_',' '),
                          cx+8,cy+3,416,25,juce::Justification::centredLeft);
            grid.drawImageWithin(shot,cx+7,cy+31,418,389,
                                  juce::RectanglePlacement::centred);
        }
    }
    if(!savePng(sheet,folder.getChildFile("contact-sheet.png"))) ++failures;
    juce::var manifest(new juce::DynamicObject());
    manifest.getDynamicObject()->setProperty("project","SkellyTune Mini - native JUCE real editor");
    manifest.getDynamicObject()->setProperty("mode",full?"full":"quick4");
    manifest.getDynamicObject()->setProperty("count",rows.size());
    manifest.getDynamicObject()->setProperty("scenes",juce::var(rows));
    if(!folder.getChildFile("manifest.json").replaceWithText(juce::JSON::toString(manifest,false))) ++failures;
    processor.releaseResources();
    std::fprintf(stdout,"SkellyTune Mini LOCAL real JUCE capture: %d/%d scenes; errors %d\n",
                 rows.size(),count,failures);
    return failures==0 && rows.size()==count ? 0 : 1;
}
