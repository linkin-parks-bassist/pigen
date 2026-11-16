#!/usr/bin/env python3
"""UART 8N1 timing explainer: a renderer for actual simulated Pigen RTL.

Reads a simulator CSV trace with columns cycle,tx,data_valid,data_ready,data,reset.
Samples are taken immediately before positive-edge state updates. The script renders an
8-second 1280x720 24 fps H.264 MP4 and a PNG poster. Every waveform sample,
decoded bit and byte value shown comes from the trace. Nothing is synthesized.

Visual design by Claude Opus (Anthropic).
"""
import argparse
import hashlib
import json
from pathlib import Path
import csv
import os
import subprocess

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

DIV, NBITS, FPS, SECONDS, W, H = 16, 11, 24, 8, 1280, 720
BG, PANEL, EDGE, INK, DIM = "#f5f3ed", "#ffffff", "#d4cec2", "#272624", "#706b63"
TEAL, PURPLE, PURPLE2, ORANGE = "#8b3328", "#355d74", "#6b703d", "#896a2f"
LABELS = ["START"] + [f"D{i}" for i in range(8)] + ["STOP", "NEXT"]
COLS = ("cycle", "tx", "data_valid", "data_ready", "data", "reset")
MONO = "DejaVu Sans Mono"


def parse_int(s, col, line):
    s = (s or "").strip()
    for base in (0, 10, 16):
        try:
            return int(s, base)
        except ValueError:
            pass
    raise ValueError(f"line {line}: column '{col}' has non-numeric value {s!r}")


def load_trace(path):
    with open(path, newline="") as fh:
        rows = [{(k or "").strip(): v for k, v in r.items()} for r in csv.DictReader(fh)]
    if not rows:
        raise ValueError(f"{path}: trace contains no data rows")
    missing = [c for c in COLS if c not in rows[0]]
    if missing:
        raise ValueError(f"{path}: missing columns {missing}")
    return {c: np.array([parse_int(r[c], c, i + 2) for i, r in enumerate(rows)], dtype=np.int64)
            for c in COLS}


def analyze(d):
    rst, tx, n = d["reset"], d["tx"], len(d["tx"])
    ones = np.flatnonzero(rst != 0)
    if ones.size:
        after = np.flatnonzero((rst == 0) & (np.arange(n) > ones[0]))
        if not after.size:
            raise ValueError("reset never deasserts in trace")
        rel = int(after[0])
    else:
        rel = 0
    falls = np.flatnonzero((tx[:-1] == 1) & (tx[1:] == 0)) + 1
    falls = falls[(falls > rel) & (rst[falls] == 0)]
    if not falls.size:
        raise ValueError("no falling TX edge found after reset")
    e = int(falls[0])
    if e + NBITS * DIV > n:
        raise ValueError(f"trace ends before {NBITS} bit periods after TX edge at sample {e}")
    if not np.all(np.diff(d["cycle"][e:e + NBITS * DIV]) == 1):
        raise ValueError("cycle column is not contiguous across the frame window")
    hs = np.flatnonzero((d["data_valid"] == 1) & (d["data_ready"] == 1) & (rst == 0))
    hs = hs[hs >= rel]
    if not hs.size:
        raise ValueError("no accepted input byte (data_valid & data_ready) after reset")
    acc = int(hs[0])
    bits = [int(tx[e + k * DIV + DIV // 2]) for k in range(NBITS)]
    dec = sum(bits[1 + i] << i for i in range(8))
    lead, tail = min(DIV, e), min(DIV // 2, n - (e + NBITS * DIV))
    return dict(e=e, acc=acc, bits=bits, dec=dec, w0=e - lead, w1=e + NBITS * DIV + tail,
                c0=int(d["cycle"][e]), acc_cycle=int(d["cycle"][acc]),
                acc_byte=int(d["data"][acc]) & 0xFF)


def style(ax, xs):
    ax.set_facecolor(BG)
    ax.set_xlim(xs[0], xs[-1])
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.spines["bottom"].set_color(EDGE)
    ax.tick_params(colors=DIM, labelsize=13, length=0)


def reveal(line, xs, ys, x):
    k = max(1, int(np.searchsorted(xs, x, side="right")))
    line.set_data(np.append(xs[:k], x), np.append(ys[:k], ys[k - 1]))


def build(d, a, trace_name):
    plt.rcParams["font.family"] = MONO
    fig = plt.figure(figsize=(W / 100, H / 100), dpi=100, facecolor=BG)
    w0, w1, c0 = a["w0"], a["w1"], a["c0"]
    cyc = d["cycle"]
    xs = np.append(cyc[w0:w1], cyc[w1 - 1] + 1).astype(float)
    ext = lambda v: np.append(v[w0:w1], v[w1 - 1]).astype(float)
    tx, va, rd = ext(d["tx"]), ext(d["data_valid"]), ext(d["data_ready"])

    fig.text(0.07, 0.935, "UART 8N1 / actual simulated Pigen RTL", color=INK, size=24,
             weight="bold", va="center")
    fig.text(0.07, 0.893, f"trace {trace_name}  ·  divider {DIV} cycles/bit  ·  "
             f"first TX fall after reset @ cycle {c0}", color=DIM, size=12, va="center")
    fig.text(0.07, 0.835, "when (frame <= {1'b1, data, 1'b0})", color=TEAL, size=17, va="center",
             bbox=dict(boxstyle="round,pad=0.55", fc=PANEL, ec=EDGE))
    fig.text(0.07, 0.787, "native Pigen fragment", color=DIM, size=11, va="center")
    fig.text(0.95, 0.835, f"first accepted byte  0x{a['acc_byte']:02X}", color=PURPLE,
             size=17, ha="right", va="center")
    fig.text(0.95, 0.787, f"data_valid & data_ready @ cycle {a['acc_cycle']}", color=DIM,
             size=11, ha="right", va="center")

    axt = fig.add_axes([0.07, 0.50, 0.88, 0.24])
    style(axt, xs)
    axt.set_ylim(-0.35, 1.45)
    axt.set_yticks([0, 1])
    axt.set_yticklabels(["tx 0", "tx 1"])
    axt.set_xticklabels([])
    for k in range(NBITS + 1):
        axt.axvline(c0 + k * DIV, color=EDGE, lw=0.8, zorder=0)
    axt.plot(xs, tx, drawstyle="steps-post", color=TEAL, alpha=0.18, lw=2)
    tx_live, = axt.plot([], [], drawstyle="steps-post", color=TEAL, lw=3)
    sx = np.array([c0 + k * DIV + DIV / 2 for k in range(NBITS)])
    sy = np.array(a["bits"], dtype=float)
    samp, = axt.plot([], [], "o", color=INK, ms=6, zorder=5)

    axh = fig.add_axes([0.07, 0.36, 0.88, 0.11])
    style(axh, xs)
    axh.set_ylim(-0.25, 2.45)
    axh.set_yticks([0.4, 1.8])
    axh.set_yticklabels(["ready", "valid"])
    axh.set_xticklabels([])
    axh.plot(xs, va * 0.8 + 1.4, drawstyle="steps-post", color=PURPLE, alpha=0.18, lw=2)
    axh.plot(xs, rd * 0.8, drawstyle="steps-post", color=PURPLE2, alpha=0.18, lw=2)
    va_live, = axh.plot([], [], drawstyle="steps-post", color=PURPLE, lw=2.6)
    rd_live, = axh.plot([], [], drawstyle="steps-post", color=PURPLE2, lw=2.6)
    if w0 <= a["acc"] < w1:
        axh.axvline(a["acc_cycle"], color=PURPLE, ls="--", lw=1.2)

    axc = fig.add_axes([0.07, 0.14, 0.88, 0.17])
    style(axc, xs)
    axc.set_ylim(0, 1)
    axc.set_yticks([])
    axc.set_xlabel("cycle", color=DIM, size=13)
    cells, vals = [], []
    for k in range(NBITS):
        lo = c0 + k * DIV
        r = Rectangle((lo + 0.6, 0.08), DIV - 1.2, 0.58, fc=PANEL, ec=EDGE, lw=1.5)
        axc.add_patch(r)
        cells.append(r)
        axc.text(lo + DIV / 2, 0.50, LABELS[k], color=INK, size=13, ha="center", va="center")
        vals.append(axc.text(lo + DIV / 2, 0.22, str(a["bits"][k]), color=TEAL, size=16,
                             ha="center", va="center", weight="bold", alpha=0))
    pointer, = axc.plot([], [], "v", color=ORANGE, ms=16)

    heads = [ax.axvline(xs[0], color=ORANGE, lw=2, zorder=6) for ax in (axt, axh)]
    b = a["bits"]
    verdict = ("matches first accepted byte" if a["dec"] == a["acc_byte"]
               else f"differs from first accepted byte 0x{a['acc_byte']:02X}")
    footer = fig.text(0.07, 0.045, f"tx mid-bit samples: start={b[0]}  D7..D0="
                      f"{''.join(str(b[8 - i]) for i in range(8))} = 0x{a['dec']:02X}  stop={b[9]}"
                      f"  ·  {verdict}", color=INK, size=14, va="center", alpha=0)

    end = c0 + NBITS * DIV
    nframes = FPS * SECONDS

    def update(f):
        p = min(1.0, f / (nframes * 0.86))
        x = xs[0] + p * (end - xs[0])
        reveal(tx_live, xs, tx, x)
        reveal(va_live, xs, va * 0.8 + 1.4, x)
        reveal(rd_live, xs, rd * 0.8, x)
        m = sx <= x
        samp.set_data(sx[m], sy[m])
        for h in heads:
            h.set_xdata([x, x])
        pointer.set_data([x], [0.86])
        for k in range(NBITS):
            lo = c0 + k * DIV
            act = lo <= x < lo + DIV
            cells[k].set_facecolor("#ede5d8" if act else PANEL)
            cells[k].set_edgecolor(ORANGE if act else EDGE)
            vals[k].set_alpha(1.0 if x >= lo + DIV / 2 else 0.0)
        footer.set_alpha(float(np.clip((f - nframes * 0.86) / (nframes * 0.06), 0, 1)))

    return fig, update, nframes


def render(trace, out_dir):
    d = load_trace(trace)
    a = analyze(d)
    os.makedirs(out_dir, exist_ok=True)
    fig, update, nframes = build(d, a, os.path.basename(trace))
    if fig.canvas.get_width_height() != (W, H):
        raise RuntimeError("unexpected canvas size")
    mp4 = os.path.join(out_dir, "uart_8n1_timing.mp4")
    png = os.path.join(out_dir, "uart_8n1_timing_poster.png")
    cmd = ["ffmpeg", "-y", "-loglevel", "error", "-f", "rawvideo", "-pix_fmt", "rgba",
           "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-", "-c:v", "libx264", "-preset", "slow",
           "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart", mp4]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
    try:
        for f in range(nframes):
            update(f)
            fig.canvas.draw()
            proc.stdin.write(fig.canvas.buffer_rgba())
    finally:
        proc.stdin.close()
        rc = proc.wait()
    if rc != 0:
        raise RuntimeError(f"ffmpeg exited with status {rc}")
    update(nframes - 1)
    fig.savefig(png, dpi=100, facecolor=BG)
    plt.close(fig)
    Path(out_dir, "uart_8n1_timing.json").write_text(json.dumps({"trace_sha256":hashlib.sha256(Path(trace).read_bytes()).hexdigest(), "visual_design":"Claude Opus", "divider":DIV, "source":str(trace)},indent=2))
    print(f"wrote {mp4}\nwrote {png}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--trace", required=True, help="simulator CSV trace path")
    args = ap.parse_args()
    render(args.trace, args.out)


if __name__ == "__main__":
    main()