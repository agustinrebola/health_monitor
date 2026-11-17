#!/usr/bin/env python3
"""Static diagrams for docs/CODE_GUIDE.md (architecture, timing, byte layouts, flowcharts).

All numbers in the byte diagrams are computed here, not typed by hand.
usage: python3 make_diagrams.py   (needs matplotlib)
"""
import numpy as np

from figlib import *  # noqa: F401,F403


# ------------------------------------------------------------ architecture

def fig_architecture():
    fig, ax = canvas(12.5, 6.4, 150, 76)
    # sensors
    box(ax, 14, 56, 24, 11, "MPU6050\naccelerometer + gyro", PALE_GRAY, MUTED, bold=True)
    box(ax, 14, 18, 24, 11, "MAX30102\nred + infrared pulse sensor", PALE_GRAY, MUTED, bold=True)
    ax.plot([14, 14], [49.5, 24.5], color=MUTED, lw=1.2, ls=":")
    label(ax, 16, 37, "shared I2C bus\nSDA = pin 21\nSCL = pin 22\n400 kHz", fs=8, color=MUTED)
    # drivers
    box(ax, 46, 56, 25, 11, "IMU::read()\nax ay az gx gy gz", PALE_BLUE, BLUE)
    box(ax, 46, 18, 25, 11, "MAX30102::available()\nand read():  red, ir", PALE_BLUE, BLUE)
    # algorithms
    box(ax, 80, 56, 28, 11, "Steps::update()\ncount, walking, cadence", PALE_GREEN, GREEN)
    box(ax, 80, 18, 28, 11, "HeartRate::update()\nbpm, valid, contact", PALE_GREEN, GREEN)
    # outputs
    box(ax, 120, 66, 33, 10, "Serial Monitor (10 lines/s)\ngx gy gz pulse hr steps", PALE_ORANGE, ORANGE)
    box(ax, 120, 37, 33, 12, "Bluetooth::send()\n20-byte packet, 2 per second", PALE_PURPLE, PURPLE, bold=False)
    box(ax, 120, 9, 33, 12, "iPhone app\nBluetoothManager, HistoryStore,\nContentView", PALE_GOLD, GOLD)
    for a, b in [((26, 56), (33.5, 56)), ((59, 56), (66, 56)), ((26, 18), (33.5, 18)), ((59, 18), (66, 18))]:
        arrow(ax, a, b)
    # both algorithms put their results on one bus that feeds the two outputs
    ax.plot([94, 99], [56, 56], color=MUTED, lw=1.3)
    ax.plot([94, 99], [18, 18], color=MUTED, lw=1.3)
    ax.plot([99, 99], [18, 66], color=MUTED, lw=1.3)
    arrow(ax, (99, 66), (103.5, 66))
    arrow(ax, (99, 37), (103.5, 37))
    label(ax, 97.5, 29, "results", fs=8, color=MUTED, ha="right")
    arrow(ax, (120, 31), (120, 15), "Bluetooth Low Energy\n(NimBLE notify)", label_offset=(12, 0), ha="left")
    label(ax, 75, 73, "everything in the middle runs in one thread: HealthWatch.ino loop()", fs=9, color=MUTED, ha="center", style="italic")
    label(ax, 75, 2.5, "the gyro and raw pulse readings also go straight to the Serial Monitor line, not only the results", fs=8.5, color=MUTED, ha="center", style="italic")
    save(fig, "architecture")


# ------------------------------------------------------------ loop timeline

def fig_loop_timeline():
    fig = plt.figure(figsize=(11, 6.6))
    gs = fig.add_gridspec(2, 1, height_ratios=[1.25, 1], hspace=0.55)
    ax = fig.add_subplot(gs[0])
    rows = [("BLE notify", np.arange(0, 1000, 500), PURPLE, 4.0),
            ("Serial print", np.arange(0, 1000, 100), ORANGE, 3.0),
            ("IMU read + Steps.update", np.arange(0, 1000, 10), BLUE, 2.0),
            ("Pulse sample arrives + HeartRate.update", np.arange(3, 1000, 10), GREEN, 1.0)]
    for name, xs, color, y in rows:
        ax.vlines(xs, y - 0.3, y + 0.3, color=color, lw=1.6 if len(xs) < 20 else 0.8)
        ax.text(-12, y, f"{name}\n({len(xs)} per second)", ha="right", va="center", fontsize=8.5, color=INK)
    ax.set_xlim(0, 1000)
    ax.set_ylim(0.4, 4.6)
    ax.set_yticks([])
    ax.set_xlabel("milliseconds")
    ax.set_title("One second of the loop: how often each job happens")
    ax.spines["left"].set_visible(False)

    ax = fig.add_subplot(gs[1])
    ax.set_xlim(0, 40)
    ax.set_ylim(0, 4.4)
    ax.set_yticks([])
    ax.spines["left"].set_visible(False)
    ax.set_xlabel("milliseconds (schematic: the blocks are drawn wider than the real few hundred microseconds)")
    ax.set_title("Zoom on 40 ms: loop() spins constantly and each job fires when its time arrives")
    ax.add_patch(Rectangle((0, 0.25), 40, 0.5, color=PALE_GRAY, lw=0))
    ax.text(40, 0.5, "loop() runs again and again, testing every deadline each pass   ", ha="right", va="center", fontsize=8, color=MUTED)
    for x in (0, 10, 20, 30):
        ax.add_patch(Rectangle((x, 1.2), 1.1, 0.6, color=BLUE))
        ax.text(x + 1.4, 1.5, "IMU\nread", fontsize=7.5, va="center", color=BLUE)
    for x in (3, 13, 23, 33):
        ax.add_patch(Rectangle((x, 2.2), 1.3, 0.6, color=GREEN))
        ax.text(x + 1.6, 2.5, "pulse read\n+ filters", fontsize=7.5, va="center", color=GREEN)
    ax.add_patch(Rectangle((0, 3.2), 1.6, 0.6, color=ORANGE))
    ax.text(1.9, 3.5, "print line (every 100 ms)", fontsize=7.5, va="center", color=ORANGE)
    ax.text(-0.5, 1.5, "IMU slot", ha="right", va="center", fontsize=8.5)
    ax.text(-0.5, 2.5, "FIFO has data", ha="right", va="center", fontsize=8.5)
    ax.text(-0.5, 3.5, "Serial", ha="right", va="center", fontsize=8.5)
    save(fig, "loop_timeline")


# ------------------------------------------------------------ IMU burst

def fig_imu_burst():
    fig, ax = canvas(12, 5.4, 120, 54)
    names = ["AX high", "AX low", "AY high", "AY low", "AZ high", "AZ low", "temp high", "temp low",
             "GX high", "GX low", "GY high", "GY low", "GZ high", "GZ low"]
    fills = [PALE_BLUE] * 6 + [PALE_GRAY] * 2 + [PALE_ORANGE] * 6
    w = 7.2
    x0 = 9
    label(ax, 1, 46, "byte read", fs=8, color=MUTED)
    label(ax, 1, 38.5, "register", fs=8, color=MUTED)
    for k in range(14):
        cx = x0 + k * w + w / 2
        box(ax, cx, 42, w - 0.5, 5.5, names[k], fills[k], MUTED, fs=6.8, rounded=False)
        ax.text(cx, 38, f"0x{0x3B + k:02X}", ha="center", va="center", fontsize=7, color=MUTED)
    pairs = [("raw[0]", 0, "ax = raw[0] / 8192", PALE_BLUE), ("raw[1]", 2, "ay = raw[1] / 8192", PALE_BLUE),
             ("raw[2]", 4, "az = raw[2] / 8192", PALE_BLUE), ("raw[3]", 6, "(temperature, unused)", PALE_GRAY),
             ("raw[4]", 8, "gx = raw[4] / 65.5", PALE_ORANGE), ("raw[5]", 10, "gy = raw[5] / 65.5", PALE_ORANGE),
             ("raw[6]", 12, "gz = raw[6] / 65.5", PALE_ORANGE)]
    for name, k, formula, fc in pairs:
        cx = x0 + (k + 1) * w
        box(ax, cx, 28.5, 2 * w - 1, 5.5, name, fc, MUTED, fs=8, rounded=False)
        arrow(ax, (cx - 1.5, 35.1), (cx - 1.5, 31.4), lw=0.9)
        arrow(ax, (cx + 1.5, 35.1), (cx + 1.5, 31.4), lw=0.9)
        ax.text(cx, 23.5, formula, ha="center", va="center", fontsize=7.4, color=INK)
    label(ax, 1, 28.5, "int16", fs=8, color=MUTED)
    label(ax, 1, 16, "How two bytes become one number:   raw = (high << 8) | low,  read as a signed 16-bit value", fs=9, bold=True)
    hi, lo = 0x20, 0x00
    label(ax, 1, 11.5, f"example: high = 0x{hi:02X}, low = 0x{lo:02X}  ->  0x{(hi << 8) | lo:04X} = {(hi << 8) | lo}  ->  {((hi << 8) | lo) / 8192:.3f} g   (a sensor lying flat reads about 1 g on z)", fs=8.5)
    hi, lo = 0x02, 0x8F
    label(ax, 1, 7.5, f"example: high = 0x{hi:02X}, low = 0x{lo:02X}  ->  0x{(hi << 8) | lo:04X} = {(hi << 8) | lo}  ->  {((hi << 8) | lo) / 65.5:.1f} degrees per second of rotation", fs=8.5)
    label(ax, 1, 3, "scale: +/-4 g spans the int16 range, so 32768 / 4 = 8192 counts per g;  +/-500 degrees per second gives 32768 / 500 = 65.5 counts per degree per second", fs=8, color=MUTED)
    save(fig, "imu_burst")


# ------------------------------------------------------------ FIFO

def fig_fifo():
    fig, ax = canvas(12, 7.4, 130, 74)
    cw = 3.5
    x0 = 12

    def strip(y, title, unread, rd, wr, note, bad=False):
        label(ax, 1, y + 13, title, fs=9.5, bold=True)
        for k in range(32):
            fc = (PALE_RED if bad else PALE_BLUE) if k in unread else "white"
            ax.add_patch(Rectangle((x0 + k * cw, y), cw, 4.2, fc=fc, ec=MUTED, lw=0.8))
            ax.text(x0 + k * cw + cw / 2, y + 2.1, str(k), ha="center", va="center", fontsize=6.5, color=MUTED)
        if rd == wr and bad:
            ax.annotate("rd = wr", xy=(x0 + rd * cw + cw / 2, y), xytext=(x0 + rd * cw + cw / 2, y - 4.5), ha="center", fontsize=8, color=RED,
                        arrowprops=dict(arrowstyle="-|>", color=RED))
        else:
            ax.annotate("rd", xy=(x0 + rd * cw + cw / 2, y), xytext=(x0 + rd * cw + cw / 2, y - 4.5), ha="center", fontsize=8, color=BLUE, fontweight="bold",
                        arrowprops=dict(arrowstyle="-|>", color=BLUE))
            ax.annotate("wr", xy=(x0 + wr * cw + cw / 2, y + 4.2), xytext=(x0 + wr * cw + cw / 2, y + 8.6), ha="center", fontsize=8, color=GREEN, fontweight="bold",
                        arrowprops=dict(arrowstyle="-|>", color=GREEN))
        label(ax, x0, y - 7.6, note, fs=8.5, color=INK if not bad else RED)

    strip(58, "A.  Normal: 5 samples waiting", set(range(2, 7)), 2, 7, "wr = 7, rd = 2   ->   (7 - 2) & 0x1F = 5 samples to read")
    strip(34, "B.  Pointers wrapped past the end:  still 5 samples", {30, 31, 0, 1, 2}, 30, 3,
          f"wr = 3, rd = 30   ->   (3 - 30) = -27,  and -27 & 0x1F = {(3 - 30) & 0x1F}   (the mask undoes the wrap-around)")
    strip(10, "C.  FIFO completely full (loop was stalled 0.3 s): all 32 cells unread", set(range(32)), 12, 12,
          "wr = rd = 12   ->   (12 - 12) & 0x1F = 0   looks EMPTY.  Fix: available() checks the overflow counter first and clears the FIFO", bad=True)
    save(fig, "fifo")


# ------------------------------------------------------------ ADC bytes

def fig_adc_bytes():
    red, ir = 126361, 157000
    b = [(red >> 16) & 0xFF, (red >> 8) & 0xFF, red & 0xFF, (ir >> 16) & 0xFF, (ir >> 8) & 0xFF, ir & 0xFF]
    assert (((b[0] << 16) | (b[1] << 8) | b[2]) & 0x3FFFF) == red and (((b[3] << 16) | (b[4] << 8) | b[5]) & 0x3FFFF) == ir
    fig, ax = canvas(11, 5.0, 110, 50)
    wbox = 12
    xs = [8 + k * (wbox + 1.5) for k in range(6)]
    label(ax, 1, 44, "6 bytes read from register 0x07, one pulse sample:", fs=9.5, bold=True)
    for k in range(6):
        group = PALE_RED if k < 3 else PALE_PURPLE
        box(ax, xs[k] + wbox / 2, 36, wbox, 6.5, f"b[{k}] = 0x{b[k]:02X}", group, MUTED, fs=9, rounded=False)
        ax.text(xs[k] + wbox / 2, 31.2, ("bits 17-16 only" if k % 3 == 0 else ("bits 15-8" if k % 3 == 1 else "bits 7-0")), ha="center", fontsize=7.5, color=MUTED)
    for g, (name, off, val, fc) in enumerate([("red", 0, red, PALE_RED), ("ir", 3, ir, PALE_PURPLE)]):
        cx = (xs[off] + xs[off + 2] + wbox) / 2
        ax.plot([xs[off], xs[off + 2] + wbox], [27, 27], color=MUTED, lw=1)
        box(ax, cx + (-4 if off == 0 else 4), 15.5, 40, 14, f"{name} = ((b[{off}] << 16) | (b[{off + 1}] << 8) | b[{off + 2}]) & 0x3FFFF\n"
            f"= (0x{b[off]:02X} << 16) | (0x{b[off + 1]:02X} << 8) | 0x{b[off + 2]:02X}\n= {val:,}", fc, MUTED, fs=7.4)
        arrow(ax, (cx, 27), (cx, 22.6))
    label(ax, 1, 4.5, "The sensor sends 24 bits but the ADC has 18, so the top 6 bits carry nothing; & 0x3FFFF keeps the 18 that matter (maximum 262,143).", fs=8.5, color=MUTED)
    save(fig, "adc_bytes")


# ------------------------------------------------------------ BLE packet

def fig_ble_packet():
    flags = 1 | 2 | 4 | 8
    seq, uptime, steps, cadence, hr = 1234, 61500, 87, 110, 84
    p = [1, flags, 2, 3]
    for v in (seq, uptime, steps):
        p += [(v >> (8 * k)) & 255 for k in range(4)]
    p += [cadence & 255, cadence >> 8, hr, 0]
    assert len(p) == 20
    fields = [("version", 0, 1, PALE_GRAY), ("flags", 1, 1, PALE_GOLD), ("activity", 2, 1, PALE_BLUE), ("quality", 3, 1, PALE_BLUE),
              ("sequence (u32)", 4, 4, PALE_PURPLE), ("uptime ms (u32)", 8, 4, PALE_GREEN), ("steps (u32)", 12, 4, PALE_ORANGE),
              ("cadence (u16)", 16, 2, PALE_BLUE), ("heart rate", 18, 1, PALE_RED), ("unused", 19, 1, PALE_GRAY)]
    fig, ax = canvas(12, 5.4, 120, 54)
    w = 5.6
    x0 = 3
    label(ax, 1, 51, "The 20-byte Bluetooth packet, byte by byte (little-endian: the low byte of a number comes first)", fs=9.5, bold=True)
    for name, start, n, fc in fields:
        cx = x0 + (start + n / 2) * w
        box(ax, cx, 41, n * w - 0.4, 7, name if n > 1 else "", fc, MUTED, fs=7.8, rounded=False)
        if n == 1:
            ax.text(cx, 41, name, ha="center", va="center", fontsize=6.3, rotation=90, color=INK)
    for k in range(20):
        cx = x0 + (k + 0.5) * w
        ax.text(cx, 46.2, str(k), ha="center", fontsize=7, color=MUTED)
        box(ax, cx, 33, w - 0.4, 5, f"{p[k]:02X}", "white", MUTED, fs=8, rounded=False)
    label(ax, 1, 28.6, "example packet, as sent (hexadecimal)", fs=8, color=MUTED)
    lines = [
        f"byte 1, flags = {flags} = 0b{flags:04b}:   bit0 IMU ok, bit1 pulse sensor ok, bit2 heart rate valid, bit3 contact",
        "byte 2, activity: 1 stationary, 2 walking          byte 3, quality: 0 no contact, 2 pulse seen, 3 rate valid",
        f"bytes 4-7 = {p[4]:02X} {p[5]:02X} {p[6]:02X} {p[7]:02X}  ->  {p[4]} + {p[5]} x 256 = {seq}  (sequence number)",
        f"bytes 8-11 = {p[8]:02X} {p[9]:02X} {p[10]:02X} {p[11]:02X}  ->  {p[8]} + {p[9]} x 256 = {uptime}  (watch has been up {uptime / 1000:.1f} s)",
        f"bytes 12-15 = {p[12]:02X} 00 00 00  ->  {steps} steps          bytes 16-17 = {p[16]:02X} {p[17]:02X}  ->  {cadence} steps per minute",
        f"byte 18 = {p[18]:02X} = {hr} bpm (0 when the rate is not valid)          byte 19 unused (SpO2 is not measured)",
    ]
    for k, text in enumerate(lines):
        label(ax, 1, 23.5 - k * 4.0, text, fs=8.3)
    save(fig, "ble_packet")


# ------------------------------------------------------------ iOS flow

def fig_ios_flow():
    fig, ax = canvas(12, 5.8, 130, 64)
    box(ax, 14, 52, 24, 9, "HealthWatch\n(ESP32)", PALE_GRAY, MUTED, bold=True)
    box(ax, 46, 52, 30, 9, "CoreBluetooth\ndidUpdateValueFor", PALE_BLUE, BLUE)
    box(ax, 82, 52, 28, 9, "WatchSnapshot\n.decode(20 bytes)", PALE_BLUE, BLUE)
    box(ax, 116, 52, 24, 9, "snapshot\n(@Published)", PALE_PURPLE, PURPLE)
    arrow(ax, (26, 52), (31, 52), "2 per second", label_offset=(1, -3.8))
    arrow(ax, (61, 52), (68, 52))
    arrow(ax, (96, 52), (104, 52))
    box(ax, 116, 32, 24, 10, "ContentView\nredraws the screen", PALE_GOLD, GOLD)
    arrow(ax, (116, 47.5), (116, 37.2), "new value", label_offset=(-7, 0))
    box(ax, 82, 32, 28, 10, "HistoryStore.add\nkeeps 1 record per minute", PALE_GREEN, GREEN)
    arrow(ax, (84, 47.5), (84, 37.2), "each packet", label_offset=(8, 0))
    box(ax, 46, 32, 30, 10, "records saved to disk\nat most every 5 s", PALE_GRAY, MUTED)
    arrow(ax, (68, 32), (61, 32))
    box(ax, 98, 10, 44, 11, "dailySteps(live: snapshot)\nstored records + the latest live packet", PALE_ORANGE, ORANGE)
    arrow(ax, (84, 27), (90, 15.7), "stored records", label_offset=(-9, 1))
    arrow(ax, (116, 27), (108, 15.7), "live snapshot", label_offset=(9, 1))
    box(ax, 46, 10, 30, 8, "\"Steps today\" on screen", PALE_GOLD, GOLD, bold=True)
    arrow(ax, (76, 10), (61, 10), "the total", label_offset=(0, 2))
    save(fig, "ios_flow")


# ------------------------------------------------------------ flowcharts

def fig_hr_flow():
    fig, ax = canvas(11, 15.2, 110, 152)
    X, SX = 36, 86
    bw, bh, dw, dh = 54, 10, 56, 14

    def start(y, text):
        box(ax, X, y, bw, 7, text, PALE_GRAY, MUTED, bold=True)

    def step(y, text, h=bh, fc=PALE_BLUE, ec=BLUE):
        box(ax, X, y, bw, h, text, fc, ec)

    def decide(y, text):
        diamond(ax, X, y, dw, dh, text)

    def side(y, text, fc=PALE_RED, ec=RED):
        box(ax, SX, y, 36, 9, text, fc, ec, fs=8)
        arrow(ax, (X + dw / 2, y), (SX - 18, y), "yes" if False else None)

    y = 146
    start(y, "HeartRate::update(ir): one new IR reading, 100 per second")
    arrow(ax, (X, y - 3.5), (X, 132 + dh / 2))
    decide(132, "ir == 0  or  ir at the top of\nthe range (clipped)?")
    side(132, "reset(), return", PALE_RED, RED)
    label(ax, X + 29, 134, "yes", fs=8, color=MUTED)
    arrow(ax, (X, 132 - dh / 2), (X, 114 + dh / 2), "no", label_offset=(2.5, 0), ha="left")
    decide(114, "2 s of history and\n|ir - dc| > 35% of dc?\n(sensor moved)")
    side(114, "reset(), return", PALE_RED, RED)
    label(ax, X + 29, 116, "yes", fs=8, color=MUTED)
    arrow(ax, (X, 114 - dh / 2), (X, 98 + 6), "no", label_offset=(2.5, 0), ha="left")
    step(98, "Filters:  dc  ->  x = ir - dc  ->  hp1  ->  hp2  ->  ac\nstore ac in the 4-second ring buffer\nupdate energy, amplitude, contact", h=12)
    arrow(ax, (X, 98 - 6), (X, 82 + dh / 2))
    decide(82, "contact  and\namplitude >= 30?")
    side(82, "valid = false\nstableScans = 0\nreturn", PALE_ORANGE, ORANGE)
    label(ax, X + 29, 84, "no", fs=8, color=MUTED)
    arrow(ax, (X, 82 - dh / 2), (X, 66 + dh / 2), "yes", label_offset=(2.5, 0), ha="left")
    decide(66, "100 samples since the last scan\nand 350 samples of history?")
    side(66, "return\n(wait for next sample)", PALE_GRAY, MUTED)
    label(ax, X + 29, 68, "no", fs=8, color=MUTED)
    arrow(ax, (X, 66 - dh / 2), (X, 50 + 7), "yes", label_offset=(2.5, 0), ha="left")
    step(50, "SCAN: correlation score for every shift 39 ... 151\nkeep local peaks; pick the shortest peak\nwithin 0.03 of the tallest", h=14)
    arrow(ax, (X, 50 - 7), (X, 36 + dh / 2))
    decide(36, "usable?  a peak exists\nand its score >= 0.65")
    side(36, "stableScans = 0\n-> hold step below", PALE_ORANGE, ORANGE)
    label(ax, X + 29, 38, "no", fs=8, color=MUTED)
    arrow(ax, (X, 36 - dh / 2), (X, 22 + 6), "yes", label_offset=(2.5, 0), ha="left")
    step(22, "same lag as last scan (within 8)?  stableScans++  else  = 1\nneeded = 2;  3 if lag >= 100;  4 if the rate jumped\n> 25% from one accepted in the last 15 scans", h=12, fc=PALE_GOLD, ec=GOLD)
    arrow(ax, (X, 22 - 6), (X, 8 + dh / 2))
    decide(8, "stableScans >= needed?")
    box(ax, SX, 8, 36, 12, "PUBLISH\nbpm = 6000 / lag (smoothed)\nvalid = true, return", PALE_GREEN, GREEN, fs=8)
    arrow(ax, (X + dw / 2, 8), (SX - 18, 8))
    label(ax, X + 29, 10, "yes", fs=8, color=MUTED)
    arrow(ax, (X, 8 - dh / 2), (X, -5), "no", label_offset=(2.5, 0), ha="left")
    box(ax, X, -8, bw, 7, "hold: ++scansSinceValid > 3  ->  valid = false", PALE_ORANGE, ORANGE)
    ax.set_ylim(-14, 152)
    save(fig, "hr_flow")


def fig_steps_flow():
    fig, ax = canvas(11, 15.0, 110, 150)
    X, SX = 36, 86
    bw, dw, dh = 58, 58, 12

    def step(y, text, h=10, fc=PALE_BLUE, ec=BLUE):
        box(ax, X, y, bw, h, text, fc, ec)

    def decide(y, text):
        diamond(ax, X, y, dw, dh, text)

    def side(y, text, fc=PALE_GRAY, ec=MUTED, h=8):
        box(ax, SX, y, 36, h, text, fc, ec, fs=8)
        arrow(ax, (X + dw / 2, y), (SX - 18, y))

    box(ax, X, 144, bw, 6.5, "Steps::update(ax, ay, az, nowMs): one sample, 100 per second", PALE_GRAY, MUTED, bold=True)
    arrow(ax, (X, 140.7), (X, 134.7))
    step(128, "mag = sqrt(ax^2 + ay^2 + az^2)\nbaseline += 0.005 x (mag - baseline)        filtered += 0.30 x ((mag - baseline) - filtered)\nnoise tracks |filtered|;  threshold = max(0.035, 1.7 x noise)", h=13)
    arrow(ax, (X, 121.5), (X, 115.5))
    step(111, "peak = previous sample was a local maximum above the threshold\nthen shift the saved previous samples", h=8, fc=PALE_GREEN, ec=GREEN)
    arrow(ax, (X, 107), (X, 99 + dh / 2))
    decide(99, "walking and no accepted\nstep for 1.5 s?")
    side(99, "walking = false\nreset the sequence\n(then carry on)", PALE_ORANGE, ORANGE, h=10)
    label(ax, X + 29, 101, "yes", fs=8, color=MUTED)
    arrow(ax, (X, 99 - dh / 2), (X, 85 + dh / 2), "no", label_offset=(2.5, 0), ha="left")
    decide(85, "peak?")
    side(85, "return", PALE_GRAY, MUTED)
    label(ax, X + 29, 87, "no", fs=8, color=MUTED)
    arrow(ax, (X, 85 - dh / 2), (X, 71 + dh / 2), "yes", label_offset=(2.5, 0), ha="left")
    decide(71, "first peak?\n(lastPeakMs == 0)")
    side(71, "remember its time,\ncandidateCount = 1,\nreturn", PALE_GRAY, MUTED, h=10)
    label(ax, X + 29, 73, "yes", fs=8, color=MUTED)
    arrow(ax, (X, 71 - dh / 2), (X, 57 + dh / 2), "no", label_offset=(2.5, 0), ha="left")
    decide(57, "interval since the last\naccepted peak < 320 ms?")
    side(57, "ignore: too soon,\nsame step. return", PALE_GRAY, MUTED)
    label(ax, X + 29, 59, "yes", fs=8, color=MUTED)
    arrow(ax, (X, 57 - dh / 2), (X, 43 + dh / 2), "no", label_offset=(2.5, 0), ha="left")
    decide(43, "interval > 1100 ms?")
    side(43, "start a new sequence\nat this peak. return", PALE_ORANGE, ORANGE)
    label(ax, X + 29, 45, "yes", fs=8, color=MUTED)
    arrow(ax, (X, 43 - dh / 2), (X, 29 + dh / 2), "no", label_offset=(2.5, 0), ha="left")
    decide(29, "interval within 35% of\nthe previous interval?")
    side(29, "restart: candidateCount = 1,\nwalking = false. return", PALE_ORANGE, ORANGE)
    label(ax, X + 29, 31, "no", fs=8, color=MUTED)
    arrow(ax, (X, 29 - dh / 2), (X, 17 + 5), "yes", label_offset=(2.5, 0), ha="left")
    step(17, "accept: cadence = 60000 / interval\nremember interval and time (lastMotionMs)", h=9, fc=PALE_GREEN, ec=GREEN)
    arrow(ax, (X, 12.5), (X, 5 + dh / 2))
    decide(5, "already walking?")
    side(5, "++count   (one step)", PALE_GREEN, GREEN)
    label(ax, X + 29, 7, "yes", fs=8, color=MUTED)
    arrow(ax, (X, 5 - dh / 2), (X, -8 + 6), "no", label_offset=(2.5, 0), ha="left")
    box(ax, X, -8, bw, 11, "++candidateCount;  if it reaches 3:\nwalking = true and count += 3 (all three at once)\notherwise return and wait for the next peak", PALE_GREEN, GREEN)
    ax.set_ylim(-15, 150)
    save(fig, "steps_flow")


def main():
    fig_architecture()
    fig_loop_timeline()
    fig_imu_burst()
    fig_fifo()
    fig_adc_bytes()
    fig_ble_packet()
    fig_ios_flow()
    fig_hr_flow()
    fig_steps_flow()


if __name__ == "__main__":
    main()
