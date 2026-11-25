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
struct LatencyListener : juce::AudioProcessorListener {
    int changes=0,last=-1;
    void audioProcessorParameterChanged(juce::AudioProcessor*,int,float) override {}
    void audioProcessorChanged(juce::AudioProcessor* p,const ChangeDetails& c) override {
        if(c.latencyChanged){++changes;last=p->getLatencySamples();}
    }
};
int main(){
    juce::ScopedJuceInitialiser_GUI gui;
    auto p=std::make_unique<SkellyTuneRealtimeProcessor>();
    p->setPlayConfigDetails(2,2,48000.,128);
    p->prepareToPlay(48000.,128);
    const int delay=p->getLatencySamples();
    if(delay!=120||p->playgroundAppliedProfile()!=3)return 1;
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
        if(p->playgroundMode()!=mode||p->playgroundRenderMode()!=mode||p->graphEditsSupported()!=(mode<3))return 5;
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
    if(!latency||!latency->onChange||latency->getNumItems()!=5||editor->findChildWithID("skelly.latencyApply"))return 18;
    if(latency->getBounds().getHeight()<22||latency->getItemId(0)!=2||latency->getItemId(4)!=6)return 19;
    LatencyPlayhead host;p->setPlayHead(&host);
    LatencyListener listener;p->addListener(&listener);
    host.playing=true;juce::AudioBuffer<float> frame(2,128);frame.clear();p->processBlock(frame,midi);
    // Simulate the popup's async selection. A repaint timer must never
    // overwrite this choice before its notification is delivered.
    latency->setSelectedId(2,juce::sendNotificationAsync);
    juce::Thread::sleep(45);juce::Timer::callPendingTimersSynchronously();
    if(latency->getSelectedId()!=2)return 20;
    latency->onChange();
    if(p->playgroundAppliedProfile()!=1||p->getLatencySamples()!=72||p->playgroundNeedsApply()||listener.last!=72)return 21;
    for(int mode=0;mode<skelly::playground::modeCount;++mode){
        selector->setSelectedId(mode+1,juce::sendNotificationSync);
        for(int profile=1;profile<int(skelly::playground::latencyNames.size());++profile){
            const int before=p->getLatencySamples(),notifications=listener.changes;
            latency->setSelectedId(profile+1,juce::sendNotificationSync);
            if(p->playgroundNeedsApply()||p->playgroundAppliedProfile()!=profile||p->playgroundRenderMode()!=mode)return 22;
            const int target=int(std::lround(48.*skelly::playground::latencyTargets[size_t(profile)]));
            const int expected=std::max(target,p->playgroundNativeDelay(mode));
            if(p->getLatencySamples()!=expected)return 23;
            if(before!=expected&&(listener.changes<=notifications||listener.last!=expected))return 24;
            frame.clear();p->processBlock(frame,midi);
            if(p->playgroundActiveMode()!=mode)return 25;
        }
    }
    juce::MemoryBlock latencyState;p->getStateInformation(latencyState);
    latency->setSelectedId(4,juce::sendNotificationSync);
    if(p->getLatencySamples()!=120||p->playgroundAppliedProfile()!=3)return 26;
    p->setStateInformation(latencyState.getData(),int(latencyState.getSize()));
    if(p->playgroundProfile()!=5||p->playgroundNeedsApply()||p->getLatencySamples()!=480)return 28;
    // Old matched-state index 0 is still accepted, mapped to Snap's default.
    auto xml=juce::AudioProcessor::getXmlFromBinary(latencyState.getData(),int(latencyState.getSize()));
    if(!xml)return 29;
    for(auto* child:xml->getChildIterator())if(child->getStringAttribute("id")=="playgroundLatencyProfile")child->setAttribute("value",0);
    juce::MemoryBlock oldState;juce::AudioProcessor::copyXmlToBinary(*xml,oldState);
    p->setStateInformation(oldState.getData(),int(oldState.getSize()));
    if(p->playgroundProfile()!=3||p->playgroundNeedsApply()||p->getLatencySamples()!=120)return 30;
    auto* enableFormant=p->parameters().getParameter("playgroundFormantEnabled");
    auto* shiftFormant=p->parameters().getParameter("playgroundFormantShift");
    if(!enableFormant||!shiftFormant||enableFormant->isAutomatable())return 31;
    const int basePdc=p->getLatencySamples();
    enableFormant->setValueNotifyingHost(1.f);
    shiftFormant->setValueNotifyingHost(shiftFormant->convertTo0to1(-7.f));
    if(!p->applyPlaygroundLatency()||p->getLatencySamples()!=basePdc+1024||listener.last!=basePdc+1024)return 32;
    juce::MemoryBlock formantState;p->getStateInformation(formantState);
    for(int i=0;i<24;++i){frame.clear();p->processBlock(frame,midi);}
    enableFormant->setValueNotifyingHost(0.f);
    if(!p->applyPlaygroundLatency()||p->getLatencySamples()!=basePdc)return 33;
    p->setStateInformation(formantState.getData(),int(formantState.getSize()));
    if(p->getLatencySamples()!=basePdc+1024||std::abs(p->parameters().getRawParameterValue("playgroundFormantShift")->load()+7.f)>.11f)return 34;
    auto legacy=juce::AudioProcessor::getXmlFromBinary(oldState.getData(),int(oldState.getSize()));
    if(!legacy)return 35;
    for(int i=legacy->getNumChildElements()-1;i>=0;--i){auto* child=legacy->getChildElement(i);if(child->getStringAttribute("id").startsWith("playgroundFormant"))legacy->removeChildElement(child,true);}
    juce::MemoryBlock legacyFormant;juce::AudioProcessor::copyXmlToBinary(*legacy,legacyFormant);
    p->setStateInformation(legacyFormant.getData(),int(legacyFormant.getSize()));
    if(p->getLatencySamples()!=basePdc||p->playgroundFormantRequested())return 36;
    p->removeListener(&listener);
    if(p->pitchEdits().targetAt(150)!=57)return 27;
    p->setPlayHead(nullptr);
    juce::Image image(juce::Image::ARGB,editor->getWidth(),editor->getHeight(),true);
    juce::Graphics graphics(image);editor->paintEntireComponent(graphics,true);
    editor.reset();p->releaseResources();
    std::cout<<"PASS: real Playground selector, live latency selection and host PDC notifications, all engines, state migration and editor paint\n";
}
