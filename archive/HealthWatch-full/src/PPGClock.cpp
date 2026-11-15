#include "PPGClock.h"

static const uint32_t SAMPLE_US = 10000;          //Nominal sensor sample period, 100 Hz
static const int32_t MAX_PHASE_ERROR_US = 200000; //Larger error means a stall; re-anchor
static const int32_t MAX_CORRECTION_US = 500;     //Bounded drift correction per batch

PPGClock::PPGClock() {
  reset();
}

//Forget the anchor so the next batch starts a fresh sequence
void PPGClock::reset() {
  initialized = false;
  nextUs = 0;
}

//Align the sequence to a new FIFO batch read at hostNowUs
void PPGClock::startBatch(uint32_t hostNowUs, uint8_t pendingSamples) {
  if (!pendingSamples) return;

  //First batch: oldest pending sample is (pending - 1) periods before now
  if (!initialized) {
    nextUs = hostNowUs - (uint32_t)(pendingSamples - 1) * SAMPLE_US;
    initialized = true;
    return;
  }

  //Compare where the newest sample should be with the host clock
  const uint32_t predictedNewest = nextUs + (uint32_t)(pendingSamples - 1) * SAMPLE_US;
  const int32_t phaseError = (int32_t)(hostNowUs - predictedNewest);

  //A long stall or clock reset breaks continuity; re-anchor visibly
  if (phaseError > MAX_PHASE_ERROR_US || phaseError < -MAX_PHASE_ERROR_US) {
    nextUs = hostNowUs - (uint32_t)(pendingSamples - 1) * SAMPLE_US;
    return;
  }

  //Slew gently toward the host clock, preserving beat intervals
  int32_t correction = phaseError / 8;
  if (correction > MAX_CORRECTION_US) correction = MAX_CORRECTION_US;
  if (correction < -MAX_CORRECTION_US) correction = -MAX_CORRECTION_US;
  nextUs += correction;
}

//Timestamp for the next sample in the batch
uint32_t PPGClock::next() {
  const uint32_t result = nextUs;
  nextUs += SAMPLE_US;
  return result;
}
