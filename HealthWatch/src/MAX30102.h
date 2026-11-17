#ifndef MAX30102_H
#define MAX30102_H

#include <Arduino.h>
#include <Wire.h>

//MAX30102 pulse sensor over I2C
class MAX30102{
  private:
    bool ready;                              //Sensor answered at init

    bool writeReg(uint8_t reg, uint8_t value);
    uint8_t readReg(uint8_t reg);

  public:
    uint32_t red, ir;                        //Latest sample, 18-bit ADC counts
    uint32_t overflows;                      //Times the FIFO filled up and was cleared

    MAX30102();

    bool init();
    uint8_t available();
    bool read();
};

#endif
