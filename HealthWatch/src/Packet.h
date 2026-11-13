#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#include "Steps.h"
#include "HeartRate.h"

//GATT identifiers shared with the iPhone app
#define SERVICE_UUID  "f0a1c001-0cb6-4f90-9c6c-4af42ca31f01"
#define SNAPSHOT_UUID "f0a1c002-0cb6-4f90-9c6c-4af42ca31f01"

static const uint8_t PACKET_SIZE = 20;            //Fits one notification at the default MTU
static const uint8_t PACKET_VERSION = 1;

//Flag bits in byte 1
static const uint8_t FLAG_IMU_READY = 1;
static const uint8_t FLAG_PPG_READY = 2;
static const uint8_t FLAG_HR_VALID = 4;
static const uint8_t FLAG_CONTACT = 8;
static const uint8_t FLAG_SPO2_VALID = 16;
static const uint8_t FLAG_POSSIBLE_FALL = 32;

//Little-endian field writers
inline void put16(uint8_t* p, uint16_t v) {
  p[0] = v & 255;
  p[1] = (v >> 8) & 255;
}
inline void put32(uint8_t* p, uint32_t v) {
  for (int i = 0; i < 4; i++)
    p[i] = (v >> (8*i)) & 255;
}

//Snapshot layout, 20 bytes:
//  0      version
//  1      flags
//  2      activity
//  3      PPG quality
//  4-7    sequence
//  8-11   uptime ms
//  12-15  steps
//  16-17  cadence steps/min
//  18     heart rate bpm, 0 when not valid
//  19     SpO2, always 0 in v1
inline void encodeSnapshot(uint8_t out[PACKET_SIZE], uint32_t sequence, uint32_t uptimeMs,
                           const StepsState& steps, const HeartRateState& heart,
                           bool imuReady, bool ppgReady, bool possibleFall) {
  out[0] = PACKET_VERSION;
  out[1] = (imuReady ? FLAG_IMU_READY : 0) |
           (ppgReady ? FLAG_PPG_READY : 0) |
           (heart.heartRateValid ? FLAG_HR_VALID : 0) |
           (heart.contactDetected ? FLAG_CONTACT : 0) |
           (heart.spo2Valid ? FLAG_SPO2_VALID : 0) |
           (possibleFall ? FLAG_POSSIBLE_FALL : 0);
  out[2] = (uint8_t)steps.activity;
  out[3] = (uint8_t)heart.quality;
  put32(out + 4, sequence);
  put32(out + 8, uptimeMs);
  put32(out + 12, steps.steps);
  put16(out + 16, steps.cadenceSpm > 0 ? (uint16_t)steps.cadenceSpm : 0);
  out[18] = heart.heartRateValid ? (uint8_t)(heart.heartRateBpm + 0.5f) : 0;
  out[19] = 0;                               //No calibrated SpO2 conversion in v1
}

#endif
