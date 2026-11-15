#include "../HealthWatch/src/HeartRate.h"
#include <cassert>
#include <cmath>

int main() {
  HeartRate processor;
  for (uint32_t i=1;i<=3000;++i) {
    const float wave=std::sin(float(i)*0.01f*2.0f*3.14159265f*1.2f);
    PpgSample s{i*10000, static_cast<uint32_t>(45000+800*wave),
                    static_cast<uint32_t>(50000+1000*wave), true};
    processor.update(s, 0);
  }
  const auto& state=processor.getState();
  assert(state.heartRateValid);
  assert(state.heartRateBpm > 68 && state.heartRateBpm < 76);
  assert(state.timingGaps == 0);
  assert(state.quality == SignalQuality::Good);
  processor.invalidateStream();
  assert(!processor.getState().heartRateValid);
  assert(processor.getState().intervalCount == 0);
  HeartRate quiet;
  for (uint32_t i=1;i<=3000;++i) {
    PpgSample s{i*10000, 45000, 50000, true};
    quiet.update(s, 0);
  }
  assert(quiet.getState().contactDetected);
  assert(!quiet.getState().heartRateValid);
  assert(quiet.getState().quality == SignalQuality::Invalid);
  HeartRate uncovered;
  for (uint32_t i=1;i<=1000;++i) {
    PpgSample s{i*10000, 1200, 2000, true};
    uncovered.update(s, 0);
  }
  assert(!uncovered.getState().contactDetected);
  assert(uncovered.getState().quality == SignalQuality::NoContact);
  // This module's observed finger signal can be below 0.1% AC/DC yet above
  // its uncovered optical noise; clean periodic beats should be recoverable.
  HeartRate weakFinger;
  for (uint32_t i=1;i<=3000;++i) {
    const float wave=std::sin(float(i)*0.01f*2.0f*3.14159265f*1.2f);
    PpgSample s{i*10000, static_cast<uint32_t>(90000+80*wave),
                    static_cast<uint32_t>(150000+120*wave), true};
    weakFinger.update(s, 0);
  }
  assert(weakFinger.getState().heartRateValid);
  assert(weakFinger.getState().heartRateBpm>68 && weakFinger.getState().heartRateBpm<76);
  assert(weakFinger.getState().periodicity>0.55f);
  // White optical noise may have comparable RMS but lacks beat periodicity.
  HeartRate opticalNoise;
  uint32_t randomState=1;
  for (uint32_t i=1;i<=6000;++i) {
    randomState=randomState*1664525u+1013904223u;
    const int noise=int((randomState>>16)%2001)-1000;
    PpgSample s{i*10000, static_cast<uint32_t>(90000+400*noise/1500),
                    static_cast<uint32_t>(150000+400*noise/1000), true};
    opticalNoise.update(s, 0);
    assert(!opticalNoise.getState().heartRateValid);
  }
  // Moving off a finger and back must invalidate the old estimate and reacquire.
  HeartRate repositioned;
  bool initiallyValid=false;
  uint32_t firstRecovery=0;
  for (uint32_t i=1;i<=7000;++i) {
    const bool onFinger = i<=3000 || i>4000;
    const float wave=std::sin(float(i)*0.01f*2.0f*3.14159265f*1.2f);
    const float ir=onFinger ? 136000+500*wave : 30000+25*wave;
    const float red=onFinger ? 90000+350*wave : 20000+15*wave;
    PpgSample s{i*10000, static_cast<uint32_t>(red), static_cast<uint32_t>(ir), true};
    repositioned.update(s, onFinger ? 0 : 120);
    if (i==3000) initiallyValid=repositioned.getState().heartRateValid;
    if (i==4000) assert(!repositioned.getState().heartRateValid);
    if (i>4000 && repositioned.getState().heartRateValid && !firstRecovery) firstRecovery=i;
  }
  assert(initiallyValid);
  assert(repositioned.getState().opticalRepositions >= 2);
  assert(firstRecovery && firstRecovery<5500); // reacquire within 15 s of return
  // A period of invalid optical samples also requires a clean reacquisition.
  HeartRate interrupted;
  bool recoveredAfterInvalid=false;
  for (uint32_t i=1;i<=5000;++i) {
    const bool blocked=i>2000 && i<=2500;
    const float wave=std::sin(float(i)*0.01f*2.0f*3.14159265f*1.2f);
    PpgSample s{i*10000,
                    blocked ? 0u : static_cast<uint32_t>(90000+350*wave),
                    blocked ? 0u : static_cast<uint32_t>(136000+500*wave), true};
    interrupted.update(s, 0);
    if (i==2500) assert(!interrupted.getState().heartRateValid);
    if (i>2500 && interrupted.getState().heartRateValid) recoveredAfterInvalid=true;
  }
  assert(recoveredAfterInvalid);
  // A slowly changing optical baseline must not erase an otherwise clear pulse.
  HeartRate drifting;
  for (uint32_t i=1;i<=6000;++i) {
    const float t=i*0.01f;
    const float dc=136000.0f + (t>30.0f ? (t-30.0f)*450.0f : 0.0f);
    const float wave=std::sin(t*2.0f*3.14159265f*1.2f);
    PpgSample s{i*10000, static_cast<uint32_t>(dc*0.67f+350*wave),
                    static_cast<uint32_t>(dc+500*wave), true};
    drifting.update(s, 0);
    if (i>3500 && i%500==0) {
      assert(drifting.getState().heartRateValid);
      assert(drifting.getState().heartRateBpm>68 && drifting.getState().heartRateBpm<76);
    }
  }
  // A secondary optical shoulder within one heartbeat must not reset cadence.
  HeartRate shouldered;
  for (uint32_t i=1;i<=6000;++i) {
    const float phase=float(i)*0.01f*2.0f*3.14159265f*1.2f;
    const float wave=std::sin(phase)+0.7f*std::sin(2*phase+1.5f);
    PpgSample s{i*10000, static_cast<uint32_t>(90000+300*wave),
                    static_cast<uint32_t>(136000+500*wave), true};
    shouldered.update(s, 0);
    if (i>1500 && i%500==0) {
      assert(shouldered.getState().heartRateValid);
      assert(shouldered.getState().heartRateBpm>68 && shouldered.getState().heartRateBpm<76);
    }
  }
  assert(shouldered.getState().heartRateValid);
  assert(shouldered.getState().heartRateBpm>68 && shouldered.getState().heartRateBpm<76);
  // Slow common-mode drift (wrist pressure, breathing) with no beat must never
  // publish a rate: it correlates at every short lag but has no period peak.
  HeartRate driftOnly;
  for (uint32_t i=1;i<=6000;++i) {
    const float t=i*0.01f;
    const float slow=std::sin(t*2.0f*3.14159265f*0.35f)+0.6f*std::sin(t*2.0f*3.14159265f*0.6f+0.8f);
    PpgSample s{i*10000, static_cast<uint32_t>(119000+350*slow),
                    static_cast<uint32_t>(137000+400*slow), true};
    driftOnly.update(s, 0);
    assert(!driftOnly.getState().heartRateValid);
  }
  // A short 0.7 Hz pressure burst in the middle of a 1.2 Hz pulse must not
  // publish a half-rate value; the established rate should resume afterwards.
  HeartRate burst;
  bool validBefore=false, validAfter=false;
  for (uint32_t i=1;i<=7000;++i) {
    const float t=i*0.01f;
    const bool drift = t>30.0f && t<=33.5f;
    const float wave = drift ? std::sin(t*2.0f*3.14159265f*0.69f)
                             : std::sin(t*2.0f*3.14159265f*1.2f);
    PpgSample s{i*10000, static_cast<uint32_t>(126000+200*wave),
                    static_cast<uint32_t>(157000+300*wave), true};
    burst.update(s, 0);
    if (burst.getState().heartRateValid) {
      assert(burst.getState().heartRateBpm>60);
      if (t<30.0f) validBefore=true;
      if (t>40.0f) validAfter=true;
    }
  }
  assert(validBefore && validAfter);
}
