#include "Steps.h"
#include <math.h>

//Filter time constants in seconds
static const float GRAVITY_TAU = 0.8f;            //Gravity direction tracker
static const float BASELINE_TAU = 0.65f;          //Removes slow vertical drift
static const float SMOOTH_TAU = 0.06f;            //Removes high-frequency noise
static const float GYRO_TAU = 0.15f;              //Rotation rate smoothing
static const float NOISE_TAU = 3.0f;              //Activity level for the adaptive threshold

//Gravity tracking is trusted only near 1 g
static const float GRAVITY_MIN_G = 0.8f;
static const float GRAVITY_MAX_G = 1.2f;

//Adaptive threshold
static const float THRESHOLD_GAIN = 1.4f;         //Threshold is this many times the activity level
static const float THRESHOLD_MIN_G = 0.075f;      //Floor so sensor noise never triggers
static const float THRESHOLD_MAX_G = 0.25f;       //Ceiling so hard running still counts

//Cadence rules in microseconds
static const uint32_t REFRACTORY_US = 250000;     //Peaks closer than this are one step
static const uint32_t MAX_STEP_GAP_US = 1500000;  //Longer gaps break the walking streak
static const uint32_t IDLE_RESET_US = 2000000;    //No candidate for this long ends walking
static const uint32_t SAMPLE_GAP_US = 100000;     //Larger timestamp gaps reset the filters

//Rotation-dominated peaks are wrist turns, not steps
static const float ROTATION_DPS = 160.0f;
static const float ROTATION_MAX_PEAK_G = 0.16f;

//Movement that keeps the activity class out of Stationary
static const float MOTION_G = 0.06f;
static const float MOTION_DPS = 12.0f;

static const float RUNNING_SPM = 150.0f;          //Cadence at or above this is running

//Clamp x to the range [lo, hi]
static float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

Steps::Steps() {
  reset();
}

//Clear the step count and all filter state
void Steps::reset() {
  state = {};
  state.activity = Activity::Unknown;
  initialized = false;
  gravity[0] = gravity[1] = 0;
  gravity[2] = 1;
  baseline = smooth = prev = prevPrev = 0;
  noise = 0.02f;
  gyroSmooth = 0;
  previousUs = lastCandidateUs = lastMotionUs = 0;
  streak = 0;
  walking = false;
}

//Process one IMU sample and update the step state
const StepsState& Steps::update(const ImuSample& s) {
  state.candidate = false;
  state.confirmed = false;
  if (!s.valid) return state;

  //Seed gravity from the first sample
  if (!initialized) {
    gravity[0] = s.ax;
    gravity[1] = s.ay;
    gravity[2] = s.az;
    previousUs = s.timestampUs;
    initialized = true;
    return state;
  }

  const uint32_t elapsedUs = s.timestampUs - previousUs;   //Unsigned, rollover safe
  if (elapsedUs < 2000) return state;        //Duplicate or out-of-order sample

  //A gap invalidates cadence continuity and filter state
  if (elapsedUs > SAMPLE_GAP_US) {
    walking = false;
    streak = 0;
    state.cadenceSpm = 0;
    lastCandidateUs = 0;
    smooth = baseline = prev = prevPrev = 0;
  }
  previousUs = s.timestampUs;

  const float dt = clampf(elapsedUs * 1e-6f, 0.002f, 0.05f);
  const float amag = sqrtf(s.ax*s.ax + s.ay*s.ay + s.az*s.az);   //Acceleration magnitude in g
  const float gyro = sqrtf(s.gx*s.gx + s.gy*s.gy + s.gz*s.gz);   //Rotation magnitude in dps

  //Track gravity slowly; high acceleration makes tilt estimation less trusted
  const float gravityAlpha = (amag > GRAVITY_MIN_G && amag < GRAVITY_MAX_G) ? dt / (GRAVITY_TAU + dt) : 0.0f;
  gravity[0] += gravityAlpha * (s.ax - gravity[0]);
  gravity[1] += gravityAlpha * (s.ay - gravity[1]);
  gravity[2] += gravityAlpha * (s.az - gravity[2]);

  const float glen = sqrtf(gravity[0]*gravity[0] + gravity[1]*gravity[1] + gravity[2]*gravity[2]);
  if (glen < 0.5f) return state;

  //Acceleration along gravity with gravity itself removed
  const float vertical = (s.ax*gravity[0] + s.ay*gravity[1] + s.az*gravity[2]) / glen - glen;

  //Two first-order stages: remove slow baseline, then smooth high-frequency noise
  baseline += dt / (BASELINE_TAU + dt) * (vertical - baseline);
  const float dynamic = vertical - baseline;
  smooth += dt / (SMOOTH_TAU + dt) * (dynamic - smooth);
  gyroSmooth += dt / (GYRO_TAU + dt) * (gyro - gyroSmooth);

  //Activity level updates slowly so one impulse does not set a permanent threshold
  noise += dt / (NOISE_TAU + dt) * (fabsf(smooth) - noise);
  state.thresholdG = clampf(THRESHOLD_GAIN * noise, THRESHOLD_MIN_G, THRESHOLD_MAX_G);
  state.filteredG = smooth;
  state.gyroDps = gyroSmooth;

  //Detect a local maximum centred one sample back
  const bool peak =
      prev > prevPrev &&                       //Previous sample was rising
      prev >= smooth &&                        //Current sample is no longer rising
      prev > state.thresholdG;                 //Peak exceeds detection threshold

  const uint32_t since = s.timestampUs - lastCandidateUs;

  if (peak && (lastCandidateUs == 0 || since >= REFRACTORY_US)) {
    state.candidate = true;

    const bool cadenceOk = lastCandidateUs != 0 && since <= MAX_STEP_GAP_US;
    const bool rotationDominated = gyroSmooth > ROTATION_DPS && fabsf(prev) < ROTATION_MAX_PEAK_G;

    if (cadenceOk && !rotationDominated) {
      if (streak < 255) ++streak;

      //Smooth the instantaneous cadence
      const float instant = 60000000.0f / since;
      state.cadenceSpm = state.cadenceSpm == 0 ? instant : 0.7f*state.cadenceSpm + 0.3f*instant;

      //Credit three consistent candidates together; afterwards count each event
      if (streak >= 2) {
        state.steps += streak == 2 ? 3 : 1;
        state.confirmed = true;
        walking = true;
      }
    } else {
      streak = 0;
      walking = false;
    }
    lastCandidateUs = s.timestampUs;
  }

  //Remember the last noticeable movement for the activity class
  if (fabsf(smooth) > MOTION_G || gyroSmooth > MOTION_DPS)
    lastMotionUs = s.timestampUs;

  //End walking when candidates stop arriving
  if (lastCandidateUs && s.timestampUs - lastCandidateUs > IDLE_RESET_US) {
    walking = false;
    streak = 0;
    state.cadenceSpm = 0;
  }

  if (walking)
    state.activity = state.cadenceSpm >= RUNNING_SPM ? Activity::Running : Activity::Walking;
  else if (s.timestampUs - lastMotionUs > IDLE_RESET_US)
    state.activity = Activity::Stationary;
  else
    state.activity = Activity::GeneralMovement;

  //Shift previous filtered samples
  prevPrev = prev;
  prev = smooth;
  return state;
}

const StepsState& Steps::getState() const { return state; }
