#ifndef IMU_H
#define IMU_H

#include <Arduino.h>
#include <Wire.h>
#include "Types.h"

//MPU6050 accelerometer and gyroscope over I2C
class IMU{
  private:
    TwoWire* bus;                            //I2C bus in use
    bool ready;                              //Sensor configured and responding
    bool calibrated;                         //Gyro bias measured at rest
    uint32_t errors;                         //I2C failures since boot
    float gyroBias[3];                       //Degrees per second removed from each axis

    bool writeReg(uint8_t reg, uint8_t value);
    bool readRegs(uint8_t reg, uint8_t* dst, uint8_t count);

  public:
    IMU();

    bool init(TwoWire& wire);
    bool read(ImuSample& out, uint32_t timestampUs);
    bool calibrateGyro(uint16_t samples = 200);

    bool isReady();
    bool isCalibrated();
    uint32_t getErrors();
};

#endif
