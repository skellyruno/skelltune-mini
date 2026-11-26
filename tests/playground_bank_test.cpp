#include "PlaygroundBank.hpp"
#include "playground_stereo_checks.hpp"
#include "playground_latency_checks.hpp"
#include "playground_amount_checks.hpp"
#include "playground_challenger_checks.hpp"
#include "playground_level_checks.hpp"
#include "TuningAnnouncement.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <cstdlib>
static void check(bool v,const char* s){if(!v){std::cerr<<"FAIL: "<<s<<"\n";std::exit(1);}}
static void tuningAnnouncementChecks(){
    skelly::TuningAnnouncement inbox;
    check(!inbox.acceptFull(0,7)&&!inbox.acceptKey(0),"invalid tuning announcement");
    check(inbox.acceptFull(1234,7),"first tuning send");
    for(int i=0;i<30;++i)check(!inbox.acceptFull(1234,7),"heartbeat reapplied");
    check(!inbox.acceptKey(7),"legacy mirror reapplied");
    check(!inbox.acceptFull(1234,7),"complete packet reapplied after mirror");
    check(inbox.acceptFull(1235,7),"new A4 ignored");
    check(inbox.acceptKey(9),"new legacy key ignored");
    check(!inbox.acceptKey(9),"legacy heartbeat reapplied");
    check(inbox.acceptFull(1236,9),"full tuning upgrade ignored");
    check(inbox.acceptFull(1234,7),"return to prior tuning ignored");
    inbox.reset();check(inbox.acceptFull(1234,7),"resend after withdrawal ignored");
    std::cout<<"PASS: tuning announcement deduplication\n";
}
int main(){tuningAnnouncementChecks();challenger_checks::run();using namespace skelly::playground; for(int fs:{44100,48000,96000}){
    level_checks::run(fs);
    playgroundStereoChecks(fs);
    playgroundLatencyChecks(fs);
    if(fs==48000)playgroundAmountChecks(fs);
    Bank b;b.prepare(fs,2);const int delay=b.latencySamples();check(delay>0,"no PDC");
    for(int mode=0;mode<modeCount;++mode){
        Bank fresh;fresh.prepare(fs,2);skelly::Parameters p;p.speedMs=0;p.pitchClasses=1u<<9;
        const int n=fs*2;std::vector<float> l(n),r(n);for(int i=0;i<n;++i){double phase=2*skelly::kPi*225*i/fs;l[i]=float(.18*std::sin(phase)+.04*std::sin(2*phase+.2));r[i]=l[i]*.7f;}
        auto src=l;auto dryL=l,dryR=r;
        for(int i=0;i<n;i+=127){float* io[]={l.data()+i,r.data()+i};fresh.process(io,2,std::min(127,n-i),p,mode);}
        for(int i=fs;i<n;++i)check(std::isfinite(l[i])&&std::isfinite(r[i]),"nonfinite wet");
        check(fresh.activeMode()==mode,"wrong engine");check(fresh.telemetry().target==57,"missing A3");
        // Independent complex projection avoids treating arbitrary phase as a pitch failure.
        auto tone=[&](double hz){double re=0,im=0;for(int i=fs;i<n;++i){const double ph=2*skelly::kPi*hz*i/fs;re+=l[i]*std::cos(ph);im+=l[i]*std::sin(ph);}return std::hypot(re,im);};
        check(tone(220)>4*tone(225),"requested engine failed pitch correction");
        for(int i=0;i<n;i+=127){float* io[]={dryL.data()+i,dryR.data()+i};fresh.process(io,2,std::min(127,n-i),p,mode,-1,true);}
        for(int i=delay+int(fs*.01);i<n;++i)check(dryL[i]==src[i-delay],"dry PDC mismatch");
        std::cout<<fs<<" "<<names[mode]<<" native="<<fresh.nativeLatency(mode)<<" common="<<delay<<" target=57"<<" wet tone="<<tone(220)<<"\n";
    }
    // Every mode, including the seventh route, must be visited. Compare
    // the exact active-mode transition sample under single-sample and odd
    // host blocks. The old switch quantized activation to callback boundaries.
    skelly::Parameters p;p.speedMs=0;
    const int segment=fs/2, length=segment*modeCount;
    std::vector<float> source(size_t(length),0.f);
    double phase=0.;for(int i=0;i<length;++i){phase+=2*skelly::kPi*225/fs;source[size_t(i)]=float(.15*std::sin(phase));}
    auto switching=[&](int block){Bank bank;bank.prepare(fs,2);auto left=source,right=source;for(float& x:right)x*=.7f;
        for(int off=0;off<length;){const int mode=off/segment;const int count=std::min(block,(mode+1)*segment-off);float* io[]={left.data()+off,right.data()+off};bank.process(io,2,count,p,mode);off+=count;}
        check(bank.activeMode()==modeCount-1,"last engine never selected");
        double maxStep=0.;float prev=0.f;for(int i=0;i<length;++i){check(std::isfinite(left[size_t(i)])&&std::isfinite(right[size_t(i)]),"switch nonfinite");maxStep=std::max(maxStep,std::abs(double(left[size_t(i)]-prev)));prev=left[size_t(i)];}
        check(maxStep<.2,"switch transient too large");std::cout<<"switch block="<<block<<" max step="<<maxStep<<"\n";return left;};
    const auto a=switching(1),c=switching(127);
    // Mini and Snap process per sample. Imported engines may use their own
    // block-rate controller; compare the five native portable routes exactly.
#if !defined(SKELLY_PLAYGROUND_FULL) || !SKELLY_PLAYGROUND_FULL
    double delta=0.;for(int i=0;i<length;++i)delta=std::max(delta,std::abs(double(a[size_t(i)]-c[size_t(i)])));
    std::cout<<"switch block-invariance max delta="<<delta<<"\n";
    check(delta<1.e-5,"algorithm handoff depends on host block size");
#endif
}std::cout<<"PLAYGROUND native engine routes + fixed dry PDC PASS\n";}
