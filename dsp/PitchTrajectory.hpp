#pragma once
#include <algorithm>
#include <cmath>
namespace skelly {
// Predict the detector's pitch at the causal read-head age. Do not predict
// across note boundaries or uncertain/octave frames. No transport/allocations.
class PitchTrajectory {
public:
 void reset() noexcept {valid_=false;note_=-1;previous_=velocity_=0.;}
 double predict(double midi,int note,double confidence,double hopSeconds,double leadSeconds,double strength) noexcept {
  if(!std::isfinite(midi)||confidence<.80||hopSeconds<=0.||note<0){reset();return 0.;}
  const double delta=midi-previous_;
  if(!valid_||note_!=note||std::abs(delta)>.35){valid_=true;note_=note;previous_=midi;velocity_=0.;return 0.;}
  const double v=std::clamp(delta/hopSeconds,-55.,55.);
  const double a=1.-std::exp(-hopSeconds/.006);
  velocity_+=a*(v-velocity_);previous_=midi;
  return std::clamp(velocity_*std::clamp(leadSeconds,-.012,.012)*strength,-.36,.36);
 }
private:
 bool valid_=false;int note_=-1;double previous_=0.,velocity_=0.;
};
}
