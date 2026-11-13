#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

//One accelerometer and gyroscope reading
struct ImuSample {
  uint32_t timestampUs;                      //Host micros(); wraps at ~71.6 min
  float ax, ay, az;                          //Acceleration in g
  float gx, gy, gz;                          //Rotation in degrees per second, bias removed
  bool valid;                                //Reading succeeded
};

//One red and infrared optical reading
struct PpgSample {
  uint32_t timestampUs;                      //Estimated from host read time and FIFO depth
  uint32_t red;                              //Red ADC counts, 18 bit
  uint32_t ir;                               //Infrared ADC counts, 18 bit
  bool valid;                                //Reading succeeded
};

//Activity classes sent to the phone
enum class Activity : uint8_t {
  Unknown,
  Stationary,
  Walking,
  Running,
  GeneralMovement
};

//Optical signal quality sent to the phone
enum class SignalQuality : uint8_t {
  NoContact,                                 //Sensor not against skin
  Invalid,                                   //Settling or pulse below the floor
  Degraded,                                  //Pulse seen but rhythm not confirmed
  Good                                       //Heart rate being published
};

#endif
