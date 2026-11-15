#include "FallDetect.h"
#include <math.h>

//Impact: strong acceleration together with fast rotation
static const float IMPACT_G = 2.8f;
static const float IMPACT_DPS = 80.0f;

//Stillness after the impact
static const float STILL_TOLERANCE_G = 0.12f;     //Magnitude within this of 1 g
static const float STILL_DPS = 15.0f;             //Rotation below this
static const uint32_t STILL_REQUIRED_US = 1500000;//Still for this long before judging posture
static const uint32_t OBSERVE_TIMEOUT_US = 5000000;//Give up observing after this

//Posture change: gravity direction rotated by more than 40 degrees
static const float POSTURE_COSINE = 0.766f;

//Gravity tracking is trusted only near 1 g
static const float GRAVITY_MIN_G = 0.8f;
static const float GRAVITY_MAX_G = 1.2f;
static const float GRAVITY_ALPHA = 0.005f;

FallDetect::FallDetect() {
  phase = FallPhase::Normal;
  impactUs = stillSinceUs = events = 0;
  gravity[0] = gravity[1] = 0;
  gravity[2] = 1;
  reference[0] = reference[1] = 0;
  reference[2] = 1;
  initialized = false;
}

//Clear a raised flag or abandon the current observation
void FallDetect::dismiss() {
  phase = FallPhase::Normal;
  stillSinceUs = 0;
}

//Process one IMU sample
void FallDetect::update(const ImuSample& s) {
  if (!s.valid) return;

  const float a = sqrtf(s.ax*s.ax + s.ay*s.ay + s.az*s.az);   //Acceleration magnitude in g
  const float g = sqrtf(s.gx*s.gx + s.gy*s.gy + s.gz*s.gz);   //Rotation magnitude in dps

  //Seed gravity from the first sample, then track it slowly while near 1 g
  if (!initialized) {
    gravity[0] = s.ax;
    gravity[1] = s.ay;
    gravity[2] = s.az;
    initialized = true;
  }
  if (a > GRAVITY_MIN_G && a < GRAVITY_MAX_G) {
    gravity[0] += GRAVITY_ALPHA * (s.ax - gravity[0]);
    gravity[1] += GRAVITY_ALPHA * (s.ay - gravity[1]);
    gravity[2] += GRAVITY_ALPHA * (s.az - gravity[2]);
  }

  //A raised flag stays until dismissed
  if (phase == FallPhase::PossibleFall) return;

  //Wait for an impact, remembering the posture at that moment
  if (phase == FallPhase::Normal) {
    if (a > IMPACT_G && g > IMPACT_DPS) {
      for (int i = 0; i < 3; i++)
        reference[i] = gravity[i];
      impactUs = s.timestampUs;
      stillSinceUs = 0;
      phase = FallPhase::Observing;
    }
    return;
  }

  //Observing: give up if the person keeps moving normally
  if (s.timestampUs - impactUs > OBSERVE_TIMEOUT_US) {
    dismiss();
    return;
  }

  //Track how long the device has been still
  const bool still = fabsf(a - 1.0f) < STILL_TOLERANCE_G && g < STILL_DPS;
  if (still) {
    if (!stillSinceUs) stillSinceUs = s.timestampUs;
  } else {
    stillSinceUs = 0;
  }
  if (!stillSinceUs || s.timestampUs - stillSinceUs < STILL_REQUIRED_US) return;

  //Still long enough: compare the posture with the one before the impact
  const float refLen = sqrtf(reference[0]*reference[0] + reference[1]*reference[1] + reference[2]*reference[2]);
  const float curLen = sqrtf(gravity[0]*gravity[0] + gravity[1]*gravity[1] + gravity[2]*gravity[2]);
  if (refLen < 0.5f || curLen < 0.5f) return;

  const float cosine = (reference[0]*gravity[0] + reference[1]*gravity[1] + reference[2]*gravity[2]) / (refLen*curLen);
  if (cosine < POSTURE_COSINE) {
    phase = FallPhase::PossibleFall;
    ++events;
  }
}

bool FallDetect::isPossibleFall() { return phase == FallPhase::PossibleFall; }
FallPhase FallDetect::getPhase() { return phase; }
uint32_t FallDetect::getEvents() { return events; }
