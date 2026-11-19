#!/usr/bin/env python3
"""
plot_recording.py - plot a PPK2 recording like the Power Profiler app does:
current over time on top, the digital channels D0-D7 underneath.

It reads the same CSV export as analyze_energy.py (Timestamp, Current, D0-D7).
A full run has ~15 million samples, far more than a screen has pixels, so the
samples are grouped into short time bins. For every bin the plot shows the
min-max range (light band) and the average (line) - the same idea the app uses
when you zoom out.

Usage (from the Models_tuning folder, with the venv active):
    python esp32/tools/plot_recording.py results/temp2.csv                       # whole run
    python esp32/tools/plot_recording.py results/temp2.csv --start 10 --end 20   # zoom: 10-20 s
    python esp32/tools/plot_recording.py results/temp2.csv --start 10 --end 20 --show
          (--show opens a window where you can zoom and pan with the mouse)

Writes results/recording_<start>-<end>s.png (or the file given with --out).
"""
import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from analyze_energy import (RESULTS_DIR, CONFIG_FILE, find_windows, load_recording,  # noqa: E402
                            read_config)

CHANNEL_NAMES = ["D0 marker", "D1 block bit 0", "D2 block bit 1", "D3 block bit 2",
                 "D4 block bit 3", "D5 test OK", "D6 pipeline", "D7 running"]

SHORT_NAMES = {   # labels written above each block when you zoom in
    "DecisionTree": "DT", "RandomForest": "RF", "XGBoost": "XGB", "LogisticRegression": "LR",
    "LinearSVM": "LinSVM", "RBF_SVM": "RBF", "GaussianNB": "NB", "KMeansAnomaly": "KM",
    "MLP_int8": "MLP", "CNN1D_int8": "CNN", "Autoencoder_int8": "AE",
}


def bin_samples(t, current, levels, start, end, n_bins):
    """Group the samples in [start, end) into n_bins time bins.

    Returns the bin centres, min / mean / max current per bin (mA) and, per
    digital channel, the fraction of the bin the pin was high (0..1).
    """
    a, b = np.searchsorted(t, [start, end])
    t, current, levels = t[a:b], current[a:b] * 1e3, levels[a:b]
    if len(t) == 0:
        sys.exit(f"No samples between {start} s and {end} s")
    n_bins = min(n_bins, len(t))
    idx = np.minimum(((t - t[0]) / (t[-1] - t[0] + 1e-12) * n_bins).astype(int), n_bins - 1)
    count = np.bincount(idx, minlength=n_bins)
    keep = count > 0
    mean = np.bincount(idx, weights=current, minlength=n_bins)[keep] / count[keep]
    lo = np.full(n_bins, np.inf)
    hi = np.full(n_bins, -np.inf)
    np.minimum.at(lo, idx, current)
    np.maximum.at(hi, idx, current)
    centre = (np.bincount(idx, weights=t, minlength=n_bins)[keep] / count[keep])
    pins = np.stack([np.bincount(idx, weights=levels[:, k], minlength=n_bins)[keep] / count[keep]
                     for k in range(8)], axis=1)
    return centre, lo[keep], mean, hi[keep], pins


def main():
    ap = argparse.ArgumentParser(description="Plot a PPK2 recording (current + digital channels).")
    ap.add_argument("csv", help="CSV exported from the Power Profiler app")
    ap.add_argument("--start", type=float, default=None, help="start time in s (default: beginning)")
    ap.add_argument("--end", type=float, default=None, help="end time in s (default: end)")
    ap.add_argument("--bins", type=int, default=4000, help="number of time bins (default 4000)")
    ap.add_argument("--out", default=None, help="output PNG (default: results/recording_<start>-<end>s.png)")
    ap.add_argument("--show", action="store_true", help="also open an interactive window")
    ap.add_argument("--no-labels", action="store_true", help="do not write model names above blocks")
    args = ap.parse_args()

    import matplotlib
    if not args.show:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    print(f"Reading {args.csv}")
    t, current, lvl, known = load_recording(args.csv)
    start = t[0] if args.start is None else args.start
    end = t[-1] if args.end is None else args.end
    centre, lo, mean, hi, pins = bin_samples(t, current, lvl, start, end, args.bins)

    fig, (ax_i, ax_d) = plt.subplots(2, 1, figsize=(13, 7.5), sharex=True,
                                     gridspec_kw={"height_ratios": [3, 2], "hspace": 0.05})

    # --- current (top) ---
    ax_i.fill_between(centre, lo, hi, step="mid", color="#378ADD", alpha=0.25, linewidth=0,
                      label="min-max in each bin")
    ax_i.plot(centre, mean, color="#185FA5", linewidth=0.8, label="average")
    ax_i.set_ylabel("Current (mA)")
    ax_i.set_ylim(bottom=0)
    ax_i.grid(alpha=0.3)
    ax_i.legend(loc="lower right", fontsize=8)
    ax_i.set_title(f"PPK2 recording {Path(args.csv).name}  ({start:.2f}-{end:.2f} s, "
                   f"{len(t[(t >= start) & (t < end)]):,} samples)")

    # --- block names above the blocks, when few enough are visible ---
    if not args.no_labels:
        try:
            defines, models = read_config(CONFIG_FILE)
            names = {m["block"]: SHORT_NAMES.get(m["model"], m["model"]) for m in models}
            names[defines.get("BLOCK_BASELINE", 0)] = "base"
            names[defines.get("BLOCK_PREPROCESS", 12)] = "prep"
            a, b = np.searchsorted(t, [start, end])
            windows, _ = find_windows(t[a:b], current[a:b], lvl[a:b], known[a:b], "d0-first")
            if 0 < len(windows) <= 40:
                top = np.nanmax(hi) * 1.04
                for w in windows:
                    label = names.get(w["block"], str(w["block"])) + ("+p" if w["pipeline"] else "")
                    ax_i.text(w["start_s"] + w["duration_s"] / 2, top, label, ha="center",
                              va="bottom", fontsize=8, color="#444441")
                ax_i.set_ylim(0, top * 1.08)
        except Exception as e:      # labels are only a help - never stop the plot
            print(f"  (no block labels: {e})")

    # --- digital channels (bottom): one row per pin, like the app ---
    for k in range(8):
        y0 = 7 - k                                  # D0 at the top
        ax_d.fill_between(centre, y0, y0 + 0.7 * pins[:, k], step="mid",
                          color="#1D9E75", alpha=0.6, linewidth=0)
        ax_d.plot(centre, y0 + 0.7 * pins[:, k], color="#0F6E56", linewidth=0.7,
                  drawstyle="steps-mid")
    ax_d.set_yticks([7 - k + 0.35 for k in range(8)], CHANNEL_NAMES, fontsize=8)
    ax_d.set_ylim(-0.2, 8)
    ax_d.set_xlabel("Time (s)")
    ax_d.grid(axis="x", alpha=0.3)

    out = Path(args.out) if args.out else RESULTS_DIR / f"recording_{start:.0f}-{end:.0f}s.png"
    out.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(out, dpi=150, bbox_inches="tight")
    print(f"  plot: {out}")
    if args.show:
        plt.show()


if __name__ == "__main__":
    main()
