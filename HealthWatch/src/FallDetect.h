#ifndef FALLDETECT_H
#define FALLDETECT_H

#include <stdint.h>
#include "Types.h"

//Detector phases
enum class FallPhase : uint8_t {
  Normal,                                    //Watching for an impact
  Observing,                                 //Impact seen; waiting for stillness and a new posture
  PossibleFall                               //Flag raised until dismissed
};

//Experimental fall event detector. It has no emergency-response role
class FallDetect{
  private:
    FallPhase phase;                         //Current detector phase
    uint32_t impactUs;                       //Timestamp of the impact that started observing
    uint32_t stillSinceUs;                   //Start of the current still period, 0 if moving
    uint32_t events;                         //Possible falls raised since boot
    float gravity[3];                        //Slow gravity direction estimate in g
    float reference[3];                      //Gravity direction at the moment of impact
    bool initialized;                        //Gravity seeded from the first sample

  public:
    FallDetect();

    void update(const ImuSample& sample);
    void dismiss();

    bool isPossibleFall();
    FallPhase getPhase();
    uint32_t getEvents();
};

#endif
