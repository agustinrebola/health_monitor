# HealthWatch: complete technical documentation

This document describes every part of the HealthWatch prototype as it exists today: the hardware it runs on, the firmware module by module, every constant and the reason it has the value it has, the Bluetooth protocol, the iPhone app, the desktop replay and test tooling, and the recorded experiments that drove each decision. It is written so that someone who has never seen the code can understand not only what it does but why it was built this way, and so that someone who wants to change a threshold knows what evidence they are overriding.

Where a number came from a measurement, the measurement is described. Where a number is a judgement call, that is said plainly. Nothing in this document should be read as a validated accuracy claim; the prototype has not been compared against a reference heart-rate monitor or a counted step ground truth.

---

## 1. What the system is

HealthWatch is a wrist-worn wellness prototype built from three parts:

1. **An ESP32 NodeMCU-32S** running an Arduino sketch. It reads two sensors over one I2C bus, counts steps, estimates heart rate, watches for a possible fall, and broadcasts a 20-byte summary over Bluetooth Low Energy twice per second.
2. **An MPU6050** inertial measurement unit (3-axis accelerometer and 3-axis gyroscope), sampled at 100 Hz.
3. **A MAX30102** optical pulse-oximeter front end (red and infrared LEDs with a photodiode), sampled at 100 Hz.

A companion **iPhone app** (SwiftUI, CoreBluetooth, iOS 17) connects to the watch, shows the live values, stores a history, and exports CSV.

A set of **desktop tools** in `replay/` compiles the exact firmware signal-processing modules on a Mac so that recorded sensor traces can be replayed deterministically, and so that unit tests guard the behaviour of each module.

The project deliberately stops short of several things: it does not report SpO2 (no calibration exists for this optical assembly), it does not persist anything on the watch across power loss, it does not pair or encrypt the BLE link, and the fall indicator has no alerting role.

---

## 2. Data flow at a glance

```
            I2C @ 400 kHz, SDA 21 / SCL 22
 ┌──────────┐                      ┌───────────┐
 │ MPU6050  │── every 10 ms ──────►│ IMU::read │──► ImuSample ──┬──► Steps::update ──► StepsState
 └──────────┘                      └───────────┘                └──► FallDetect::update
 ┌──────────┐                      ┌──────────────┐
 │ MAX30102 │── FIFO, polled ─────►│ MAX30102::   │──► PpgSample ──► HeartRate::update ──► HeartRateState
 │  FIFO    │   every 20 ms        │ drain        │   (timestamps from PPGClock)
 └──────────┘                      └──────────────┘
                                                            every 500 ms
                              StepsState + HeartRateState ─────────────► encodeSnapshot ──► Bluetooth::publish ──► iPhone
                                                            every 5 s
                              all counters ───────────────────────────► Serial status line
```

Everything runs on one core, in one `loop()`, with no interrupts and no RTOS tasks created by the sketch. Each piece of work has a deadline; `loop()` checks the deadlines in a fixed order and does whatever is due.

---

## 3. Hardware configuration and why

### 3.1 I2C bus

Both sensors share one bus at 400 kHz (`I2C_HZ`) on GPIO 21 (SDA) and GPIO 22 (SCL), the ESP32's default Wire pins. Two reasons for 400 kHz rather than the 100 kHz default:

- The MAX30102 FIFO drain reads up to 30 bytes per transaction, several transactions per poll. At 100 kHz a 30-byte read takes about 3 ms; at 400 kHz about 0.8 ms. The IMU slot is 10 ms, so bus time has to stay small or IMU samples are missed.
- Both chips are specified for 400 kHz fast mode.

`Wire.setTimeOut(20)` caps any single stuck transaction at 20 ms so that an unplugged or hung sensor cannot stall the loop indefinitely. Every bus failure is counted per device (`errors`) and shown on the status line, because an intermittent bus is the first thing to rule out when a sensor misbehaves.

### 3.2 MPU6050 register configuration

`IMU::init` writes five registers after checking `WHO_AM_I` (0x75) reads back 0x68. The identity check means a missing or mis-addressed sensor is reported at boot instead of producing garbage.

| Register | Value | Meaning | Why |
|---|---|---|---|
| PWR_MGMT_1 (0x6B) | 0x01 | Wake from sleep; clock from gyro X PLL | The chip boots asleep. The PLL clock is more stable than the internal oscillator, which matters for consistent 100 Hz output. |
| CONFIG (0x1A) | 0x03 | Digital low-pass filter setting 3 (~44 Hz accel, ~42 Hz gyro) | Step content is below 5 Hz; anything above ~40 Hz is noise. Enabling the DLPF also sets the gyro output rate to 1 kHz, which the divider below relies on. |
| SMPLRT_DIV (0x19) | 0x09 | Sample rate = 1 kHz / (9 + 1) = 100 Hz | 100 Hz gives 10 ms resolution on peak timing, enough for cadence up to 180 steps/min, and matches the optical sensor so the two streams share one time grid. |
| GYRO_CONFIG (0x1B) | 0x08 | ±500 °/s | Wrist rotation during walking peaks around 200 to 300 °/s. ±250 would clip on arm swings; ±1000 wastes resolution. |
| ACCEL_CONFIG (0x1C) | 0x08 | ±4 g | Walking impacts reach 2 g; running and falls exceed that. ±2 g clips on running; ±8 g halves resolution on the small vertical oscillation that step detection relies on. |

Scale factors follow from the ranges: 8192 LSB/g for ±4 g, 65.5 LSB/(°/s) for ±500 °/s. The read is one 14-byte burst from ACCEL_XOUT_H (0x3B): six bytes of acceleration, two of temperature (skipped), six of gyro.

### 3.3 Gyro calibration

The gyroscope has a per-device zero-rate offset that can be several degrees per second. `IMU::calibrateGyro` averages 200 samples at 10 ms spacing (two seconds) at boot and subtracts the mean from every later reading. The calibration is refused, and the firmware says so on Serial, if any of these hold:

- Acceleration magnitude leaves 0.9 to 1.1 g during the run: the device was moving, so the average would include real rotation.
- The per-axis variance exceeds 4 (°/s)²: too noisy to be a resting measurement.
- The per-axis mean exceeds 20 °/s: that is not a bias, that is motion.

Bias accumulates across calls (`bias = gyroBias + mean`) because `read` already subtracts the current bias; a second calibration refines rather than replaces. Calibration is not persisted, so the watch must be still for two seconds after every power-up. This was accepted for a prototype; a production device would store the offsets in flash.

### 3.4 MAX30102 register configuration

`MAX30102::init` checks PART_ID (0xFF) reads 0x15, issues a soft reset (MODE_CONFIG 0x09 = 0x40), waits 10 ms for the reset bit to clear, then writes:

| Register | Value | Meaning | Why |
|---|---|---|---|
| FIFO_WR_PTR, OVF_COUNTER, FIFO_RD_PTR (0x04 to 0x06) | 0 | Clear the FIFO | Start from a known phase. |
| FIFO_CONFIG (0x08) | 0x00 | No sample averaging, no rollover, almost-full = 0 | Averaging on the chip would blur beat timing. Rollover disabled means an overflow is detected (the overflow counter increments) instead of silently overwriting. |
| SPO2_CONFIG (0x0A) | 0x27 | ADC range 4096 nA, 100 samples/s, 411 µs pulse width (18-bit) | 100 sps matches the IMU grid. The longest pulse width gives full 18-bit resolution, which matters because a wrist pulse is about 0.1% of the baseline, roughly 100 to 200 counts out of 150,000. |
| LED1_PA, LED2_PA (0x0C, 0x0D) | `ledCurrent` | Red and IR drive current | See 3.5. |
| MODE_CONFIG (0x09) | 0x03 | SpO2 mode: red and IR both sampled | Two channels allow the red/IR agreement check that rejects single-channel artefacts. |

### 3.5 LED current: the full story

The LED current register value is 0.2 mA per count. Four named values exist in `MAX30102.h`:

| Name | Register | Current | Status |
|---|---|---|---|
| `LED_NORMAL` | 0x24 (36) | 7.2 mA | Original value. Fine on a fingertip; produced no valid wrist readings in a 20 s trial. |
| `LED_WRIST` | 0x2C (44) | 8.8 mA | **Boot default.** |
| `LED_MODERATE` | 0x38 (56) | 11.2 mA | Earlier default; about 15% valid on the wrist. |
| `LED_HIGH` | 0x64 (100) | 20 mA | Clips every red and IR sample on both finger and wrist. Kept only for experiments. |

How 44 was chosen. A sweep was recorded on the wrist in 30-second captures at 60, 52, 48 and 44 with the firmware of that day. Replaying each through the heart-rate module gave valid readings for 0%, 0%, 21% and 78% of the trace respectively. 44 also left ADC headroom (IR about 166,000 of a 262,143 maximum). A second upward sweep later (48, 56, 64, 72, 80, 20 s each, hand-held) produced no sustained readings at any setting, 80 clipped completely and 72 peaked within 1% of clipping. That sweep established that light level was not the limiting factor: its baseline drifted 3 to 10% per two seconds against 0.3% in the valid trace, and a 0.1% pulse cannot survive that. The conclusion, documented in `PPG_TEST.md`, is that contact stability dominates LED current, and the best results came with the sensor strapped to the back of the wrist.

Any value from 1 to 100 can be set at runtime with the Serial command `L<n>` followed by newline, and the named values with `N`, `W`, `M`, `H`. `setLedCurrent` pauses sampling (MODE_CONFIG = 0x83, the shutdown bit plus SpO2 mode) before clearing the FIFO pointers and writing the new currents, because clearing the pointers while the ADC is running was observed to leave the overflow counter racing the producer. It then resets the sample clock so timestamps re-anchor cleanly.

### 3.6 A note on green light

Commercial wrist wearables use green LEDs because haemoglobin absorbs green strongly and the shallow penetration suits the wrist. The MAX30102 has only red (660 nm) and IR (880 nm) emitters; there is no register that enables a green channel. Switching to green would mean a different sensor (for example the MAX30101 or MAX86150 family). This was investigated and ruled out as a software change.

---

## 4. Firmware architecture

### 4.1 Why a cooperative scheduler

The sketch uses no FreeRTOS tasks of its own, no hardware timers and no interrupts. `loop()` reads `micros()` once, then calls six service functions in a fixed order, each of which checks whether its deadline has passed and returns immediately if not.

Reasons:

- **Determinism.** The signal-processing modules are plain C++ with no locking. Running everything on one thread means the replay tools on the desktop execute exactly the same code path in exactly the same order as the device, sample for sample.
- **The work is small.** One IMU read, one FIFO poll and the step and heart-rate updates together take well under 2 ms at 240 MHz. There is slack in the 10 ms slot even with the 1 ms periodic autocorrelation scan.
- **The BLE stack already has its own tasks.** The ESP32 BLE library runs on the Bluetooth controller core; the sketch only calls `notify()` from `loop()`, which is the pattern the library expects.

The one concession to latency is `Serial.setTxBufferSize(1024)`: the status line is about 400 characters, and at 115200 baud that is 35 ms of UART time. Without a transmit queue, `Serial.printf` would block the loop for that long and three IMU slots would be missed every five seconds. With the queue, the call returns immediately and the UART drains in the background.

### 4.2 Deadline arithmetic

Every deadline comparison is written as `(int32_t)(now - deadline) >= 0`. The unsigned subtraction wraps correctly when `micros()` rolls over at 2³² (about 71.6 minutes) and `millis()` at 2³² (about 49.7 days), and the cast to signed turns "has the deadline passed" into a sign test. The same idiom appears in every module that compares timestamps. All timestamp differences are computed as unsigned subtraction first for the same reason.

### 4.3 The IMU slot and missed-slot accounting

`serviceImu` runs on a fixed 10 ms grid: the next deadline is always `previous + n × 10 ms`, never `now + 10 ms`. If the loop arrives late by more than one period, `missedImu` is increased by the number of whole periods lost and the deadline jumps forward to the next grid point. This matters for two reasons:

- The step detector's timing (cadence, refractory interval) is computed from actual sample timestamps, so a skipped slot is harmless to the maths, but the operator needs to know it happened. `missed` on the status line is that count; a healthy run shows 0 or 1 (the 1 comes from gyro calibration at boot).
- The grid itself must not drift. If the deadline were set relative to `now`, every late arrival would permanently shift the sampling phase and the effective rate would fall below 100 Hz.

The MPU6050 is sampled by register read rather than by its own FIFO or data-ready interrupt. This means the host read time is not the conversion time; the two differ by up to one sample period plus bus latency. For step detection at 100 Hz this is irrelevant.

### 4.4 The PPG poll

`servicePpg` runs every 20 ms (`PPG_POLL_US`) and drains whatever the sensor's 32-sample FIFO holds, typically two samples. Polling at half the sample rate halves the I2C overhead compared with polling every sample while keeping the FIFO far from full (it would take 320 ms to overflow). Unlike the IMU slot, if the poll falls behind by more than one period the deadline is reset to `now + 20 ms` rather than catching up; there is no benefit to back-to-back polls of a FIFO.

Before and after the drain, the sketch snapshots the sensor's error and overflow counters. If either changed, `heart.invalidateStream()` is called: a bus error or overflow means the sample sequence is no longer continuous and every filter and history buffer must restart rather than straddle the gap.

### 4.5 Retry

Every 5 s (`RETRY_PERIOD_MS`) `serviceRetry` re-initialises any sensor that has not produced a sample in 2 s (`SENSOR_STALE_US`). This is what makes the system tolerate a sensor being unplugged and replugged, or a brown-out on one module, without a reboot. The heart-rate stream is invalidated before re-initialising the MAX30102 because the re-init clears its FIFO.

### 4.6 Loop order

```
serviceImu      -- sample, step detector, fall detector
servicePpg      -- FIFO drain, heart-rate processor
heart.expire    -- age out a stale estimate even if no sample arrived
serviceRetry
serviceBle      -- encode and notify
serviceStatus   -- diagnostics line
serviceCommands -- Serial input
```

The two sensor services come first so that their deadlines get the least jitter. `expire` runs every pass because the heart-rate validity has a 3-second hold that must lapse even when the optical stream stops entirely (sensor removed). BLE and status come after processing so that a packet always reflects the samples just processed.

---

## 5. `Config.h`

| Constant | Value | Rationale |
|---|---|---|
| `SDA_PIN`, `SCL_PIN` | 21, 22 | ESP32 default Wire pins. |
| `I2C_HZ` | 400000 | Fast mode; keeps FIFO reads under 1 ms. |
| `IMU_PERIOD_US` | 10000 | 100 Hz, matching the sensor's own output rate so no sample is read twice or skipped. |
| `PPG_POLL_US` | 20000 | Two samples per poll; FIFO stays far from full. |
| `BLE_PERIOD_MS` | 500 | Two notifications per second. Originally 1000; halved so that the step count on the phone visibly tracks each step. A 20-byte notification at 2 Hz is negligible against a 30 ms connection interval. |
| `STATUS_PERIOD_MS` | 5000 | Long enough for the rate estimates (`imu=`, `ppg=`) to be stable to 0.1 Hz; short enough to see a fault within seconds. |
| `RETRY_PERIOD_MS` | 5000 | Re-init attempts are cheap but each one resets the heart-rate stream, so they must not be frequent. |
| `SENSOR_STALE_US` | 2000000 | Two seconds without a sample is unambiguous failure at 100 Hz; the same window marks the BLE "ready" flags. |

---

## 6. `Types.h`

Two sample structs and two enums are shared by everything.

`ImuSample` carries a host timestamp in microseconds, acceleration in g, rotation in °/s with gyro bias already removed, and a `valid` flag. `PpgSample` carries an estimated sensor-time timestamp, 18-bit red and IR counts, and `valid`. Both are plain aggregates so the desktop tests can construct them with brace initialisers.

`Activity` has five classes: `Unknown` (before the first sample), `Stationary`, `Walking`, `Running`, `GeneralMovement` (moving but not with walking cadence). `SignalQuality` has four: `NoContact`, `Invalid`, `Degraded`, `Good`. Both are `uint8_t`-backed so they can be written straight into the BLE packet, and the iPhone app's Swift enums use the same raw values.

---

## 7. `IMU` module

Covered in section 3. Three implementation details worth stating:

- `read` writes `out = {}` and the timestamp before checking `ready`, so a caller always gets a well-defined struct with `valid = false` on failure.
- `readRegs` uses a repeated start (`endTransmission(false)`) between the register-address write and the data read, as the MPU6050 requires; a full stop would make the chip reset its register pointer.
- The temperature bytes (offsets 6 and 7 of the burst) are read and discarded. Reading them costs 2 bytes of bus time; skipping them would cost a second transaction.

---

## 8. `MAX30102` module and `PPGClock`

### 8.1 The FIFO drain

`drain(nowUs, sink)` is called every 20 ms and does the following, in order:

1. Reads FIFO_WR_PTR, FIFO_RD_PTR and OVF_COUNTER (three single-byte reads).
2. If the overflow counter is non-zero, adds it to `overflows`, clears all three pointers, resets the sample clock, and returns 0. The samples still in the FIFO are abandoned on purpose: their timestamps can no longer be trusted, and a beat interval computed across an unknown gap would be wrong by an unknown amount. The caller sees the overflow count change and invalidates the heart-rate stream.
3. Computes `available = (wr - rd) & 0x1F`, the number of unread samples in the 32-entry circular FIFO.
4. Tells the clock a batch of that size is being read now.
5. Reads the samples in chunks of five (30 bytes). The Arduino Wire buffer on the ESP32 is large, but 32 bytes is the common floor across cores and keeping the chunk under it avoids a silent truncation if the sketch is ever built for another board. Each sample is six bytes: three for red, three for IR, 18 significant bits each (`& 0x3FFFF`).
6. If a chunk read fails part-way, the FIFO read pointer may have advanced by an unknown amount. The drain clears the pointers, resets the clock and stops. Again the caller sees the error count change and resets the processor.
7. Each decoded sample gets its timestamp from `clock.next()` and is handed to the sink function.

The sink is a plain function pointer rather than a template so that the module has no code in its header beyond the declaration, which is the style used throughout.

### 8.2 Why `PPGClock` exists

The FIFO delivers samples in batches whose size varies with loop jitter: sometimes one, sometimes three. If each sample were stamped with the host time at which the batch was read, consecutive samples within a batch would share one timestamp and the gap between batches would absorb all the jitter. A heartbeat interval measured from such timestamps would carry ±20 ms of noise, and the autocorrelation scan would see a jittery sample grid.

`PPGClock` keeps one nominal 10 ms sequence:

- The **first** batch after a reset anchors the sequence: the oldest pending sample is placed at `hostNow - (pending - 1) × 10 ms`.
- Every **later** batch predicts where its newest sample should fall on the existing sequence and compares that with the host clock. The difference is the phase error.
- If the phase error exceeds ±200 ms (`MAX_PHASE_ERROR_US`), something discontinuous happened (a stall, a reset), and the sequence re-anchors. This is deliberately visible: the resulting timestamp jump is caught by the heart-rate module's gap check and counted in `gaps`.
- Otherwise one eighth of the error is applied, capped at ±500 µs per batch (`MAX_CORRECTION_US`). This slews the sequence toward the host clock slowly enough that no single beat interval is distorted by more than 0.5 ms, while still tracking the small frequency difference between the sensor's oscillator and the ESP32's.

`test_ppg_clock.cpp` pins this behaviour: a two-sample batch at 20,000 µs yields 10,000 and 20,000; a following three-sample batch at 40,000 µs must not duplicate 20,000 and must stay within ±500 µs of 30,000; a reset followed by a single sample re-anchors exactly.

---

## 9. `Steps` module

### 9.1 Signal chain

For each valid IMU sample the detector computes, in order:

1. **Elapsed time** `dt`, clamped to 2 to 50 ms. Samples closer than 2 ms are treated as duplicates and ignored. Gaps longer than 100 ms (`SAMPLE_GAP_US`) reset the walking state and the band-pass filters, because the filters' state would otherwise be applied across a hole in the data.

2. **Gravity direction**, a three-component exponential average with time constant 0.8 s (`GRAVITY_TAU`), updated only while the acceleration magnitude is between 0.8 and 1.2 g. Outside that band the device is accelerating hard and the reading says little about which way is down. The time constant is long enough that walking oscillations average out and short enough to follow a change in wrist orientation within a couple of seconds.

3. **Vertical acceleration**: the dot product of the raw acceleration with the unit gravity vector, minus the gravity magnitude. This projects the signal onto the axis along which steps produce their characteristic impact, regardless of how the watch is rotated on the wrist. Using a projection rather than the raw magnitude halves the sensitivity to arm swing, which is mostly horizontal.

4. **Baseline removal**, a first-order high-pass with 0.65 s time constant (`BASELINE_TAU`). Removes residual gravity error and slow posture changes while passing the 1.5 to 3 Hz step content.

5. **Smoothing**, a first-order low-pass with 60 ms time constant (`SMOOTH_TAU`, about 2.7 Hz cutoff). Removes sensor noise and the higher harmonics of the impact so that each step produces one clean peak rather than several.

6. **Gyro smoothing**, 150 ms time constant (`GYRO_TAU`), on the rotation magnitude. Used for the rotation-dominated rejection and the motion classification.

7. **Adaptive threshold.** A 3-second average of the absolute filtered signal (`NOISE_TAU`) tracks how active the wearer is. The peak threshold is 1.4 times that (`THRESHOLD_GAIN`), clamped to 0.075 to 0.25 g. The floor keeps sensor noise from ever triggering; the ceiling keeps hard running from pushing the threshold so high that lighter steps after it are missed. The 3-second constant is slow on purpose: one isolated bump must not raise the threshold for the next ten steps.

### 9.2 Peak detection

A peak is declared when the sample one step back was rising, is no longer rising at the current sample, and exceeds the threshold:

```
peak = prev > prevPrev && prev >= smooth && prev > thresholdG
```

This is the same three-sample local-maximum test used in the author's reference `Steps.cpp`, and it introduces one sample (10 ms) of latency, which is immaterial.

### 9.3 Cadence confirmation

Peaks become step candidates only if at least 250 ms (`REFRACTORY_US`) have passed since the previous candidate; faster than that is not a human step (240 steps/min) and is treated as the same footfall. A candidate continues a walking streak if:

- the previous candidate was within 1.5 s (`MAX_STEP_GAP_US`, 40 steps/min), and
- the peak is not **rotation-dominated**: smoothed gyro above 160 °/s together with a filtered peak below 0.16 g. That combination is a wrist turn or a gesture, not a footfall, and would otherwise be counted when someone waves or turns a door handle.

Cadence in steps per minute is `60,000,000 / since` smoothed 70/30. The streak counter increments on each accepted candidate. **Walking is confirmed at the third consistent candidate** (streak reaches 2 after the first interval, meaning three peaks have been seen): at that moment all three are credited at once (`steps += 3`), and every later consistent candidate adds one. This is the "credit three together" rule from the reference implementation. The cost is that the first two steps of any walk are displayed about a second late; the benefit is that a single bump or a two-bump gesture never counts.

A candidate that breaks the streak (too long a gap, or rotation-dominated) resets the streak to zero and clears walking. Two seconds without any candidate (`IDLE_RESET_US`) also ends walking and zeroes the cadence.

### 9.4 Activity classification

- Walking confirmed and cadence ≥ 150 spm (`RUNNING_SPM`): `Running`.
- Walking confirmed otherwise: `Walking`.
- Not walking, and no sample in the last 2 s exceeded 0.06 g filtered or 12 °/s (`MOTION_G`, `MOTION_DPS`): `Stationary`.
- Otherwise: `GeneralMovement`.

### 9.5 What the reference template does that this module does not

The author's `Steps.cpp` includes an orientation gate: pitch and roll limits with a 60% valid-fraction window, intended to suppress counting when the wrist is held in a non-walking pose. This module does not compute pitch and roll and has no such gate; it relies on the gravity projection and the rotation-dominated test instead. The gate could be added in front of the candidate logic without touching anything else. The reference also uses a fixed 320 to 1100 ms interval band with a 35% consistency test between successive intervals; this module uses 250 to 1500 ms and smooths cadence rather than testing consecutive intervals. Neither has been validated against a counted ground truth, so neither is known to be better.

### 9.6 Tests

`test_motion.cpp` asserts that 10 s of a stationary 1 g signal produces zero steps and `Stationary`; that one isolated 1 g impulse produces zero steps; and that a 2 Hz, 0.25 g vertical sinusoid for 30 s produces more than 30 steps. These are mechanics checks, not accuracy checks.

---

## 10. `HeartRate` module

This is the largest module and the one with the most history. The design went through several revisions in response to recorded traces, and each surviving rule exists because a specific trace broke the previous version. The chronology is in section 17; this section explains the current algorithm.

### 10.1 Overview

The published heart rate comes from the **periodicity of the IR waveform**, not from counting peaks. Once per second the module computes the normalized autocorrelation of the last two seconds of filtered IR signal at every lag from 0.33 to 1.50 s, picks the best genuine peak, applies a series of plausibility gates, and requires the answer to be stable across consecutive scans before publishing. Peak-to-peak beat intervals are still computed, but only as diagnostics on the status line.

Why periodicity rather than peak counting: the first implementation counted peaks and failed on both finger and wrist traces. A wrist PPG at this sensor's signal-to-noise has secondary bumps (the dicrotic notch), missed peaks and spurious peaks, and a five-beat consistency check kept rejecting windows. Autocorrelation uses the entire waveform shape, so a missed or doubled peak barely moves the result, and it degrades gracefully: when the signal becomes noise the correlation simply falls below threshold.

### 10.2 Sample validation and resets

Each incoming sample is checked:

- **Zero or saturated** (either channel ≥ 0x3FFF0, within 16 counts of the 18-bit maximum): counted in `invalidRawSamples` and the whole stream is invalidated. A clipped sample has no waveform information and a filter fed one clipped sample rings for seconds.
- **Reposition**: after the filters have settled (200 samples), an IR sample more than 35% away from the baseline (`REPOSITION_FRACTION`) is a placement change, not a pulse (a pulse is 0.1%). Counted in `opticalRepositions`; the stream is invalidated so acquisition restarts cleanly rather than chasing a step.
- **Timing gap**: consecutive timestamps closer than 5 ms or farther than 50 ms (`MIN/MAX_SAMPLE_GAP_US`) mean the FIFO lost phase. Counted in `timingGaps`; the period estimate and history are reset and the filters are marked unsettled.

`invalidateStream` zeroes every filter, the beat-interval buffer and the history; it does **not** zero `periodicBpm` and `periodicTimestampUs`, the memory of the last published rate, because the continuity guard (10.7) needs that memory to survive a brief reset. Those two fields are only cleared in the constructor.

### 10.3 Filters

Four stages, each a first-order exponential filter at 100 Hz. For an alpha of `a`, the cutoff is approximately `a × 100 / 2π` Hz.

| Stage | Alpha | Cutoff | Purpose |
|---|---|---|---|
| DC tracker (`irDc`, `redDc`) | 0.02 | 0.32 Hz high-pass | Follows the slow optical baseline. The baseline itself is used for contact detection, the relative amplitude and the drift gate, so it must track the DC level faithfully; hence it is slow. |
| High-pass stage 1 (`irHp1`) | 0.045 | 0.72 Hz | Removes pressure and breathing drift. |
| High-pass stage 2 (`irHp2`) | 0.045 | 0.72 Hz | Same again; two stages give 12 dB/octave below 0.7 Hz. |
| AC smoothing (`irAc`, `redAc`) | 0.18 | 2.9 Hz low-pass | Removes sensor noise above the cardiac band. |

The two 0.72 Hz stages were added after the strapped-wrist trace. Its spectrum showed the pulse as a clear line at 1.4 to 1.5 Hz but with 0.5 to 0.7 Hz content three to ten times stronger (wrist pressure and breathing). With only the 0.32 Hz tracker, that drift dominated the two-second correlation window and no lag reached the threshold. Five filter variants were compared offline across six traces; the cascade of the existing tracker plus two 0.045 stages raised usable scans on the strapped trace from 9 of 57 to 30 of 57, raised the finger trace from 36 to 48, left the earlier good wrist trace at 27 of 27, and lowered the no-pulse trace from 9 to 5. Alternatives with a single sharper stage gained less; replacing the 0.02 tracker outright would have disturbed the contact and drift logic, so the extra stages were added on the AC path only.

The cost is attenuation at low heart rates: at 40 bpm (0.67 Hz) the two stages remove about 6 dB. Because the correlation is normalized this reduces signal-to-noise rather than shifting the estimate.

A 1-second energy tracker (`ENERGY_ALPHA` = 0.01) on the squared AC signal gives the RMS amplitude reported as `ir_rms`, and `relativeAmplitude` = RMS / baseline.

### 10.4 History buffers

Three ring buffers of 400 samples (4 s): filtered IR, filtered red, and the IR baseline. The correlation scan needs a 200-sample window plus up to 150 samples of lag, so 350 are required before the first scan (`SCAN_MIN_HISTORY`); 400 leaves margin. The baseline history exists only for the drift gate, which compares the baseline now with the baseline 200 samples ago.

### 10.5 Gates applied every sample

In priority order, each sample falls into exactly one branch:

1. **No contact.** The baseline must exceed 5000 counts to enter contact and stay above 3000 to remain (`CONTACT_ENTER_IR`, `CONTACT_EXIT_IR`). These come from this breakout's logs: uncovered the IR baseline is 800 to 2,700 counts; on skin it is 130,000 to 170,000. The 2,000-count hysteresis band exists because a reading near 1,000 was once observed to flicker contact on and off. Contact is also refused during the first 200 samples after a reset (`SETTLE_SAMPLES`) while the baseline tracker converges. Quality is `NoContact` (or `Invalid` while settling); the period estimate is reset.

2. **Weak pulse.** RMS at or below 30 counts (`MIN_PULSE_RMS`) or relative amplitude at or below 0.00015 (`MIN_PULSE_FRACTION`). Uncovered optical noise measured about 11 counts RMS, so 30 is a 2.7× margin; valid strapped-wrist pulses measured 57 to 115 counts and finger pulses 55 to 125. The fraction floor is 22 counts at a 150,000 baseline. Both floors were originally 50 counts and 0.0003 and were lowered after a live wrist session showed the pulse hovering at 45 to 55 counts with the estimator silent. In this branch the rate is withheld and quality is `Invalid`, but the **history is kept**. The earlier version reset the history here, which meant a pulse that dipped below the floor for one sample lost 3.5 s of acquisition; on the wrist that happened every few seconds. `weakSignalResets` counts episodes (entries into this branch), not samples.

3. **Motion.** Smoothed wrist rotation above 80 °/s (`MOTION_REJECT_DPS`). The optical path is corrupted by motion artefact long before the step detector cares, so the threshold is low. Quality `Degraded`; period estimate reset; `motionRejectedSamples` counts samples.

4. **Processing.** Everything else: beat-interval diagnostics, the optical ratio, and the periodicity scan. Quality is `Degraded` unless a rate is published, in which case `Good`.

### 10.6 The periodicity scan

Runs once per 100 samples (`SCAN_EVERY`) once 350 samples of history exist. Steps:

**Score every lag.** For each lag from 33 to 150 samples (`MIN_LAG`, `MAX_LAG`; 0.33 to 1.50 s; 180 to 40 bpm), compute the Pearson correlation between the most recent 200 filtered IR samples and the same window shifted back by the lag. A perfectly periodic signal scores 1.0 at its period; noise scores near 0.

**Require a genuine peak.** Only lags where the score exceeds the neighbour on the left and is at least the neighbour on the right are candidates. This rule was added after a trace with no pulse at all produced scores of 0.84 at the 0.33 s floor: a slowly drifting signal correlates highly at every short lag because it barely changes over 0.33 s, and the previous "take the shortest lag near the best score" rule picked the floor and would have displayed 182 bpm. With the peak requirement, monotonically decaying correlation has no candidate and the scan returns nothing. `test_physiology.cpp` has a case with 0.35 and 0.6 Hz drift and no pulse that must never publish.

**Prefer the shortest of near-equal peaks.** Among peaks within 0.03 of the strongest (`PEAK_TIE_MARGIN`), the shortest lag wins. A waveform with period T also correlates at 2T, often almost as well; without this rule half the true rate would be reported.

**Half-period correction.** If the chosen lag is at least 66 and the correlation at exactly half that lag is at least 0.50 and at least 80% of the chosen score (`HALF_PERIOD_MIN`, `HALF_PERIOD_RATIO`), halve the lag. This catches the case where the 2T peak is slightly stronger than the T peak by more than the tie margin, which happens with a pronounced dicrotic notch. `half_corr` on the status line shows that score.

**Drift gate.** The IR baseline must have changed by no more than 3% over the 200-sample window (`MAX_DC_DRIFT`). Above that, the sensor is sliding. This gate also actively invalidates an existing estimate rather than merely withholding a new one, because a large baseline move means the previous estimate's window was contaminated too.

**Amplitude gate.** Relative amplitude must be at most 3% (`MAX_RELATIVE_AMPLITUDE`). A real pulse is 0.05 to 0.5%; anything above 3% is motion artefact or a sensor being lifted. Also actively invalidates.

**Channel agreement.** The red and IR filtered waveforms over the window must correlate at least 0.70 (`MIN_CHANNEL_AGREEMENT`). Both LEDs see the same blood volume change, so a true pulse appears in both; an artefact from, say, ambient light leakage or an electrical glitch tends to appear in one. Drift is common-mode and passes this gate, which is why the drift gate exists separately.

**Correlation threshold.** The chosen peak must score at least 0.65 (`MIN_PERIODICITY`).

**Lag floor.** The chosen lag must be at least 40 (`MIN_ACCEPTED_LAG`, 150 bpm). Candidates at the search floor are, in every trace recorded so far, drift rather than tachycardia. This also actively invalidates. The watch therefore cannot report rates above 150 bpm; raising this limit should wait for a trace that actually contains a fast heart rate.

### 10.7 Stability and continuity

A scan that passes every gate is "usable". Publishing additionally requires agreement across consecutive scans:

- Consecutive usable scans whose lags differ by at most 8 samples (`LAG_TOLERANCE`, 80 ms) increment `stablePeriodScans`. A lag change larger than that resets the count to 1 and invalidates the current estimate.
- The number of agreeing scans required (`scansNeeded`) is normally **2**.
- It is **3** when the lag is 100 or more (`SLOW_LAG`, periods over 1.0 s). A 2-second window holds barely one cycle at such periods, so each scan carries less evidence; demanding a third scan equalises the total evidence with faster rates.
- It is **4** when a rate was published within the last 15 s (`RATE_MEMORY_US`) and the new candidate differs from it by more than 25% (`RATE_JUMP_FRACTION`). This is the continuity guard. The strapped-wrist trace, examined scan by scan, showed the pulse steady at lags 66 to 75 (80 to 90 bpm) all minute, interrupted by bursts of two or three scans at lags 131 to 145 where residual 0.7 Hz drift formed its own correlation peak. Those bursts passed every single-scan gate and the two-scan stability rule, and briefly displayed 42 bpm. A resting heart does not change by 25% within two seconds, so a candidate that far from a recent published rate must persist four scans. The guard removed every half-rate value from every recorded trace while leaving the finger and wrist valid fractions unchanged. A test case with a 3.5 s burst of 0.69 Hz sinusoid inside a 1.2 Hz pulse asserts that nothing below 60 bpm is ever published and that the 72 bpm rate resumes afterwards.

When the required count is reached, the rate `6000 / lag` is published. If an estimate was already valid, the new one is blended 60/40 with the old (`BPM_KEEP`, `BPM_TAKE`), so consecutive scans smooth the display without lag across a reacquisition. The scan's timestamp is recorded.

The estimate stays valid for 3 s after its scan (`RATE_HOLD_US`). `expire`, called every loop pass, enforces this even when samples stop arriving. This is why the phone sees the heart rate blink in and out on the wrist: with roughly a third of scans usable, holds of 3 s cover some gaps and not others.

### 10.8 Beat-interval diagnostics

Inside the processing branch, a three-sample peak detector on the smoothed IR signal (peak must exceed half the RMS to count) records the interval since the previous peak. Intervals inside the refractory period (333 ms, 180 bpm) increment `earlyPeaksIgnored`. Intervals between 333 ms and 1.5 s are kept in a five-slot buffer; when five are held and the longest exceeds 1.25× the shortest, `unstableBeatWindows` increments. Intervals outside the band increment `longIntervalResets` and empty the buffer. `intervalCount` (status line `ibi`) is the buffer fill level.

None of this affects the published rate. It exists because, when the rate disappears, the combination of these counters with `period`, `lag` and `half_corr` tells the operator which rule withheld it without needing a raw trace.

### 10.9 The optical ratio

`ratioOfRatios` = (red RMS / red DC) / (IR RMS / IR DC), the quantity that pulse oximeters map to SpO2 through an empirically calibrated curve. It is printed as `R=` on the status line and is **not** converted to a percentage anywhere, because the calibration curve depends on the specific LEDs, photodiode, optical geometry and skin contact of the assembly, and none of that has been characterised. Displaying a plausible-looking SpO2 from an uncalibrated ratio would be misleading, so the BLE flag for SpO2 validity is permanently zero and the app says so.

### 10.10 Quality levels in practice

- `NoContact`: nothing on the sensor.
- `Invalid`: settling, or on skin with the pulse below the floor.
- `Degraded`: on skin with a pulse-sized signal, rhythm not confirmed; also any motion rejection.
- `Good`: a rate is being published right now.

On a strapped wrist the display alternates between `Degraded` and `Good`. A constant `Degraded` with `lag` jumping around means drift from unstable contact; a constant `Invalid` with `weak` climbing means the pulse is too small at this placement.

---

## 11. `FallDetect` module

An experimental three-phase state machine with no alerting role.

**Normal.** Gravity is tracked with a 0.005 alpha (about 2 s time constant) while the acceleration magnitude is within 0.8 to 1.2 g. An impact is an acceleration magnitude above 2.8 g (`IMPACT_G`) together with rotation above 80 °/s (`IMPACT_DPS`). Both are required: a hard hand-clap produces the acceleration without the rotation, and a fast wrist turn produces the rotation without the acceleration. On impact, the current gravity direction is saved as the posture reference and the phase becomes Observing.

**Observing.** For up to 5 s (`OBSERVE_TIMEOUT_US`), the detector waits for stillness: magnitude within 0.12 g of 1 g and rotation below 15 °/s (`STILL_TOLERANCE_G`, `STILL_DPS`), sustained for 1.5 s (`STILL_REQUIRED_US`). Any movement restarts the still timer. If 5 s pass without a qualifying still period, the person kept moving normally and the detector returns to Normal.

**Judgement.** Once still, the tracked gravity direction is compared with the reference by cosine. If the angle exceeds 40° (`POSTURE_COSINE` = 0.766) the posture changed (standing to lying) and the phase becomes PossibleFall; `events` increments.

**PossibleFall** latches until `dismiss()` is called, which the sketch does on the Serial command `d`. The BLE flag bit 5 carries it to the phone, which shows a red banner. There is no way to dismiss it from the phone in this version; a production device needs an authenticated cancel path.

The thresholds are engineering estimates from the literature on wrist-worn fall detection and have not been tested against any fall surrogate or any set of ordinary activities. False positives from sitting down hard and then resting the arm are expected.

---

## 12. The BLE protocol and the `Bluetooth` module

### 12.1 Packet

One fixed 20-byte little-endian packet, defined in `Packet.h` and mirrored byte for byte in `BLEProtocol.swift`:

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | version | Always 1. The app rejects any other value. |
| 1 | 1 | flags | bit 0 IMU fresh, bit 1 PPG fresh, bit 2 HR valid, bit 3 contact, bit 4 SpO2 valid (always 0), bit 5 possible fall |
| 2 | 1 | activity | `Activity` raw value |
| 3 | 1 | PPG quality | `SignalQuality` raw value |
| 4 | 4 | sequence | Increments per packet; lets the app spot drops. |
| 8 | 4 | uptime ms | Device `millis()`. Lets the app order packets and detect a reboot. |
| 12 | 4 | steps | Since boot. |
| 16 | 2 | cadence | Steps per minute, 0 when not walking. |
| 18 | 1 | heart rate | bpm rounded; 0 unless bit 2 is set. |
| 19 | 1 | SpO2 | Always 0. |

Twenty bytes is the payload of one notification at the default ATT MTU of 23 without negotiating a larger MTU, which some phones are slow to do. Little-endian matches both the ESP32 and ARM, so neither side byte-swaps. The "fresh" flags are set when the sensor is initialised and has produced a sample within the last 2 s, so the app can distinguish "sensor missing" from "sensor present but no contact".

The heart-rate byte is zero when invalid rather than carrying a stale value, and the app decodes it to `nil` in that case. This is deliberate: a stale number on the screen is worse than "Unavailable".

### 12.2 Transport

The `Bluetooth` class wraps the ESP32 Arduino core's bundled BLE library (`BLEDevice`, `BLEServer`, `BLEService`, `BLECharacteristic`, `BLE2902`). The library ships with the core, so no additional install is needed and the sketch builds on a fresh Arduino IDE with only the ESP32 board package. NimBLE-Arduino, an alternative with a smaller flash footprint, was considered when the sketch reached 86% of flash; it is not installed and has not been adopted.

`init` creates one primary service with the custom 128-bit UUID `f0a1c001-…`, one characteristic `f0a1c002-…` with READ and NOTIFY properties, and the Client Characteristic Configuration descriptor (0x2902) that a central writes to enable notifications. It seeds the value with zeros so an early read returns a well-formed packet, starts the service, and advertises the service UUID so the phone can filter its scan.

`publish` always stores the packet in the characteristic (so a read returns the latest) and calls `notify()` only when a central is connected. The connect and disconnect callbacks maintain that flag; on disconnect, advertising is restarted so the phone can reconnect without a watch reboot.

The link is unpaired and unencrypted. Anyone in range can read the packet. For a prototype with no personal identifiers in the data this was accepted; it is listed as a limitation.

---

## 13. The sketch: Serial interfaces

### 13.1 Output modes

Three compile-time switches, at most one set to 1:

- `RECORD_IMU`: 921600 baud, one CSV line per IMU sample (`timestamp_us,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps`). For labelled step recordings, replayable with `replay/replay`.
- `RECORD_PPG`: 921600 baud, one CSV line per optical sample with the processor's full state appended. For deep debugging.
- `SERIAL_PLOTTER`: 115200 baud, five labelled values at 25 Hz for the Arduino Serial Plotter: filtered acceleration, threshold, gyro, candidate flag, confirmed flag.

With all three at 0 (normal mode) the sketch prints a status line every 5 s and accepts commands.

### 13.2 Status line

One line, space-separated `key=value`, prefixed `imu=`. The capture and monitoring scripts in this project parse it by that prefix.

| Field | Meaning |
|---|---|
| `imu` | Achieved IMU sample rate over the last 5 s. Should read 100.0 Hz. |
| `missed` | Cumulative IMU slots skipped. 0 or 1 is normal. |
| `imu_err` | Cumulative MPU6050 I2C failures. |
| `ppg` | Achieved optical sample rate. 99.4 to 99.6 Hz is normal (the sensor's oscillator runs slightly slow). |
| `ppg_err` | Cumulative MAX30102 I2C failures. |
| `fifo_ovf` | Cumulative FIFO overflow count. A flat value is fine; a climbing one means the loop is stalling. The large resting value seen on this board accumulated during early development and persists until reboot. |
| `gaps` | Cumulative timing gaps detected by the heart-rate module. |
| `led` | LED current register value. |
| `contact` | 1 when the optical baseline says skin is present. |
| `ir_dc` | IR baseline in counts. Uncovered ≈ 1,000; wrist ≈ 130,000 to 170,000; finger similar. |
| `ir_rms` | IR pulse amplitude in counts after filtering. |
| `rel` | `ir_rms / ir_dc`. |
| `ibi` | Beat intervals currently held (0 to 5). Diagnostic only. |
| `period` | Best correlation score this scan. |
| `lag` | Chosen period in 10 ms samples; bpm = 6000 / lag. 0 when no peak qualified. |
| `dc_drift` | Fractional baseline change over the last 2 s. |
| `weak`, `early`, `long`, `unstable` | Cumulative diagnostic counters (section 10.5, 10.8). |
| `motion_reject` | Cumulative samples dropped for wrist rotation. |
| `raw_bad` | Cumulative zero or saturated samples. |
| `reposition` | Cumulative large baseline steps. |
| `steps` | Steps since boot. |
| `activity` | `Activity` raw value. |
| `hr` | Published rate, or 0.0. |
| `hr_valid` | 1 when `hr` is current. |
| `q` | `SignalQuality` raw value. |
| `R` | Red/IR ratio of ratios. |
| `ble` | 1 when a phone is connected. |

### 13.3 Commands

| Command | Effect |
|---|---|
| `d` | Dismiss a latched possible-fall flag. |
| `p` | Flush pending status text, print the header `timestamp_us,red,ir`, and start streaming one raw sample per line. The status line is suppressed while streaming so the CSV stays clean. |
| `s` | Stop the raw stream. |
| `N`, `W`, `M`, `H` | LED current normal (36), wrist (44), moderate (56), high (100). |
| `L<digits>\n` | LED current register value 1 to 100. Up to three digits; terminated by newline or carriage return. Any other character cancels. |

Every LED change invalidates the heart-rate stream (the optical level steps) and falls back to a full sensor re-init if the register write fails.

---

## 14. The iPhone app

### 14.1 Structure

Four files plus the app entry point:

- `BLEProtocol.swift`: the UUIDs, the `Activity` and `PPGQuality` enums with the same raw values as the firmware, and `WatchSnapshot` with a `decode` that validates length and version, reads the little-endian fields, and maps the heart-rate and SpO2 bytes to optionals based on the flag bits.
- `BluetoothManager.swift`: a `CBCentralManager` on the main queue. `scan` filters by the service UUID. `connect` stops scanning and connects; on connection it discovers the service, then the characteristic, enables notifications and performs one read so the first value arrives without waiting half a second. Each notification is decoded into `snapshot` (a `@Published` property that drives the UI) and passed to the history store. Malformed packets increment `invalidPackets`, shown on the Device tab.
- `HistoryStore.swift`: see 14.2.
- `ContentView.swift`: four tabs. Dashboard (live values), Activity (steps today and the recent record list), Heart (current rate and a chart of the last 300 stored records that had a valid rate), Device (scan, connect, diagnostics, detailed-recording toggle, CSV export via `ShareLink`).
- `HealthWatchApp.swift`: creates the two stores, injects them as environment objects, links the Bluetooth manager to the history store on appear, and flushes the history to disk when the scene goes inactive or to the background.

### 14.2 History and the step total

The store keeps up to 10,000 snapshots in a JSON file in Application Support. Normally it keeps **one snapshot per minute** per device (it drops any snapshot within 60 s of the last stored one on the same day), which gives about seven days of continuous history. With **Detailed recording** on (Device tab) every packet is stored, which is what the heart-rate test procedure uses to export per-packet validity; at 2 Hz the 10,000-record cap fills in 83 minutes, so the toggle is meant to be switched off afterwards.

**Steps today** is computed, not stored. The store walks the records in order and, for each pair of consecutive records from the same device where uptime did not go backwards (no reboot) and the step count did not decrease, adds the difference if the later record was received today. This makes the total robust to watch reboots (a reboot resets steps to zero and uptime to zero; that pair is skipped and counting resumes from the next pair) and to multiple watches.

Because only one record per minute is stored, this total used to change once a minute no matter how often packets arrived, which was the real reason the display felt slow. The fix: `dailySteps(live:)` takes the latest live snapshot and appends it to the walk as if it were a stored record, unless it already is the last stored one. The live packet is counted by the same rule, so when it is later stored (at the next minute boundary) nothing is double-counted. Both "Steps today" labels pass `bluetooth.snapshot`.

**Persistence is coalesced.** Encoding 10,000 records to JSON on every packet would take tens of milliseconds on the main actor twice per second. `add` now schedules a save at most once every 5 s; the app also saves on leaving the foreground. The worst case is losing the last 5 s of history on a crash, which is one record in normal mode.

### 14.3 Known app limits

Daily totals are attributed to the phone's receipt date, so steps taken while disconnected are credited when the phone reconnects, all at once, to that day. Time disconnected cannot be reconstructed because the watch stores nothing. The chart shows stored records only, so in normal mode it is one point per minute.

---

## 15. Desktop tooling

### 15.1 Build and tests

`replay/build.sh` compiles, with `-Wall -Wextra -pedantic`, the three signal-processing modules (`Steps.cpp`, `HeartRate.cpp`, `PPGClock.cpp`) together with the harnesses, and runs the three test binaries. The modules are compiled from the firmware tree directly, not copied, so the desktop and the device always run the same source. This is the single most important property of the project's workflow: every tuning change in this document was evaluated by replaying recorded traces on the desktop before being flashed.

The modules can be compiled on a desktop because they include only `<stdint.h>`, `<math.h>` and `<stdlib.h>`; nothing Arduino-specific. The drivers (`IMU`, `MAX30102`, `Bluetooth`) include Arduino headers and are not part of the desktop build.

`test_physiology.cpp` cases, each with its reason:

| Case | Asserts | Guards against |
|---|---|---|
| Clean 1.2 Hz pulse, 800/1000 counts | HR valid, 68 to 76 bpm, quality Good, no gaps | Basic function |
| Constant input | Contact yes, HR no, quality Invalid | Zero-amplitude signal must not produce a rate |
| 1,200/2,000 counts constant | No contact, quality NoContact | Contact threshold |
| Weak 1.2 Hz pulse, 80/120 counts on 90k/150k | HR valid, periodicity > 0.55 | The floors must admit this module's weak finger signal |
| White noise, ±400 counts | HR never valid over 60 s | Amplitude alone must not satisfy the estimator |
| Finger off for 10 s then back | Valid before, invalid during, reacquired within 15 s, ≥ 2 repositions | Reposition handling |
| 5 s of zero samples | Invalid during, recovers after | Saturation/zero handling |
| Baseline ramp of 450 counts/s after 30 s | HR stays valid and correct | Slow drift must not erase a clear pulse |
| Pulse with a 0.7× second harmonic | HR valid at the fundamental throughout | Dicrotic-notch shape must not halve or double the rate |
| 0.35 + 0.6 Hz drift, no pulse | HR never valid | Peak requirement (the 182 bpm bug) |
| 1.2 Hz pulse with a 3.5 s 0.69 Hz burst | Never below 60 bpm; valid before and after | Continuity guard (the 42 bpm bug) |

### 15.2 Replay tools

`ppg_replay file.csv [every]` feeds a `timestamp_us,red,ir` trace through `HeartRate` with zero gyro and prints the state every `every` samples (default 500, i.e. 5 s; use 100 for per-scan decisions). Its summary line on stderr gives sample count, the fraction of samples during which a rate was valid, timing gaps, and the min/mean/max of valid rates. That summary is the figure of merit used throughout this document.

`replay imu.csv` does the same for `Steps` on an IMU CSV, printing candidate and confirmed events. `evaluate.py` runs it over a manifest of files with known step counts and reports count error.

### 15.3 `capture_ppg.py`

Records the raw stream from the live board on macOS without Arduino Serial Monitor:

```
python3 replay/capture_ppg.py /dev/cu.usbserial-0001 60 ~/Downloads/ppg.csv [normal|wrist|moderate|high|1..100]
```

It opens the port, configures 115200 baud raw mode with `stty` **while holding the descriptor open** (macOS resets a USB serial port's settings when the last handle closes, which caused the first version to send its commands at 9600 baud and record an empty file), waits 4 s for the board's USB-open reset and gyro calibration, sends `s` to stop any stream a previous run left active and drains the backlog, sends the LED command and waits 1 s for the level to settle, sends `p`, and then records only lines that match `digits,digits,digits` with increasing timestamps and in-range values until the time is up. It reports rows saved, malformed rows rejected and timing gaps over 15 ms. It sends `s` and waits for it to transmit before closing, which the first version did not, leaving the board streaming into the next session.

---

## 16. The recorded evidence

All traces are 100 Hz red/IR CSVs in `~/Downloads/`. Results are from the current firmware unless stated.

| Trace | Placement | LED | Samples | Valid | Range (bpm) | What it showed |
|---|---|---|---|---|---|---|
| `ppg-controlled` | Fingertip, 60 s | 36 | 5,976 | 50% | 74 to 82 | Clean repeating waveform early; baseline collapse later. No reference. |
| `ppg-wrist-high` | Wrist | 100 | — | clipped | — | Every sample at 262,143. |
| `ppg-wrist-normal` | Wrist, 20 s | 36 | 1,993 | 10% | 91 | Unclipped, weak. |
| `ppg-wrist-moderate` | Wrist, 20 s | 56 | 1,993 | 35% | 85 to 91 | |
| `ppg-wrist-led60/52/48/44` | Wrist, 30 s each | 60/52/48/44 | ~2,990 | 20% / 0% / 18% / 78% | 86 to 94 | The sweep that chose 44. Pre-fix firmware showed 182 bpm at 60, 52 and 48. |
| `ppg-wrist-led44-60s` | Wrist hand-held, 60 s | 44 | 5,976 | 0% | — | No cardiac line in the spectrum; drift only. Exposed the 182 bpm floor bug. |
| `ppg-wrist-sweep2-led48…80` | Wrist hand-held, 20 s each | 48 to 80 | 1,993 | 0 to 27% | 76 to 91 | Baseline drift 3 to 10% per 2 s; 80 clipped. Established that contact, not light, was limiting. |
| `ppg-wrist-strapped-led44` | Back of wrist, strapped, 60 s | 44 | 5,977 | 32% | 81 to 87 | Pulse present under 3 to 10× drift. Drove the extra high-pass stages and the continuity guard. |

Live on the strapped wrist after the final changes, the board published 81 to 82 bpm at quality 3 intermittently, and the user reported that the back of the wrist works better than the inside. No trace has an independent pulse reference, so the consistency between traces (low 80s to low 90s at rest) is encouraging but is not an accuracy result.

---

## 17. Chronology of decisions

In order, with the trigger for each:

1. **Peak counting with a five-interval consistency check** was the first heart-rate design. Status logs showed `unstable` climbing to 65 within a minute on a finger: the peak picker could not select consistently among primary peaks, dicrotic bumps and noise.
2. **Pulse floor tuning** (50 counts, 0.0003) from the observed uncovered noise (11 counts) and weak finger amplitudes (55 to 125).
3. **Contact hysteresis** (5,000 / 3,000) after a reading near 1,000 counts flickered contact on.
4. **A capture script** to get raw waveforms, since five-second summaries could not show which peaks were real. Its first two versions failed (baud ordering; stop handling) and were fixed.
5. **Periodicity replaces peak counting**, after the first clean finger trace showed a waveform repeating every 0.75 to 0.81 s even where the peak picker lost intervals. Half-period correction and the drift and amplitude gates were added when replay found a two-beat selection and a stale estimate surviving a baseline drop.
6. **LED current made selectable; 100 shown to clip; 56 then 44 chosen** by sweep.
7. **Lag floor of 40**, after 60, 52 and 48 all produced exactly 181.8 bpm (lag 33).
8. **Peak requirement in the scan**, after a drift-only 60 s trace produced 0.84 correlation at the floor with no pulse present.
9. **Two 0.7 Hz high-pass stages**, after the strapped trace's spectrum showed the pulse under 3 to 10× drift; chosen from five variants by replaying six traces.
10. **Three scans for slow periods and four scans for a > 25% jump**, after the per-scan dump of the strapped trace showed two-to-three-scan bursts at twice the beat lag.
11. **Floors lowered to 30 and 0.00015, history preserved through the weak gate**, after a live session showed the pulse hovering at 45 to 55 counts with the estimator repeatedly restarting.
12. **BLE period halved to 500 ms and the app's step total made live**, after the step display was found to update once per minute because of history gating, independent of radio rate. History writes were coalesced to make 2 Hz sustainable.
13. **Rewrite into the project's style** (one class per module, named constants, `//Comment` annotations, no namespaces), verified by identical replay numbers and passing tests.

---

## 18. Known limitations and open work

- **No accuracy validation.** Heart rate needs a simultaneous reference (chest strap or counted pulse). Steps need counted ground truth across the activity list in `ENGINEERING.md`.
- **Wrist coverage is about a third of the time** when strapped. Improving it is now a matter of contact mechanics (enclosure, strap pressure) more than software.
- **Rates above 150 bpm are rejected** by the lag floor. Revisit with an exercise trace.
- **No persistence on the watch.** Steps and calibration reset on power loss. The ESP32 has NVS/Preferences for this.
- **BLE is open.** No pairing, no encryption, no authenticated commands.
- **Fall detection is untested** against falls or against daily activities.
- **SpO2 is not reported** and should not be until the optical ratio is calibrated against a reference oximeter on this assembly.
- **The FIFO overflow counter** on the test board carries a large count from early development; it is cumulative since boot and only its rate of change matters.
- **`PPG_TEST.md`** carries the step-by-step test procedure and the detailed history of the wrist experiments; `ENGINEERING.md` carries the validation plan.
