#include "IMU.h"
#include <math.h>

#define MPU6050_ADDR 0x68

//Register map
static const uint8_t REG_WHO_AM_I = 0x75;
static const uint8_t REG_PWR_MGMT_1 = 0x6B;
static const uint8_t REG_CONFIG = 0x1A;
static const uint8_t REG_SMPLRT_DIV = 0x19;
static const uint8_t REG_GYRO_CONFIG = 0x1B;
static const uint8_t REG_ACCEL_CONFIG = 0x1C;
static const uint8_t REG_ACCEL_XOUT_H = 0x3B;

//Scale factors for the configured ranges
static const float ACCEL_LSB_PER_G = 8192.0f;     //+/-4 g range
static const float GYRO_LSB_PER_DPS = 65.5f;      //+/-500 dps range

//Calibration acceptance limits
static const float CAL_MIN_G = 0.9f;              //Device must be at rest
static const float CAL_MAX_G = 1.1f;
static const float CAL_MAX_VARIANCE = 4.0f;       //Gyro noise limit while resting
static const float CAL_MAX_BIAS_DPS = 20.0f;      //Larger offsets mean movement, not bias

//Combine two bytes into a signed 16-bit word
static int16_t word(const uint8_t* data) {
  return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

IMU::IMU() {
  bus = nullptr;
  ready = calibrated = false;
  errors = 0;
  gyroBias[0] = gyroBias[1] = gyroBias[2] = 0;
}

//Write one register, counting bus failures
bool IMU::writeReg(uint8_t reg, uint8_t value) {
  bus->beginTransmission(MPU6050_ADDR);
  bus->write(reg);
  bus->write(value);
  if (bus->endTransmission() == 0) return true;
  ++errors;
  return false;
}

//Read consecutive registers, counting bus failures
bool IMU::readRegs(uint8_t reg, uint8_t* dst, uint8_t count) {
  bus->beginTransmission(MPU6050_ADDR);
  bus->write(reg);
  if (bus->endTransmission(false) != 0 || bus->requestFrom((uint8_t)MPU6050_ADDR, count) != count) {
    ++errors;
    return false;
  }
  for (uint8_t i = 0; i < count; i++)
    dst[i] = bus->read();
  return true;
}

//Configure the sensor: 100 Hz output, ~44 Hz low-pass, +/-500 dps, +/-4 g
bool IMU::init(TwoWire& wire) {
  bus = &wire;
  ready = false;

  uint8_t who = 0;
  if (!readRegs(REG_WHO_AM_I, &who, 1) || who != 0x68) return false;

  ready = writeReg(REG_PWR_MGMT_1, 0x01) &&     //Wake, clock from gyro X PLL
          writeReg(REG_CONFIG, 0x03) &&         //DLPF ~44 Hz accel, ~42 Hz gyro
          writeReg(REG_SMPLRT_DIV, 0x09) &&     //1 kHz / (9 + 1) = 100 Hz
          writeReg(REG_GYRO_CONFIG, 0x08) &&    //+/-500 dps
          writeReg(REG_ACCEL_CONFIG, 0x08);     //+/-4 g
  return ready;
}

//Read one scaled sample; false if the sensor is down or the bus failed
bool IMU::read(ImuSample& out, uint32_t timestampUs) {
  out = {};
  out.timestampUs = timestampUs;
  if (!ready) return false;

  uint8_t data[14];
  if (!readRegs(REG_ACCEL_XOUT_H, data, sizeof(data))) return false;

  out.ax = word(data) / ACCEL_LSB_PER_G;
  out.ay = word(data + 2) / ACCEL_LSB_PER_G;
  out.az = word(data + 4) / ACCEL_LSB_PER_G;
  out.gx = word(data + 8) / GYRO_LSB_PER_DPS - gyroBias[0];   //Bytes 6-7 are temperature
  out.gy = word(data + 10) / GYRO_LSB_PER_DPS - gyroBias[1];
  out.gz = word(data + 12) / GYRO_LSB_PER_DPS - gyroBias[2];
  out.valid = true;
  return true;
}

//Measure gyro bias while the device rests; one-time startup use only
bool IMU::calibrateGyro(uint16_t samples) {
  if (!ready || samples < 20) return false;

  float sum[3] = {0, 0, 0};
  float sumSq[3] = {0, 0, 0};

  for (uint16_t i = 0; i < samples; i++) {
    ImuSample s;
    if (!read(s, micros())) return false;

    //Reject the run if the device is not resting under 1 g
    const float am = sqrtf(s.ax*s.ax + s.ay*s.ay + s.az*s.az);
    if (am < CAL_MIN_G || am > CAL_MAX_G) return false;

    const float g[3] = {s.gx, s.gy, s.gz};
    for (int j = 0; j < 3; j++) {
      sum[j] += g[j];
      sumSq[j] += g[j]*g[j];
    }
    delay(10);
  }

  //Accept only a quiet, plausible offset on every axis
  float bias[3];
  for (int j = 0; j < 3; j++) {
    const float mean = sum[j] / samples;
    const float variance = sumSq[j] / samples - mean*mean;
    if (variance > CAL_MAX_VARIANCE || fabsf(mean) > CAL_MAX_BIAS_DPS) return false;
    bias[j] = gyroBias[j] + mean;              //Readings already had the old bias removed
  }

  for (int j = 0; j < 3; j++)
    gyroBias[j] = bias[j];
  calibrated = true;
  return true;
}

bool IMU::isReady() { return ready; }
bool IMU::isCalibrated() { return calibrated; }
uint32_t IMU::getErrors() { return errors; }
