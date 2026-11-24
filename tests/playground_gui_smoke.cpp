// Real JUCE processor/editor startup and seven/five-mode state/graph smoke.
#include "../Source/PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <iostream>
#include <cmath>
struct LatencyPlayhead : juce::AudioPlayHead {
    bool playing=false;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;p.setIsPlaying(playing);p.setTimeInSamples(0);return p;
    }
};
int main(){
    juce::ScopedJuceInitialiser_GUI gui;
    auto p=std::make_unique<SkellyTuneRealtimeProcessor>();
    p->setPlayConfigDetails(2,2,48000.,128);
    p->prepareToPlay(48000.,128);
    const int delay=p->getLatencySamples();
    if(delay<=0)return 1;
    auto editor=std::unique_ptr<juce::AudioProcessorEditor>(p->createEditor());
    if(!editor||editor->getWidth()!=432)return 2;
    auto* selector=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("skelly.algorithm"));
    if(!selector||selector->getNumItems()!=skelly::playground::modeCount)return 3;
    auto* param=p->parameters().getParameter("playgroundEngine");
    if(!param)return 4;
    juce::MidiBuffer midi;
    double phase=0;
    for(int mode=0;mode<skelly::playground::modeCount;++mode){
        selector->setSelectedId(mode+1,juce::sendNotificationSync);
        if(p->playgroundMode()!=mode||p->graphEditsSupported()!=(mode<3))return 5;
        for(int k=0;k<400;++k){
            juce::AudioBuffer<float> b(2,128);
            for(int i=0;i<128;++i){phase+=2*skelly::kPi*225./48000.;const float x=float(.18*std::sin(phase)+.04*std::sin(2*phase));b.setSample(0,i,x);b.setSample(1,i,.8f*x);}
            p->processBlock(b,midi);
            for(int c=0;c<2;++c)for(int i=0;i<128;++i)if(!std::isfinite(b.getSample(c,i)))return 6;
        }
        if(p->playgroundActiveMode()!=mode||p->getLatencySamples()!=delay||p->displayedPitchHz()<100.f)return 7;
        juce::AudioBuffer<double> dbl(2,257);dbl.clear();p->processBlock(dbl,midi);
        for(int c=0;c<2;++c)for(int i=0;i<257;++i)if(!std::isfinite(dbl.getSample(c,i)))return 8;
        std::cout<<skelly::playground::names[size_t(mode)]<<" active, native="<<p->playgroundNativeDelay(mode)<<" PDC="<<delay<<'\n';
    }
    juce::MemoryBlock state;p->getStateInformation(state);
    param->setValueNotifyingHost(0.f);
    p->setStateInformation(state.getData(),int(state.getSize()));
    if(p->playgroundMode()!=skelly::playground::modeCount-1)return 9;
    // Check the real toggle's component ID and run its handler synchronously;
    // a missing control or a posted-but-unhandled click cannot pass.
    auto* graph=dynamic_cast<juce::Button*>(editor->findChildWithID("skelly.analyzerToggle"));
    if(!graph||!graph->onClick)return 10;
    graph->onClick();
    if(editor->getWidth()!=432||editor->getHeight()!=700)return 11;
    auto* undo=editor->findChildWithID("skelly.undoPitchEdits");
    auto* clear=editor->findChildWithID("skelly.clearPitchEdits");
    if(!undo||!clear||!undo->isVisible()||!clear->isVisible())return 12;
    if(p->pitchEdits().add({100,250,57})<0)return 13;
    juce::MemoryBlock graphState;p->getStateInformation(graphState);
    p->pitchEdits().clear();
    p->setStateInformation(graphState.getData(),int(graphState.getSize()));
    if(p->pitchEdits().targetAt(150)!=57)return 14;
    selector->setSelectedId(1,juce::sendNotificationSync);
    if(!p->graphEditsSupported()||p->pitchEdits().targetAt(150)!=57)return 15;
    graph->onClick();
    if(editor->getHeight()!=414)return 16;
    graph->onClick();
    if(editor->getHeight()!=700)return 17;
    auto* latency=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("skelly.latencyProfile"));
    auto* apply=dynamic_cast<juce::Button*>(editor->findChildWithID("skelly.latencyApply"));
    if(!latency||!apply||!apply->onClick||latency->getNumItems()!=int(skelly::playground::latencyNames.size()))return 18;
    LatencyPlayhead host;p->setPlayHead(&host);
    host.playing=true;juce::AudioBuffer<float> frame(2,128);frame.clear();p->processBlock(frame,midi);
    latency->setSelectedId(2,juce::sendNotificationSync);
    if(!p->playgroundNeedsApply()||p->applyPlaygroundLatency()||p->getLatencySamples()!=delay)return 19;
    host.playing=false;frame.clear();p->processBlock(frame,midi);
    apply->onClick();
    if(p->playgroundAppliedProfile()!=1||p->getLatencySamples()!=72||p->playgroundNeedsApply())return 20;
    for(int mode=0;mode<skelly::playground::modeCount;++mode){
        selector->setSelectedId(mode+1,juce::sendNotificationSync);
        for(int profile=1;profile<int(skelly::playground::latencyNames.size());++profile){
            latency->setSelectedId(profile+1,juce::sendNotificationSync);apply->onClick();
            if(p->playgroundNeedsApply()||p->playgroundAppliedProfile()!=profile||p->playgroundRenderMode()!=mode)return 21;
            const int target=int(std::lround(48.*skelly::playground::latencyTargets[size_t(profile)]));
            if(p->getLatencySamples()!=std::max(target,p->playgroundNativeDelay(mode)))return 22;
            frame.clear();p->processBlock(frame,midi);
            if(p->playgroundActiveMode()!=mode)return 23;
        }
    }
    juce::MemoryBlock latencyState;p->getStateInformation(latencyState);
    latency->setSelectedId(1,juce::sendNotificationSync);apply->onClick();
    if(p->getLatencySamples()!=delay||p->playgroundAppliedProfile()!=0)return 24;
    p->setStateInformation(latencyState.getData(),int(latencyState.getSize()));
    if(p->playgroundProfile()!=5||!p->playgroundNeedsApply())return 25;
    apply->onClick();if(p->getLatencySamples()!=480||p->playgroundNeedsApply())return 26;
    if(p->pitchEdits().targetAt(150)!=57)return 27;
    p->setPlayHead(nullptr);
    juce::Image image(juce::Image::ARGB,editor->getWidth(),editor->getHeight(),true);
    juce::Graphics graphics(image);editor->paintEntireComponent(graphics,true);
    editor.reset();p->releaseResources();
    std::cout<<"PASS: real Playground JUCE selector, all active engines, 32/64-bit callbacks, stable PDC, state and editor paint\n";
}
