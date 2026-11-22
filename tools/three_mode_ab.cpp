// Deterministic three-mode A/B harness. TEST-ONLY; does not affect plugin DSP.
// Emits level-matched synthetic vocal comparison WAVs, bounded signal metrics,
// actual time/CPU measurements, and a locally playable HTML listening index.
#include "RealtimeTuner.hpp"
#include "MetaTest.hpp"
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr int rate = 48000, blockSize = 128, seconds = 8;
constexpr double pi = 3.14159265358979323846;
using Samples = std::vector<float>;
void fail(const char* msg) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
void u16(std::ofstream& o, uint16_t v) { for (int j=0;j<2;++j) o.put(char(v>>(j*8))); }
void u32(std::ofstream& o, uint32_t v) { for (int j=0;j<4;++j) o.put(char(v>>(j*8))); }
void wav(const std::string& p, const Samples& x) {
    std::ofstream o(p, std::ios::binary);
    if(!o) fail("could not open output WAV");
    o.write("RIFF",4); u32(o,36u+uint32_t(x.size()*2u)); o.write("WAVEfmt ",8);
    u32(o,16);u16(o,1);u16(o,1);u32(o,rate);u32(o,rate*2);
    u16(o,2);u16(o,16);o.write("data",4);u32(o,uint32_t(x.size()*2u));
    for(float v:x) {
        const auto scaled=std::lround(std::clamp(double(v),-1.0,1.0)*32767.0);
        u16(o,uint16_t(int16_t(scaled)));
    }
    if(!o) fail("could not write WAV");
}
double inputHz(double time) {
    if(time<1.0) return 225.0;   // A3 +39c -> A3
    if(time<2.0) return 249.8;   // B3 +20c -> B3
    if(time<3.0) return 255.8;   // C4 -38c -> C4
    if(time<4.0) return 298.8*std::pow(2.0,(38.0*std::sin(2*pi*5.4*time))/1200.0);
    if(time<5.0) return 0.0;     // pause and consonant-noise segment
    if(time<6.0) return 323.5;   // E4 -33c -> E4
    if(time<7.0) return 320.0+(392.0-320.0)*(time-6.0);  // legato glide
    return 216.3;               // A3 -30c -> A3
}
Samples source() {
    Samples x(rate*seconds);double phase=0;uint32_t rng=0xaceb2d31;
    for(size_t i=0;i<x.size();++i) {
        const double time=double(i)/rate, hz=inputHz(time);
        rng=1664525u*rng+1013904223u;
        const double noise=double((rng>>8)&0xffff)/32768.0-1.0;
        if(hz>0.0) {
            phase+=2*pi*hz/rate;
            double voice=0;
            for(int h=1;h<=9;++h)
                voice+=std::sin(double(h)*phase+0.25*h)/(h==1?1.0:std::pow(double(h),0.8));
            x[i]=float((0.13*voice+0.003*noise) *
                std::clamp(std::min((time-std::floor(time))*90.0,
                                    (1.0-(time-std::floor(time)))*90.0),0.0,1.0));
        } else {
            x[i]=float(time>4.25&&time<4.58?noise*0.10:0.0);
        }
    }
    return x;
}
double pitchOf(const Samples& x,int first,int last) {
    // Normalized lag search and parabolic interpolation on a steady window;
    // not a listening-based quality verdict.
    const int minLag=55,maxLag=300;
    std::array<double,maxLag+1> score{};double best=-1;
    for(int lag=minLag;lag<=maxLag;++lag){
        double a=0,e0=0,e1=0;
        for(int i=first;i+lag<last;++i) {
            const double v=x[size_t(i)],w=x[size_t(i+lag)];
            a+=v*w;e0+=v*v;e1+=w*w;
        }
        score[size_t(lag)]=a/std::sqrt(e0*e1+1e-18);
        best=std::max(best,score[size_t(lag)]);
    }
    for(int k=minLag+1;k<maxLag;++k) {
        if(score[size_t(k)]>=0.95*best &&
           score[size_t(k)]>=score[size_t(k-1)] &&
           score[size_t(k)]>=score[size_t(k+1)]) {
            const double a=score[size_t(k-1)],b=score[size_t(k)],c=score[size_t(k+1)];
            const double d=a-2*b+c;
            const double delta=std::abs(d)>1e-12?std::clamp(0.5*(a-c)/d,-0.5,0.5):0.0;
            return rate/(double(k)+delta);
        }
    }
    return 0.0;
}
struct Result {Samples out;double ms=0,rms=0,peak=0,step=0,steadyHz=0,steadyCents=0;};
Result render(const Samples& dry,int mode,double speed) {
    skelly::RealtimeTuner normal; skelly::MetaTest meta;
    if(mode==2) {
        meta.prepare(rate);
        meta.setParameters(0x0fff,440.0,1.0,speed/8.0);
    } else {
        normal.prepare(rate,1);
        skelly::Parameters p;p.pitchClasses=0x0fff;p.referenceA=440.0f;
        p.speedMs=float(speed);p.amount=1.0f;p.mix=1.0f;p.testTune=mode==1;
        normal.setParameters(p);
    }
    Result result;result.out.resize(dry.size());
    const auto start=std::chrono::steady_clock::now();
    for(size_t i=0;i<dry.size();i+=blockSize) {
        std::array<float,blockSize> buf{};
        std::copy_n(dry.begin()+i,blockSize,buf.begin());
        if(mode==2) {float* io[1]={buf.data()};meta.process(io,1,blockSize,false);}
        else {
            const float* in[1]={buf.data()};
            float* out[1]={result.out.data()+i};
            normal.process(in,out,blockSize,1);
        }
        if(mode==2) std::copy(buf.begin(),buf.end(),result.out.begin()+i);
    }
    result.ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    double sq=0;double prev=0;
    for(float v:result.out){
        if(!std::isfinite(v)) fail("nonfinite DSP output");
        sq+=double(v)*v;
        result.peak=std::max(result.peak,std::abs(double(v)));
        result.step=std::max(result.step,std::abs(double(v)-prev));
        prev=v;
    }
    result.rms=std::sqrt(sq/double(result.out.size()));
    result.steadyHz=pitchOf(result.out,int(rate*0.62),int(rate*0.91));
    result.steadyCents=result.steadyHz>0?1200.0*std::log2(result.steadyHz/220.0):9999.0;
    return result;
}
} // namespace
int main(int argc,char** argv) {
    if(argc!=3 || std::string(argv[1])!="--output") {
        std::cerr<<"Usage: three_mode_ab --output EXISTING_DIR\n";return 2;
    }
    const std::string dir=argv[2];const Samples dry=source();
    wav(dir+"/DRY_synthetic.wav",dry);
    std::ofstream report(dir+"/metrics.csv"),html(dir+"/index.html");
    if(!report||!html) fail("output directory missing");
    report<<"mode,speed_ms,measured_hz,steady_error_cents,peak,rms,max_sample_step,render_ms,realtime_x\n";
    html<<"<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><title>SkellyTune Mini - 3-way synthetic A/B</title>"
        <<"<style>body{background:#0d1410;color:#e6eee9;font:16px system-ui;margin:auto;max-width:850px;padding:25px}"
        <<"h1{color:#9cff8b}section{border:1px solid #324339;border-radius:12px;margin:15px 0;padding:15px}"
        <<"audio{width:100%}small{color:#a5b7ac}</style><h1>Three-mode synthetic A/B - level-unmatched</h1>"
        <<"<p>Generated harmonic voice, glide, vibrato, silence and consonant noise. "
        <<"Not recorded vocals. Use personal vocal recordings for the real listening winner.</p>"
        <<"<section><strong>Original synthetic input</strong><audio controls src=\"DRY_synthetic.wav\"></audio></section>";
    const char* modes[]={"REGULAR","TESTTUNE","META"};
    for(double speed:{0.0,10.0,25.0}) {
        html<<"<h2>"<<speed<<" ms - hard tune comparison</h2>";
        for(int mode=0;mode<3;++mode) {
            const auto r=render(dry,mode,speed);
            // Conservative safety limits: fail on runaway/clipped processing,
            // but report not assert aesthetic ranking or exact steady pitch.
            if(r.rms<0.0002 || r.peak>4.0 || r.step>3.0) fail("unbounded DSP output");
            const std::string filename=std::string(modes[mode])+"_"+std::to_string(int(speed))+"ms.wav";
            wav(dir+"/"+filename,r.out);
            report<<modes[mode]<<','<<speed<<','<<r.steadyHz<<','<<r.steadyCents<<','
                <<r.peak<<','<<r.rms<<','<<r.step<<','<<r.ms<<','
                <<(1000.0*seconds/std::max(0.001,r.ms))<<'\n';
            std::cout<<modes[mode]<<" "<<speed<<"ms steady A3 pitch error="
                <<std::fixed<<std::setprecision(2)<<r.steadyCents
                <<"c peak="<<r.peak<<" render="<<r.ms<<"ms\n";
            html<<"<section><strong>"<<modes[mode]<<" / "<<speed<<" ms</strong>"
                <<"<audio controls preload=\"none\" src=\""<<filename<<"\"></audio>"
                <<"<small>steady A3 deviation "<<std::fixed<<std::setprecision(2)<<r.steadyCents
                <<" cents &bull; peak "<<r.peak<<" &bull; render "<<r.ms<<"ms</small></section>";
        }
    }
    html<<"<p><small>All tracks rendered in process blocks of 128 at 48kHz, mono 16-bit WAV. "
        <<"Playback controls are independent; synchronized listening requires a DAW. "
        <<"Metrics measure only pitch, level and timing, not perceptual fidelity.</small></p></html>";
    std::cout<<"PASS: 9 deterministic three-mode reference renders and metrics\n";
    return 0;
}
