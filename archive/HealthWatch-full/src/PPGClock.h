#ifndef PPGCLOCK_H
#define PPGCLOCK_H

#include <stdint.h>

//Keeps one nominal 100 Hz time sequence across FIFO reads so batch jitter
//does not change beat intervals
class PPGClock{
  private:
    bool initialized;                        //First batch has anchored the clock
    uint32_t nextUs;                         //Timestamp for the next sample

  public:
    PPGClock();

    void reset();
    void startBatch(uint32_t hostNowUs, uint8_t pendingSamples);
    uint32_t next();
};

#endif
