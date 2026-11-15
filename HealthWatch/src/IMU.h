#ifndef IMU_H
#define IMU_H

#include <Arduino.h>
#include <Wire.h>

//MPU6050 accelerometer and gyroscope over I2C
class IMU{
  private:
    bool ready;                              //Sensor answered at init

    bool writeReg(uint8_t reg, uint8_t value);

  public:
    float ax, ay, az;                        //Acceleration in g
    float gx, gy, gz;                        //Rotation in degrees per second

    IMU();

    bool init();
    bool read();
};

#endif
