#include "RealtimeTuner.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {
int failures = 0;
void check(bool ok, const char* msg) {
    std::cout << (ok ? "PASS  " : "FAIL  ") << msg << '\n';
    if (!ok) ++failures;
}
float hzToCents(float got, float expected) {
    return float(1200.0 * std::log2(got/expected));
}

float frequencyFromAutocorrelation(const std::vector<float>& audio, int sampleRate, int start, int len) {
    const int minLag = sampleRate/400, maxLag = sampleRate/90;
    int best = minLag;
    double bestCorrelation = -1.0;
    for (int lag = minLag; lag <= maxLag; ++lag) {
        double ab = 0.0, aa = 0.0, bb = 0.0;
        for (int i = start + lag; i < start+len; ++i) {
            const double a = audio[size_t(i)], b = audio[size_t(i-lag)];
            ab += a*b; aa += a*a; bb += b*b;
        }
        // Prefer the shortest strong period rather than 2x or 3x subharmonics.
        const double corr = ab / std::sqrt(std::max(1e-30, aa*bb))
                          - 0.025 * lag / double(maxLag);
        if (corr > bestCorrelation) { bestCorrelation = corr; best = lag; }
    }
    return float(sampleRate)/best;
}

void testDryLatency(int rate, int block) {
    skelly::RealtimeTuner e;
    e.prepare(rate, 2);
    skelly::Parameters p;
    p.mix = 0;
    e.setParameters(p);
    const int count = e.latencySamples() + 4*block + 32;
    std::vector<float> a(size_t(count), 0.0f), b(size_t(count), 0.0f),
        oa(size_t(count), -1.0f), ob(size_t(count), -1.0f);
    a[0] = 0.8f; b[0] = -0.5f;
    for (int n = 0; n < count; n+=block) {
        const int length = std::min(count-n, block);
        const float* src[2] = { a.data()+n, b.data()+n };
        float* dst[2] = { oa.data()+n, ob.data()+n };
        e.process(src, dst, length, 2);
    }
    int nonzero = 0;
    bool good = true;
    for (int n = 0; n < count; ++n) {
        const float expA = n == e.latencySamples() ? 0.8f : 0.0f;
        const float expB = n == e.latencySamples() ? -0.5f : 0.0f;
        if (std::abs(oa[n]-expA)>1e-5f || std::abs(ob[n]-expB)>1e-5f) good=false;
        if (std::abs(oa[n])>1e-6f) ++nonzero;
    }
    check(good && nonzero == 1, "sample-exact stereo dry latency");
}

void testDetector(double rate, float freq) {
    skelly::CausalPitchTracker detector;
    detector.prepare(rate);
    skelly::PitchEstimate e{};
    for (int i = 0; i < int(rate*0.55); ++i) {
        const double t = i / rate;
        float sample = float(0.5 * std::sin(2*skelly::kPi*freq*t)
            + 0.14 * std::sin(2*skelly::kPi*freq*2*t));
        detector.push(sample,e);
    }
    const bool good = e.valid && std::abs(hzToCents(e.hertz,freq)) < 18.0f && e.confidence > .65f;
    if (!good) std::cout << "  tracker: rate="<<rate<<" Hz="<<freq<<" actual="<<e.hertz<<" conf="<<e.confidence<<'\n';
    check(good, "pitch detector synthetic fundamental +/-18 cents");
}

void testAudibleShift() {
    constexpr int sampleRate = 48000;
    constexpr float inputHz = 225.0f;
    constexpr float expectedHz = 220.0f;
    skelly::RealtimeTuner e;
    e.prepare(sampleRate,1);
    skelly::Parameters p;
    p.speedMs = 6.0f;
    p.amount = 1.0f;
    p.mix = 1.0f;
    e.setParameters(p);
    constexpr int count=sampleRate*2;
    std::vector<float> raw(count), out(count);
    for (int i=0;i<count;++i) {
        const double t=double(i)/sampleRate;
        raw[i]=float(.35*std::sin(2*skelly::kPi*inputHz*t)+.12*std::sin(4*skelly::kPi*inputHz*t));
    }
    for (int n=0;n<count;n+=64) {
        const float* src[1] = {raw.data()+n};
        float* dst[1] = {out.data()+n};
        e.process(src,dst,std::min(64,count-n),1);
    }
    const auto fIn=frequencyFromAutocorrelation(raw,sampleRate, sampleRate, 24000);
    const auto fOut=frequencyFromAutocorrelation(out,sampleRate, sampleRate, 24000);
    const auto deviation=std::abs(hzToCents(fOut,expectedHz));
    std::cout << "  input="<<fIn<<"Hz output="<<fOut<<"Hz target="<<expectedHz<<"Hz, err="<<deviation<<" cents\n";
    check(std::abs(hzToCents(fIn,inputHz)) < 18, "baseline input pitch recognized");
    check(deviation < 35.0f && std::abs(fOut-fIn)>1.5f, "real audio is shifted toward A3, not bypassed");
    const auto peak=*std::max_element(out.begin(),out.end());
    check(std::isfinite(peak) && peak < 1.5f, "resynthesis finite amplitude, no major overshoot");
    check(e.currentTargetNote()==57, "note target A3 selected");
}

void testStereoInversionRegression(int sign, int rate, int block) {
    // v0.1 used (L+R)*0.5, which is identically zero for side-only vocals.
    // Both output channels must receive the same pitch correction without
    // accidentally collapsing the audible opposite-polarity stereo field.
    skelly::RealtimeTuner engine;
    engine.prepare(rate, 2);
    skelly::Parameters p;
    p.speedMs = 6.0f;
    p.amount = p.mix = 1.0f;
    engine.setParameters(p);
    const int count = rate * 2;
    std::vector<float> left(count),right(count),outL(count),outR(count);
    for (int i = 0; i < count; ++i) {
        const double t = double(i) / rate;
        const float sample = float(.35 * std::sin(2*skelly::kPi*225.0*t) +
                                 .10 * std::sin(4*skelly::kPi*225.0*t));
        left[i] = sample;
        right[i] = float(sign) * sample;
    }
    for (int n = 0; n < count; n += block) {
        const float* input[2] = {left.data()+n,right.data()+n};
        float* output[2] = {outL.data()+n,outR.data()+n};
        engine.process(input,output,std::min(block,count-n),2);
    }
    const int start = rate, length = rate/2;
    const float got = frequencyFromAutocorrelation(outL,rate,start,length);
    bool finite = true;
    double symmetry = 0.0, power = 0.0;
    for (int i = start; i < start+length; ++i) {
        finite = finite && std::isfinite(outL[i]) && std::isfinite(outR[i]);
        const double diff = outL[i] - sign*outR[i];
        symmetry += diff*diff;
        power += double(outL[i])*outL[i];
    }
    const bool pitchShifted = std::abs(hzToCents(got,220.0f)) < 38.0f &&
                              std::abs(got-225.0f) > 1.0f;
    if (!pitchShifted || symmetry > 1.0e-7) {
        std::cout << "  sign="<<sign<<" sr="<<rate<<" block="<<block
                  <<" f="<<got<<" stereo difference="<<symmetry<<'\n';
    }
    check(engine.currentTargetNote()==57 && pitchShifted && finite && power>5.0,
          sign<0 ? "opposite-polarity stereo retains tuned pitch" :
                   "correlated stereo retains tuned pitch");
    check(symmetry < 1.0e-7, "stereo phase relationship preserved");
}
void testRightOnlyStereo() {
    constexpr int rate=48000, count=rate*2;
    skelly::RealtimeTuner engine;
    engine.prepare(rate,2);
    skelly::Parameters p;
    p.speedMs = 6;
    engine.setParameters(p);
    std::vector<float> left(count,0),right(count),outL(count),outR(count);
    for(int i=0;i<count;++i)
        right[i]=float(.35*std::sin(2*skelly::kPi*225.0*i/rate));
    for(int n=0;n<count;n+=127) {
        const float* src[2]={left.data()+n,right.data()+n};
        float* dst[2]={outL.data()+n,outR.data()+n};
        engine.process(src,dst,std::min(127,count-n),2);
    }
    const float f=frequencyFromAutocorrelation(outR,rate,rate,rate/2);
    double leftPower=0;
    for(float v:outL) leftPower+=double(v)*v;
    check(std::abs(hzToCents(f,220.0f))<38.0f && engine.currentTargetNote()==57,
          "right-only stereo source is pitch corrected");
    check(leftPower < 1.0e-12, "right-only source does not leak into left channel");
}

void testOnsetCue() {
    constexpr int rate=48000;
    skelly::CausalOnsetCue onset;
    onset.prepare(rate);
    bool found=false;
    for (int i=0;i<rate/8;++i) {
        const float x= i<rate/20 ? 0.0f : float(.60*std::sin(2*skelly::kPi*225*i/rate));
        onset.push(x);
        if (i>rate/20 && onset.score()>.7f) found=true;
    }
    check(found, "causal attack cue reacts to a clean syllable onset");
    check(onset.score()<.7f, "attack cue expires without retriggering on steady vowel");
}
void testShortPassingNoteGuard() {
    constexpr int rate=48000, length=rate*2;
    skelly::RealtimeTuner engine;
    engine.prepare(rate,1);
    skelly::Parameters p;
    p.speedMs=6.0f;
    p.mix=p.amount=1.0f;
    engine.setParameters(p);
    std::vector<float> in(length),out(length);
    double phase=0;
    for (int i=0;i<length;++i) {
        // 40-ms short passing note: sharp change, then return to A.
        const int start=rate, end=start+int(rate*0.040);
        double hz=(i>=start && i<end) ? 255.0 : 225.0;
        phase += 2*skelly::kPi*hz/rate;
        in[i]=float(0.34*std::sin(phase)+0.12*std::sin(2*phase));
    }
    int pendingFrames=0;
    bool switchedToPassingNote=false;
    for (int n=0;n<length;n+=64) {
        const float* src[1]={in.data()+n};
        float* dst[1]={out.data()+n};
        engine.process(src,dst,std::min(64,length-n),1);
        if(n>=rate && n<rate+rate/12) {
            pendingFrames+=engine.targetPending()?1:0;
            if(engine.currentTargetNote()==60) switchedToPassingNote=true;
        }
    }
    std::cout<<"  40ms passing note pending block count="<<pendingFrames
             <<" accepted="<<switchedToPassingNote<<"\n";
    check(pendingFrames>2, "short passing note causes pending-state protection");
    check(!switchedToPassingNote, "40ms passing note not forced into long-lived new target");
    // During a pending note the output should approach delayed dry instead
    // of being retuned against the previous musical target. Independent of
    // input block boundaries and insensitive to overall input amplitude.
    double err=0.0, refEnergy=0.0;
    for(int i=rate+int(rate*.020);i<rate+int(rate*.030);++i) {
        const double dry=in[size_t(i-engine.latencySamples())];
        const double delta=out[size_t(i)]-dry;
        err+=delta*delta;
        refEnergy+=dry*dry;
    }
    const double residual=std::sqrt(err/std::max(refEnergy,1e-15));
    std::cout<<"  passing-note 20-30ms delayed-dry residual="<<residual<<"\n";
    check(residual < 0.15, "pending note avoids forcing old target (dry residual under 0.15)");
    bool finite=true;
    for(float v:out) finite=finite && std::isfinite(v) && std::abs(v)<2.0f;
    check(finite, "passing-note output finite and bounded");
}
void testStrongVocalOnsetTransition() {
    constexpr int rate=48000, length=rate*2;
    skelly::RealtimeTuner engine;
    engine.prepare(rate,1);
    skelly::Parameters p; p.speedMs=6.0f; engine.setParameters(p);
    std::vector<float> in(length),out(length);
    double phase=0;
    for(int i=0;i<length;++i) {
        const double hz=i<rate ? 225.0 : 255.0;
        const double amp=i<rate ? 0.085 : 0.38;
        phase+=2*skelly::kPi*hz/rate;
        in[i]=float(amp*std::sin(phase));
    }
    int firstChanged=-1;
    for(int n=0;n<length;n+=64) {
        const float* src[1]={in.data()+n}; float* dst[1]={out.data()+n};
        engine.process(src,dst,std::min(64,length-n),1);
        if(n>=rate && engine.currentTargetNote()==60 && firstChanged<0) firstChanged=n-rate;
    }
    std::cout<<"  strong-attack confirmation="<<double(firstChanged)/rate*1000.0<<" ms\n";
    check(firstChanged>0 && firstChanged<rate/12,
          "strong amplitude onset accelerates confident note commitment");
}
void testConfirmedLegatoTransition() {
    constexpr int rate=48000, length=rate*2;
    skelly::RealtimeTuner engine;
    engine.prepare(rate,1);
    skelly::Parameters p; p.speedMs=6.0f; p.mix=p.amount=1.0f;
    engine.setParameters(p);
    std::vector<float> in(length),out(length);
    double phase=0;
    for(int i=0;i<length;++i) {
        const double hz=i<rate ? 225.0 : 255.0;
        phase+=2*skelly::kPi*hz/rate;
        in[i]=float(.35*std::sin(phase)+.09*std::sin(2*phase));
    }
    int firstChanged=-1, pendingBlocks=0;
    for(int n=0;n<length;n+=64) {
        const float* src[1]={in.data()+n};
        float* dst[1]={out.data()+n};
        engine.process(src,dst,std::min(64,length-n),1);
        if(n>=rate && engine.targetPending())++pendingBlocks;
        if(n>=rate && engine.currentTargetNote()==60 && firstChanged<0) firstChanged=n-rate;
    }
    std::cout<<"  legato confirmation="<<double(firstChanged)/rate*1000.0
             <<" ms; pending blocks="<<pendingBlocks<<"\n";
    check(firstChanged>0 && firstChanged<rate/4, "legato note changes after reasonable confirmation interval");
    const float got=frequencyFromAutocorrelation(out,rate,rate+rate/2,rate/3);
    check(std::abs(hzToCents(got,261.626f))<40.0f, "confirmed new C4 note produces tuned audio");
}

void testPitchCyclePhaseMatcher() {
    // Quasi-voiced, vibrato-rich source exposes the difference between
    // jumping by a perfect mathematical period and searching for a real
    // locally coherent cycle. Expected improvement is measured at source
    // read heads, independently of output pitch or audio level.
    constexpr int rate=48000;
    constexpr int total=rate*4;
    skelly::RealtimeTuner engine;
    engine.prepare(rate,1);
    skelly::Parameters p;
    p.speedMs=3;
    p.phaseAlignedJumps=true;
    p.transientProtection=0;
    engine.setParameters(p);
    std::vector<float> input(total),output(total);
    double phase=0;
    uint32_t state=0x0BADCAFE;
    for(int i=0;i<total;++i) {
        const double t=double(i)/rate;
        const double hz=222.0+7.0*std::sin(2*skelly::kPi*5*t)
                              +2.0*std::sin(2*skelly::kPi*11*t);
        phase+=2*skelly::kPi*hz/rate;
        state^=state<<13;state^=state>>17;state^=state<<5;
        const double noise=(double(state & 65535)/32767.5-1.0);
        const double env=0.7+0.3*std::pow(std::sin(2*skelly::kPi*0.8*t),2);
        input[i]=float(env*(.21*std::sin(phase)+.095*std::sin(2*phase+.9)
                      +.075*std::sin(3*phase+1.3)+.035*std::sin(4*phase+2)
                      +.015*noise));
    }
    for(int n=0;n<total;n+=127) {
        const float* in[]={input.data()+n};float* out[]={output.data()+n};
        engine.process(in,out,std::min(127,total-n),1);
    }
    std::cout << "  actual aligned jumps=" << engine.phaseAlignedJumpCount()
              << " phase mismatch reduction=" << engine.phaseMatchGainPercent() << "%\n";
    check(engine.phaseAlignedJumpCount()>=2,"phase-matched cycle shifts occur in realtime with guarded source-age window");
    check(engine.phaseMatchGainPercent()>20.0,"phase match reduces source-cycle mismatch by >20%");
    check(std::all_of(output.begin(),output.end(),[](float v){return std::isfinite(v) && std::abs(v)<1.6f;}),
          "phase-aligned shifter remains finite through vibrato");
}

void testBroadbandTransientCue() {
    constexpr int rate=48000;
    skelly::TransientNoiseCue cue;
    cue.prepare(rate);
    float voiced=0, noise=0;
    for(int n=0;n<rate;++n) {
        const auto x=float(.3*std::sin(2*skelly::kPi*200.0*n/rate));
        voiced=cue.push(x);
    }
    cue.reset();
    uint32_t state=1009;
    for(int n=0;n<rate;++n) {
        state^=state<<13;state^=state>>17;state^=state<<5;
        noise=cue.push(float(.3*(double(state)/4294967295.0*2.0-1.0)));
    }
    std::cout<<"  transient score voiced="<<voiced<<" broadband="<<noise<<'\n';
    check(voiced < 0.08f && noise>0.70f,"sibilant noise cue distinguishes voiced pitch from broadband noise");
}

void testSilentResetAndNonfinite() {
    skelly::RealtimeTuner tuner;tuner.prepare(48000,1);
    std::vector<float> in(1024,0),out(1024,0);
    in[5]=std::numeric_limits<float>::quiet_NaN();
    in[11]=std::numeric_limits<float>::infinity();
    const float* src[]={in.data()};float* dst[]={out.data()};
    tuner.process(src,dst,1024,1);
    check(std::all_of(out.begin(),out.end(),[](float x){return std::isfinite(x);}),
          "nonfinite input does not propagate into rendered output");
    tuner.reset();
    std::fill(in.begin(),in.end(),0);
    tuner.process(src,dst,1024,1);
    check(std::all_of(out.begin(),out.end(),[](float x){return x==0;}),
          "reset clears all audible history and DSP state");
}
}
int main() {
    for (int rate : {44100,48000,96000})
        for (int block : {1,32,64,127,256}) testDryLatency(rate, block);
    for (int rate : {44100,48000,96000})
        for (float freq : {80.f,110.f,225.f,440.f,700.f}) testDetector(rate, freq);
    testAudibleShift();
    for (int rate : {44100,48000,96000})
        for (int block : {1,64,127})
            for (int sign : {-1,1}) testStereoInversionRegression(sign,rate,block);
    testRightOnlyStereo();
    testOnsetCue();
    testShortPassingNoteGuard();
    testConfirmedLegatoTransition();
    testStrongVocalOnsetTransition();
    testPitchCyclePhaseMatcher();
    testBroadbandTransientCue();
    testSilentResetAndNonfinite();
    std::cout << "Total failures: " << failures << '\n';
    return failures ? 1:0;
}
