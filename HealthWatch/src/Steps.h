#ifndef STEPS_H
#define STEPS_H

#include <stdint.h>
#include "Types.h"

//Everything the rest of the firmware reads from the step detector
struct StepsState {
  uint32_t steps;                            //Steps credited since boot
  float cadenceSpm;                          //Smoothed steps per minute while walking
  float filteredG;                           //Band-passed vertical acceleration
  float thresholdG;                          //Current adaptive peak threshold
  float gyroDps;                             //Smoothed rotation rate
  Activity activity;                         //Current activity class
  bool candidate;                            //This sample produced a candidate peak
  bool confirmed;                            //This sample credited a step
};

//Step detector on vertical acceleration with cadence confirmation
class Steps{
  private:
    StepsState state;                        //Public view of the detector
    bool initialized;                        //Gravity seeded from the first sample
    float gravity[3];                        //Slow gravity direction estimate in g
    float baseline;                          //Slow baseline of vertical acceleration
    float smooth;                            //Band-passed vertical acceleration
    float prev, prevPrev;                    //Previous two filtered samples for peak detection
    float noise;                             //Slow activity estimate for the threshold
    float gyroSmooth;                        //Smoothed rotation rate
    uint32_t previousUs;                     //Timestamp of the previous sample
    uint32_t lastCandidateUs;                //Timestamp of the last candidate peak
    uint32_t lastMotionUs;                   //Timestamp of the last noticeable movement
    uint8_t streak;                          //Consecutive candidates with walking cadence
    bool walking;                            //Walking confirmed

  public:
    Steps();

    void reset();
    const StepsState& update(const ImuSample& sample);

    const StepsState& getState() const;
};

#endif
