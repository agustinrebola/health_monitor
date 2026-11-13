# Motion replay

Build with `./build.sh`, then run `./replay path/to/imu.csv`. A CSV header is optional:

```csv
timestamp_us,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps
10000,0,0,1,0,0,0
20000,0,0,1,0,0,0
```

The executable prints candidate and confirmed event rows to stdout and counts to stderr. Timestamps must be increasing within a single 32-bit `micros()` epoch. Split longer recordings at rollover, or extend the replay format when long recordings are available. Ground truth counts and labels remain external; compare each recorded activity separately. The replay uses the same motion implementation as the firmware.

For count-error summaries, create a manifest CSV with `id,path,actual_steps` and run `python3 evaluate.py manifest.csv` after building. Zero-step sessions report absolute false counts. Precision, recall, and F1 require per-step ground truth timestamps, which this format does not contain.

`./ppg_replay raw_ppg.csv` runs the firmware's physiological processor on a `timestamp_us,red,ir` recording and summarizes its state every 500 samples. The raw trace has no IMU stream, so replay assumes no motion rejection; compare with the live diagnostic counters when movement occurs.
