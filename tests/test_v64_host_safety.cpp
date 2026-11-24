#include "RealtimeTuner.hpp"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

static void check(bool ok,const char* msg) {
    if (!ok) { std::cerr << "FAIL: " << msg << "\n"; std::exit(1); }
}
int main() {
    constexpr auto nan=std::numeric_limits<double>::quiet_NaN();
    constexpr auto inf=std::numeric_limits<double>::infinity();
    skelly::CausalPitchTracker tracker;
    tracker.prepare(nan);
    check(std::isfinite(tracker.reducedRate()) && tracker.reducedRate()>0.0,
          "NaN detector sample rate poisoned the tracker");
    tracker.prepare(inf);
    check(std::isfinite(tracker.reducedRate()) && tracker.reducedRate()>0.0,
          "infinite detector sample rate poisoned the tracker");
    tracker.prepare(-100.0);
    check(std::isfinite(tracker.reducedRate()) && tracker.reducedRate()>0.0,
          "negative detector sample rate poisoned the tracker");
    tracker.setMinimumConfidence(float(nan));
    check(tracker.minimumConfidence()==0.62f,
          "NaN tracker confidence disabled candidate rejection");
    tracker.setMinimumConfidence(float(inf));
    check(tracker.minimumConfidence()==0.62f,
          "infinite tracker confidence accepted");
    tracker.setMinimumConfidence(0.30f);
    check(tracker.minimumConfidence()==0.48f,
          "finite lower confidence clamp changed");

    skelly::RealtimeTuner tuner;
    tuner.prepare(48000.0,2);
    tuner.setShortVoicedBridgeDurationMs(float(nan));
    check(tuner.shortVoicedBridgeDurationMs()==12.0,
          "NaN bridge timeout reached the realtime gate");
    tuner.setShortVoicedBridgeDurationMs(float(inf));
    check(tuner.shortVoicedBridgeDurationMs()==12.0,
          "infinite bridge timeout reached the realtime gate");
    tuner.setShortVoicedBridgeDurationMs(44.0f);
    check(tuner.shortVoicedBridgeDurationMs()==35.0,
          "valid bridge upper bound changed");

    std::array<float,64> source{},output{};
    source.fill(0.25f); output.fill(0.75f);
    const float* goodInput[2]={source.data(),source.data()};
    float* goodOutput[2]={output.data(),output.data()};
    tuner.process(goodInput,goodOutput,64,0);
    check(output[13]==0.75f,"zero channels must leave output untouched");
    const float* badInput[2]={source.data(),nullptr};
    tuner.process(badInput,goodOutput,64,2);
    check(output[13]==0.75f,"null input lane partially wrote output");
    float* badOutput[2]={output.data(),nullptr};
    tuner.process(goodInput,badOutput,64,2);
    check(output[13]==0.75f,"null output lane partially wrote output");
    tuner.process(goodInput,goodOutput,0,2);
    check(output[13]==0.75f,"zero length altered output");

    // Normal prepared stereo still processes finite valid frames.
    tuner.process(goodInput,goodOutput,64,2);
    for(float x:output)check(std::isfinite(x),"valid stereo output nonfinite");
    std::cout << "PASS: detector/bridge nonfinite safety, host planar guards\n";
}
