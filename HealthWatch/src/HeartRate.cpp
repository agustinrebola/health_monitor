#include "HeartRate.h"
#include <math.h>
#include <stdlib.h>

static const float CONTACT_LEVEL = 5000.0f;       //IR baseline above this means skin; uncovered is ~1000
static const float MIN_AMPLITUDE = 30.0f;         //Pulse smaller than this is noise; uncovered noise is ~11
static const int WINDOW = 200;                    //2 s of samples compared in each scan
static const int MIN_LAG = 40;                    //0.40 s period, 150 bpm
static const int MAX_LAG = 150;                   //1.50 s period, 40 bpm
static const float MIN_CORRELATION = 0.65f;       //How well the waveform must repeat
static const float TIE_MARGIN = 0.03f;            //Peaks this close count as equal; shortest wins
static const int LAG_TOLERANCE = 8;               //Scans within 80 ms agree
static const int SCAN_EVERY = 100;                //One scan per second
static const int HOLD_SCANS = 3;                  //Keep the rate this many scans after the last good one
static const int MEMORY_SCANS = 15;               //A rate this recent guards against sudden jumps

HeartRate::HeartRate() {
  reset();
}

//Forget everything; used at start and when the sensor moves
void HeartRate::reset() {
  dc = hp1 = hp2 = ac = energy = 0;
  for (int i = 0; i < HISTORY; i++)
    history[i] = 0;
  head = filled = sinceScan = 0;
  lastLag = stableScans = 0;
  scansSinceValid = 1000;                    //Long ago: no recent rate to compare with
  bpm = amplitude = 0;
  valid = contact = false;
}

//How well the last 2 s of waveform matches itself shifted back by lag samples.
//1.0 means a perfect repeat, 0 means no relation
float HeartRate::correlation(int lag) {
  float sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
  int x = head - 1;
  if (x < 0) x += HISTORY;
  int y = x - lag;
  if (y < 0) y += HISTORY;

  for (int i = 0; i < WINDOW; i++) {
    const float a = history[x];
    const float b = history[y];
    sx += a; sy += b; sxx += a*a; syy += b*b; sxy += a*b;
    if (--x < 0) x = HISTORY - 1;
    if (--y < 0) y = HISTORY - 1;
  }

  const float vx = sxx - sx*sx/WINDOW;
  const float vy = syy - sy*sy/WINDOW;
  if (vx <= 0 || vy <= 0) return 0;
  return (sxy - sx*sy/WINDOW) / sqrtf(vx*vy);
}

//Process one IR sample
void HeartRate::update(uint32_t ir) {

  //Clipped or empty sample: start over
  if (ir == 0 || ir >= 0x3FFF0) {
    reset();
    return;
  }

  //A big jump in the baseline means the sensor moved: start over
  if (filled >= 200 && fabsf((float)ir - dc) > 0.35f * dc) {
    reset();
    return;
  }

  //Filters: remove the baseline, remove slow drift twice, smooth
  if (filled == 0) dc = ir;
  dc += 0.02f * ((float)ir - dc);
  float x = (float)ir - dc;
  hp1 += 0.045f * (x - hp1);
  x -= hp1;
  hp2 += 0.045f * (x - hp2);
  x -= hp2;
  ac += 0.18f * (x - ac);

  history[head] = ac;
  head = (head + 1) % HISTORY;
  if (filled < HISTORY) filled++;

  energy += 0.01f * (ac*ac - energy);
  amplitude = sqrtf(energy);

  contact = filled >= 200 && dc > CONTACT_LEVEL;

  //No skin or no pulse: nothing to measure
  if (!contact || amplitude < MIN_AMPLITUDE) {
    valid = false;
    stableScans = 0;
    return;
  }

  //Scan once per second after enough history
  if (++sinceScan < SCAN_EVERY || filled < WINDOW + MAX_LAG) return;
  sinceScan = 0;

  //Score every period, keep only local peaks of the score
  float score[MAX_LAG + 2];
  for (int lag = MIN_LAG - 1; lag <= MAX_LAG + 1; lag++)
    score[lag] = correlation(lag);

  float bestPeak = -1;
  for (int lag = MIN_LAG; lag <= MAX_LAG; lag++)
    if (score[lag] > score[lag-1] && score[lag] >= score[lag+1] && score[lag] > bestPeak)
      bestPeak = score[lag];

  //Shortest peak nearly as good as the best, so a two-beat period loses
  int lag = 0;
  for (int l = MIN_LAG; l <= MAX_LAG; l++)
    if (score[l] > score[l-1] && score[l] >= score[l+1] && score[l] >= bestPeak - TIE_MARGIN) {
      lag = l;
      break;
    }

  const bool usable = lag != 0 && score[lag] >= MIN_CORRELATION;

  if (usable) {
    //Count scans that agree on the period
    if (lastLag && abs(lag - lastLag) <= LAG_TOLERANCE) stableScans++;
    else stableScans = 1;
    lastLag = lag;

    //Two agreeing scans publish the rate. Slow periods need three, because a
    //2 s window holds barely one cycle. A rate more than 25% away from one
    //accepted in the last 15 scans needs four: a resting heart does not jump
    //like that, but wrist drift at twice the beat period does
    const float fresh = 6000.0f / lag;
    int needed = lag >= 100 ? 3 : 2;
    if (scansSinceValid < MEMORY_SCANS && fabsf(fresh - bpm) > 0.25f * bpm) needed = 4;

    if (stableScans >= needed) {
      bpm = valid ? 0.6f*bpm + 0.4f*fresh : fresh;
      valid = true;
      scansSinceValid = 0;
      return;
    }
  } else {
    stableScans = 0;
  }

  //Keep showing the last rate briefly, then drop it
  if (++scansSinceValid > HOLD_SCANS) valid = false;
}
