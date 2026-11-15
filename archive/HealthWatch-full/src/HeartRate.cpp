#include "HeartRate.h"
#include <math.h>
#include <stdlib.h>

//Optical contact, with hysteresis. Tuned to this breakout: uncovered IR is
//~0.8-2.7k counts, skin is ~130-170k counts
static const float CONTACT_ENTER_IR = 5000.0f;    //Baseline above this enters contact
static const float CONTACT_EXIT_IR = 3000.0f;     //Baseline below this leaves contact

//Pulse floors. Uncovered noise is ~11 counts RMS; weak finger pulses are
//55-125 counts and valid strapped-wrist pulses 57-115 counts at a ~150k
//baseline. The periodicity, peak and red/IR gates reject noise above the floor
static const float MIN_PULSE_RMS = 30.0f;         //Absolute amplitude floor in counts
static const float MIN_PULSE_FRACTION = 0.00015f; //Amplitude floor as a fraction of baseline

//Filter coefficients at 100 Hz
static const float DC_ALPHA = 0.02f;              //~0.5 s baseline tracker
static const float HP_ALPHA = 0.045f;             //~0.7 Hz high-pass, applied twice
static const float AC_ALPHA = 0.18f;              //~3 Hz smoothing
static const float ENERGY_ALPHA = 0.01f;          //~1 s amplitude tracker

//Sample validity
static const uint32_t ADC_SATURATED = 0x3FFF0;    //Treat the top of the 18-bit range as clipped
static const uint32_t MIN_SAMPLE_GAP_US = 5000;   //Closer samples are a timing fault
static const uint32_t MAX_SAMPLE_GAP_US = 50000;  //Wider samples are a timing fault
static const float REPOSITION_FRACTION = 0.35f;   //Baseline step this large is a placement change
static const uint16_t SETTLE_SAMPLES = 200;       //Filters are trusted after 2 s
static const float MOTION_REJECT_DPS = 80.0f;     //Wrist rotation above this drops the sample

//Period search over the filtered IR waveform
static const int SCAN_WINDOW = 200;               //2 s correlation window
static const int MIN_LAG = 33;                    //0.33 s, 180 bpm search limit
static const int MAX_LAG = 150;                   //1.50 s, 40 bpm search limit
static const int MIN_ACCEPTED_LAG = 40;           //0.40 s, 150 bpm; shorter is drift at the floor
static const int SLOW_LAG = 100;                  //Periods over 1 s need an extra scan
static const uint16_t SCAN_EVERY = 100;           //One scan per second
static const uint16_t SCAN_MIN_HISTORY = 350;     //History needed before the first scan
static const float MIN_PERIODICITY = 0.65f;       //Correlation needed at the chosen lag
static const float MIN_CHANNEL_AGREEMENT = 0.70f; //Red and IR must show the same waveform
static const float MAX_RELATIVE_AMPLITUDE = 0.03f;//Larger AC/DC is motion, not pulse
static const float MAX_DC_DRIFT = 0.03f;          //Two-second baseline change limit
static const float PEAK_TIE_MARGIN = 0.03f;       //Peaks this close to the best tie; shortest wins
static const int LAG_TOLERANCE = 8;               //Scans within 80 ms agree
static const float HALF_PERIOD_MIN = 0.50f;       //Half period needs this correlation
static const float HALF_PERIOD_RATIO = 0.80f;     //and this fraction of the full-period score
static const float RATE_JUMP_FRACTION = 0.25f;    //Larger jump from a recent rate needs four scans
static const uint32_t RATE_MEMORY_US = 15000000;  //How long a published rate guards against jumps
static const uint32_t RATE_HOLD_US = 3000000;     //Estimate expires this long after its scan
static const float BPM_KEEP = 0.6f;               //Weight kept from the previous estimate
static const float BPM_TAKE = 0.4f;               //Weight given to the new scan

//Beat-timing diagnostics
static const uint32_t MIN_IBI_US = 333000;        //Refractory interval, 180 bpm
static const uint32_t MAX_IBI_US = 1500000;       //Longest accepted interval, 40 bpm
static const float UNSTABLE_RATIO = 1.25f;        //Longest/shortest of five beats above this is unstable

HeartRate::HeartRate() {
  state = {};
  state.quality = SignalQuality::NoContact;
  for (int i = 0; i < 5; i++)
    ibis[i] = 0;
  for (int i = 0; i < HISTORY_SIZE; i++)
    irHistory[i] = redHistory[i] = irDcHistory[i] = 0;
  periodicBpm = 0;                           //Rate memory survives stream resets by design
  periodicTimestampUs = 0;
  invalidateStream();
}

//Normalized correlation of the IR waveform with itself shifted by intervalSeconds
float HeartRate::calculatePeriodicity(float intervalSeconds) const {
  const int lag = (int)(intervalSeconds * 100.0f + 0.5f);
  if (lag < MIN_LAG || lag > MAX_LAG || historyCount < lag + SCAN_WINDOW) return 0;

  float sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
  int xIndex = historyHead ? historyHead - 1 : HISTORY_SIZE - 1;
  int yIndex = xIndex - lag;
  if (yIndex < 0) yIndex += HISTORY_SIZE;

  for (int i = 0; i < SCAN_WINDOW; i++) {
    const float x = irHistory[xIndex];
    const float y = irHistory[yIndex];
    sx += x; sy += y; sxx += x*x; syy += y*y; sxy += x*y;
    if (--xIndex < 0) xIndex = HISTORY_SIZE - 1;
    if (--yIndex < 0) yIndex = HISTORY_SIZE - 1;
  }

  const float vx = sxx - sx*sx/SCAN_WINDOW;
  const float vy = syy - sy*sy/SCAN_WINDOW;
  if (vx <= 0 || vy <= 0) return 0;
  return (sxy - sx*sy/SCAN_WINDOW) / sqrtf(vx*vy);
}

//Normalized correlation between the red and IR waveforms over the scan window
float HeartRate::calculateRedIrAgreement() const {
  if (historyCount < SCAN_WINDOW) return 0;

  float sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
  int index = historyHead ? historyHead - 1 : HISTORY_SIZE - 1;

  for (int i = 0; i < SCAN_WINDOW; i++) {
    const float x = irHistory[index];
    const float y = redHistory[index];
    sx += x; sy += y; sxx += x*x; syy += y*y; sxy += x*y;
    if (--index < 0) index = HISTORY_SIZE - 1;
  }

  const float vx = sxx - sx*sx/SCAN_WINDOW;
  const float vy = syy - sy*sy/SCAN_WINDOW;
  if (vx <= 0 || vy <= 0) return 0;
  return (sxy - sx*sy/SCAN_WINDOW) / sqrtf(vx*vy);
}

//Drop the period estimate and the waveform history behind it
void HeartRate::resetPeriodicEstimate() {
  periodicValid = false;
  stablePeriodScans = previousPeriodLag = 0;
  samplesSinceScan = 0;
  historyHead = historyCount = 0;
  state.periodicity = 0;
  state.dcDrift = 0;
  state.halfPeriodicity = 0;
  state.periodLag = 0;
  state.heartRateValid = false;
}

//Once per second, search the waveform for its repeating period and decide
//whether it is trustworthy enough to publish
void HeartRate::updatePeriodicEstimate(uint32_t timestampUs, float relativeAmplitude) {
  if (++samplesSinceScan < SCAN_EVERY || historyCount < SCAN_MIN_HISTORY) return;
  samplesSinceScan = 0;

  //Score every lag in the search range
  float scores[MAX_LAG + 1] = {};
  float best = -1.0f;
  for (int lag = MIN_LAG; lag <= MAX_LAG; lag++) {
    scores[lag] = calculatePeriodicity(lag * 0.01f);
    if (scores[lag] > best) best = scores[lag];
  }

  //Slow baseline drift correlates highly at every short lag without forming a
  //peak, which once surfaced as 182 bpm at the search floor. Only a local
  //maximum is a candidate period; among peaks within PEAK_TIE_MARGIN of the
  //strongest, prefer the shortest so a two-beat lag loses
  float bestPeak = -1.0f;
  for (int lag = MIN_LAG + 1; lag < MAX_LAG; lag++) {
    if (scores[lag] > scores[lag-1] && scores[lag] >= scores[lag+1] && scores[lag] > bestPeak)
      bestPeak = scores[lag];
  }
  int chosen = 0;
  for (int lag = MIN_LAG + 1; lag < MAX_LAG; lag++) {
    if (scores[lag] > scores[lag-1] && scores[lag] >= scores[lag+1] && scores[lag] >= bestPeak - PEAK_TIE_MARGIN) {
      chosen = lag;
      break;
    }
  }
  if (chosen) best = scores[chosen];
  state.periodicity = best;

  //A two-beat lag can correlate slightly better than the true beat period.
  //Select its half when that shorter cycle also has strong correlation
  state.halfPeriodicity = chosen >= 2*MIN_LAG ? calculatePeriodicity((chosen/2) * 0.01f) : 0;
  if (chosen >= 2*MIN_LAG && state.halfPeriodicity >= HALF_PERIOD_MIN && state.halfPeriodicity >= HALF_PERIOD_RATIO*best)
    chosen /= 2;
  state.periodLag = chosen;

  //Baseline change over the scan window
  const int oldIndex = (historyHead + HISTORY_SIZE - 1 - SCAN_WINDOW) % HISTORY_SIZE;
  state.dcDrift = fabsf(irDc - irDcHistory[oldIndex]) / fmaxf(irDc, 1.0f);

  const float channelAgreement = calculateRedIrAgreement();
  const bool usable =
      best >= MIN_PERIODICITY &&
      channelAgreement >= MIN_CHANNEL_AGREEMENT &&
      relativeAmplitude <= MAX_RELATIVE_AMPLITUDE &&
      state.dcDrift <= MAX_DC_DRIFT &&
      chosen >= MIN_ACCEPTED_LAG;

  if (!usable) {
    stablePeriodScans = 0;
    if (relativeAmplitude > MAX_RELATIVE_AMPLITUDE || state.dcDrift > MAX_DC_DRIFT || chosen < MIN_ACCEPTED_LAG)
      periodicValid = false;
    return;
  }

  //Count scans that agree on the period
  if (previousPeriodLag && abs(chosen - (int)previousPeriodLag) <= LAG_TOLERANCE) {
    if (stablePeriodScans < 255) ++stablePeriodScans;
  } else {
    stablePeriodScans = 1;
    periodicValid = false;
  }
  previousPeriodLag = chosen;

  const float bpm = 6000.0f / chosen;

  //Evidence per scan falls with period length: a 2 s window holds barely one
  //cycle above SLOW_LAG, so slow candidates need a third scan
  uint8_t scansNeeded = chosen >= SLOW_LAG ? 3 : 2;

  //Strapped-wrist traces showed 0.7 Hz drift bursts of 2-3 scans at roughly
  //twice the beat period. A large jump from a recently published rate must
  //persist for four scans before it replaces that rate
  if (periodicTimestampUs && (int32_t)(timestampUs - periodicTimestampUs) <= (int32_t)RATE_MEMORY_US &&
      fabsf(bpm - periodicBpm) > RATE_JUMP_FRACTION*periodicBpm)
    scansNeeded = 4;

  if (stablePeriodScans >= scansNeeded) {
    periodicBpm = periodicValid ? BPM_KEEP*periodicBpm + BPM_TAKE*bpm : bpm;
    periodicValid = true;
    periodicTimestampUs = timestampUs;
  }
}

//Forget all filter state; used after sensor faults and placement changes
void HeartRate::invalidateStream() {
  state.heartRateValid = false;
  state.contactDetected = false;
  state.quality = SignalQuality::Invalid;
  irDc = redDc = irAc = redAc = 0;
  irHp1 = irHp2 = redHp1 = redHp2 = 0;
  irEnergy = redEnergy = previous = previous2 = 0;
  previousUs = lastPeakUs = 0;
  settled = 0;
  ibiCount = ibiIndex = 0;
  weakGateActive = false;
  resetPeriodicEstimate();
  state.intervalCount = 0;
}

//Process one red/IR sample
void HeartRate::update(const PpgSample& s, float gyroDps) {

  //Zero or saturated samples carry no information
  if (!s.valid || s.ir == 0 || s.red == 0 || s.ir >= ADC_SATURATED || s.red >= ADC_SATURATED) {
    ++state.invalidRawSamples;
    invalidateStream();
    return;
  }

  //A large optical step is a placement change, not a pulse; start fresh
  if (settled >= SETTLE_SAMPLES && irDc > 1000 && fabsf((float)s.ir - irDc) > REPOSITION_FRACTION*irDc) {
    ++state.opticalRepositions;
    invalidateStream();
  }

  //Missing FIFO samples invalidate beat continuity
  if (previousUs && (s.timestampUs - previousUs < MIN_SAMPLE_GAP_US || s.timestampUs - previousUs > MAX_SAMPLE_GAP_US)) {
    ++state.timingGaps;
    resetPeriodicEstimate();
    ibiCount = 0;
    lastPeakUs = 0;
    settled = 0;
  }
  previousUs = s.timestampUs;

  //Seed the baselines from the first sample after a reset
  if (settled == 0) {
    irDc = s.ir;
    redDc = s.red;
  }

  //Slow baseline removal
  irDc += DC_ALPHA * ((float)s.ir - irDc);
  redDc += DC_ALPHA * ((float)s.red - redDc);
  const float ir0 = (float)s.ir - irDc;
  const float red0 = (float)s.red - redDc;

  //Two further high-pass stages. On strapped-wrist recordings 0.5-0.7 Hz
  //pressure and breathing drift was 3-10x the pulse; with these stages usable
  //scans rose from 9 to 30 of 57
  irHp1 += HP_ALPHA * (ir0 - irHp1);
  const float ir1 = ir0 - irHp1;
  irHp2 += HP_ALPHA * (ir1 - irHp2);
  const float ir = ir1 - irHp2;
  redHp1 += HP_ALPHA * (red0 - redHp1);
  const float red1 = red0 - redHp1;
  redHp2 += HP_ALPHA * (red1 - redHp2);
  const float red = red1 - redHp2;

  //Smooth and store
  irAc += AC_ALPHA * (ir - irAc);
  redAc += AC_ALPHA * (red - redAc);
  irHistory[historyHead] = irAc;
  redHistory[historyHead] = redAc;
  irDcHistory[historyHead] = irDc;
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) ++historyCount;

  irEnergy += ENERGY_ALPHA * (irAc*irAc - irEnergy);
  redEnergy += ENERGY_ALPHA * (redAc*redAc - redEnergy);
  if (settled < 2*SETTLE_SAMPLES) ++settled;

  const float irRms = sqrtf(fmaxf(irEnergy, 0));
  const float relative = irRms / fmaxf(irDc, 1);
  state.irDc = irDc;
  state.irRms = irRms;
  state.relativeAmplitude = relative;

  //Contact and usable pulse amplitude are separate conditions; a steady finger
  //can remain in contact while its AC component briefly falls below the floor
  const float contactThreshold = state.contactDetected ? CONTACT_EXIT_IR : CONTACT_ENTER_IR;
  const bool contact = settled >= SETTLE_SAMPLES && irDc > contactThreshold;
  state.contactDetected = contact;

  if (!contact) {
    resetPeriodicEstimate();
    state.quality = settled < SETTLE_SAMPLES ? SignalQuality::Invalid : SignalQuality::NoContact;
    state.heartRateValid = false;
    ibiCount = 0;
    lastPeakUs = 0;

  } else if (irRms <= MIN_PULSE_RMS || relative <= MIN_PULSE_FRACTION) {
    //Withhold HR but keep the waveform history: a wrist pulse hovering at the
    //floor would otherwise restart acquisition every time it dipped
    periodicValid = false;
    stablePeriodScans = 0;
    if (!weakGateActive) ++state.weakSignalResets;
    weakGateActive = true;
    state.quality = SignalQuality::Invalid;
    state.heartRateValid = false;
    ibiCount = 0;
    lastPeakUs = 0;

  } else if (gyroDps > MOTION_REJECT_DPS) {
    resetPeriodicEstimate();
    weakGateActive = false;
    ++state.motionRejectedSamples;
    state.quality = SignalQuality::Degraded;
    state.heartRateValid = false;
    ibiCount = 0;
    lastPeakUs = 0;

  } else {
    weakGateActive = false;
    state.quality = SignalQuality::Degraded;

    //Beat timing is diagnostic only; the published rate comes from periodicity
    const bool peak =
        previous > previous2 &&                //Previous sample was rising
        previous >= irAc &&                    //Current sample is no longer rising
        previous > 0.5f * irRms;               //Peak is a real excursion
    const uint32_t sincePeak = s.timestampUs - lastPeakUs;

    if (peak && lastPeakUs && sincePeak < MIN_IBI_US) ++state.earlyPeaksIgnored;

    if (peak && (!lastPeakUs || sincePeak >= MIN_IBI_US)) {
      if (lastPeakUs) {
        const uint32_t ibi = s.timestampUs - lastPeakUs;
        if (ibi >= MIN_IBI_US && ibi <= MAX_IBI_US) {
          ibis[ibiIndex] = ibi * 1e-6f;
          ++state.acceptedIntervals;
          ibiIndex = (ibiIndex + 1) % 5;
          if (ibiCount < 5) ++ibiCount;

          //Check the spread of the last five intervals
          if (ibiCount >= 5) {
            float shortest = ibis[0], longest = ibis[0];
            for (uint8_t i = 0; i < ibiCount; i++) {
              if (ibis[i] < shortest) shortest = ibis[i];
              if (ibis[i] > longest) longest = ibis[i];
            }
            if (longest > UNSTABLE_RATIO*shortest) ++state.unstableBeatWindows;
          }
        } else {
          ++state.longIntervalResets;
          ibiCount = 0;
        }
      }
      lastPeakUs = s.timestampUs;
    }

    //Optical ratio diagnostic
    const float redRms = sqrtf(fmaxf(redEnergy, 0));
    if (irRms > 0 && redDc > 0)
      state.ratioOfRatios = (redRms/redDc) / (irRms/irDc);

    updatePeriodicEstimate(s.timestampUs, relative);
    state.heartRateValid = periodicValid && (int32_t)(s.timestampUs - periodicTimestampUs) <= (int32_t)RATE_HOLD_US;
    if (state.heartRateValid) {
      state.heartRateBpm = periodicBpm;
      state.lastValidHrUs = periodicTimestampUs;
      state.quality = SignalQuality::Good;
    }
  }

  //Shift previous samples for peak detection
  previous2 = previous;
  previous = irAc;
  state.intervalCount = ibiCount;
}

//Age out the estimate and the contact flag when samples stop arriving
void HeartRate::expire(uint32_t nowUs) {
  if (state.heartRateValid && (int32_t)(nowUs - state.lastValidHrUs) > (int32_t)RATE_HOLD_US)
    state.heartRateValid = false;
  if (previousUs && (int32_t)(nowUs - previousUs) > 2000000) {
    state.quality = SignalQuality::Invalid;
    state.heartRateValid = false;
    state.contactDetected = false;
  }
}

const HeartRateState& HeartRate::getState() const { return state; }
