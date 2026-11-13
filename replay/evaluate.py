#!/usr/bin/env python3
"""Evaluate count error over labeled sessions using the firmware's motion core."""
import csv
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: evaluate.py manifest.csv", file=sys.stderr)
        return 2
    manifest = pathlib.Path(sys.argv[1]).resolve()
    with manifest.open(newline="") as file:
        rows = list(csv.DictReader(file))
    if not rows or not {"id", "path", "actual_steps"}.issubset(rows[0]):
        print("manifest needs id,path,actual_steps", file=sys.stderr)
        return 2
    print("id,actual,detected,absolute_error,percent_error")
    for row in rows:
        truth = int(row["actual_steps"])
        if truth < 0:
            raise ValueError("actual_steps cannot be negative")
        recording = (manifest.parent / row["path"]).resolve()
        result = subprocess.run([str(ROOT / "replay"), str(recording)],
                                capture_output=True, text=True, check=True)
        match = re.search(r"counted_steps=(\d+)", result.stderr)
        if not match:
            raise RuntimeError(f"missing count for {recording}")
        detected = int(match.group(1))
        error = abs(detected - truth)
        percent = f"{100 * error / truth:.1f}" if truth else "NA"
        print(f"{row['id']},{truth},{detected},{error},{percent}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
