#ifndef STEPS_H
#define STEPS_H

#include <stdint.h>

//Step counter on acceleration magnitude with cadence confirmation
class Steps{
  private:
    float baseline;                          //Slow average of magnitude, about 1 g
    float filtered, prevFiltered, prevPrevFiltered;   //Smoothed signal and two past values
    float noise;                             //Slow average of signal activity
    float threshold;                         //Adaptive peak threshold
    uint32_t lastPeakMs;                     //Time of the last accepted peak
    uint32_t lastMotionMs;                   //Time of the last consistent peak
    uint32_t previousIntervalMs;             //Interval between the last two peaks
    uint8_t candidateCount;                  //Consistent peaks seen so far

  public:
    uint32_t count;                          //Steps since boot
    bool walking;                            //Walking confirmed
    float cadence;                           //Steps per minute while walking

    Steps();

    void reset();
    void update(float ax, float ay, float az, uint32_t nowMs);
};

#endif
