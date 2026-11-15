# HealthWatch prototype

Fresh implementation for an ESP32 NodeMCU-32S, MPU6050 and MAX30102. The firmware is an Arduino IDE sketch in `HealthWatch/`; select **ESP32 Dev Module** and install the **NimBLE-Arduino** library (2.x) from Library Manager. Wire SDA is GPIO 21 and SCL is GPIO 22. Inspect the exact MAX30102 breakout's power and I2C level shifting before wiring it. Do not infer a safe supply voltage from the sensor chip alone.

The complete technical documentation, covering every module, constant and the experiment behind each decision, is `docs/ARCHITECTURE.md`. Configuration rationale, algorithm limits, and a validation plan are in `docs/ENGINEERING.md`.
For a heart-rate-specific recording, follow `docs/PPG_TEST.md`.

The firmware samples the MPU6050 at a 100 Hz target, drains the MAX30102 FIFO independently, processes motion locally, and sends a compact BLE snapshot twice per second. The step detector includes candidate filtering and a short cadence confirmation period. It is a starting algorithm and needs labeled wrist recordings before accuracy claims. HR is experimental. SpO2 is deliberately unavailable until a calibration for the actual optical assembly is established; the raw ratio is available through Serial diagnostics.

## Bring-up

1. Install ESP32 boards in Arduino IDE and the NimBLE-Arduino library from Library Manager. Open `HealthWatch/HealthWatch.ino` and select ESP32 Dev Module.
2. Check module supply, ground, SDA/SCL voltage and pull-ups. Connect SDA 21, SCL 22.
3. Upload and open Serial Monitor at 115200. The firmware prints ten lines per second of `gx:<dps>,gy:<dps>,gz:<dps>,pulse:<ir counts>`: the three gyro axes in degrees per second and the raw infrared reading from the pulse sensor. The same line plots in Arduino Serial Plotter. Send `v` to toggle it and `i` to turn on the long diagnostic status line (every 5 s, off by default). A missing sensor does not prevent the other sensor from running.
4. Open the iOS app project in `ios/` with Xcode and run it on an iPhone.

For labeled IMU recordings, set `RECORD_IMU` to `1` in the sketch, use Serial at 921600 baud, and save the CSV stream. Record ground-truth steps and activity labels separately. The Serial stream can alter timing, so inspect missed samples in a normal run and repeat captures if the output is incomplete.

For Arduino Serial Plotter, set `SERIAL_PLOTTER` to `1` instead. It emits labeled filtered acceleration, threshold, gyro magnitude, candidate and confirmation values at 25 Hz without status text.

For a heart-rate sensor trace, set `RECORD_PPG` to `1` instead and capture Serial at 921600 baud. It emits timestamped raw red/IR counts, motion intensity, signal quality, AC/DC metrics, beat-interval state, heart-rate validity and error counters. Keep the sensor firmly against the wrist and record a quiet baseline, then a controlled movement segment. This stream is distinct from the iPhone's half-second BLE snapshots.

The normal firmware also accepts `p` over Serial at 115200 baud to stream a compact `timestamp_us,red,ir` CSV and `s` to stop. On macOS, `python3 replay/capture_ppg.py /dev/cu.usbserial-0001 60 ~/Downloads/ppg.csv` records one minute after Arduino Serial Monitor is closed.

At boot, keep the watch still for gyro calibration. If motion is detected, calibration is skipped and the firmware reports it. Calibration is not persisted; no accelerometer scale calibration is applied.

## Replay

`replay/replay.cpp` uses the exact `Motion` implementation built into the firmware. Run `./replay/build.sh`, then `./replay/replay imu.csv`. CSV fields are `timestamp_us,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps` (header optional). It prints detected events and a total. This does not establish accuracy without ground truth.

## BLE protocol v1

Custom service `f0a1c001-0cb6-4f90-9c6c-4af42ca31f01`, read/notify snapshot characteristic `f0a1c002-0cb6-4f90-9c6c-4af42ca31f01`. Fixed 20 byte little-endian packet:

| Offset | Type | Meaning |
|---|---|---|
| 0 | u8 | version = 1 |
| 1 | u8 | flags: bit 0 IMU, bit 1 PPG, bit 2 HR valid, bit 3 contact, bit 4 SpO2 valid (currently 0), bit 5 possible fall |
| 2 | u8 | activity: 0 unknown, 1 stationary, 2 walking, 3 running, 4 general movement |
| 3 | u8 | PPG quality: 0 no contact, 1 invalid, 2 degraded, 3 good |
| 4 | u32 | snapshot sequence |
| 8 | u32 | device uptime milliseconds |
| 12 | u32 | steps since boot |
| 16 | u16 | cadence steps/minute |
| 18 | u8 | heart rate bpm, 0 if invalid |
| 19 | u8 | SpO2 percent, 0 if invalid |

Notifications are transient; there is no historical device log or secure pairing in v1. The iPhone app stores received snapshots locally. Daily totals are derived from step-count deltas and cannot reconstruct time spent disconnected. These limits must be resolved before using the watch as a reliable activity record.

## Validation priorities

Record labeled sessions of normal/slow/fast walking, running, stationary arm swing, wrist shaking, limited arm swing and daily tasks. Evaluate count error and false events separately. Check actual IMU sample timing, FIFO overflow, I2C errors, BLE reconnection, and current draw on the assembled hardware. HR should be compared with an independent reference; SpO2 needs an appropriate calibration and validation procedure.

This is a wellness research prototype, not a medical device or emergency fall detector.

The experimental fall indicator requires an impact, orientation change, and subsequent stillness. It latches until `d` is sent through Serial. It is not an alerting service and has not been validated. A physical or authenticated cancellation control is needed for a wearable deployment.

## Firmware layout

Each module is a class with `init`/`update`/`reset`-style methods and `get` accessors, tunables as named `static const` values at the top of its `.cpp`, and `//Comment` annotations. `Types.h` holds the shared sample structs and enums.

| File | Role |
|---|---|
| `HealthWatch.ino` | Scheduler: IMU slot, FIFO poll, retry, BLE, status line, Serial commands |
| `Config.h` | Pins, bus speed, scheduler periods |
| `IMU` | MPU6050 driver and gyro calibration |
| `MAX30102` | Pulse-oximeter driver, FIFO drain, LED current |
| `PPGClock` | Sample timestamp sequence across FIFO batches |
| `Steps` | Step detector and activity class |
| `HeartRate` | Heart-rate estimator and optical diagnostics |
| `FallDetect` | Experimental fall event detector |
| `Packet.h` | 20-byte snapshot layout and UUIDs shared with the app |
| `Bluetooth` | GATT service on the NimBLE-Arduino library |
