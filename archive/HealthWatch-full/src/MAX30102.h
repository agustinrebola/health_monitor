#ifndef MAX30102_H
#define MAX30102_H

#include <Arduino.h>
#include <Wire.h>
#include "Types.h"
#include "PPGClock.h"

//LED pulse-current register values; 1 count is 0.2 mA per LED
static const uint8_t LED_NORMAL = 0x24;           //36, about 7.2 mA: unclipped finger
static const uint8_t LED_WRIST = 0x2C;            //44, about 8.8 mA: best valid-HR fraction on the wrist
static const uint8_t LED_MODERATE = 0x38;         //56, about 11.2 mA
static const uint8_t LED_HIGH = 0x64;             //100, about 20 mA: clipped every wrist sample

//Called once per sample drained from the FIFO
typedef void (*PpgSink)(const PpgSample& sample);

//MAX30102 pulse oximeter front end over I2C
class MAX30102{
  private:
    TwoWire* bus;                            //I2C bus in use
    bool ready;                              //Sensor configured and responding
    uint32_t errors;                         //I2C failures since boot
    uint32_t overflows;                      //FIFO overflow samples since boot
    uint8_t ledCurrent;                      //Current register value for both LEDs
    PPGClock clock;                          //Sample timestamp sequence

    bool writeReg(uint8_t reg, uint8_t value);
    bool readReg(uint8_t reg, uint8_t& value);
    bool readFifo(uint8_t* bytes, uint8_t count);
    void clearFifo();

  public:
    MAX30102();

    bool init(TwoWire& wire);
    bool setLedCurrent(uint8_t value);
    uint8_t drain(uint32_t nowUs, PpgSink sink);

    bool isReady();
    uint8_t getLedCurrent();
    uint32_t getErrors();
    uint32_t getOverflows();
};

#endif
