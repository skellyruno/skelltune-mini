#pragma once
// Studio Pitch + Formant: bounded stereo phase-vocoder colour stage.
// Opt-in revision; preserve legacy SpectralPitchFormant and its published state.
// Adapted from user-supplied MIT Auralis Voice-Changer src-tauri/src/dsp/pitch.rs.
// 1024-point Hann STFT, 4x overlap, 1024 samples latency (aligned active and neutral paths). This is NOT a
// low-latency pitch-correction/autotune engine or a neural voice conversion.
// All buffers are fixed arrays; no allocation, filesystem or host calls in DSP.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>

namespace skell::research {
class AlignedSpectralPitchChannel {
public:
    static constexpr int fftSize=1024,oversample=4,hop=fftSize/oversample;
    static constexpr int fifoOffset=fftSize-hop, latency=fftSize, bins=fftSize/2+1;
    void reset() noexcept {
        inFifo.fill(0.f);outFifo.fill(0.f);outAccum.fill(0.f);
        dryDelay.fill(0.f);dryWrite=0;blend=0.f;
        lastPhase.fill(0.f);sumPhase.fill(0.f);rover=fifoOffset;
    }
    void prepare(double rate) noexcept {
        sr=std::isfinite(rate)?std::clamp(rate,8000.,192000.):48000.;
        constexpr double windowPi=3.141592653589793238462643383279;
        for(int k=0;k<fftSize;++k)
            window[(size_t)k]=.5f-.5f*std::cos((float)(2*windowPi*k/fftSize));
        blendStep=1.f-std::exp(-1.f/(.025f*(float)sr));
        reset();
    }
    float process(float sample,float pitchRatio,float formantRatio) noexcept {
        const float in=std::isfinite(sample)?sample:0.f;
        inFifo[(size_t)rover]=in;
        const float out=outFifo[(size_t)(rover-fifoOffset)];
        const float aligned=dryDelay[(size_t)dryWrite];
        dryDelay[(size_t)dryWrite]=in;
        if(++dryWrite==latency)dryWrite=0;
        if(++rover>=fftSize) {
            rover=fifoOffset;
            processFrame(pitchRatio,formantRatio);
        }
        // Exact dry identity at 0-st pitch+formant while retaining the same
        // sample delay as the spectral engine. Smoothed transitions prevent
        // zipper clicks when moving into/out of the processor.
        const float target=std::abs(pitchRatio-1.f)<1e-5f &&
                           std::abs(formantRatio-1.f)<1e-5f?0.f:1.f;
        blend+=blendStep*(target-blend);
        const float value=aligned+(out-aligned)*blend;
        return std::isfinite(value)?value:0.f;
    }
private:
    using Complex=std::complex<float>;
    static constexpr float pi=3.14159265358979323846f;
    static void fft(std::array<Complex,fftSize>& v,bool inverse) noexcept {
        for(int i=1,j=0;i<fftSize;++i) {
            int bit=fftSize>>1;
            for(;j&bit;bit>>=1)j^=bit;
            j^=bit;
            if(i<j)std::swap(v[(size_t)i],v[(size_t)j]);
        }
        for(int len=2;len<=fftSize;len<<=1) {
            const float phase=(inverse?2.f:-2.f)*pi/len;
            const Complex step(std::cos(phase),std::sin(phase));
            for(int off=0;off<fftSize;off+=len) {
                Complex twist(1.f,0.f);
                for(int j=0;j<len/2;++j) {
                    const auto u=v[(size_t)(off+j)];
                    const auto t=v[(size_t)(off+j+len/2)]*twist;
                    v[(size_t)(off+j)]=u+t;
                    v[(size_t)(off+j+len/2)]=u-t;
                    twist*=step;
                }
            }
        }
    }
    void processFrame(float pitchRatio,float formantRatio) noexcept {
        pitchRatio=std::isfinite(pitchRatio)?std::clamp(pitchRatio,.5f,2.f):1.f;
        formantRatio=std::isfinite(formantRatio)?std::clamp(formantRatio,.5f,2.f):1.f;
        const float freqPerBin=(float)sr/fftSize;
        const float expected=2.f*pi*hop/fftSize;
        for(int k=0;k<fftSize;++k)
            spectrum[(size_t)k]=Complex(inFifo[(size_t)k]*window[(size_t)k],0.f);
        fft(spectrum,false);
        for(int k=0;k<bins;++k){
            const auto c=spectrum[(size_t)k];
            const float magnitude=std::abs(c);
            const float phase=std::arg(c);
            float delta=phase-lastPhase[(size_t)k];
            lastPhase[(size_t)k]=phase;
            delta-=(float)k*expected;
            int quotient=(int)(delta/pi);
            quotient+=quotient>=0?(quotient&1):-(quotient&1);
            delta-=pi*(float)quotient;
            delta=(float)oversample*delta/(2.f*pi);
            anaMagnitude[(size_t)k]=magnitude;
            anaPhase[(size_t)k]=phase;
            anaFrequency[(size_t)k]=((float)k+delta)*freqPerBin;
        }
        // Moving-average spectral envelope (formant profile).
        for(int k=0;k<bins;++k){
            const int lo=std::max(0,k-6),hi=std::min(bins-1,k+6);
            float sum=0.f;
            for(int j=lo;j<=hi;++j)sum+=anaMagnitude[(size_t)j];
            envelope[(size_t)k]=sum/(hi-lo+1);
        }
        synMagnitude.fill(0.f);synFrequency.fill(0.f);
        for(int k=0;k<bins;++k) {
            const int index=(int)((float)k*pitchRatio);
            if(index<bins) {
                synMagnitude[(size_t)index]+=
                    anaMagnitude[(size_t)k]/std::max(1e-9f,envelope[(size_t)k]);
                synFrequency[(size_t)index]=anaFrequency[(size_t)k]*pitchRatio;
            }
        }
        spectrum.fill(Complex(0.f,0.f));
        for(int k=0;k<bins;++k) {
            const float source=(float)k/formantRatio;
            float shape=0.f;
            if(source<bins-1){
                const int index=(int)source;
                const float fraction=source-index;
                shape=envelope[(size_t)index]*(1.f-fraction)+
                      envelope[(size_t)index+1]*fraction;
            }
            // At low sample rates a formant envelope bin may contain almost
            // no energy at the new pitch (e.g. 220 -> 440 Hz at 8 kHz).
            // Unbounded envelope transfer then erases the new fundamental.
            // Retain at least 35% of the source spectral envelope so pitch
            // transposition remains audible without disabling formant shape.
            const int originalBin=std::clamp(
                (int)std::round((float)k/pitchRatio),0,bins-1);
            const float safeShape=std::max(
                shape,envelope[(size_t)originalBin]*.35f);
            const float magn=synMagnitude[(size_t)k]*safeShape;
            float freqError=synFrequency[(size_t)k]-(float)k*freqPerBin;
            freqError=freqError/freqPerBin*2.f*pi/oversample+(float)k*expected;
            sumPhase[(size_t)k]=std::remainder(sumPhase[(size_t)k]+freqError,2.f*pi);
            if(std::abs(pitchRatio-1.f)<1e-5f) sumPhase[(size_t)k]=anaPhase[(size_t)k];
            spectrum[(size_t)k]=std::polar(magn,sumPhase[(size_t)k]);
        }
        spectrum[0]=Complex(spectrum[0].real(),0.f);
        spectrum[(size_t)(bins-1)]=Complex(spectrum[(size_t)(bins-1)].real(),0.f);
        for(int k=1;k<bins-1;++k)
            spectrum[(size_t)(fftSize-k)]=std::conj(spectrum[(size_t)k]);
        fft(spectrum,true);
        // FFT is intentionally unnormalised in both directions, matching the
        // reference overlap-add gain formula.
        const float scale=1.f/((fftSize*.5f)*oversample);
        for(int k=0;k<fftSize;++k)
            outAccum[(size_t)k]+=window[(size_t)k]*spectrum[(size_t)k].real()*scale;
        for(int k=0;k<hop;++k)outFifo[(size_t)k]=outAccum[(size_t)k];
        for(int k=0;k<2*fftSize-hop;++k)
            outAccum[(size_t)k]=outAccum[(size_t)(k+hop)];
        for(int k=2*fftSize-hop;k<2*fftSize;++k)outAccum[(size_t)k]=0.f;
        for(int k=0;k<fftSize-hop;++k)inFifo[(size_t)k]=inFifo[(size_t)(k+hop)];
    }
    double sr=48000.;
    int rover=fifoOffset;
    std::array<float,fftSize> window{},inFifo{};
    std::array<float,latency> dryDelay{};
    int dryWrite=0;
    float blend=0.f,blendStep=.001f;
    std::array<float,hop> outFifo{};
    std::array<float,fftSize*2> outAccum{};
    std::array<float,bins> lastPhase{},sumPhase{},anaMagnitude{},anaFrequency{},anaPhase{},
                           envelope{},synMagnitude{},synFrequency{};
    std::array<Complex,fftSize> spectrum{};
};
class AlignedSpectralPitchFormant {
public:
    static constexpr int latencySamples=AlignedSpectralPitchChannel::latency;
    void prepare(double sampleRate) noexcept {
        for(auto& channel:voices)channel.prepare(sampleRate);
        pitch=1.f;formant=1.f;
    }
    void set(float pitchValue,float formantValue,float pitchRange,float formantRange) noexcept {
        const auto unit=[](float x,float fallback) noexcept {
            return std::isfinite(x)?std::clamp(x,0.f,1.f):fallback;
        };
        const float p=unit(pitchValue,.5f),f=unit(formantValue,.5f);
        const float pitchSemitones=(p-.5f)*2.f*(3.f+9.f*unit(pitchRange,1.f));
        const float formantSemitones=(f-.5f)*2.f*(2.f+10.f*unit(formantRange,1.f));
        pitch=std::pow(2.f,pitchSemitones/12.f);
        formant=std::pow(2.f,formantSemitones/12.f);
    }
    std::array<float,2> process(float left,float right) noexcept {
        return {voices[0].process(left,pitch,formant),
                voices[1].process(right,pitch,formant)};
    }
private:
    std::array<AlignedSpectralPitchChannel,2> voices{};
    float pitch=1.f,formant=1.f;
};
} // namespace skell::research
