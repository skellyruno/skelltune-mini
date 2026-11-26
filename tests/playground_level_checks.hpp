#pragma once
#include "PlaygroundBank.hpp"
#include <iostream>
#include <cstdlib>
#include <vector>
#include <cmath>
namespace level_checks {
inline void require(bool ok,const char* what){if(!ok){std::cerr<<"FAIL: "<<what<<'\n';std::exit(1);}}
inline void run(int fs){
    // Exercise the actual guard at a reproducible attenuation, including
    // inverted stereo. Check convergence without a second pitch detector.
    skelly::WetLevelGuard g;g.prepare(fs);
    const float dry[2]={.2f,-.14f};float wet[2]{};
    for(int i=0;i<fs/5;++i){wet[0]=.1f;wet[1]=-.07f;g.process(dry,wet,2,true,1.,1.);}
    require(wet[0]>.13f&&wet[0]<=.13501f,"wet recovery bounded and effective");
    require(std::abs(wet[1]+.7f*wet[0])<1.e-6f,"wet recovery stereo link");
    for(double mix:{0.,.5,.95}){wet[0]=.1f;wet[1]=-.07f;g.process(dry,wet,2,true,mix,mix);require(wet[0]==.1f&&wet[1]==-.07f,"partial mix changed");}
    wet[0]=.1f;wet[1]=-.07f;g.process(dry,wet,2,true,1.,0.);require(wet[0]==.1f&&wet[1]==-.07f,"attack anchor changed");
    wet[0]=.1f;wet[1]=-.07f;g.process(dry,wet,2,false,1.,1.);require(wet[0]==.1f&&wet[1]==-.07f,"unvoiced recovery changed");
    wet[0]=.999f;wet[1]=-.7f;g.process(dry,wet,2,true,1.,1.);require(wet[0]<=1.f&&wet[0]>=.999f,"new overload");
    wet[0]=1.2f;wet[1]=-.9f;g.process(dry,wet,2,true,1.,1.);require(wet[0]==1.2f&&wet[1]==-.9f,"overload unexpectedly modified");
    g.reset();const float silent[2]={0,0};for(int i=0;i<fs/10;++i){wet[0]=wet[1]=0;g.process(silent,wet,2,true,1.,1.);require(wet[0]==0&&wet[1]==0,"silent guard output");}
    // Guard release must settle promptly after a level deficit disappears.
    g.reset();
    for(int i=0;i<fs/5;++i){wet[0]=.1f;wet[1]=-.07f;g.process(dry,wet,2,true,1.,1.);}
    const float recovered[2]={.3f,-.21f};
    for(int i=0;i<int(fs*.018);++i){wet[0]=recovered[0];wet[1]=recovered[1];g.process(dry,wet,2,true,1.,1.);}
    require(wet[0]>=.3f&&wet[0]<.305f,"gain release overshoot persisted");
    // New bounded overshoot control only on eligible full-wet voiced output.
    g.reset();
    for(int i=0;i<fs/5;++i){wet[0]=.3f;wet[1]=-.21f;g.process(dry,wet,2,true,1.,1.,true);}
    require(wet[0]>=.26399f&&wet[0]<.265f&&std::abs(wet[1]+.7f*wet[0])<1.e-6f,"bounded stereo overshoot attenuation");
    for(double mix:{0.,.5,.95}){wet[0]=.3f;wet[1]=-.21f;g.process(dry,wet,2,true,mix,mix,true);require(wet[0]==.3f&&wet[1]==-.21f,"overshoot changed partial Mix");}
    wet[0]=1.2f;wet[1]=-.84f;g.process(dry,wet,2,true,1.,1.,true);require(wet[0]==1.2f&&wet[1]==-.84f,"overshoot changed overloaded input");
    // Adaptive vowel candidate: identical envelopes at different volume are not coloration targets.
    skelly::AdaptiveVowelGuard vowel;vowel.prepare(fs);
    for(int i=0;i<fs/4;++i){const float x=.18f*std::sin(float(i)*.13f);float d[2]{x,-.7f*x},w[2]{.6f*x,-.42f*x};vowel.process(d,w,2,1.,.65);require(std::abs(w[0]-.6f*x)<2.e-5f&&std::abs(w[1]+.7f*w[0])<2.e-6f,"adaptive vowel mistaken level shift or stereo unlink");}
    for(int i=0;i<fs/10;++i){float d[2]{.4f,-.2f},w[2]{.1f,-.05f};vowel.process(d,w,2,0.,.65);require(w[0]==.1f&&w[1]==-.05f,"adaptive changed protected attack/bypass");}
    // New candidate controls: no level pumping at silence, protected endpoints, stereo and headroom.
    skelly::ChallengerDynamics dynamics;dynamics.prepare(fs);
    for(int i=0;i<fs/5;++i){float d[2]{.2f,-.14f},w[2]{.12f,-.084f};dynamics.process(d,w,2,true,1.,1.);if(i>fs/10)require(w[0]>.17f&&w[0]<=.181f&&std::abs(w[1]+.7f*w[0])<2.e-6f,"candidate wet recovery");}
    for(int i=0;i<fs/10;++i){float d[2]{.1f,-.07f},w[2]{.17f,-.119f};dynamics.process(d,w,2,true,1.,1.);if(i>fs/20)require(w[0]<.14f&&w[0]>.128f,"candidate overshoot");}
    {float d[2]{.4f,-.2f},w[2]{.999f,-.5f};dynamics.process(d,w,2,true,1.,1.);require(w[0]<=1.f,"candidate new overload");}
    {float d[2]{.4f,-.2f},w[2]{.1f,-.05f};dynamics.process(d,w,2,false,1.,1.);require(w[0]==.1f&&w[1]==-.05f,"candidate changed consonant");dynamics.process(d,w,2,true,.5,1.);require(w[0]==.1f&&w[1]==-.05f,"candidate changed partial mix");}
    for(int mode:{0,1,2,3,4})for(float ms:{0.f,1.f,10.f,25.f,100.f}){const float v=skelly::challengerSpeed(ms,mode);require(v>=0.f&&v<=ms,"candidate speed out of bounds");if(mode==2||mode==3||ms<=1||ms>=100)require(v==ms,"protected retune changed");}
    require(skelly::listeningSpeed(10.f,0)==skelly::challengerSpeed(10.f,0),"TESTTUNE promotion does not match measured 10ms");
    require(skelly::listeningSpeed(25.f,0)==25.f,"TESTTUNE 25ms motion regression promoted");
    require(skelly::listeningSpeed(10.f,1)==skelly::challengerSpeed(10.f,1),"META promotion mismatch");
    for(int mode:{2,3,4})require(skelly::listeningSpeed(10.f,mode)==10.f,"unmeasured engine promotion");
    require(skelly::listeningSpeed(25.f,4)==skelly::challengerSpeed(25.f,4),"MetaTune 25ms promotion mismatch");
    // Explicit higher-delay profiles now spend their existing host budget
    // on native synthesis instead of padding. All lower profiles are frozen.
    for(int profile=0;profile<6;++profile){
        skelly::playground::Bank legacy,quality;
        legacy.setNativeQualityEnabled(false);
        legacy.prepare(fs,2,127,profile,0);quality.prepare(fs,2,127,profile,0);
        if(profile>=4){for(int mode:{0,1})require(quality.nativeLatency(mode)==int(std::lround(fs*skelly::playground::latencyTargets[size_t(profile)]*.001)),"Mini quality native latency");}
        else for(int mode:{0,1})require(quality.nativeLatency(mode)==legacy.nativeLatency(mode),"low latency changed");
        require(quality.latencySamples()==legacy.latencySamples(),"Mini host delay changed");
    }
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
    {skelly::playground::Bank legacy,quality;legacy.setNativeQualityEnabled(false);
     legacy.prepare(fs,2,127,5,4);quality.prepare(fs,2,127,5,4);
     require(quality.nativeLatency(4)>legacy.nativeLatency(4)&&quality.latencySamples()==legacy.latencySamples(),"MetaTune quality must use existing 10ms budget");}
#endif
    // Previously the entire wet block inherited its final pitch flag. Use
    // voice/silence transitions to check the exact same audio at odd sizes.
    for(int mode:{0,1})for(int profile:{1,3,5}){
        const int n=fs;std::vector<float> src(n);double phase=0.;
        for(int i=0;i<n;++i){double t=double(i)/fs;phase+=2*skelly::kPi*(225.+15.*std::sin(2*skelly::kPi*5.*t))/fs;double v=std::fmod(t,.25);double env=std::clamp(std::min((v-.025)*120.,(.19-v)*120.),0.,1.);src[i]=float(env*(.13*std::sin(phase)+.035*std::sin(2*phase+.2)));}
        auto render=[&](int block){skelly::playground::Bank b;b.prepare(fs,2,127,profile,mode);auto l=src,r=src;for(auto& x:r)x*=-.7f;skelly::Parameters p;p.speedMs=10;p.pitchClasses=1u<<9;for(int i=0;i<n;i+=block){float* io[]={l.data()+i,r.data()+i};b.process(io,2,std::min(block,n-i),p,mode);}return std::make_pair(l,r);};
        require(render(1)==render(127),"wet level recovery depends on host block size");
    }
    // Recording response is opt-in for A/B, profile-limited, and has no
    // transport delay or standalone parameter changes. Check the actual
    // wet output on a moving pitch with silence and stereo polarity.
    for(int mode:{1,4}){
        if(mode>=skelly::playground::modeCount)continue;
        for(int profile:{1,3,5})for(float speed:{0.f,10.f,100.f}){
            const int n=fs/2;std::vector<float> src(n);double phase=0.;
            for(int i=0;i<n;++i){double t=double(i)/fs;phase+=2*skelly::kPi*(235.+20.*std::sin(2*skelly::kPi*5.*t))/fs;
                const double env=std::clamp(std::min((t-.02)*100.,(.46-t)*100.),0.,1.);
                src[i]=float(env*(.15*std::sin(phase)+.04*std::sin(2*phase+.3)));}
            int oldPdc=-1;
            auto render=[&](bool enabled){skelly::playground::Bank b;b.setRecordingResponseEnabled(enabled);b.prepare(fs,2,127,profile,mode);
                if(oldPdc<0)oldPdc=b.latencySamples();else require(b.latencySamples()==oldPdc,"recording response changed PDC");
                auto l=src,r=src;for(float& x:r)x*=-.7f;skelly::Parameters p;p.speedMs=speed;p.pitchClasses=1u<<9;
                for(int i=0;i<n;i+=127){float* io[]={l.data()+i,r.data()+i};b.process(io,2,std::min(127,n-i),p,mode);}
                for(int i=0;i<n;++i)require(std::isfinite(l[i])&&std::isfinite(r[i])&&std::abs(l[i])<1.f&&std::abs(r[i])<1.f,"recording response invalid output");
                return std::make_pair(l,r);};
            const bool changed=render(false)!=render(true);
            if(profile==5||speed>=50.f||(mode==4&&speed==0.f))require(!changed,"recording response changed protected setting");
            else if(mode==1)require(changed,"META recording response did not activate");
        }
    }
    // Freeze Classic/Crystal: exact paired output at every supported profile,
    // including non-unity Mix, polarity and moving note/level input.
    for(int mode:{2,3})for(int profile=0;profile<int(skelly::playground::latencyNames.size());++profile){
        const int n=fs/2;std::vector<float> src(n);double phase=0.;
        for(int i=0;i<n;++i){double t=double(i)/fs;phase+=2*skelly::kPi*(225.+17.*std::sin(2*skelly::kPi*4.*t))/fs;src[i]=float((.12+.08*std::sin(2*skelly::kPi*3.*t))*std::sin(phase));}
        auto render=[&](bool enabled){skelly::playground::Bank b;b.setListeningPromotion(enabled);b.setSynthesisCandidate(enabled);b.setEnvelopeCandidate(enabled);b.setAdaptiveVowelCandidate(enabled?.65:0.);b.setMotionCandidate(enabled);b.setDynamicsCandidate(enabled);b.setWetLevelRecoveryEnabled(enabled);b.setNativeQualityEnabled(enabled);b.setOvershootBalanceEnabled(enabled);b.setRecordingResponseEnabled(enabled);b.prepare(fs,2,127,profile,mode);auto l=src,r=src;for(auto& x:r)x*=-.7f;skelly::Parameters p;p.speedMs=10;p.pitchClasses=1u<<9;for(int i=0;i<n;i+=127){p.mix=i<n/2?1.f:.5f;float* io[]={l.data()+i,r.data()+i};b.process(io,2,std::min(127,n-i),p,mode);}return std::make_pair(l,r);};
        require(render(false)==render(true),"Classic/Crystal reference changed");
    }
    std::cout<<fs<<" PASS: bounded wet recovery and 12 frozen Classic/Crystal profile pairs\n";
}
} // namespace level_checks
