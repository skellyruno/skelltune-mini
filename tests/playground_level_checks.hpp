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
    // Explicit higher-delay profiles now spend their existing host budget
    // on native synthesis instead of padding. All lower profiles are frozen.
    for(int profile=0;profile<6;++profile){
        skelly::playground::Bank legacy,quality;
        legacy.setNativeQualityEnabled(false);
        legacy.prepare(fs,2,127,profile,0);quality.prepare(fs,2,127,profile,0);
        if(profile>=4){for(int mode:{0,1,2})require(quality.nativeLatency(mode)==int(std::lround(fs*skelly::playground::latencyTargets[size_t(profile)]*.001)),"Mini quality native latency");}
        else for(int mode:{0,1,2})require(quality.nativeLatency(mode)==legacy.nativeLatency(mode),"low latency changed");
        require(quality.latencySamples()==legacy.latencySamples(),"Mini host delay changed");
    }
#if defined(SKELLY_PLAYGROUND_FULL) && SKELLY_PLAYGROUND_FULL
    {skelly::playground::Bank legacy,quality;legacy.setNativeQualityEnabled(false);
     legacy.prepare(fs,2,127,5,6);quality.prepare(fs,2,127,5,6);
     require(quality.nativeLatency(6)>legacy.nativeLatency(6)&&quality.latencySamples()==legacy.latencySamples(),"MetaTune quality must use existing 10ms budget");}
#endif
    // Previously the entire wet block inherited its final pitch flag. Use
    // voice/silence transitions to check the exact same audio at odd sizes.
    for(int mode:{0,1,2})for(int profile:{1,3,5}){
        const int n=fs;std::vector<float> src(n);double phase=0.;
        for(int i=0;i<n;++i){double t=double(i)/fs;phase+=2*skelly::kPi*(225.+15.*std::sin(2*skelly::kPi*5.*t))/fs;double v=std::fmod(t,.25);double env=std::clamp(std::min((v-.025)*120.,(.19-v)*120.),0.,1.);src[i]=float(env*(.13*std::sin(phase)+.035*std::sin(2*phase+.2)));}
        auto render=[&](int block){skelly::playground::Bank b;b.prepare(fs,2,127,profile,mode);auto l=src,r=src;for(auto& x:r)x*=-.7f;skelly::Parameters p;p.speedMs=10;p.pitchClasses=1u<<9;for(int i=0;i<n;i+=block){float* io[]={l.data()+i,r.data()+i};b.process(io,2,std::min(block,n-i),p,mode);}return std::make_pair(l,r);};
        require(render(1)==render(127),"wet level recovery depends on host block size");
    }
    // Recording response is opt-in for A/B, profile-limited, and has no
    // transport delay or standalone parameter changes. Check the actual
    // wet output on a moving pitch with silence and stereo polarity.
    for(int mode:{2,6}){
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
            if(profile==5||speed>=50.f||(mode==6&&speed==0.f))require(!changed,"recording response changed protected setting");
            else if(mode==2)require(changed,"META recording response did not activate");
        }
    }
    // Freeze Classic/Crystal: exact paired output at every supported profile,
    // including non-unity Mix, polarity and moving note/level input.
    for(int mode:{3,4})for(int profile=0;profile<int(skelly::playground::latencyNames.size());++profile){
        const int n=fs/2;std::vector<float> src(n);double phase=0.;
        for(int i=0;i<n;++i){double t=double(i)/fs;phase+=2*skelly::kPi*(225.+17.*std::sin(2*skelly::kPi*4.*t))/fs;src[i]=float((.12+.08*std::sin(2*skelly::kPi*3.*t))*std::sin(phase));}
        auto render=[&](bool enabled){skelly::playground::Bank b;b.setWetLevelRecoveryEnabled(enabled);b.setNativeQualityEnabled(enabled);b.setOvershootBalanceEnabled(enabled);b.setRecordingResponseEnabled(enabled);b.prepare(fs,2,127,profile,mode);auto l=src,r=src;for(auto& x:r)x*=-.7f;skelly::Parameters p;p.speedMs=10;p.pitchClasses=1u<<9;for(int i=0;i<n;i+=127){p.mix=i<n/2?1.f:.5f;float* io[]={l.data()+i,r.data()+i};b.process(io,2,std::min(127,n-i),p,mode);}return std::make_pair(l,r);};
        require(render(false)==render(true),"Classic/Crystal reference changed");
    }
    std::cout<<fs<<" PASS: bounded wet recovery and 12 frozen Classic/Crystal profile pairs\n";
}
} // namespace level_checks
