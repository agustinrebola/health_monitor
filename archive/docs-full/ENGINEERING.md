# Engineering notes and validation plan

## Hardware and timing choices

The MPU6050 uses ±4 g, ±500 degrees/s, DLPF setting 3, and sample-rate divider 9. With DLPF enabled, its 1 kHz gyro output divided by 10 gives a 100 Hz register update rate. The host schedules reads every 10 ms using unsigned `micros()` differences and skips overdue slots. A host read timestamp is not the sensor conversion timestamp. The selected ranges are starting values for wrist activity; check clipping in recorded sudden movements before changing them.

The MAX30102 uses red plus IR SpO2 mode, 100 samples/s, 411 µs pulse width, no FIFO averaging, and a default LED current of `0x2C` (44, about 8.8 mA) per channel. `0x24` (36), `0x38` (56) and `0x64` (100) remain selectable, as does any value 1–100 over Serial. At the tested wrist placement `0x64` saturated both ADC channels and `0x2C` gave the highest valid-HR fraction in a 30-second sweep; the full sweep and the later finding that contact stability matters more than current are in `PPG_TEST.md` and `ARCHITECTURE.md`. These are signal-quality comparisons, not HR accuracy measurements. The MAX30102 has no green LED. It is polled every 20 ms and drained in chunks of five paired samples. The first batch is anchored to host read time; later samples use one continuous nominal 10 ms sequence with bounded drift correction, so FIFO batch-size jitter cannot create artificial beat-interval gaps. Absolute alignment with IMU samples and sensor clock accuracy remain approximate. Overflow invalidates that FIFO segment. The LED current and optical thresholds require tuning with the actual enclosure and skin contact.

Both devices share one 400 kHz I2C bus, but have separate schedules. The firmware is single-threaded. BLE notifications occur twice per second (500 ms); the 20-byte packet is small enough that this does not approach the connection interval budget. The iPhone app shows the step total from stored history plus the live packet, so the display tracks each packet rather than the one-per-minute history record. Serial uses a 1024-byte transmit queue so the periodic status line does not wait for every byte to leave the UART. The status line reports effective sample counts over 5 seconds, missed IMU slots, I2C error counts and FIFO overflow count. This does not measure jitter distribution or conversion-to-read latency; a logic analyzer or timestamp logging is needed for those.

## Motion algorithm

The motion core estimates a slowly varying gravity vector from accelerometer samples near 1 g, projects acceleration onto it, removes a slow baseline, smooths the remaining signal, and searches for positive local peaks above an adaptive threshold. Candidates require 250–1500 ms spacing. Three consistent candidates activate walking/running and are credited together. Large gyro motion with weak vertical peaks rejects a candidate. Activity falls back to general movement or stationary after inactivity.

This is an explainable baseline, not a validated wrist step detector. Periodic hand movements may still be indistinguishable from walking using one wrist IMU. The code does not yet calculate autocorrelation or use labeled peak morphology. Record real walking and stationary hand movements before adjusting thresholds. Evaluate walking count error and stationary false counts separately; keep tuning and validation sessions separate.

## Heart rate (`HeartRate` module)

The HR path removes the IR and red DC baselines with a 0.5-second tracker, applies two further ~0.7 Hz high-pass stages against wrist pressure and breathing drift, smooths both waveforms, and estimates the IR period once per second from normalized autocorrelation over a two-second window. It searches periods of 0.33–1.50 seconds, accepts only a local maximum of the correlation (never a period shorter than 0.40 s), requires correlation at least 0.65 and red/IR waveform agreement at least 0.70, and requires two successive period estimates within 80 ms (three for periods over 1.0 s, four when the candidate is more than 25% from a rate published in the last 15 s). A strong half-period match corrects a selected two-beat period. The two-second IR baseline drift must be at most 3%, and AC/DC at most 3%; abrupt optical shifts invalidate the estimate. The reported rate is smoothed only across consecutive valid estimates and expires 3 s after its last scan. Peak timing and five-interval consistency remain as diagnostics (`ibi`, `early`, `long`, `unstable`), but do not determine the reported HR. Every rule and the trace that motivated it are documented in `ARCHITECTURE.md`.

The processor suppresses HR on poor contact, weak pulsatility, clipping, missing samples or significant motion. Contact uses a settled DC level with hysteresis (enter 5000, exit 3000 IR counts), selected from current hardware logs showing uncovered values around 800–2700 and finger values around 130000–150000. Pulse quality requires IR RMS above 30 counts and AC/DC above 0.00015, selected from uncovered RMS near 11 counts, weak finger RMS around 55–125 and strapped-wrist RMS around 57–115; below the floor HR is withheld but the waveform history is kept. Quality is degraded until a valid periodic estimate exists. A large optical baseline jump resets the filters and history before reacquisition. These are hardware-specific prototype thresholds, not a wearer-independent calibration. The 60-second controlled finger trace had 5976 samples, no timing gaps, and a repeating waveform around 76–82 bpm in its earlier stable portion. Its later optical baseline fell substantially, so those portions remain unavailable. This recording has no independent pulse reference and does not establish HR accuracy or wrist performance. Synthetic white noise with similar RMS is rejected in the desktop test, but strong periodic motion artifacts may still be mistaken for pulses. The red/IR RMS ratio is printed as an optical diagnostic. The firmware does not convert it to SpO2 because no calibration for this assembly is available. HRV is not reported.

## Experimental fall indicator

An impact above 2.8 g with angular speed above 80 degrees/s starts a five-second observation. A subsequent still period and orientation change above 40 degrees latches a possible-fall flag. This has no emergency role. It can be dismissed with `d` over Serial. The algorithm and thresholds need deliberate non-fall and fall-surrogate recordings; false alerts are expected until validated.

## Reproducible validation

1. Record a separate CSV for each condition: normal, slow and fast walking; running; stationary arm swings; wrist shaking; limited arm swing; sitting; and ordinary daily tasks.
2. Manually count steps. Use the same wear position and document which wrist, enclosure, sampling diagnostics and ground-truth method.
3. Run `replay/build.sh`, then `replay/evaluate.py` on a manifest. Inspect candidate and confirmed timestamps around every error.
4. Tune only on a training set. Freeze thresholds and measure held-out count error plus zero-step false counts. Per-event precision/recall/F1 need timestamped ground-truth footsteps.
5. Compare HR against an independent reference during rest and motion. Do not enable SpO2 conversion without calibration and quality validation.
6. Check BLE reconnection, missing sensors, FIFO overflow, 32-bit timer rollover, long runs, power current and full-enclosure optical contact on hardware.

## Known design limits

The ESP32 does not persist steps or raw samples across power loss. The phone retains about seven days of minute snapshots and cannot reconstruct data while disconnected. BLE v1 is unpaired and does not provide confidential transport or authenticated commands. Battery reporting requires battery measurement circuitry. No end-to-end hardware, iOS runtime, step accuracy, HR accuracy, SpO2 accuracy or fall-detection performance has been measured here.

## Source references

- [MPU-6000/MPU-6050 register map](https://invensense.tdk.com/wp-content/uploads/2015/02/MPU-6000-Register-Map.pdf)
- [MAX30102 datasheet](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX30102.pdf)
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
