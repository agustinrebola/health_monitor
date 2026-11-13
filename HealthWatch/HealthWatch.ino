#include <Arduino.h>
#include <Wire.h>
#include "src/Config.h"
#include "src/Types.h"
#include "src/IMU.h"
#include "src/MAX30102.h"
#include "src/Steps.h"
#include "src/HeartRate.h"
#include "src/FallDetect.h"
#include "src/Packet.h"
#include "src/Bluetooth.h"

//Serial output modes. Set at most one to 1. Recording modes stream CSV at
//921600 baud and change timing; check missedImu afterwards
#define RECORD_IMU 0                              //timestamp,ax,ay,az,gx,gy,gz at 100 Hz
#define RECORD_PPG 0                              //Raw red/IR with processor state at 100 Hz
#define SERIAL_PLOTTER 0                          //Step filter trace for Arduino Serial Plotter
#if (RECORD_IMU + RECORD_PPG + SERIAL_PLOTTER) > 1
#error Select only one Serial output mode
#endif
#define NORMAL_SERIAL (!RECORD_IMU && !RECORD_PPG && !SERIAL_PLOTTER)

//Modules
IMU imu;
MAX30102 ppg;
Steps steps;
HeartRate heart;
FallDetect fall;
Bluetooth ble;

//Scheduler deadlines
uint32_t nextImuUs = 0;                           //Next IMU sample slot
uint32_t nextPpgUs = 0;                           //Next FIFO poll
uint32_t nextBleMs = 0;                           //Next snapshot notify
uint32_t nextStatusMs = 0;                        //Next status line
uint32_t nextRetryMs = 0;                         //Next sensor re-init check

//Counters for the status line
uint32_t imuSamples = 0, missedImu = 0;           //IMU samples read and slots missed
uint32_t ppgSamples = 0;                          //PPG samples drained
uint32_t sequence = 0;                            //Snapshot sequence number
uint32_t lastImuSamples = 0, lastPpgSamples = 0;  //Counts at the previous status line
uint32_t lastStatusMs = 0;                        //Time of the previous status line
uint32_t lastImuUs = 0, lastPpgUs = 0;            //Last successful read from each sensor

//Serial plotter state
uint32_t lastPlotUs = 0;
bool plotCandidate = false, plotConfirmed = false;

//Serial command state
bool ppgTrace = false;                            //Streaming timestamp,red,ir on request
bool readingLedValue = false;                     //Digits of an L command are arriving
uint16_t pendingLedValue = 0;                     //Value being assembled
uint8_t pendingLedDigits = 0;                     //Digits received so far

//Handle one sample drained from the MAX30102 FIFO
void onPpgSample(const PpgSample& s) {
  heart.update(s, steps.getState().gyroDps);

  if (ppgTrace)
    Serial.printf("%lu,%lu,%lu\n", (unsigned long)s.timestampUs, (unsigned long)s.red, (unsigned long)s.ir);

  if (RECORD_PPG) {
    const HeartRateState& p = heart.getState();
    Serial.printf("%lu,%lu,%lu,%.1f,%u,%u,%.1f,%.1f,%.5f,%u,%.2f,%.1f,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n",
        (unsigned long)s.timestampUs, (unsigned long)s.red, (unsigned long)s.ir,
        steps.getState().gyroDps, (unsigned)p.quality, p.contactDetected, p.irDc, p.irRms,
        p.relativeAmplitude, (unsigned)p.intervalCount, p.periodicity,
        p.heartRateValid ? p.heartRateBpm : 0.0f, p.heartRateValid,
        (unsigned long)p.timingGaps, (unsigned long)p.invalidRawSamples,
        (unsigned long)p.motionRejectedSamples, (unsigned long)p.acceptedIntervals,
        (unsigned long)p.opticalRepositions, (unsigned long)ppg.getOverflows(),
        (unsigned long)ppg.getErrors());
  }
}

//Apply a new LED current, re-initialising the sensor if the write failed
void applyLedCurrent(uint8_t value) {
  heart.invalidateStream();
  if (!ppg.setLedCurrent(value)) ppg.init(Wire);
}

//Sample the IMU on its 10 ms slot
void serviceImu(uint32_t nowUs) {
  if ((int32_t)(nowUs - nextImuUs) < 0) return;

  //Count slots we were too late for, then schedule the next one on the grid
  const uint32_t late = nowUs - nextImuUs;
  if (late >= IMU_PERIOD_US) missedImu += late / IMU_PERIOD_US;
  nextImuUs += (late / IMU_PERIOD_US + 1) * IMU_PERIOD_US;

  ImuSample sample;
  if (!imu.read(sample, nowUs)) return;

  steps.update(sample);
  fall.update(sample);
  ++imuSamples;
  lastImuUs = nowUs;

  if (RECORD_IMU)
    Serial.printf("%lu,%.5f,%.5f,%.5f,%.3f,%.3f,%.3f\n", (unsigned long)sample.timestampUs,
                  sample.ax, sample.ay, sample.az, sample.gx, sample.gy, sample.gz);

  if (SERIAL_PLOTTER) {
    plotCandidate |= steps.getState().candidate;
    plotConfirmed |= steps.getState().confirmed;
    if (nowUs - lastPlotUs >= 40000) {
      lastPlotUs = nowUs;
      const StepsState& m = steps.getState();
      Serial.printf("filtered:%.3f threshold:%.3f gyro:%.1f candidate:%u confirmed:%u\n",
                    m.filteredG, m.thresholdG, m.gyroDps, plotCandidate, plotConfirmed);
      plotCandidate = plotConfirmed = false;
    }
  }
}

//Drain the MAX30102 FIFO on its 20 ms slot
void servicePpg(uint32_t nowUs) {
  if ((int32_t)(nowUs - nextPpgUs) < 0) return;
  nextPpgUs += PPG_POLL_US;
  if ((int32_t)(nowUs - nextPpgUs) >= 0) nextPpgUs = nowUs + PPG_POLL_US;   //Fell behind; resync

  const uint32_t errorsBefore = ppg.getErrors();
  const uint32_t overflowsBefore = ppg.getOverflows();

  const uint8_t read = ppg.drain(nowUs, onPpgSample);
  ppgSamples += read;

  //Bus errors or FIFO overflow break timestamp continuity
  if (ppg.getErrors() != errorsBefore || ppg.getOverflows() != overflowsBefore)
    heart.invalidateStream();
  if (read) lastPpgUs = nowUs;
}

//Re-initialise any sensor that has gone quiet
void serviceRetry(uint32_t nowUs, uint32_t nowMs) {
  if ((int32_t)(nowMs - nextRetryMs) < 0) return;
  nextRetryMs = nowMs + RETRY_PERIOD_MS;

  if (!lastImuUs || nowUs - lastImuUs > SENSOR_STALE_US)
    imu.init(Wire);
  if (!lastPpgUs || nowUs - lastPpgUs > SENSOR_STALE_US) {
    heart.invalidateStream();
    ppg.init(Wire);
  }
}

//Send the snapshot packet to the phone
void serviceBle(uint32_t nowUs, uint32_t nowMs) {
  if ((int32_t)(nowMs - nextBleMs) < 0) return;
  nextBleMs = nowMs + BLE_PERIOD_MS;

  const bool imuFresh = imu.isReady() && lastImuUs && nowUs - lastImuUs < SENSOR_STALE_US;
  const bool ppgFresh = ppg.isReady() && lastPpgUs && nowUs - lastPpgUs < SENSOR_STALE_US;

  uint8_t packet[PACKET_SIZE];
  encodeSnapshot(packet, sequence++, nowMs, steps.getState(), heart.getState(), imuFresh, ppgFresh, fall.isPossibleFall());
  ble.publish(packet);
}

//Print the diagnostic status line
void serviceStatus(uint32_t nowMs) {
  if (!NORMAL_SERIAL || ppgTrace) return;
  if ((int32_t)(nowMs - nextStatusMs) < 0) return;
  nextStatusMs = nowMs + STATUS_PERIOD_MS;

  const uint32_t elapsedMs = nowMs - lastStatusMs;
  const float imuHz = elapsedMs ? 1000.0f * (imuSamples - lastImuSamples) / elapsedMs : 0;
  const float ppgHz = elapsedMs ? 1000.0f * (ppgSamples - lastPpgSamples) / elapsedMs : 0;
  lastStatusMs = nowMs;
  lastImuSamples = imuSamples;
  lastPpgSamples = ppgSamples;

  const HeartRateState& p = heart.getState();
  const StepsState& m = steps.getState();
  Serial.printf("imu=%.1fHz missed=%lu imu_err=%lu ppg=%.1fHz ppg_err=%lu fifo_ovf=%lu gaps=%lu led=%u contact=%u ir_dc=%.0f ir_rms=%.1f rel=%.5f ibi=%u period=%.2f lag=%u dc_drift=%.3f weak=%lu early=%lu long=%lu unstable=%lu motion_reject=%lu raw_bad=%lu reposition=%lu steps=%lu activity=%u hr=%.1f hr_valid=%u q=%u R=%.3f ble=%u\n",
      imuHz, (unsigned long)missedImu, (unsigned long)imu.getErrors(),
      ppgHz, (unsigned long)ppg.getErrors(), (unsigned long)ppg.getOverflows(),
      (unsigned long)p.timingGaps, (unsigned)ppg.getLedCurrent(), p.contactDetected,
      p.irDc, p.irRms, p.relativeAmplitude, (unsigned)p.intervalCount,
      p.periodicity, (unsigned)p.periodLag, p.dcDrift,
      (unsigned long)p.weakSignalResets, (unsigned long)p.earlyPeaksIgnored,
      (unsigned long)p.longIntervalResets, (unsigned long)p.unstableBeatWindows,
      (unsigned long)p.motionRejectedSamples, (unsigned long)p.invalidRawSamples,
      (unsigned long)p.opticalRepositions,
      (unsigned long)m.steps, (unsigned)m.activity,
      p.heartRateValid ? p.heartRateBpm : 0.0f, p.heartRateValid,
      (unsigned)p.quality, p.ratioOfRatios, ble.isConnected());
}

//Serial commands:
//  d        dismiss a possible-fall flag
//  p / s    start / stop the raw timestamp,red,ir stream
//  N W M H  LED current normal, wrist, moderate, high
//  L<n>\n   LED current register value 1-100
void serviceCommands() {
  while (Serial.available()) {
    const char command = Serial.read();

    //Collect digits for an L command until a non-digit arrives
    if (readingLedValue) {
      if (command >= '0' && command <= '9' && pendingLedDigits < 3) {
        pendingLedValue = pendingLedValue*10 + (uint8_t)(command - '0');
        ++pendingLedDigits;
      } else {
        if ((command == '\n' || command == '\r') && pendingLedDigits && pendingLedValue >= 1 && pendingLedValue <= 100)
          applyLedCurrent((uint8_t)pendingLedValue);
        readingLedValue = false;
      }
      continue;
    }

    if (command == 'L') {
      readingLedValue = true;
      pendingLedValue = 0;
      pendingLedDigits = 0;
    } else if (command == 'd') {
      fall.dismiss();
    } else if (command == 'p' && NORMAL_SERIAL) {
      Serial.flush();                        //Keep queued status text out of the CSV stream
      Serial.println("timestamp_us,red,ir");
      Serial.flush();
      ppgTrace = true;
    } else if (command == 's') {
      ppgTrace = false;
    } else if (command == 'N') {
      applyLedCurrent(LED_NORMAL);
    } else if (command == 'W') {
      applyLedCurrent(LED_WRIST);
    } else if (command == 'M') {
      applyLedCurrent(LED_MODERATE);
    } else if (command == 'H') {
      applyLedCurrent(LED_HIGH);
    }
  }
}

void setup() {
  Serial.setTxBufferSize(1024);              //Queue diagnostics instead of blocking a 10 ms slot
  Serial.begin((RECORD_IMU || RECORD_PPG) ? 921600 : 115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_HZ);
  Wire.setTimeOut(20);

  const bool imuOk = imu.init(Wire);
  const bool ppgOk = ppg.init(Wire);
  if (NORMAL_SERIAL) Serial.printf("IMU=%u MAX30102=%u\n", imuOk, ppgOk);

  if (imuOk) {
    const bool calibrated = imu.calibrateGyro();
    if (NORMAL_SERIAL) Serial.printf("gyro_calibration=%u\n", calibrated);
  }

  ble.init("HealthWatch");

  if (RECORD_IMU) Serial.println("timestamp_us,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps");
  if (RECORD_PPG) Serial.println("timestamp_us,red,ir,gyro_dps,quality,contact_detected,ir_dc,ir_rms,relative_amplitude,interval_count,periodicity,heart_rate_bpm,hr_valid,timing_gaps,invalid_raw,motion_rejected,accepted_intervals,optical_repositions,fifo_overflows,i2c_errors");

  nextImuUs = nextPpgUs = micros();
  nextBleMs = millis();
  lastStatusMs = millis();
  nextStatusMs = lastStatusMs + STATUS_PERIOD_MS;
  nextRetryMs = lastStatusMs + RETRY_PERIOD_MS;
}

void loop() {
  const uint32_t nowUs = micros();
  serviceImu(nowUs);
  servicePpg(nowUs);
  heart.expire(nowUs);

  const uint32_t nowMs = millis();
  serviceRetry(nowUs, nowMs);
  serviceBle(nowUs, nowMs);
  serviceStatus(nowMs);
  serviceCommands();
}
