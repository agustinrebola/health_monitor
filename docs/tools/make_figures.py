#!/usr/bin/env python3
"""Data-driven figures for docs/CODE_GUIDE.md.

Every curve comes from running the real firmware source (HeartRate.cpp, Steps.cpp)
through the dump tools in this folder, so the figures show what the code does.

Heart-rate figures use recorded 100 Hz traces (timestamp_us,red,ir CSVs). They are
read from TRACE_DIR (default ~/Downloads) and are NOT stored in the repository.
Step figures use synthetic acceleration, because no walking recording exists yet;
each such figure says so.

usage:  python3 make_figures.py            (needs matplotlib and numpy)
        FIG_PNG_DIR=/tmp/png python3 make_figures.py   (also write PNG copies)
"""
import os
import pathlib
import subprocess
import tempfile

import numpy as np

from figlib import *  # noqa: F401,F403

REPO = HERE.parent.parent
SRC = REPO / "HealthWatch" / "src"
TRACE_DIR = pathlib.Path(os.environ.get("TRACE_DIR", pathlib.Path.home() / "Downloads"))
WORK = pathlib.Path(os.environ.get("FIG_WORK") or tempfile.mkdtemp(prefix="codeguide_"))
WORK.mkdir(parents=True, exist_ok=True)

TRACE_STRAPPED = "ppg-wrist-strapped-led44"   # wrist strapped on the back, 60 s
TRACE_WRIST = "ppg-wrist-led44"               # earlier wrist trace, 30 s, strong pulse
TRACE_FINGER = "ppg-controlled"               # fingertip, 60 s
TRACE_NOPULSE = "ppg-wrist-led44-60s"         # hand-held wrist, 60 s, drift only


# ---------------------------------------------------------------- building

def compile_cpp(out, *sources, include=None):
    cmd = ["c++", "-std=c++17", "-O2"]
    if include:
        cmd += ["-I", str(include)]
    cmd += [str(s) for s in sources] + ["-o", str(out)]
    subprocess.run(cmd, check=True)


# Text replaced in a COPY of HeartRate.cpp to switch one rule off for the ablation figure.
VARIANTS = {
    "full": [],
    "no_guard": [("if (scansSinceValid < MEMORY_SCANS && fabsf(fresh - bpm) > 0.25f * bpm) needed = 4;", "")],
    "no_extra_hp": [("hp1 += 0.045f * (x - hp1);", "hp1 += 0.0f * (x - hp1);"),
                    ("hp2 += 0.045f * (x - hp2);", "hp2 += 0.0f * (x - hp2);")],
    "no_tie": [("static const float TIE_MARGIN = 0.03f;", "static const float TIE_MARGIN = 0.0f;")],
}
_built = {}


def hr_tool(tag):
    if tag in _built:
        return _built[tag]
    text = (SRC / "HeartRate.cpp").read_text()
    for old, new in VARIANTS[tag]:
        assert old in text, f"variant {tag}: HeartRate.cpp no longer contains {old!r}; update VARIANTS"
        text = text.replace(old, new)
    d = WORK / f"variant_{tag}"
    d.mkdir(exist_ok=True)
    (d / "HeartRate.cpp").write_text(text)
    exe = WORK / f"dump_heartrate_{tag}"
    compile_cpp(exe, HERE / "dump_heartrate.cpp", d / "HeartRate.cpp", include=SRC)
    _built[tag] = exe
    return exe


class Run:
    """One recorded trace pushed through the real HeartRate code."""

    def __init__(self, trace, tag="full"):
        prefix = WORK / f"{trace}_{tag}"
        subprocess.run([hr_tool(tag), TRACE_DIR / f"{trace}.csv", prefix], check=True, capture_output=True)
        s = np.loadtxt(f"{prefix}_samples.csv", delimiter=",", skiprows=1)
        (self.i, self.ir, self.dc, self.x0, self.x1, self.x2, self.ac, self.amplitude,
         self.contact, self.valid, self.bpm, self.filled) = s.T
        self.i = self.i.astype(int)
        self.t = self.i / 100.0
        sc = np.loadtxt(f"{prefix}_scans.csv", delimiter=",", skiprows=1, ndmin=2)
        self.scan_i = sc[:, 0].astype(int)
        self.scan_valid, self.scan_bpm, self.scan_lastlag, self.scan_stable, self.scan_since = sc[:, 1:6].T
        self.scores = sc[:, 6:]          # columns are lags 39..151
        self.lags = np.arange(39, 152)

    def valid_fraction(self):
        return float(np.mean(self.valid == 1))

    def valid_bpm(self):
        return np.where(self.valid == 1, self.bpm, np.nan)


def pick(row):
    """The firmware's lag choice, re-done in Python (checked against its own state)."""
    sc = {lag: row[lag - 39] for lag in range(39, 152)}
    peaks = [lag for lag in range(40, 151) if sc[lag] > sc[lag - 1] and sc[lag] >= sc[lag + 1]]
    if not peaks:
        return 0, [], 0.0, 0
    best = max(sc[lag] for lag in peaks)
    best_lag = max(peaks, key=lambda lag: sc[lag])
    chosen = next(lag for lag in peaks if sc[lag] >= best - 0.03)
    return chosen, peaks, best, best_lag


def pearson(ac, end, lag, window=200):
    x = ac[end - window + 1:end + 1]
    y = ac[end - lag - window + 1:end - lag + 1]
    return float(np.corrcoef(x, y)[0, 1])


# ---------------------------------------------------------------- filters

def lowpass(alpha, f, fs=100.0):
    z1 = np.exp(-1j * 2 * np.pi * f / fs)
    return alpha / (1 - (1 - alpha) * z1)


def highpass(alpha, f, fs=100.0):
    return 1 - lowpass(alpha, f, fs)


def fig_filters():
    f = np.logspace(np.log10(0.05), np.log10(30), 800)
    old = np.abs(highpass(0.02, f) * lowpass(0.18, f))
    new = np.abs(highpass(0.02, f) * highpass(0.045, f) * highpass(0.045, f) * lowpass(0.18, f))
    single = np.abs(highpass(0.045, f))

    fig, ax = plt.subplots(figsize=(9, 4.2))
    shade(ax, 0.3, 0.7, RED, 0.12)
    shade(ax, 0.67, 2.5, GREEN, 0.14)
    ax.plot(f, old, color=MUTED, lw=1.6, ls="--", label="baseline remover + smoother only (what was tried first)")
    ax.plot(f, new, color=BLUE, lw=2.4, label="all four stages in HeartRate::update (current code)")
    ax.plot(f, single, color=ORANGE, lw=1.0, ls=":", label="one extra high-pass stage alone (alpha 0.045)")
    ax.axhline(0.707, color=GRID, lw=1)
    ax.text(0.052, 0.72, "-3 dB (0.707)", fontsize=8, color=MUTED)
    ax.set_xscale("log")
    ax.set_xlim(0.05, 30)
    ax.set_ylim(0, 1.3)
    ax.set_xlabel("frequency (Hz)")
    ax.set_ylabel("fraction of the signal that passes")
    ax.set_title("What the heart-rate filters keep: the beat band passes, slow drift is pushed down")
    ax.text(0.68, 1.27, "wrist pressure and\nbreathing drift", fontsize=8, color=RED, va="top", ha="right")
    ax.text(0.72, 1.27, "heartbeat\n40 to 150 bpm", fontsize=8, color=GREEN, va="top", ha="left")
    # where does the new chain pass 0.707 and what does it do to drift?
    i_peak = int(np.argmax(new))
    ax.plot(f[i_peak], new[i_peak], "o", color=BLUE)
    ax.text(f[i_peak] * 1.25, new[i_peak] + 0.04, f"peak {new[i_peak]:.2f} at {f[i_peak]:.1f} Hz", fontsize=8, color=BLUE)
    for fx in (0.5, 0.7):
        k = int(np.argmin(np.abs(f - fx)))
        ax.plot(f[k], new[k], "o", ms=4, color=RED)
        ax.text(f[k] * (0.93 if fx == 0.5 else 1.08), new[k] - 0.07, f"{new[k]:.2f}", fontsize=8, color=RED, ha="center")
        ax.plot(f[k], old[k], "o", ms=4, color=MUTED)
        ax.text(f[k] * (0.93 if fx == 0.5 else 1.08), old[k] + 0.03, f"{old[k]:.2f}", fontsize=8, color=MUTED, ha="center")
    ax.legend(loc="upper center", fontsize=8, ncol=1, bbox_to_anchor=(0.5, -0.17))
    save(fig, "hr_filter_response")

    # steps
    g = np.logspace(np.log10(0.05), np.log10(40), 800)
    steps = np.abs(highpass(0.005, g) * lowpass(0.30, g))
    fig, ax = plt.subplots(figsize=(9, 3.6))
    shade(ax, 1.0, 3.0, GREEN, 0.14)
    ax.plot(g, steps, color=BLUE, lw=2.4, label="Steps::update: gravity remover (0.005) then smoother (0.30)")
    ax.axhline(0.707, color=GRID, lw=1)
    ax.set_xscale("log")
    ax.set_xlim(0.05, 40)
    ax.set_ylim(0, 1.3)
    ax.set_xlabel("frequency (Hz)")
    ax.set_ylabel("fraction that passes")
    ax.set_title("What the step filters keep: footsteps (1 to 3 per second) pass, gravity and fast jitter do not")
    ax.text(1.03, 1.27, "steps: 60 to 180 per minute", fontsize=8, color=GREEN, va="top")
    k = int(np.argmax(steps))
    ax.plot(g[k], steps[k], "o", color=BLUE)
    ax.text(g[k] * 0.9, steps[k] + 0.06, f"peak {steps[k]:.2f} at {g[k]:.1f} Hz", fontsize=8, color=BLUE, ha="right")
    for fx in (1.0, 2.0, 3.0):
        j = int(np.argmin(np.abs(g - fx)))
        ax.plot(g[j], steps[j], "o", ms=4, color=GREEN)
        ax.text(g[j], steps[j] - 0.13, f"{steps[j]:.2f}", fontsize=8, color=GREEN, ha="center")
    ax.legend(loc="lower left", fontsize=8)
    save(fig, "steps_filter_response")


# ---------------------------------------------------------------- heart rate

def fig_hr_stages():
    R = Run(TRACE_WRIST)
    a, b = 1000, 1800
    sl = slice(a, b)
    t = (R.i[sl] - a) / 100
    fig, axs = plt.subplots(6, 1, figsize=(9, 11.5), sharex=True)

    ax = axs[0]
    ax.plot(t, R.ir[sl], color=BLUE, lw=1.2)
    ax.set_ylim(0, R.ir[sl].max() * 1.15)
    ax.set_title("1. Raw IR reading, full scale: the pulse is far too small to see")
    ax.set_ylabel("counts")

    ax = axs[1]
    ax.plot(t, R.ir[sl], color=BLUE, lw=0.9, label="ir: raw reading")
    ax.plot(t, R.dc[sl], color=ORANGE, lw=1.8, label="dc: slow baseline (follows the average)")
    ax.set_title("2. Same data with the axis zoomed in: a ripple on a slowly wandering level")
    ax.set_ylabel("counts")
    ax.legend(loc="upper right", fontsize=8, ncol=2)

    ax = axs[2]
    ax.plot(t, R.x0[sl], color=BLUE, lw=1)
    ax.axhline(0, color=MUTED, lw=0.8)
    ax.set_title("3. x = ir - dc: baseline removed, but slow drift is still in it")
    ax.set_ylabel("counts")

    ax = axs[3]
    ax.plot(t, R.x1[sl], color=BLUE, lw=1)
    ax.axhline(0, color=MUTED, lw=0.8)
    ax.set_title("4. After the first drift remover (hp1)")
    ax.set_ylabel("counts")

    ax = axs[4]
    ax.plot(t, R.x2[sl], color=BLUE, lw=1)
    ax.axhline(0, color=MUTED, lw=0.8)
    ax.set_title("5. After the second drift remover (hp2): nearly only the heartbeat is left")
    ax.set_ylabel("counts")

    ax = axs[5]
    ax.plot(t, R.ac[sl], color=GREEN, lw=1.5, label="ac: smoothed waveform (this is what gets stored)")
    amp = R.amplitude[sl]
    ax.plot(t, amp, color=GOLD, lw=1.2, ls="--", label="amplitude: RMS size of ac")
    ax.plot(t, -amp, color=GOLD, lw=1.2, ls="--")
    ax.axhline(30, color=RED, lw=1, ls=":")
    ax.axhline(-30, color=RED, lw=1, ls=":")
    ax.text(0.05, 112, "dotted red = MIN_AMPLITUDE (30): an amplitude below this is treated as noise", fontsize=8, color=RED, va="bottom")
    ax.set_ylim(-290, 160)
    ax.set_title("6. After smoothing: ac, and its amplitude")
    ax.set_ylabel("counts")
    ax.set_xlabel("seconds (8-second excerpt of a recorded wrist trace)")
    ax.legend(loc="lower center", fontsize=8, ncol=2)
    fig.tight_layout()
    save(fig, "hr_stages")


def fig_hr_overlay():
    R = Run(TRACE_WRIST)
    k = int(np.argmin(np.abs(R.scan_i - 1500)))
    i = int(R.scan_i[k])
    chosen, peaks, best, best_lag = pick(R.scores[k])
    cases = [("one beat period", chosen), ("half a period", int(round(chosen / 2))), ("two beat periods", 2 * chosen)]
    for name, lag in cases:                  # check the Python maths against the firmware's numbers
        if 39 <= lag <= 151:
            assert abs(pearson(R.ac, i, lag) - R.scores[k][lag - 39]) < 2e-3, (name, lag)

    fig, axs = plt.subplots(3, 1, figsize=(9, 8.2), sharex=True)
    t = (np.arange(i - 199, i + 1) - i) / 100
    x = R.ac[i - 199:i + 1]
    for ax, (name, lag) in zip(axs, cases):
        y = R.ac[i - lag - 199:i - lag + 1]
        r = pearson(R.ac, i, lag)
        ax.plot(t, x, color=BLUE, lw=2.0, label="newest 2 seconds of ac")
        ax.plot(t, y, color=ORANGE, lw=1.8, ls="--", label=f"same waveform from {lag / 100:.2f} s earlier, slid forward")
        ax.axhline(0, color=MUTED, lw=0.8)
        verdict = "lines up" if r >= 0.65 else "does not line up"
        ax.set_title(f"Shift by {name} ({lag} samples = {lag / 100:.2f} s):  correlation {r:.2f}, {verdict}")
        ax.set_ylabel("counts")
        ax.legend(loc="upper left", fontsize=8, ncol=2)
    axs[-1].set_xlabel("seconds before the scan")
    fig.tight_layout()
    save(fig, "hr_overlay")
    return chosen


def style_score_axis(ax):
    ax.set_xlim(38, 152)
    ax.set_ylim(-0.6, 1.05)
    ax.axhline(0.65, color=RED, lw=1.2, ls="--")
    ax.text(151, 0.67, "MIN_CORRELATION 0.65", fontsize=8, color=RED, ha="right", va="bottom")
    shade(ax, 38, 39.5, MUTED, 0.2)
    shade(ax, 150.5, 152, MUTED, 0.2)
    sec = ax.secondary_xaxis("top", functions=(lambda lag: 6000.0 / np.maximum(lag, 1), lambda b: 6000.0 / np.maximum(b, 1)))
    sec.set_xticks([150, 120, 100, 90, 80, 70, 60, 50, 40])
    sec.set_xlabel("equivalent heart rate (bpm)", fontsize=8)
    sec.tick_params(labelsize=8)
    ax.set_xlabel("shift tested (samples; 100 samples = 1 second)")
    ax.set_ylabel("correlation score")


def plot_scores(ax, R, k, title):
    row = R.scores[k]
    chosen, peaks, best, best_lag = pick(row)
    ax.plot(R.lags, row, color=BLUE, lw=1.6)
    style_score_axis(ax)
    ax.set_title(title)
    if peaks:
        ax.plot(peaks, [row[p - 39] for p in peaks], "v", color=GOLD, ms=6, label="local peaks (the only candidates)")
    if chosen and row[chosen - 39] >= 0.65:
        ax.axvline(chosen, color=GREEN, lw=1.6)
        ax.plot(chosen, row[chosen - 39], "o", color=GREEN, ms=8, label=f"chosen: lag {chosen} = {6000 / chosen:.0f} bpm")
    elif chosen:
        ax.plot(chosen, row[chosen - 39], "o", mfc="none", mec=RED, ms=8, mew=1.6,
                label=f"best candidate: lag {chosen}, but score {row[chosen - 39]:.2f} is below 0.65: rejected")
    ax.legend(loc="lower left", fontsize=8)
    return chosen, peaks, best, best_lag


def fig_hr_scores():
    Rw = Run(TRACE_WRIST)
    k = int(np.argmin(np.abs(Rw.scan_i - 1500)))
    Rf = Run(TRACE_FINGER)
    kf = None
    for j in range(len(Rf.scan_i)):                      # a scan where the loudest peak is the two-beat one
        c, pk, bst, bl = pick(Rf.scores[j])
        if c and bl >= 1.6 * c and bst >= 0.65:
            kf = j
            break
    Rn = Run(TRACE_NOPULSE)
    kn = min((j for j in range(len(Rn.scan_i)) if pick(Rn.scores[j])[0] and 4 <= Rn.scan_i[j] // 100 <= 8),
             key=lambda j: abs(Rn.scores[j][pick(Rn.scores[j])[0] - 39] - 0.2))
    Rd = Run(TRACE_NOPULSE, "no_extra_hp")               # drift that the extra stages normally remove
    kd = None
    for j in range(len(Rd.scan_i)):
        if pick(Rd.scores[j])[0] == 0 and max(Rd.scores[j][0], Rd.scores[j][1]) > 0.7:
            kd = j
            break

    panels = 2 + (kf is not None) + (kd is not None)
    fig, axs = plt.subplots(panels, 1, figsize=(9, 3.9 * panels))
    plot_scores(axs[0], Rw, k, "A. A clean pulse: the score peaks at one beat period and again at two")
    row = 1
    if kf is not None:
        c, pk, bst, bl = plot_scores(axs[row], Rf, kf, "B. The two-beat trap: the tallest peak is at twice the real period, the code takes the shorter one")
        axs[row].plot(bl, Rf.scores[kf][bl - 39], "s", mfc="none", mec=RED, ms=10, label=f"tallest peak: lag {bl} = {6000 / bl:.0f} bpm (wrong)")
        axs[row].legend(loc="lower left", fontsize=8)
        row += 1
    plot_scores(axs[row], Rn, kn, "C. No pulse (hand-held wrist): there is a bump, but its score is far below 0.65")
    row += 1
    if kd is not None:
        plot_scores(axs[row], Rd, kd, "D. Drift, with the extra high-pass stages switched off: high score at the shortest shifts, but no peak, so rejected")
        axs[row].text(95, 0.35, "the score only falls as the shift grows:\nno local maximum in 40 to 150, no candidate", fontsize=9, color=RED, ha="center")
    fig.tight_layout()
    save(fig, "hr_scores")
    return kf is not None


def fig_hr_timeline():
    R = Run(TRACE_STRAPPED)
    fig, axs = plt.subplots(4, 1, figsize=(9, 9), sharex=True, gridspec_kw={"height_ratios": [1.1, 1.1, 1.5, 0.45]})
    ax = axs[0]
    ax.plot(R.t, R.amplitude, color=BLUE, lw=1.1)
    ax.axhline(30, color=RED, ls="--", lw=1)
    ax.text(59.5, 31, "MIN_AMPLITUDE 30", color=RED, fontsize=8, ha="right", va="bottom")
    ax.set_ylabel("amplitude\n(counts)")
    ax.set_title("1. How big is the pulse? Above the red line the scan is allowed to run")

    chosen_list, score_list = [], []
    for j in range(len(R.scan_i)):
        c, pk, bst, bl = pick(R.scores[j])
        chosen_list.append(c)
        score_list.append(R.scores[j][c - 39] if c else np.nan)
    ts = R.scan_i / 100
    chosen_arr = np.array(chosen_list)
    score_arr = np.array(score_list)
    usable = (chosen_arr > 0) & (score_arr >= 0.65)

    ax = axs[1]
    ax.axhline(0.65, color=RED, ls="--", lw=1)
    ax.plot(ts[usable], score_arr[usable], "o", color=GREEN, label="scan passes (score 0.65 or more)")
    ax.plot(ts[~usable], score_arr[~usable], "o", mfc="none", color=MUTED, label="scan rejected")
    ax.set_ylim(-0.1, 1.05)
    ax.set_ylabel("score at the\nchosen lag")
    ax.set_title("2. Once a second: how well does the waveform repeat?")
    ax.legend(loc="lower right", fontsize=8, ncol=2)

    ax = axs[2]
    ax.plot(ts[usable], 6000.0 / chosen_arr[usable], "o", color=GREEN, label="period found by a passing scan")
    ax.plot(ts[~usable & (chosen_arr > 0)], 6000.0 / chosen_arr[~usable & (chosen_arr > 0)], "o", mfc="none", color=MUTED, label="period found by a rejected scan")
    ax.plot(R.t, R.valid_bpm(), color=ORANGE, lw=3, label="heart rate the watch actually reports (heart.bpm while heart.valid)")
    ax.set_ylim(30, 150)
    ax.set_ylabel("bpm")
    ax.text(1, 33, "passing scans near 42 bpm are drift at twice the beat period; the 25% jump guard keeps them from being reported", fontsize=8, color=MUTED)
    ax.set_title("3. From period to reported rate: several passing scans must agree first")
    ax.legend(loc="upper right", fontsize=8)

    ax = axs[3]
    ax.fill_between(R.t, 0, R.valid, step="mid", color=ORANGE, alpha=0.6)
    ax.set_yticks([0, 1])
    ax.set_yticklabels(["no", "yes"])
    ax.set_ylabel("valid?")
    ax.set_xlabel("seconds (recorded wrist trace, sensor strapped to the back of the wrist)")
    fig.tight_layout()
    save(fig, "hr_timeline")
    return R.valid_fraction()


def fig_hr_ablation():
    cols = [
        ("Wrist, strapped to the back (60 s)", TRACE_STRAPPED, [("full", "Full code"),
                                                                 ("no_guard", "Without the 25% jump guard"),
                                                                 ("no_extra_hp", "Without the two extra high-pass stages")]),
        ("Fingertip (60 s)", TRACE_FINGER, [("full", "Full code"),
                                            ("no_tie", "Without 'prefer the shortest peak' (TIE_MARGIN = 0)"),
                                            ("no_extra_hp", "Without the two extra high-pass stages")]),
    ]
    fig, axs = plt.subplots(3, 2, figsize=(11, 8.6), sharex="col", sharey=True)
    for c, (colname, trace, rows) in enumerate(cols):
        for r, (tag, title) in enumerate(rows):
            R = Run(trace, tag)
            ax = axs[r, c]
            b = R.valid_bpm()
            ax.plot(R.t, b, color=BLUE if tag == "full" else RED, lw=2.4)
            ax.axhspan(60, 100, color=GREEN, alpha=0.07, lw=0)
            vf = R.valid_fraction()
            valid_b = b[~np.isnan(b)]
            if len(valid_b):
                low = int(np.sum(valid_b < 60))
                note = f"valid {vf * 100:.0f}%,  range {valid_b.min():.0f} to {valid_b.max():.0f} bpm"
                if low:
                    note += f"\n{low / 100:.0f} s of readings below 60 bpm"
            else:
                note = "valid 0%: no reading at all"
            ax.set_title(title, fontsize=9)
            panel_note(ax, note, "upper right", fs=8, color=INK)
            ax.set_ylim(30, 120)
            if r == 2:
                ax.set_xlabel("seconds")
            if c == 0:
                ax.set_ylabel("reported bpm")
        axs[0, c].text(0.0, 1.32, colname, transform=axs[0, c].transAxes, fontsize=10.5, fontweight="bold", color=INK)
    fig.tight_layout()
    save(fig, "hr_ablation")


# ---------------------------------------------------------------- steps

def run_steps(name, t_s, ax_, ay_, az_):
    path = WORK / f"steps_{name}.csv"
    with open(path, "w") as f:
        f.write("t_ms,ax,ay,az\n")
        for t, a, b, c in zip(t_s, ax_, ay_, az_):
            f.write(f"{int(round(t * 1000))},{a:.5f},{b:.5f},{c:.5f}\n")
    exe = WORK / "dump_steps"
    if not exe.exists():
        compile_cpp(exe, HERE / "dump_steps.cpp", SRC / "Steps.cpp")
    out = subprocess.run([exe, path], check=True, capture_output=True, text=True).stdout
    rows = np.array([[float(v) for v in line.split(",")] for line in out.strip().splitlines()[1:]])
    keys = ["t_ms", "mag", "baseline", "filtered", "threshold", "peak", "count", "walking", "cadence", "cand", "lastpeak"]
    d = {k: rows[:, j] for j, k in enumerate(keys)}
    d["t"] = d["t_ms"] / 1000.0
    return d


def synth(duration, events, noise=0.01, seed=1):
    """100 Hz synthetic wrist acceleration: gravity on z, sensor noise, plus bumps.

    events: list of (time_s, amplitude_g). Each bump is a 45 ms wide pulse.
    """
    rng = np.random.default_rng(seed)
    t = np.arange(0, duration, 0.01)
    az = 1.0 + rng.normal(0, noise, t.size)
    for te, amp in events:
        az += amp * np.exp(-0.5 * ((t - te) / 0.045) ** 2)
    ax_ = rng.normal(0, noise, t.size)
    ay_ = rng.normal(0, noise, t.size)
    return t, ax_, ay_, az


def walk_events(t0, t1, per_min, amp, seed=2, jitter=0.012, swing=0.4):
    """Heel strikes with a smaller secondary swing bump 230 ms later."""
    rng = np.random.default_rng(seed)
    ev = []
    t = t0
    step = 60.0 / per_min
    while t < t1:
        ev.append((t, amp))
        if swing:
            ev.append((t + 0.23, amp * swing))
        t += step + rng.normal(0, jitter)
    return ev


def peak_classes(d):
    """Split peaks into used-as-timing-reference and ignored (too close to the last one)."""
    pk = np.where(d["peak"] == 1)[0]
    used, ignored = [], []
    for j in pk:
        before = d["lastpeak"][j - 1] if j > 0 else 0
        if d["lastpeak"][j] != before:
            used.append(j)
        else:
            ignored.append(j)
    return np.array(used, int), np.array(ignored, int)


def plot_filtered(ax, d, show_legend=False, ylim=None, legend_loc="upper right", legend_cols=2):
    ax.plot(d["t"], d["filtered"], color=BLUE, lw=1.0, label="filtered signal")
    ax.plot(d["t"], d["threshold"], color=RED, lw=1.4, ls="--", label="threshold")
    used, ignored = peak_classes(d)
    # the peak is centred one sample before the sample where it is detected
    if used.size:
        ax.plot(d["t"][used] - 0.01, d["filtered"][used - 1], "v", color=GREEN, ms=6, label="peak used as a step candidate")
    if ignored.size:
        ax.plot(d["t"][ignored] - 0.01, d["filtered"][ignored - 1], "v", mfc="none", mec=MUTED, ms=6, label="peak ignored (too soon after the last)")
    ax.axhline(0, color=MUTED, lw=0.7)
    if ylim:
        ax.set_ylim(*ylim)
    if show_legend:
        ax.legend(loc=legend_loc, fontsize=8, ncol=legend_cols)


def fig_steps_walk():
    ev = walk_events(3.0, 16.0, 110, 0.30)
    t, a, b, c = synth(22, ev)
    d = run_steps("walk", t, a, b, c)
    fig, axs = plt.subplots(4, 1, figsize=(9, 10), sharex=True, gridspec_kw={"height_ratios": [1, 1.5, 1.1, 0.8]})
    ax = axs[0]
    ax.plot(d["t"], d["mag"], color=MUTED, lw=0.9)
    ax.set_ylabel("g")
    ax.set_title("1. Input (synthetic): acceleration magnitude. Still, then walking at 110 steps/min from 3 s to 16 s, then still")

    plot_filtered(axs[1], d, show_legend=True)
    axs[1].set_ylabel("g")
    axs[1].set_title("2. After the filters: each footfall is a peak above the dashed threshold; the smaller swing bump is ignored")

    ax = axs[2]
    ax.step(d["t"], d["count"], where="post", color=ORANGE, lw=2.2)
    ax.set_ylabel("steps counted")
    ax.set_title("3. The count: nothing until the third consistent peak, then +3 at once, then +1 per peak")
    first = np.where(d["count"] > 0)[0][0]
    ax.annotate("+3 at once\n(three peaks credited together)", (d["t"][first], d["count"][first]), xytext=(d["t"][first] + 2.2, 1.2),
                arrowprops=dict(arrowstyle="->", color=INK), fontsize=8)

    ax = axs[3]
    ax.fill_between(d["t"], 0, d["walking"], step="post", color=GREEN, alpha=0.5, label="walking = true")
    ax2 = ax.twinx()
    ax2.plot(d["t"], d["cadence"], color=PURPLE, lw=1.4)
    ax2.set_ylabel("cadence\n(steps/min)", color=PURPLE, fontsize=8)
    ax2.grid(False)
    ax2.spines["right"].set_visible(True)
    ax.set_yticks([0, 1])
    ax.set_yticklabels(["no", "yes"])
    ax.set_ylabel("walking?")
    ax.set_xlabel("seconds")
    ax.set_title("4. walking flag (green) and cadence (purple); the flag drops 1.5 s after the last step")
    fig.tight_layout()
    save(fig, "steps_walk")
    return d


def fig_steps_rejections():
    rng = np.random.default_rng(5)
    scen = []
    scen.append(("Standing still: only sensor noise", synth(20, [], seed=3)))
    scen.append(("One bump of 0.5 g at 5 s", synth(20, [(5.0, 0.5)], seed=3)))
    irregular, tt = [], 2.0
    for k in range(18):
        irregular.append((tt, 0.30))
        tt += 0.40 if k % 2 == 0 else 1.00
    scen.append(("Irregular taps: gaps alternate 0.40 s and 1.00 s", synth(20, irregular, seed=3)))
    scen.append(("Fast shaking at 6 per second", synth(20, [(2 + k / 6.0, 0.30) for k in range(90)], seed=3)))
    scen.append(("Slow walking at 45 steps/min (1.33 s apart)", synth(24, walk_events(2.0, 22.0, 45, 0.30, swing=0.0), seed=3)))
    fig, axs = plt.subplots(len(scen), 1, figsize=(9, 2.7 * len(scen)), sharex=False)
    results = []
    for ax, (title, (t, a, b, c)) in zip(axs, scen):
        d = run_steps("rej", t, a, b, c)
        plot_filtered(ax, d, ylim=(-0.25, 0.45))
        ax2 = ax.twinx()
        ax2.step(d["t"], d["count"], where="post", color=ORANGE, lw=2)
        ax2.set_ylim(0, max(6, d["count"].max() * 1.2))
        ax2.set_ylabel("steps", color=ORANGE, fontsize=8)
        ax2.grid(False)
        ax2.spines["right"].set_visible(True)
        ax.set_title(f"{title}  ->  counted {int(d['count'][-1])} steps")
        ax.set_ylabel("g")
        results.append((title, int(d["count"][-1])))
    axs[-1].set_xlabel("seconds (synthetic input)")
    handles = [plt.Line2D([], [], color=BLUE, lw=1), plt.Line2D([], [], color=RED, ls="--", lw=1.4),
               plt.Line2D([], [], marker="v", color=GREEN, ls="", ms=6), plt.Line2D([], [], marker="v", mfc="none", mec=MUTED, ls="", ms=6),
               plt.Line2D([], [], color=ORANGE, lw=2)]
    fig.legend(handles, ["filtered signal", "threshold", "peak used as candidate", "peak ignored", "step count (right axis)"],
               loc="upper center", ncol=5, fontsize=8, bbox_to_anchor=(0.5, 1.0))
    fig.tight_layout(rect=(0, 0, 1, 0.97))
    save(fig, "steps_rejections")
    return results


def fig_steps_threshold():
    ev = []
    for t0, t1, amp in [(2, 22, 0.10), (22, 42, 0.30), (42, 62, 0.60), (62, 90, 0.10)]:
        ev += walk_events(t0, t1, 110, amp, seed=int(t0), swing=0.0)
    t, a, b, c = synth(92, ev, seed=4)
    d = run_steps("thr", t, a, b, c)
    fig, axs = plt.subplots(2, 1, figsize=(9, 6.6), sharex=True, gridspec_kw={"height_ratios": [1.7, 1]})
    ax = axs[0]
    plot_filtered(ax, d, show_legend=True, legend_loc="lower center", legend_cols=4)
    for y, text, dy in [(0.035, "MIN_THRESHOLD 0.035", -9), (0.102, "highest possible threshold 0.102\n(MAX_NOISE 0.06 x GAIN 1.7)", 10)]:
        ax.axhline(y, color=GOLD, lw=1, ls=":")
        ax.annotate(text, xy=(1, y), xycoords=("axes fraction", "data"), xytext=(5, dy),
                    textcoords="offset points", fontsize=8, color=GOLD, va="center", ha="left")
    for x, label_ in [(12, "steps 0.10 g"), (32, "steps 0.30 g"), (52, "steps 0.60 g"), (76, "back to 0.10 g")]:
        ax.text(x, 0.52, label_, ha="center", fontsize=8.5, color=INK, fontweight="bold")
    for x in (22, 42, 62):
        ax.axvline(x, color=GRID, lw=1)
    ax.set_ylim(-0.42, 0.62)
    ax.set_ylabel("g")
    ax.set_title("1. The threshold follows how hard the arm is moving, between 0.035 g and 0.102 g")
    ax = axs[1]
    ax.step(d["t"], d["count"], where="post", color=ORANGE, lw=2)
    ax.set_ylabel("steps counted")
    ax.set_xlabel("seconds (synthetic input, steady 110 steps/min throughout)")
    ax.set_title("2. Steps counted at a steady 110 per minute: the count stalls after the hard steps, until the threshold decays")
    fig.tight_layout()
    save(fig, "steps_threshold")
    return d


# ---------------------------------------------------------------- iOS

def fig_ios_steps():
    dt = 0.5
    t = np.arange(0, 241, dt)
    walking = (t >= 20) & (t < 200)
    steps = np.cumsum(walking * 100 / 60 * dt)           # watch step counter, 100 steps/min
    steps = np.floor(steps)
    stored_idx = [0]
    for j in range(1, t.size):
        if t[j] - t[stored_idx[-1]] >= 60:               # HistoryStore.add keeps one record per 60 s
            stored_idx.append(j)
    before = np.zeros(t.size)
    for j in range(t.size):
        last = max(s for s in stored_idx if s <= j)
        before[j] = steps[last] - steps[0]
    after = steps - steps[0]
    fig, ax = plt.subplots(figsize=(9, 4.4))
    ax.step(t, before, where="post", color=RED, lw=2.2, label="before the change: total from stored records only")
    ax.plot(t, after, color=GREEN, lw=2.2, label="after the change: stored records + latest live packet")
    ax.plot(t[stored_idx], steps[stored_idx] - steps[0], "o", color=INK, ms=5, label="record kept in history (one per minute)")
    ax.set_xlabel("seconds (simulation of the app's step-total rule, walking 20 s to 200 s at 100 steps/min)")
    ax.set_ylabel("'Steps today' shown")
    ax.set_title("Why the step count only moved once a minute, and what the live packet fixes")
    ax.legend(loc="upper left", fontsize=8.5)
    save(fig, "ios_steps_total")


def main():
    print("work dir:", WORK)
    fig_filters()
    fig_hr_stages()
    fig_hr_overlay()
    fig_hr_scores()
    vf = fig_hr_timeline()
    print("strapped valid fraction", round(vf, 3))
    fig_hr_ablation()
    fig_steps_walk()
    for title, count in fig_steps_rejections():
        print(f"  {count:3d}  {title}")
    fig_steps_threshold()
    fig_ios_steps()


if __name__ == "__main__":
    main()
