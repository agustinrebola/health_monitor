#include "IMU.h"

#define MPU6050_ADDR 0x68

IMU::IMU() {
  ready = false;
  ax = ay = az = gx = gy = gz = 0;
}

//Write one register
bool IMU::writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

//Wake the sensor and set 100 Hz output, +/-4 g, +/-500 dps
bool IMU::init() {
  ready = writeReg(0x6B, 0x01) &&            //Wake up, clock from gyro PLL
          writeReg(0x1A, 0x03) &&            //Low-pass filter ~44 Hz
          writeReg(0x19, 0x09) &&            //1 kHz / (9 + 1) = 100 Hz
          writeReg(0x1B, 0x08) &&            //Gyro +/-500 dps
          writeReg(0x1C, 0x08);              //Accel +/-4 g
  return ready;
}

//Read acceleration and rotation; false if the sensor did not answer
bool IMU::read() {
  if (!ready) return false;

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);                          //First data register
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU6050_ADDR, 14) != 14) return false;

  int16_t raw[7];                            //ax ay az temp gx gy gz
  for (int i = 0; i < 7; i++) {
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    raw[i] = (int16_t)((high << 8) | low);
  }

  ax = raw[0] / 8192.0f;                     //8192 LSB per g at +/-4 g
  ay = raw[1] / 8192.0f;
  az = raw[2] / 8192.0f;
  gx = raw[4] / 65.5f;                       //65.5 LSB per dps at +/-500 dps
  gy = raw[5] / 65.5f;
  gz = raw[6] / 65.5f;
  return true;
}
