"""Shared plotting helpers for the CODE_GUIDE figures.

Needs matplotlib (pip install matplotlib). Figures are written as SVG into
docs/figures; set FIG_PNG_DIR to also write PNG copies for quick viewing.
"""
import os
import pathlib

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Polygon, Rectangle

HERE = pathlib.Path(__file__).resolve().parent
FIG_DIR = HERE.parent / "figures"
PNG_DIR = os.environ.get("FIG_PNG_DIR")

# One palette, all readable on white. Meaning is also carried by labels and
# line style, never by color alone.
INK = "#1f2328"
MUTED = "#59636e"
GRID = "#d8dee4"
BLUE = "#0969da"
ORANGE = "#bc4c00"
GREEN = "#1a7f37"
RED = "#cf222e"
PURPLE = "#8250df"
GOLD = "#9a6700"
PALE_BLUE = "#ddf4ff"
PALE_GREEN = "#dafbe1"
PALE_ORANGE = "#fff1e5"
PALE_RED = "#ffebe9"
PALE_GRAY = "#f6f8fa"
PALE_PURPLE = "#fbefff"
PALE_GOLD = "#fff8c5"

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 9,
    "axes.edgecolor": MUTED,
    "axes.labelcolor": INK,
    "axes.titlesize": 10,
    "axes.titleweight": "bold",
    "axes.titlelocation": "left",
    "axes.grid": True,
    "grid.color": GRID,
    "grid.linewidth": 0.6,
    "axes.spines.top": False,
    "axes.spines.right": False,
    "xtick.color": MUTED,
    "ytick.color": MUTED,
    "figure.facecolor": "white",
    "savefig.facecolor": "white",
    "svg.fonttype": "path",
    "legend.frameon": False,
})


def save(fig, name):
    """Write docs/figures/<name>.svg (and a PNG copy when FIG_PNG_DIR is set)."""
    FIG_DIR.mkdir(parents=True, exist_ok=True)
    fig.savefig(FIG_DIR / f"{name}.svg", bbox_inches="tight")
    if PNG_DIR:
        pathlib.Path(PNG_DIR).mkdir(parents=True, exist_ok=True)
        fig.savefig(pathlib.Path(PNG_DIR) / f"{name}.png", dpi=80, bbox_inches="tight")
    plt.close(fig)
    print("wrote", name)


def canvas(width, height, w_units, h_units):
    """A blank drawing surface measured in free units, for diagrams."""
    fig = plt.figure(figsize=(width, height))
    ax = fig.add_axes([0, 0, 1, 1])
    ax.set_xlim(0, w_units)
    ax.set_ylim(0, h_units)
    ax.axis("off")
    ax.grid(False)
    return fig, ax


def box(ax, cx, cy, w, h, text, fc=PALE_GRAY, ec=MUTED, fs=8.5, bold=False,
        color=INK, lw=1.2, rounded=True, ha="center"):
    """Rectangle centred on (cx, cy) with centred text."""
    style = "round,pad=0,rounding_size=1.2" if rounded else "square,pad=0"
    patch = FancyBboxPatch((cx - w / 2, cy - h / 2), w, h, boxstyle=style,
                           fc=fc, ec=ec, lw=lw, zorder=2)
    ax.add_patch(patch)
    tx = cx if ha == "center" else cx - w / 2 + 1.2
    ax.text(tx, cy, text, ha=ha, va="center", fontsize=fs, color=color,
            fontweight="bold" if bold else "normal", zorder=3, linespacing=1.25)


def diamond(ax, cx, cy, w, h, text, fc=PALE_GOLD, ec=GOLD, fs=8):
    pts = [(cx, cy + h / 2), (cx + w / 2, cy), (cx, cy - h / 2), (cx - w / 2, cy)]
    ax.add_patch(Polygon(pts, closed=True, fc=fc, ec=ec, lw=1.2, zorder=2))
    ax.text(cx, cy, text, ha="center", va="center", fontsize=fs, color=INK, zorder=3,
            linespacing=1.2)


def arrow(ax, p1, p2, label=None, color=MUTED, lw=1.3, fs=8, label_offset=(0, 0.8),
          style="-|>", connection="arc3", ls="-", ha="center"):
    ax.add_patch(FancyArrowPatch(p1, p2, arrowstyle=style, mutation_scale=11, lw=lw,
                                 color=color, zorder=1, connectionstyle=connection,
                                 linestyle=ls, shrinkA=0, shrinkB=0))
    if label:
        mx, my = (p1[0] + p2[0]) / 2, (p1[1] + p2[1]) / 2
        ax.text(mx + label_offset[0], my + label_offset[1], label, fontsize=fs,
                color=color, ha=ha, va="center", zorder=3)


def label(ax, x, y, text, fs=8.5, color=INK, ha="left", va="center", bold=False, style="normal"):
    ax.text(x, y, text, fontsize=fs, color=color, ha=ha, va=va,
            fontweight="bold" if bold else "normal", fontstyle=style, linespacing=1.3)


def shade(ax, x0, x1, color, alpha=0.25, zorder=0):
    ax.axvspan(x0, x1, color=color, alpha=alpha, lw=0, zorder=zorder)


def panel_note(ax, text, loc="upper right", fs=8, color=MUTED):
    """Small caption inside an axes."""
    x, ha = (0.99, "right") if "right" in loc else (0.01, "left")
    y, va = (0.95, "top") if "upper" in loc else (0.05, "bottom")
    ax.text(x, y, text, transform=ax.transAxes, ha=ha, va=va, fontsize=fs, color=color)
