# HealthWatch

A wrist wearable prototype: ESP32 NodeMCU-32S, MPU6050 (accelerometer and gyro) and MAX30102 (pulse sensor). The watch counts steps, estimates heart rate, prints the sensor values to Serial Monitor and sends a small packet to the iPhone app over Bluetooth twice per second.

This is the simplified version. The full-featured version (fall detection, diagnostics, CSV recording modes, tests, long documentation) is kept in `archive/`.

## Wiring

| Signal | ESP32 pin |
|---|---|
| SDA (both sensors) | GPIO 21 |
| SCL (both sensors) | GPIO 22 |

Check each breakout's supply voltage before wiring.

## Build

1. Arduino IDE: install the **ESP32** boards package and the **NimBLE-Arduino** library (Library Manager).
2. Open `HealthWatch/HealthWatch.ino`, select **ESP32 Dev Module**, upload.
3. Open Serial Monitor at 115200.

## What you see on Serial Monitor

Ten lines per second:

```
gx:2.3,gy:-2.5,gz:-0.8,pulse:165523  hr=82  steps=14
```

`gx gy gz` are the gyro axes in degrees per second. `pulse` is the raw infrared reading: about 1,000 uncovered, about 130,000 to 170,000 on skin. `hr=` appears only while a heart rate is accepted. The same line graphs in Arduino Serial Plotter.

Send `p` to stream every pulse sample as `timestamp_us,red,ir` (for `replay/capture_ppg.py`) and `s` to stop.

## Files

| File | Lines | What it does |
|---|---|---|
| `HealthWatch.ino` | ~70 | Reads the IMU every 10 ms, drains the pulse sensor FIFO, prints, sends BLE |
| `src/IMU` | ~70 | MPU6050: init and read six values |
| `src/MAX30102` | ~80 | Pulse sensor: init, FIFO count, read one sample |
| `src/Steps` | ~130 | Step counter: magnitude, baseline, adaptive threshold, 3-peak confirmation |
| `src/HeartRate` | ~170 | Heart rate: filters, then finds the period at which the waveform repeats |
| `src/Bluetooth` | ~110 | NimBLE service with one 20-byte notify characteristic |

## How the heart rate works, in short

1. The IR signal is filtered: a slow baseline is subtracted, two more high-pass stages remove wrist pressure and breathing drift, and a low-pass smooths it.
2. Once a second, the last two seconds of waveform are compared with themselves shifted back by every possible beat period from 0.40 s to 1.50 s. The period where the waveform best repeats is the heartbeat.
3. The match must score at least 0.65, must be a genuine peak of the score (not just the shortest shift), and two consecutive scans must agree. A rate that jumps more than 25% from a recent one must persist four scans, because wrist drift at twice the beat period can look like a very slow heart.
4. The rate is held for three seconds after the last good scan, then dropped.

Readings are only published when confident, so on the wrist the rate blinks in and out. Strapping the sensor to the back of the wrist works best. Nothing here has been checked against a reference monitor.

## How the step counter works, in short

Acceleration magnitude, minus a slow average (gravity), smoothed. A peak above an adaptive threshold is a candidate step. Candidates must be 320 to 1100 ms apart and within 35% of the previous interval. Three consistent candidates confirm walking and are credited together; after that each peak is one step. 1.5 s without a step ends walking.

## Replay

`replay/build.sh` compiles `HeartRate.cpp` on the Mac; `replay/ppg_replay trace.csv` reports how much of a recorded trace produced a rate and its range. `replay/capture_ppg.py /dev/cu.usbserial-0001 60 ~/Downloads/trace.csv` records a trace from the board (close Serial Monitor first).

## BLE packet (20 bytes, little-endian)

| Offset | Field |
|---|---|
| 0 | version = 1 |
| 1 | flags: bit0 IMU ok, bit1 pulse sensor ok, bit2 heart rate valid, bit3 contact |
| 2 | activity: 1 stationary, 2 walking |
| 3 | quality: 0 no contact, 2 pulse seen, 3 rate valid |
| 4 | sequence (u32) |
| 8 | uptime ms (u32) |
| 12 | steps (u32) |
| 16 | cadence steps/min (u16) |
| 18 | heart rate bpm, 0 if not valid |
| 19 | unused |

The iPhone app in `ios/` reads this packet; it is unchanged from the full version.
