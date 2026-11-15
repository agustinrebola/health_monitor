#include "Steps.h"
#include <math.h>

static const float MIN_THRESHOLD = 0.035f;        //Smallest peak that can be a step, in g
static const float THRESHOLD_GAIN = 1.7f;         //Threshold is this many times the activity level
static const float MAX_NOISE = 0.06f;             //Cap on the activity level
static const uint32_t MIN_INTERVAL_MS = 320;      //Faster than this is not a step
static const uint32_t MAX_INTERVAL_MS = 1100;     //Slower than this breaks the walk
static const uint32_t WALK_TIMEOUT_MS = 1500;     //No step for this long ends walking
static const float CONSISTENCY = 0.35f;           //Intervals must agree within this fraction

Steps::Steps() {
  reset();
}

//Clear the count and all detection state
void Steps::reset() {
  count = lastPeakMs = lastMotionMs = previousIntervalMs = 0;
  candidateCount = 0;
  baseline = 1.0f;
  filtered = prevFiltered = prevPrevFiltered = 0;
  noise = 0.012f;
  threshold = MIN_THRESHOLD;
  walking = false;
  cadence = 0;
}

//Process one accelerometer sample
void Steps::update(float ax, float ay, float az, uint32_t nowMs) {

  //Acceleration magnitude, about 1 g at rest
  const float mag = sqrtf(ax*ax + ay*ay + az*az);

  //Remove gravity with a slow average, then smooth what is left
  baseline += 0.005f * (mag - baseline);
  filtered += 0.30f * ((mag - baseline) - filtered);

  //Adaptive threshold from the recent activity level
  noise += 0.001f * (fabsf(filtered) - noise);
  noise = fminf(noise, MAX_NOISE);
  threshold = fmaxf(MIN_THRESHOLD, noise * THRESHOLD_GAIN);

  //A peak is where the previous sample was rising and is now no longer rising
  const bool peak =
      prevFiltered > prevPrevFiltered &&
      prevFiltered >= filtered &&
      prevFiltered > threshold;

  prevPrevFiltered = prevFiltered;
  prevFiltered = filtered;

  //Stop walking if no consistent step for a while
  if (walking && nowMs - lastMotionMs > WALK_TIMEOUT_MS) {
    walking = false;
    candidateCount = 0;
    lastPeakMs = previousIntervalMs = 0;
    cadence = 0;
  }

  if (!peak) return;

  //First peak only sets the timing reference
  if (lastPeakMs == 0) {
    lastPeakMs = nowMs;
    candidateCount = 1;
    return;
  }

  const uint32_t interval = nowMs - lastPeakMs;
  if (interval < MIN_INTERVAL_MS) return;    //Too soon, same step

  //Too long: start a new candidate sequence
  if (interval > MAX_INTERVAL_MS) {
    walking = false;
    candidateCount = 1;
    previousIntervalMs = 0;
    lastPeakMs = nowMs;
    return;
  }

  //Interval must be similar to the previous one
  const bool consistent =
      previousIntervalMs == 0 ||
      fabsf((float)interval - (float)previousIntervalMs) <= CONSISTENCY * previousIntervalMs;

  lastPeakMs = nowMs;

  if (!consistent) {
    walking = false;
    candidateCount = 1;
    previousIntervalMs = 0;
    return;
  }

  previousIntervalMs = interval;
  lastMotionMs = nowMs;
  cadence = 60000.0f / interval;

  //Three consistent peaks confirm walking; credit them together
  if (!walking) {
    if (++candidateCount >= 3) {
      walking = true;
      count += candidateCount;
    }
    return;
  }

  ++count;
}
