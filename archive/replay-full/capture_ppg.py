#!/usr/bin/env python3
"""Capture the firmware's runtime red/IR CSV stream on macOS."""
import os
import pathlib
import re
import select
import subprocess
import sys
import termios
import time


def main() -> int:
    if len(sys.argv) not in (4, 5):
        print("usage: capture_ppg.py /dev/cu.usbserial-0001 seconds output.csv [normal|wrist|moderate|high|1..100]", file=sys.stderr)
        return 2
    port, seconds_text, destination = sys.argv[1:4]
    led_mode = sys.argv[4] if len(sys.argv) == 5 else "wrist"
    if led_mode in ("normal", "wrist", "moderate", "high"):
        led_command = {"normal": b"N", "wrist": b"W", "moderate": b"M", "high": b"H"}[led_mode]
    elif led_mode.isascii() and led_mode.isdecimal() and 1 <= int(led_mode) <= 100:
        led_command = f"L{int(led_mode)}\n".encode("ascii")
    else:
        raise ValueError("LED mode must be normal, wrist, moderate, high, or a register value from 1 to 100")
    seconds = float(seconds_text)
    if seconds <= 0:
        raise ValueError("seconds must be positive")
    output = pathlib.Path(destination).expanduser()
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    rows = 0
    rejected = 0
    gaps = 0
    try:
        # macOS USB serial settings can revert when the last handle closes;
        # keep this descriptor open while stty configures the same device.
        subprocess.run(["stty", "-f", port, "115200", "raw", "-echo"], check=True)
        time.sleep(4)  # allow a possible USB-open reset and gyro calibration
        os.write(fd, b"s")  # stop any trace left active by an earlier session
        termios.tcdrain(fd)
        # Clear both the queued backlog and bytes still arriving during stop.
        until = time.monotonic() + 1.0
        while time.monotonic() < until:
            if select.select([fd], [], [], 0.1)[0]:
                os.read(fd, 4096)
        os.write(fd, led_command)
        termios.tcdrain(fd)
        time.sleep(1)  # discard the FIFO and let the new optical level settle
        os.write(fd, b"p")
        termios.tcdrain(fd)
        deadline = time.monotonic() + seconds + 5
        started = False
        buffer = b""
        previous = None
        with output.open("w", encoding="ascii") as file:
            file.write("timestamp_us,red,ir\n")
            while time.monotonic() < deadline:
                if not select.select([fd], [], [], 0.5)[0]:
                    continue
                buffer += os.read(fd, 4096)
                while b"\n" in buffer:
                    line, buffer = buffer.split(b"\n", 1)
                    value = line.decode("ascii", errors="ignore").strip()
                    if value == "timestamp_us,red,ir":
                        started = True
                        deadline = time.monotonic() + seconds
                    elif started:
                        match = re.fullmatch(r"(\d+),(\d+),(\d+)", value)
                        if not match:
                            rejected += 1
                            continue
                        timestamp, red, ir = map(int, match.groups())
                        if (timestamp > 0xFFFFFFFF or red > 0x3FFFF or ir > 0x3FFFF
                                or (previous is not None and timestamp <= previous)):
                            rejected += 1
                            continue
                        if previous is not None and timestamp - previous > 15000:
                            gaps += 1
                        previous = timestamp
                        file.write(f"{timestamp},{red},{ir}\n")
                        rows += 1
        os.write(fd, b"s")
        termios.tcdrain(fd)
    finally:
        os.close(fd)
    print(f"saved {rows} samples to {output} at {led_mode} LED current; "
          f"rejected {rejected} malformed rows; {gaps} timing gaps")
    return 0 if rows else 1


if __name__ == "__main__":
    raise SystemExit(main())
