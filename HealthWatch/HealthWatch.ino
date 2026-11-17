#include <Wire.h>
#include "src/IMU.h"
#include "src/MAX30102.h"
#include "src/Steps.h"
#include "src/HeartRate.h"
#include "src/Bluetooth.h"

//Timing
static const uint32_t IMU_PERIOD_US = 10000;      //100 Hz
static const uint32_t PRINT_PERIOD_MS = 100;      //Serial readout 10 times per second
static const uint32_t BLE_PERIOD_MS = 500;        //Phone update twice per second

IMU imu;
MAX30102 ppg;
Steps steps;
HeartRate heart;
Bluetooth ble;

bool imuOk = false, ppgOk = false;
bool rawStream = false;                           //Print every pulse sample as CSV (p to start, s to stop)
uint32_t nextImuUs = 0, nextPrintMs = 0, nextBleMs = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  Wire.setClock(400000);

  imuOk = imu.init();
  ppgOk = ppg.init();
  Serial.printf("IMU=%d MAX30102=%d\n", imuOk, ppgOk);

  ble.init("HealthWatch");
  nextImuUs = micros();
}

void loop() {
  //Read the IMU every 10 ms and feed the step counter
  const uint32_t nowUs = micros();
  if ((int32_t)(nowUs - nextImuUs) >= 0) {
    nextImuUs += IMU_PERIOD_US;
    if ((int32_t)(nowUs - nextImuUs) >= 0) nextImuUs = nowUs + IMU_PERIOD_US;   //Fell behind, resync
    if (imu.read()) steps.update(imu.ax, imu.ay, imu.az, millis());
  }

  //Read every pulse sample waiting in the sensor FIFO
  const uint32_t overflowsBefore = ppg.overflows;
  while (ppg.available() && ppg.read()) {
    heart.update(ppg.ir);
    if (rawStream) Serial.printf("%lu,%lu,%lu\n", (unsigned long)micros(), (unsigned long)ppg.red, (unsigned long)ppg.ir);
  }

  //An overflow lost samples, so the waveform has a hole: start the heart rate over
  if (ppg.overflows != overflowsBefore) heart.reset();

  const uint32_t nowMs = millis();

  //Serial Monitor: three gyro axes and the pulse reading
  if (!rawStream && (int32_t)(nowMs - nextPrintMs) >= 0) {
    nextPrintMs = nowMs + PRINT_PERIOD_MS;
    Serial.printf("gx:%.1f,gy:%.1f,gz:%.1f,pulse:%lu", imu.gx, imu.gy, imu.gz, (unsigned long)ppg.ir);
    if (heart.valid) Serial.printf("  hr=%.0f", heart.bpm);
    Serial.printf("  steps=%lu\n", (unsigned long)steps.count);
  }

  //Phone update
  if ((int32_t)(nowMs - nextBleMs) >= 0) {
    nextBleMs = nowMs + BLE_PERIOD_MS;
    ble.send(steps.count, (uint16_t)steps.cadence, steps.walking,
             (uint8_t)(heart.bpm + 0.5f), heart.valid, heart.contact, imuOk, ppgOk);
  }

  //Serial commands
  while (Serial.available()) {
    const char c = Serial.read();
    if (c == 'p') {
      Serial.println("timestamp_us,red,ir");
      rawStream = true;
    }
    if (c == 's') rawStream = false;
  }
}
