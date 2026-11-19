#!/usr/bin/env python3
"""
analyze_energy.py - energy per inference from a Power Profiler Kit 2 recording.

The esp32/energy firmware runs every model in blocks and shows on the PPK2
logic inputs what it is doing (see esp32/energy/README.md):
    D0 marker (block running)   D1-D4 block number   D5 self-test OK
    D6 pipeline (raw input)     D7 sequence running
This script reads the CSV exported from the Power Profiler app, finds every
block, and computes for each model:
    time per inference, average current/power, energy per inference
    (total, above idle, above the busy-loop baseline).

The number of inferences in a block is read from esp32/energy/main/energy_config.h,
so keep that file the same as the one that was flashed.

Usage (from the Models_tuning folder, with the venv active):
    python esp32/tools/analyze_energy.py recording.csv
    python esp32/tools/analyze_energy.py recording.csv --voltage 5.0
    python esp32/tools/analyze_energy.py - --plot-only     # only redraw the chart

Export in the Power Profiler app: "Export" -> select the whole recording ->
tick "Timestamp", "Current" and "Digital logic pins (single string field)".

Writes (in results/):
    energy_results.csv   one row per block type (averaged over the rounds)
    energy_windows.csv   one row per measured block (every round)
    energy_per_inference.png
"""
import argparse
import csv
import re
import sys
from pathlib import Path

import numpy as np
import pandas as pd

TOOLS_DIR = Path(__file__).resolve().parent          # esp32/tools
ESP32_DIR = TOOLS_DIR.parent                          # esp32
RESULTS_DIR = ESP32_DIR.parent / "results"
CONFIG_FILE = ESP32_DIR / "energy" / "main" / "energy_config.h"

RAIL_VOLTAGE = 3.3   # the ESP32-S3 runs from the board's 3.3 V regulator


# ----------------------------------------------------------------- config --
def read_config(path):
    """Block numbers, pass counts and timing from energy_config.h."""
    text = Path(path).read_text(encoding="utf-8")
    defines = {name: int(value) for name, value in
               re.findall(r"^\s*#define\s+(\w+)\s+(\d+)\b", text, flags=re.M)}
    models = []
    for block, project, passes_model, passes_pipe in re.findall(
            r'X\(\s*(\d+)\s*,\s*"([^"]+)"\s*,\s*(\d+)\s*,\s*(\d+)\s*\)', text):
        models.append({"block": int(block), "project": project,
                       "passes_model": int(passes_model), "passes_pipeline": int(passes_pipe)})
    if not models:
        sys.exit(f"No model table (X(...) lines) found in {path}")
    for project_info in models:
        project_info["model"] = model_name(project_info["project"])
    return defines, models


def model_name(project):
    """MODEL_NAME from <project>/main/model_info.h (falls back to the folder name)."""
    info = ESP32_DIR / project / "main" / "model_info.h"
    if info.exists():
        m = re.search(r'#define\s+MODEL_NAME\s+"([^"]+)"', info.read_text(encoding="utf-8"))
        if m:
            return m.group(1)
    return project


def n_test_samples():
    test_data = ESP32_DIR / "01_decision_tree" / "main" / "test_data.h"
    m = re.search(r"#define\s+N_TEST\s+(\d+)", test_data.read_text(encoding="utf-8"))
    return int(m.group(1)) if m else 400


# ------------------------------------------------------------- recording --
def load_recording(path, chunk_rows=2_000_000):
    """Read the PPK2 CSV export.

    Returns t (s), current (A) and two (n, 8) uint8 arrays: the logic level of
    D0..D7 in the order of the text column, and a mask that is 1 where the
    level is known ('0' or '1'; the app writes 'X' when a pin changed during
    the sample and '-' when there is no logic data).
    """
    header = pd.read_csv(path, nrows=0).columns.tolist()
    time_col = next((c for c in header if c.lower().startswith("timestamp")), None)
    current_col = next((c for c in header if c.lower().startswith("current")), None)
    if time_col is None or current_col is None:
        sys.exit(f"Expected Timestamp and Current columns, found: {header}")
    unit = re.search(r"\((\w+)\)", current_col)
    scale = {"A": 1.0, "mA": 1e-3, "uA": 1e-6, "nA": 1e-9}.get(unit.group(1) if unit else "uA")
    t_scale = 1e-3 if "(ms)" in time_col else (1e-6 if "(us)" in time_col else 1.0)

    single = next((c for c in header if c.replace(" ", "") == "D0-D7"), None)
    separate = [c for c in header if re.fullmatch(r"D[0-7]", c.strip())]
    if single is None and len(separate) != 8:
        sys.exit("The CSV has no digital channels. Export again with the digital "
                 "logic pins included.")
    use = [time_col, current_col] + ([single] if single else separate)

    t_parts, i_parts, lvl_parts, known_parts = [], [], [], []
    reader = pd.read_csv(path, usecols=use, chunksize=chunk_rows,
                         dtype={c: str for c in use[2:]})
    for chunk in reader:
        t_parts.append(chunk[time_col].to_numpy(np.float64) * t_scale)
        i_parts.append(chunk[current_col].to_numpy(np.float64) * scale)
        if single:
            chars = chunk[single].fillna("--------").str.strip().str.pad(8, fillchar="-")
            codes = np.frombuffer("".join(chars.str[:8]).encode("ascii"),
                                  dtype=np.uint8).reshape(-1, 8)
        else:
            codes = np.stack([chunk[c].fillna("-").str.strip().str[:1]
                              .map(lambda s: ord(s) if s else ord("-")).to_numpy(np.uint8)
                              for c in separate], axis=1)
        lvl_parts.append((codes == ord("1")).astype(np.uint8))
        known_parts.append(((codes == ord("1")) | (codes == ord("0"))).astype(np.uint8))
        print(f"  read {sum(len(p) for p in t_parts):,} samples ...", end="\r")
    print()
    return (np.concatenate(t_parts), np.concatenate(i_parts),
            np.concatenate(lvl_parts), np.concatenate(known_parts))


# --------------------------------------------------------------- windows --
def runs_of(mask):
    """Start/end indices (end exclusive) of the True runs in a boolean array."""
    d = np.diff(np.concatenate([[0], mask.astype(np.int8), [0]]))
    return np.flatnonzero(d == 1), np.flatnonzero(d == -1)


def majority(levels, known):
    """1 if most of the known samples are high, 0 if most are low, -1 if none known."""
    n_known = int(known.sum())
    if n_known == 0:
        return -1
    return int(levels[known.astype(bool)].sum() * 2 > n_known)


def find_windows(t, current, lvl, known, order, min_samples=5):
    """All blocks (marker high) with their block number, flags and idle reference."""
    ch = list(range(8)) if order == "d0-first" else list(range(7, -1, -1))
    L = lvl[:, ch]          # column k is now pin Dk
    K = known[:, ch]
    marker = (L[:, 0] == 1) & (K[:, 0] == 1)
    starts, ends = runs_of(marker)
    dt = float(np.median(np.diff(t[: min(len(t), 100_000)])))

    windows = []
    prev_end = 0
    for a, b in zip(starts, ends):
        if b - a < min_samples:
            continue
        bits = [majority(L[a:b, k], K[a:b, k]) for k in range(1, 5)]
        # D7 (sequence running) must be high: skips the random pin levels the
        # PPK2 reads before the board is powered
        if min(bits) < 0 or majority(L[a:b, 7], K[a:b, 7]) != 1:
            prev_end = b
            continue
        block = bits[0] | bits[1] << 1 | bits[2] << 2 | bits[3] << 3
        pipeline = majority(L[a:b, 6], K[a:b, 6]) == 1
        test_ok = majority(L[a:b, 5], K[a:b, 5]) == 1

        # idle reference: the gap before this block (same block number, marker low),
        # without the first and last 10 % (pin changes, wake-up)
        gap = np.arange(prev_end, a)
        if len(gap):
            idb = (L[gap, 1] | L[gap, 2] << 1 | L[gap, 3] << 2 | L[gap, 4] << 3)
            same = gap[(idb == block) & (K[gap, 1:5].min(axis=1) == 1) & (L[gap, 0] == 0)]
        else:
            same = gap
        if len(same) >= 10:
            cut = len(same) // 10
            same = same[cut: len(same) - cut]
        idle_current = float(current[same].mean()) if len(same) >= 10 else float("nan")

        if current[a:b].mean() < 1e-3:   # < 1 mA: board not powered yet (floating pins)
            prev_end = b
            continue
        windows.append({
            "start_s": float(t[a]),
            "duration_s": float(t[b - 1] - t[a]) + dt,
            "block": block,
            "pipeline": pipeline,
            "test_ok": test_ok,
            "current_A": float(current[a:b].mean()),
            "idle_current_A": idle_current,
            "samples": int(b - a),
        })
        prev_end = b
    return windows, dt


def expected_cycle(defines, models):
    cycle = [(defines.get("BLOCK_BASELINE", 0), False), (defines.get("BLOCK_PREPROCESS", 12), False)]
    cycle += [(m["block"], False) for m in models]
    if defines.get("RUN_PIPELINE", 1):
        cycle += [(m["block"], True) for m in models]
    return cycle


def order_score(windows, cycle):
    """Share of consecutive blocks that follow the expected order."""
    pairs = {(cycle[i], cycle[(i + 1) % len(cycle)]) for i in range(len(cycle))}
    seq = [(w["block"], w["pipeline"]) for w in windows]
    if len(seq) < 2:
        return 0.0
    return sum((seq[i], seq[i + 1]) in pairs for i in range(len(seq) - 1)) / (len(seq) - 1)


# --------------------------------------------------------------- results --
def describe(windows, defines, models, n_test, voltage):
    by_block = {m["block"]: m for m in models}
    baseline_block = defines.get("BLOCK_BASELINE", 0)
    preprocess_block = defines.get("BLOCK_PREPROCESS", 12)

    seen = {}
    busy_power_by_round = {}
    for w in windows:
        key = (w["block"], w["pipeline"])
        w["round"] = seen.get(key, 0) + 1
        seen[key] = w["round"]
        w["power_W"] = voltage * w["current_A"]
        w["idle_power_W"] = voltage * w["idle_current_A"]
        if w["block"] == baseline_block:
            busy_power_by_round[w["round"]] = w["power_W"]

    rows = []
    for w in windows:
        blk = w["block"]
        if blk == baseline_block:
            project, name, n = "-", "Baseline (busy loop, no model)", 0
        elif blk == preprocess_block:
            project, name, n = "-", "Preprocessing only (log1p + z-score)", \
                defines.get("PASSES_PREPROCESS", 0) * n_test
        elif blk in by_block:
            m = by_block[blk]
            project, name = m["project"], m["model"]
            n = (m["passes_pipeline"] if w["pipeline"] else m["passes_model"]) * n_test
        else:
            print(f"  warning: unknown block number {blk} at t={w['start_s']:.3f} s - skipped")
            continue
        if blk == baseline_block:
            inp = "-"
        elif blk == preprocess_block:
            inp = "raw (preprocessing only)"
        else:
            inp = "raw (preprocessing + model)" if w["pipeline"] else "preprocessed (model only)"
        busy = busy_power_by_round.get(w["round"], np.nan)
        t_inf = w["duration_s"] / n if n else np.nan
        rows.append({
            "project": project, "model": name, "block": blk,
            "input": inp,
            "round": w["round"], "start_s": w["start_s"], "test_ok": w["test_ok"],
            "inferences": n, "block_ms": w["duration_s"] * 1e3,
            "time_per_inference_us": t_inf * 1e6,
            "current_mA": w["current_A"] * 1e3, "idle_current_mA": w["idle_current_A"] * 1e3,
            "power_mW": w["power_W"] * 1e3, "idle_power_mW": w["idle_power_W"] * 1e3,
            "busy_power_mW": busy * 1e3,
            "energy_uJ": w["power_W"] * t_inf * 1e6,
            "energy_above_idle_uJ": (w["power_W"] - w["idle_power_W"]) * t_inf * 1e6,
            "energy_above_busy_uJ": (w["power_W"] - busy) * t_inf * 1e6,
            "energy_rail_uJ": RAIL_VOLTAGE * w["current_A"] * t_inf * 1e6,
            "block_energy_mJ": w["power_W"] * w["duration_s"] * 1e3,
        })
    return pd.DataFrame(rows)


SUMMARY_TITLES = {
    "project": "Project",
    "model": "Model",
    "block": "Block (D1-D4)",
    "input": "Input",
    "rounds": "Rounds",
    "all_ok": "Self-test and checks OK",
    "inferences": "Inferences per block",
    "block_ms": "Block length (ms)",
    "time_per_inference_us": "Time per inference (us)",
    "current_mA": "Average current during block (mA)",
    "idle_current_mA": "Average current while idle (mA)",
    "power_mW": "Average power during block (mW)",
    "idle_power_mW": "Average power while idle (mW)",
    "energy_uJ": "Energy per inference - total (uJ)",
    "energy_std_uJ": "Energy per inference - std over rounds (uJ)",
    "energy_above_idle_uJ": "Energy per inference - above idle (uJ)",
    "energy_above_busy_uJ": "Energy per inference - above busy-loop baseline (uJ)",
    "energy_rail_uJ": "Energy per inference at the 3.3 V rail - estimate (uJ)",
    "voltage": "Supply voltage (V)",
}


def summarize(df, voltage):
    keys = ["project", "model", "block", "input"]
    mean_cols = ["inferences", "block_ms", "time_per_inference_us", "current_mA", "idle_current_mA",
                 "power_mW", "idle_power_mW", "energy_uJ", "energy_above_idle_uJ",
                 "energy_above_busy_uJ", "energy_rail_uJ"]
    g = df.groupby(keys, sort=False)
    out = g[mean_cols].mean()
    out["energy_std_uJ"] = g["energy_uJ"].std()
    out["rounds"] = g.size()
    out["all_ok"] = g["test_ok"].all()
    out["voltage"] = voltage
    out = out.reset_index()
    return out[[c for c in SUMMARY_TITLES if c in out.columns]]


def plot(summary, path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        from matplotlib.ticker import FixedLocator, FuncFormatter, NullLocator
    except ImportError:
        print("  (matplotlib not installed - no plot)")
        return
    models = summary[summary["inferences"] > 0]
    only = models[models["input"].str.startswith("preprocessed")].set_index("model")
    pipe = models[models["input"].str.startswith("raw (preprocessing +")].set_index("model")
    names = sorted(only.index, key=lambda n: only.loc[n, "energy_uJ"])
    y = np.arange(len(names))
    e_only = np.array([only.loc[n, "energy_uJ"] for n in names])
    e_pipe = np.array([pipe.loc[n, "energy_uJ"] if n in pipe.index else np.nan for n in names])

    fig, ax = plt.subplots(figsize=(9, 0.5 * len(names) + 1.6))
    ax.barh(y - 0.2, e_only, height=0.38, label="model only", color="#4C72B0")
    if len(pipe):
        ax.barh(y + 0.2, e_pipe, height=0.38, label="preprocessing + model", color="#DD8452")
    pre = models[models["model"].str.startswith("Preprocessing")]
    if len(pre):
        ax.axvline(pre["energy_uJ"].iloc[0], color="grey", ls="--", lw=1,
                   label=f"preprocessing only ({pre['energy_uJ'].iloc[0]:.1f} µJ)")

    # value at the end of every bar (so nobody has to read it off the log axis)
    def fmt(v):
        return f"{v:.2f}" if v < 1 else (f"{v:.1f}" if v < 10 else f"{v:.0f}")
    for yi, v in zip(y - 0.2, e_only):
        ax.text(v * 1.08, yi, fmt(v), va="center", fontsize=8, color="#2C4F7C")
    for yi, v in zip(y + 0.2, e_pipe):
        if np.isfinite(v):
            ax.text(v * 1.08, yi, fmt(v), va="center", fontsize=8, color="#8A4A1C")

    # log axis with plain-number ticks (0.1, 0.2, 0.5, 1, 2, 5, ...) instead of 10^x
    ax.set_xscale("log")
    lo = 10 ** np.floor(np.log10(np.nanmin(e_only)))
    hi = np.nanmax(np.r_[e_only, e_pipe]) * 2.2          # room for the value labels
    ticks = [m * 10 ** k for k in range(-3, 5) for m in (1, 2, 5) if lo <= m * 10 ** k <= hi]
    ax.xaxis.set_major_locator(FixedLocator(ticks))
    ax.xaxis.set_major_formatter(FuncFormatter(lambda v, _: f"{v:g}"))
    ax.xaxis.set_minor_locator(NullLocator())
    ax.set_xlim(lo, hi)

    ax.set_yticks(y, names)
    ax.set_xlabel("Energy per inference (µJ, log scale) - whole board at 5 V")
    ax.set_title("ESP32-S3: energy per inference (PPK2, mean of 3 rounds)")
    ax.legend(loc="lower right", fontsize=8)
    ax.grid(axis="x", alpha=0.3)
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    print(f"  plot: {path}")


def plot_from_results(results_csv, path):
    """Redraw the chart from an existing energy_results.csv (no need to read the recording)."""
    df = pd.read_csv(results_csv)
    back = {title: key for key, title in SUMMARY_TITLES.items()}
    plot(df.rename(columns=back), path)


# ------------------------------------------------------------------ main --
def main():
    ap = argparse.ArgumentParser(description="Energy per inference from a PPK2 CSV export.")
    ap.add_argument("csv", help="CSV exported from the Power Profiler app")
    ap.add_argument("--voltage", type=float, default=5.0,
                    help="PPK2 supply voltage set in the app (default 5.0 V)")
    ap.add_argument("--config", default=str(CONFIG_FILE), help="energy_config.h that was flashed")
    ap.add_argument("--bit-order", choices=["auto", "d0-first", "d7-first"], default="auto",
                    help="order of the pins in the D0-D7 text column (default: detect)")
    ap.add_argument("--out", default=str(RESULTS_DIR), help="output folder (default: results/)")
    ap.add_argument("--plot-only", action="store_true",
                    help="only redraw the chart from <out>/energy_results.csv (csv argument is ignored)")
    args = ap.parse_args()

    if args.plot_only:
        plot_from_results(Path(args.out) / "energy_results.csv",
                          Path(args.out) / "energy_per_inference.png")
        return

    defines, models = read_config(args.config)
    n_test = n_test_samples()
    print(f"Config: {len(models)} models, {n_test} samples per pass, "
          f"{defines.get('N_ROUNDS', '?')} rounds")

    print(f"Reading {args.csv}")
    t, current, lvl, known = load_recording(args.csv)
    print(f"  {len(t):,} samples, {t[-1] - t[0]:.1f} s, "
          f"sample rate ~{1 / np.median(np.diff(t[:100_000])):,.0f} /s")

    cycle = expected_cycle(defines, models)
    orders = ["d0-first", "d7-first"] if args.bit_order == "auto" else [args.bit_order]
    best = None
    for order in orders:
        windows, dt = find_windows(t, current, lvl, known, order)
        score = order_score(windows, cycle)
        if best is None or score > best[0]:
            best = (score, order, windows)
    score, order, windows = best
    print(f"  found {len(windows)} blocks (pin order {order}, "
          f"{100 * score:.0f}% in the expected sequence)")
    if not windows:
        sys.exit("No blocks found - is D0 (marker) connected and in the export?")
    if score < 0.9:
        print("  WARNING: the blocks are not in the expected order. Check the wiring "
              "(D1-D4 = GPIO1-4) and that energy_config.h matches the flashed firmware.")

    df = describe(windows, defines, models, n_test, args.voltage)
    if not df["test_ok"].all():
        print("  WARNING: D5 (self-test / checks OK) was low during some blocks - "
              "see the board's serial output.")
    summary = summarize(df, args.voltage)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    df.to_csv(out / "energy_windows.csv", index=False, float_format="%.6g")
    summary.rename(columns=SUMMARY_TITLES).to_csv(out / "energy_results.csv", index=False,
                                                  float_format="%.6g", quoting=csv.QUOTE_MINIMAL)

    print("\n" + "=" * 112)
    print(f"{'model':<38}{'input':<16}{'rounds':>7}{'us/inf':>10}{'mA':>9}{'mW':>9}"
          f"{'uJ/inf':>11}{'+-':>8}{'above idle':>12}")
    print("-" * 112)
    for _, r in summary.iterrows():
        inp = r["input"].split(" ")[0]
        print(f"{r['model'][:37]:<38}{inp:<16}{r['rounds']:>7}{r['time_per_inference_us']:>10.3f}"
              f"{r['current_mA']:>9.2f}{r['power_mW']:>9.1f}{r['energy_uJ']:>11.4f}"
              f"{r['energy_std_uJ']:>8.4f}{r['energy_above_idle_uJ']:>12.4f}")
    print("=" * 112)
    print(f"Supply {args.voltage} V (whole board). Results: {out / 'energy_results.csv'}")
    print(f"Every block: {out / 'energy_windows.csv'}")
    plot(summary, out / "energy_per_inference.png")


if __name__ == "__main__":
    main()
