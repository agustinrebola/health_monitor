#!/usr/bin/env python3
"""Assembles docs/CODE_GUIDE.md.

Code blocks are copied from the real source files at build time, so they cannot
drift from the code. The explanations live in guide_a.py .. guide_d.py as
(first_line, last_line, text) items. The build fails if any non-blank source line is
not explained exactly once, which is how a stale guide is noticed after the code changes.

usage: python3 build_guide.py
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
REPO = HERE.parent.parent
OUT = HERE.parent / "CODE_GUIDE.md"
FIG_DIR = HERE.parent / "figures"
sys.path.insert(0, str(HERE))

from guide_a import FILES_A  # noqa: E402
from guide_b import FILES_B  # noqa: E402
from guide_c import FILES_C  # noqa: E402
from guide_d import FILES_D  # noqa: E402

PARTS = [
    ("Part 1. The watch firmware", "The code that runs on the ESP32. Start with the sketch, then the two sensor drivers, then the two algorithms, then Bluetooth.",
     FILES_A + FILES_B + FILES_C[:2]),
    ("Part 2. The iPhone app", "Swift and SwiftUI. Receives the watch's packet, keeps a history and shows the screens.", FILES_D),
    ("Part 3. The replay and capture tools", "Run on the computer. They let the firmware's heart-rate code be tested on recordings.", FILES_C[2:]),
]

PREFACE = """# HealthWatch code guide

A line-by-line study guide to the HealthWatch prototype: a wrist wearable built from an ESP32 board, an MPU6050 motion sensor and a MAX30102 pulse sensor, plus an iPhone app that shows what the watch measures.

It explains **every non-blank line of code** in the firmware (`HealthWatch/`), the iPhone app (`ios/`) and the replay tools (`replay/`), and it uses graphs to show how the code behaves, so the behaviour can be studied and not only the syntax. It does not cover the old full-featured version in `archive/`, the Xcode project file, or the scripts in `docs/tools/` that make the figures.

## How to read this guide

- Each file has its own section. The code is shown in numbered blocks; the number on the left is the real line number in the file. After each block comes an explanation of what those lines do and why they are written that way.
- The code blocks are copied from the actual files when this guide is built, and the build refuses to finish if a line of code has no explanation. So the code shown here is the code that exists.
- A few neighbouring lines that do exactly the same job on different data (for example, converting the x, y and z readings) are explained together. Blank lines are not shown. Comments are shown with the code they sit in.
- Where a graph helps, it appears right after the lines that produce the behaviour. The figures are numbered in order of first appearance.
- Suggested order: skim this page, read the sketch, then read each module in the order it is called. If you want to study only one thing, the heart-rate code and its graphs are the richest part.

## Where the graphs come from

The graphs are not drawings of what the code is supposed to do. Each one was made by running the **real source files** on real or synthetic input and plotting what they computed.

| Figures | Input | Notes |
|---|---|---|
| Heart-rate filters, stages, correlation, timeline, rule-by-rule comparison | Recorded 100 Hz pulse-sensor traces from this watch: a fingertip (60 s), the wrist hand-held (30 s and 60 s) and the wrist strapped to its back (60 s) | The traces are the author's own measurements and are not stored in the repository. The timeline graph shows a trace in which the watch reports a heart rate only about a third of the time. |
| Step counter graphs | **Synthetic** acceleration made by a script: gravity plus noise plus bumps at chosen times | No recording of real walking exists yet, so the step graphs show the logic's behaviour on idealised input, not how accurate it is on a real wrist. |
| Byte layouts, FIFO, flowcharts, timing | Diagrams | The numbers in the byte diagrams are computed in code, not typed. The timing zoom is a schematic and says so. |
| Step total on the phone | Simulation of the app's rule | Illustrates one rule in `HistoryStore.swift`. |

The recorded traces have **no independent heart-rate reference** (no chest strap or counted pulse), so none of these graphs says the heart rate shown is *correct*. They show how the code decides.

## Units and words used

- **Hz**: times per second. **ms**: milliseconds (a thousandth of a second). **µs**: microseconds (a millionth).
- **g**: the acceleration of gravity. A sensor lying still reads about 1 g. **dps**: degrees per second, a rotation rate.
- **bpm**: beats per minute. **counts**: the raw whole-number readings of an analogue-to-digital converter.

## Glossary

| Term | Meaning |
|---|---|
| **Sample** | One measurement. The sensors here take 100 samples per second. |
| **I2C** | A two-wire connection (data wire SDA, clock wire SCL) that lets one microcontroller talk to several chips, each with its own address. |
| **Register** | A numbered storage cell inside a chip. Writing one changes a setting; reading one gets a measurement or a status. |
| **ADC** | Analogue-to-digital converter: turns a light level or a voltage into a whole number. 18-bit means numbers from 0 to 262,143. |
| **FIFO** | "First in, first out": a queue. The pulse sensor writes samples into a 32-entry queue until the microcontroller reads them. |
| **Baseline (DC)** | The slowly changing average level of a signal. |
| **AC component** | What is left after the baseline is removed: the fast ripple, here the heartbeat. |
| **Filter** | A calculation that keeps some speeds of change and removes others. A *high-pass* filter removes slow changes; a *low-pass* filter removes fast ones. |
| **Correlation** | A number from -1 to 1 for how closely two series rise and fall together. 1 means identical shape. |
| **Ring buffer** | An array used as a circle: writing past the end wraps around to the start, overwriting the oldest value. |
| **Little-endian** | Storing a multi-byte number with the low byte first. |
| **BLE** | Bluetooth Low Energy. The watch advertises; the phone connects; the watch *notifies* the phone each time a value changes. |
| **Header file / source file** | In C++, a `.h` file declares what a class contains and a `.cpp` file holds the working code. |
| **Constructor** | A function that runs automatically when an object is created. |

"""

APPENDIX = """
## Appendix A. Known quirks and limits

These came up while writing the guide. They are facts about the code as it is, not guesses.

**Watch firmware**

- **Gyro readings are not zero-corrected.** A board lying still prints a few degrees per second on each axis. The earlier full version measured this offset at boot and removed it.
- **Sensor status is a boot-time snapshot.** `imuOk` and `ppgOk` are set once in `setup()` and sent unchanged. If a sensor fails later nothing re-initialises it and the phone still shows it as ready. A sensor that was missing at boot is never retried; reset the board.
- **The raw-stream timestamp is the read time**, not the time the sensor measured. The replay tools ignore it.
- **Cadence can go stale.** When a rhythm is broken by a too-long or inconsistent gap, `cadence` keeps its last value while the activity reads stationary. It is only cleared by the 1.5 s walking timeout.
- **Step counting limits** (see the graph of rejected inputs): walking slower than about 55 steps per minute is never confirmed; a smooth 2 Hz swing weaker than about 0.1 g is counted for only about 40% of its steps, because the adaptive threshold learns the swing and settles just above it (stronger swings count every step); shaking at 6 per second *is* counted, as about 180 steps per minute; after hard steps the threshold falls slowly (about 14 seconds in the example graph), during which gentle steps are missed.
- **The step counter has never been run on real walking data.** Only synthetic input has been used.
- **Heart-rate range** is 40 to 150 bpm. The shortest accepted period is 0.40 s because shorter periods were drift at the edge of the search.
- **Heart-rate gates removed in the simplification.** The archived full version also required the red and infrared waveforms to agree, limited baseline drift and pulse size, and rejected samples while the wrist was rotating fast (it read the gyro). The simplified code uses none of these. On the recorded traces it behaves, but those were mostly still arms; vigorous arm movement has not been tested and may produce false readings.
- **A desk can look like a wrist.** The board lying face-down measured about 7,600 counts, above the contact level of 5000. The pulse-size check then finds no pulse, so no rate is shown, but `contact` reads true.
- **One rule is untested.** The extra scan required for periods of 1.0 s or longer made no difference on any recorded trace.
- **Coverage on the wrist.** With the sensor strapped to the back of the wrist the watch shows a heart rate for about 39% of the time in the recorded minute. No heart-rate value here has been compared with a reference device.
- **Bluetooth is open.** No pairing, no encryption, and the watch keeps no history while the phone is away.

**iPhone app**

- **Steps are credited to the day the phone received them.** Steps taken while disconnected are added when the phone reconnects.
- **The "Protocol" row** on the Device screen is the fixed text "1", not read from the packet.
- **Swift compiler warnings.** The delegate methods in `BluetoothManager` are on a main-actor class and Swift warns that they do not match the framework's non-isolated declarations. It works today because the central manager is created on the main queue, but a stricter Swift language mode would turn the warnings into errors.

**Replay tools**

- `capture_ppg.py` still sends an LED-current command and prints "at wrist LED current"; the simplified firmware ignores the command, so that label means nothing now.

## Appendix B. Regenerating this guide and its figures

The figures need Python 3 with `matplotlib` and `numpy`, a C++ compiler, and the recorded traces (set `TRACE_DIR`, default `~/Downloads`).

```
cd docs/tools
python3 make_diagrams.py     # static diagrams
python3 make_figures.py      # graphs computed from the real code
python3 build_guide.py       # this document
```

`build_guide.py` fails if any non-blank line of code has no explanation. After editing the code, the line numbers in `guide_a.py` to `guide_d.py` must be brought into line before it will build again.
"""


def read_lines(path):
    return (REPO / path).read_text().splitlines()


def validate(f, lines):
    covered = {}
    last = 0
    for it in f["items"]:
        if it[0] in ("T", "F"):
            continue
        a, b, text = it
        if not (1 <= a <= b <= len(lines)):
            raise SystemExit(f"{f['path']}: bad range {a}-{b} (file has {len(lines)} lines)")
        if a <= last:
            raise SystemExit(f"{f['path']}: items out of order or overlapping at line {a}")
        last = b
        for n in range(a, b + 1):
            covered[n] = True
    missing = [n for n, line in enumerate(lines, 1) if line.strip() and n not in covered]
    if missing:
        raise SystemExit(f"{f['path']}: lines with no explanation: {missing}")
    return len(covered)


def anchor(i):
    return f"sec-{i}"


def render():
    fig_numbers = {}
    out = [PREFACE]
    toc = ["## Contents", ""]
    body = []
    total_lines = 0
    total_items = 0
    sec = 0
    for part_title, part_blurb, files in PARTS:
        toc.append(f"**{part_title}**")
        toc.append("")
        body.append(f"\n---\n\n# {part_title}\n\n{part_blurb}\n")
        for f in files:
            sec += 1
            lines = read_lines(f["path"])
            covered = validate(f, lines)
            total_lines += len([l for l in lines if l.strip()])
            toc.append(f"{sec}. [{f['title']}](#{anchor(sec)}) (`{f['path']}`, {len(lines)} lines)")
            body.append(f'\n<a id="{anchor(sec)}"></a>\n\n## {sec}. {f["title"]}\n\n`{f["path"]}`\n\n')
            if f.get("intro"):
                body.append(f["intro"] + "\n")
            for it in f["items"]:
                if it[0] == "T":
                    body.append(it[1] + "\n")
                    continue
                if it[0] == "F":
                    name, caption = it[1], it[2]
                    if not (FIG_DIR / f"{name}.svg").exists():
                        raise SystemExit(f"missing figure {name}.svg (run make_figures.py and make_diagrams.py)")
                    if name in fig_numbers:
                        label_ = f"Figure {fig_numbers[name]} (shown again)"
                    else:
                        fig_numbers[name] = len(fig_numbers) + 1
                        label_ = f"Figure {fig_numbers[name]}"
                    body.append(f"\n![{label_}: {caption}](figures/{name}.svg)\n\n*{label_}. {caption}*\n")
                    continue
                a, b, text = it
                total_items += 1
                head = f"Line {a}" if a == b else f"Lines {a} to {b}"
                code = "\n".join(f"{n:>3}  {lines[n - 1].rstrip()}".rstrip() for n in range(a, b + 1))
                body.append(f"\n**{head}**\n\n```{f['lang']}\n{code}\n```\n\n{text.strip()}\n")
        toc.append("")
    toc.append("**Appendices**")
    toc.append("")
    toc.append("- Appendix A: known quirks and limits")
    toc.append("- Appendix B: regenerating this guide")
    toc.append("- Figure index (below)")
    toc.append("")
    index = ["\n## Figure index\n"]
    for name, n in sorted(fig_numbers.items(), key=lambda kv: kv[1]):
        index.append(f"{n}. `{name}`")
    stats = (f"\n*{total_items} explained blocks covering {total_lines} non-blank lines of code in "
             f"{sum(len(files) for _, _, files in PARTS)} files, with {len(fig_numbers)} figures.*\n")
    return "".join(out) + "\n".join(toc) + stats + "".join(body) + "\n" + APPENDIX + "\n".join(index) + "\n", total_items, total_lines, len(fig_numbers)


def main():
    text, items, lines, figs = render()
    OUT.write_text(text)
    print(f"wrote {OUT}: {items} explained blocks, {lines} code lines, {figs} figures, {len(text.split()):,} words")


if __name__ == "__main__":
    main()
