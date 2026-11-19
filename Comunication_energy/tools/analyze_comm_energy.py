#!/usr/bin/env python3
"""
analyze_comm_energy.py - energy and latency per message for MQTT and CoAP,
from a Power Profiler Kit 2 recording of the Comunication_energy firmware.

The firmware sends one message per slot (~1 s) and shows on the logic pins
what it is doing (see main/energy_config.h and README.md):
    D0 slot marker   D1-D3 block (0 idle, 1 MQTT QoS 0, 2 MQTT QoS 1, 3 CoAP NON, 4 CoAP CON)
    D4 Wi-Fi power save on   D5 OK   D6 busy (send -> done)   D7 running

For every slot the script takes the average current and the slot length, and
subtracts the idle slots (block 0) of the same round and power-save mode:

    energy per message = (P_slot - P_idle) x T_slot          (energy above idle)

so everything the radio and the network stack do for one message - sending,
the broker's/server's answer, TCP ACKs, the radio staying awake afterwards -
is counted, while the board's normal Wi-Fi-connected idle power is not.

Latency per message = how long D6 was high (send -> done). The PPK2 samples the
pins at the recording's sampling rate (0.1 ms at 10 kS/s), so very short sends
(MQTT QoS 0, CoAP NON) may show 0 ms. The ESP32 measures every latency in us
itself; add --latency-log <file> with its serial output (the "LAT,..." lines)
to use those exact values instead.

Usage (from the Comunication_energy folder):
    python tools/analyze_comm_energy.py recording.csv
    python tools/analyze_comm_energy.py recording.csv --latency-log serial_output.txt

Writes (in results/):
    comm_energy_results.csv   one row per power-save mode and protocol
    comm_energy_messages.csv  one row per message
    comm_energy.png           energy and latency per message
"""
import argparse
import re
import sys
from pathlib import Path

import numpy as np
import pandas as pd

PROJECT_DIR = Path(__file__).resolve().parent.parent      # Comunication_energy
CONFIG_FILE = PROJECT_DIR / "main" / "energy_config.h"
RESULTS_DIR = PROJECT_DIR / "results"

BLOCK_NAMES = {0: "Idle (Wi-Fi connected)", 1: "MQTT QoS 0", 2: "MQTT QoS 1",
               3: "CoAP NON", 4: "CoAP CON"}
PS_NAMES = {0: "Power save off", 1: "Modem sleep"}


# ----------------------------------------------------------------- config --
def read_config(path):
    text = Path(path).read_text(encoding="utf-8")
    return {k: int(v) for k, v in re.findall(r"^\s*#define\s+(\w+)\s+(\d+)\b", text, flags=re.M)}


# ------------------------------------------------------------- recording --
def load_recording(path, chunk_rows=2_000_000):
    """PPK2 CSV export -> time (s), current (A), pin levels (n x 8, D0 first), known mask."""
    header = pd.read_csv(path, nrows=0).columns.tolist()
    time_col = next(c for c in header if c.lower().startswith("timestamp"))
    current_col = next(c for c in header if c.lower().startswith("current"))
    unit = re.search(r"\((\w+)\)", current_col)
    scale = {"A": 1.0, "mA": 1e-3, "uA": 1e-6, "nA": 1e-9}.get(unit.group(1) if unit else "uA")
    single = next((c for c in header if c.replace(" ", "") == "D0-D7"), None)
    separate = [c for c in header if re.fullmatch(r"D[0-7]", c.strip())]
    if single is None and len(separate) != 8:
        sys.exit("The CSV has no digital channels - export again with the digital logic pins.")
    use = [time_col, current_col] + ([single] if single else separate)

    t_parts, i_parts, lvl_parts, known_parts = [], [], [], []
    for chunk in pd.read_csv(path, usecols=use, chunksize=chunk_rows,
                             dtype={c: str for c in use[2:]}):
        t_parts.append(chunk[time_col].to_numpy(np.float64) * 1e-3)
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


def majority(levels, known):
    n = int(known.sum())
    return -1 if n == 0 else int(levels[known.astype(bool)].sum() * 2 > n)


# ----------------------------------------------------------------- slots --
def find_slots(t, current, lvl, known, cfg):
    """Every D0-high stretch = one message slot."""
    dt = float(np.median(np.diff(t[: min(len(t), 100_000)])))
    marker = (lvl[:, 0] == 1) & (known[:, 0] == 1)
    d = np.diff(np.concatenate([[0], marker.astype(np.int8), [0]]))
    starts, ends = np.flatnonzero(d == 1), np.flatnonzero(d == -1)
    min_len = 0.5 * cfg.get("SLOT_MS", 1000) / 1000

    slots = []
    for a, b in zip(starts, ends):
        duration = float(t[b - 1] - t[a]) + dt
        if duration < min_len:
            continue
        if majority(lvl[a:b, 7], known[a:b, 7]) != 1:          # D7: sequence running
            continue
        if current[a:b].mean() < 1e-3:                           # board not powered
            continue
        bits = [majority(lvl[a:b, k], known[a:b, k]) for k in (1, 2, 3)]
        if min(bits) < 0:
            continue
        busy = (lvl[a:b, 6] == 1) & (known[a:b, 6] == 1)
        slots.append({
            "start_s": float(t[a]),
            "duration_s": duration,
            "block": bits[0] | bits[1] << 1 | bits[2] << 2,
            "ps": majority(lvl[a:b, 4], known[a:b, 4]),
            "ok": majority(lvl[a:b, 5], known[a:b, 5]) == 1,
            "current_A": float(current[a:b].mean()),
            "busy_ms": float(busy.sum()) * dt * 1e3,
            "busy_current_A": float(current[a:b][busy].mean()) if busy.any() else np.nan,
        })
    return slots, dt


def describe(slots, cfg, voltage):
    df = pd.DataFrame(slots)
    if df.empty:
        sys.exit("No message slots found - is D0 connected and in the export?")
    n_msg = cfg.get("MSGS_PER_BLOCK", 10)
    df["msg"] = df.groupby(["ps", "block"]).cumcount()
    df["round"] = df["msg"] // n_msg + 1
    df["msg"] = df["msg"] % n_msg
    df["power_mW"] = voltage * df["current_A"] * 1e3

    idle = (df[df["block"] == 0].groupby(["round", "ps"])["power_mW"].mean()
            .rename("idle_power_mW"))
    df = df.join(idle, on=["round", "ps"])
    df["energy_above_idle_uJ"] = (df["power_mW"] - df["idle_power_mW"]) * df["duration_s"] * 1e3
    df["energy_total_uJ"] = df["power_mW"] * df["duration_s"] * 1e3
    df["latency_ms"] = df["busy_ms"]
    timeout_ms = cfg.get("REPLY_TIMEOUT_MS", 800)
    df["failed"] = df["block"].isin([2, 4]) & (df["busy_ms"] >= 0.95 * timeout_ms)
    df["protocol"] = df["block"].map(BLOCK_NAMES)
    df["power_save"] = df["ps"].map(PS_NAMES)
    return df


def apply_latency_log(df, path):
    """Replace the D6-based latency with the ESP32's own measurement (LAT lines)."""
    rows = []
    for line in Path(path).read_text(errors="ignore").splitlines():
        m = re.search(r"LAT,(\d+),(\d+),(\d+),(\d+),(-?\d+)", line)
        if m:
            rows.append([int(x) for x in m.groups()])
    if not rows:
        print(f"  (no LAT lines in {path} - keeping latencies from D6)")
        return df
    lat = pd.DataFrame(rows, columns=["round", "ps", "block", "msg", "latency_us"])
    lat = lat.drop_duplicates(["round", "ps", "block", "msg"], keep="last")
    df = df.merge(lat, on=["round", "ps", "block", "msg"], how="left")
    have = df["latency_us"].notna() & (df["block"] > 0)
    df.loc[have, "latency_ms"] = df.loc[have, "latency_us"].clip(lower=0) / 1000
    df.loc[have, "failed"] = df.loc[have, "latency_us"] < 0
    print(f"  latencies from the ESP32 log for {int(have.sum())} messages")
    return df.drop(columns="latency_us")


SUMMARY_TITLES = {
    "power_save": "Wi-Fi power save",
    "protocol": "Protocol",
    "messages": "Messages",
    "failed": "Failed",
    "energy_mean": "Energy per message above idle - mean (uJ)",
    "energy_std": "Energy per message above idle - std (uJ)",
    "energy_median": "Energy per message above idle - median (uJ)",
    "latency_mean": "Latency send to done - mean (ms)",
    "latency_median": "Latency send to done - median (ms)",
    "latency_p95": "Latency send to done - 95th percentile (ms)",
    "power_mean": "Average power during slot (mW)",
    "idle_power": "Idle power, Wi-Fi connected (mW)",
    "slot_s": "Slot length (s)",
}


def summarize(df):
    rows = []
    for (ps, block), g in df[df["block"] > 0].groupby(["ps", "block"]):
        ok = g[~g["failed"]]
        rows.append({
            "power_save": PS_NAMES.get(ps, ps), "protocol": BLOCK_NAMES.get(block, block),
            "messages": len(g), "failed": int(g["failed"].sum()),
            "energy_mean": ok["energy_above_idle_uJ"].mean(),
            "energy_std": ok["energy_above_idle_uJ"].std(),
            "energy_median": ok["energy_above_idle_uJ"].median(),
            "latency_mean": ok["latency_ms"].mean(),
            "latency_median": ok["latency_ms"].median(),
            "latency_p95": ok["latency_ms"].quantile(0.95),
            "power_mean": g["power_mW"].mean(), "idle_power": g["idle_power_mW"].mean(),
            "slot_s": g["duration_s"].mean(),
        })
    return pd.DataFrame(rows)[list(SUMMARY_TITLES)]


# ------------------------------------------------------------------ plot --
def plot(summary, path):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("  (matplotlib not installed - no plot)")
        return
    protocols = [BLOCK_NAMES[b] for b in (1, 2, 3, 4)]
    modes = [m for m in PS_NAMES.values() if m in set(summary["power_save"])]
    colors = {"Power save off": "#4C72B0", "Modem sleep": "#DD8452"}
    x = np.arange(len(protocols))
    width = 0.8 / max(len(modes), 1)

    fig, (ax_e, ax_l) = plt.subplots(1, 2, figsize=(12, 4.8))
    for k, mode in enumerate(modes):
        s = summary[summary["power_save"] == mode].set_index("protocol").reindex(protocols)
        off = (k - (len(modes) - 1) / 2) * width
        for ax, col, err, fmt in ((ax_e, "energy_mean", "energy_std", lambda v: f"{v:.0f}"),
                                  (ax_l, "latency_median", None,
                                   lambda v: f"{v:.2f}" if v < 1 else f"{v:.1f}")):
            vals = s[col].to_numpy(dtype=float)
            ax.bar(x + off, vals, width, color=colors.get(mode), label=mode,
                   yerr=None if err is None else s[err].to_numpy(dtype=float), capsize=3)
            for xi, v in zip(x + off, vals):
                if np.isfinite(v):
                    ax.text(xi, v, fmt(v), ha="center", va="bottom", fontsize=8)
    ax_e.set_ylabel("Energy per message above idle (µJ)")
    ax_e.set_title("Energy per message (mean ± std)")
    ax_l.set_ylabel("Latency send → done (ms)")
    ax_l.set_title("Latency (median)")
    for ax in (ax_e, ax_l):
        ax.set_xticks(x, protocols)
        ax.grid(axis="y", alpha=0.3)
        ax.legend(fontsize=8)
    fig.suptitle("ESP32-S3 over Wi-Fi: MQTT vs CoAP (PPK2, whole board at 5 V)")
    fig.tight_layout()
    fig.savefig(path, dpi=150)
    print(f"  plot: {path}")


# ------------------------------------------------------------------ main --
def main():
    ap = argparse.ArgumentParser(description="MQTT/CoAP energy per message from a PPK2 CSV.")
    ap.add_argument("csv", help="CSV exported from the Power Profiler app")
    ap.add_argument("--voltage", type=float, default=5.0, help="PPK2 supply voltage (default 5.0)")
    ap.add_argument("--latency-log", default=None,
                    help="serial output of the ESP32 with the LAT lines (exact latencies)")
    ap.add_argument("--config", default=str(CONFIG_FILE), help="energy_config.h that was flashed")
    ap.add_argument("--out", default=str(RESULTS_DIR), help="output folder (default: results/)")
    args = ap.parse_args()

    cfg = read_config(args.config)
    print(f"Reading {args.csv}")
    t, current, lvl, known = load_recording(args.csv)
    print(f"  {len(t):,} samples, {t[-1] - t[0]:.1f} s, "
          f"sample rate ~{1 / np.median(np.diff(t[:100_000])):,.0f} /s")

    slots, dt = find_slots(t, current, lvl, known, cfg)
    df = describe(slots, cfg, args.voltage)
    expected = (cfg.get("N_ROUNDS", 3) * cfg.get("N_PS_MODES", 2) * cfg.get("N_BLOCKS", 5)
                * cfg.get("MSGS_PER_BLOCK", 10))
    print(f"  found {len(df)} message slots (expected {expected}), "
          f"slot length {df['duration_s'].mean() * 1e3:.1f} ms")
    if not df["ok"].all():
        print("  WARNING: D5 (OK) was low in some slots - a message failed or the warm-up failed")
    if args.latency_log:
        df = apply_latency_log(df, args.latency_log)
    else:
        print(f"  latency from D6 (resolution {dt * 1e3:.2f} ms) - use --latency-log for exact values")

    summary = summarize(df)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    df.to_csv(out / "comm_energy_messages.csv", index=False, float_format="%.6g")
    summary.rename(columns=SUMMARY_TITLES).to_csv(out / "comm_energy_results.csv", index=False,
                                                  float_format="%.6g")

    print("\n" + "=" * 104)
    print(f"{'power save':<16}{'protocol':<14}{'msgs':>6}{'failed':>8}{'energy uJ':>12}"
          f"{'± std':>10}{'latency ms':>12}{'p95 ms':>10}{'idle mW':>10}")
    print("-" * 104)
    for _, r in summary.iterrows():
        print(f"{r['power_save']:<16}{r['protocol']:<14}{r['messages']:>6}{r['failed']:>8}"
              f"{r['energy_mean']:>12.1f}{r['energy_std']:>10.1f}{r['latency_median']:>12.2f}"
              f"{r['latency_p95']:>10.2f}{r['idle_power']:>10.1f}")
    print("=" * 104)
    print(f"Results: {out / 'comm_energy_results.csv'}")
    print(f"Every message: {out / 'comm_energy_messages.csv'}")
    plot(summary, out / "comm_energy.png")


if __name__ == "__main__":
    main()
