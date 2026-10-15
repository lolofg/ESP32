#!/usr/bin/env python3
"""
serial_eval.py - test a flashed model on the ESP32 with the larger test set.

The firmware in every project prints a self-test report after reset and then
waits for lines of comma-separated RAW features on the serial port. This script

  1. resets the board and captures the self-test RESULT line,
  2. sends every row of <project>/test_set.csv and reads back the prediction,
  3. prints accuracy, agreement with the Python model and on-device latency,
  4. saves one row per project in results/device_results.csv (a re-test replaces the old row).

Usage (close the PlatformIO serial monitor first):
    python esp32/tools/serial_eval.py --project esp32/07_mlp --port /dev/ttyUSB0
    python esp32/tools/serial_eval.py --project esp32/02_random_forest --port COM5 --n 500

Requires:  pip install pyserial
"""
import argparse
import csv
import statistics
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing:  pip install pyserial")


def read_line(ser, timeout):
    end = time.time() + timeout
    while time.time() < end:
        raw = ser.readline()
        if raw:
            return raw.decode(errors="replace").strip()
    return None


# columns of the RESULT line printed by the board's self-test (see main.cpp)
SELFTEST_COLUMNS = ["model", "selftest_samples", "selftest_accuracy_pct", "selftest_agreement_pct",
                    "preprocess_us", "inference_us", "model_flash_bytes", "model_ram_bytes",
                    "heap_used_by_init_bytes", "min_free_heap_bytes"]


def parse_result_line(line):
    """'RESULT,DecisionTree,400,100.000,...' -> {'model': 'DecisionTree', 'selftest_samples': '400', ...}"""
    if not line or not line.startswith("RESULT,"):
        return {col: "" for col in SELFTEST_COLUMNS}
    values = line.split(",")[1:]
    return dict(zip(SELFTEST_COLUMNS, values))


def save_row(path, row):
    """Write one row per project: a new test of the same project replaces its old row."""
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = []
    if path.exists():
        with open(path, newline="") as f:
            rows = [r for r in csv.DictReader(f) if r.get("project") != row["project"]]
    rows.append(row)
    rows.sort(key=lambda r: r["project"])
    with open(path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(row.keys()))
        writer.writeheader()
        writer.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--project", required=True, help="project folder, e.g. esp32/07_mlp")
    ap.add_argument("--port", required=True, help="serial port, e.g. /dev/ttyUSB0 or COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--n", type=int, default=None, help="only use the first N rows")
    ap.add_argument("--no-reset", action="store_true", help="don't reset the board first")
    ap.add_argument("--out", default=None, help="CSV to append results to (default: results/device_results.csv)")
    args = ap.parse_args()

    project = Path(args.project)
    rows = list(csv.reader(open(project / "test_set.csv")))
    header, rows = rows[0], rows[1:]
    if args.n:
        rows = rows[: args.n]
    n_feat = len(header) - 2  # last two columns: label, python_pred

    ser = serial.Serial(args.port, args.baud, timeout=0.5)
    result_line = None
    if not args.no_reset:  # EN/RST via the RTS line (works on most DevKits)
        ser.dtr = False
        ser.rts = True
        time.sleep(0.2)
        ser.rts = False
    print("waiting for the board (self-test runs first) ...")
    t_end = time.time() + 60
    while time.time() < t_end:
        line = read_line(ser, 1)
        if not line:
            if args.no_reset:
                break
            continue
        print("  |", line)
        if line.startswith("RESULT,"):
            result_line = line
        if line.startswith("READY"):
            break
    else:
        sys.exit("no READY line from the board - is the right project flashed and the monitor closed?")

    preds, lat, errors = [], [], 0
    t0 = time.time()
    for i, r in enumerate(rows):
        ser.write((",".join(r[:n_feat]) + "\n").encode())
        while True:
            line = read_line(ser, 5)
            if line is None:
                sys.exit(f"timeout waiting for an answer to row {i}")
            if line.startswith("PRED,"):
                _, p, score, us = line.split(",")
                preds.append(int(p))
                lat.append(float(us))
                break
            if line.startswith("ERR"):
                errors += 1
                preds.append(-1)
                lat.append(float("nan"))
                break
        if (i + 1) % 200 == 0:
            print(f"  {i + 1}/{len(rows)}")
    ser.close()

    labels = [int(r[n_feat]) for r in rows]
    py = [int(r[n_feat + 1]) for r in rows]
    acc = 100 * sum(p == y for p, y in zip(preds, labels)) / len(rows)
    agr = 100 * sum(p == q for p, q in zip(preds, py)) / len(rows)
    lat_ok = [x for x in lat if x == x]
    print(f"\n{project.name}: {len(rows)} samples in {time.time() - t0:.1f}s")
    print(f"  accuracy on device      : {acc:.2f} %")
    print(f"  agreement with Python   : {agr:.2f} %")
    print(f"  latency (preprocess+inference) mean {statistics.mean(lat_ok):.2f} us, "
          f"median {statistics.median(lat_ok):.2f} us, max {max(lat_ok):.2f} us")
    if errors:
        print(f"  {errors} rows were rejected by the board")

    out = Path(args.out) if args.out else project.parent.parent / "results" / "device_results.csv"
    row = {
        "time": datetime.now().isoformat(timespec="seconds"),
        "project": project.name,
        # --- serial test: samples sent from the PC one by one ---
        "samples": len(rows),
        "accuracy_pct": round(acc, 3),
        "agreement_pct": round(agr, 3),
        "latency_mean_us": round(statistics.mean(lat_ok), 3),      # preprocessing + inference
        "latency_median_us": round(statistics.median(lat_ok), 3),
        "latency_max_us": round(max(lat_ok), 3),
    }
    row.update(parse_result_line(result_line))
    save_row(out, row)
    print(f"  saved to {out}")


if __name__ == "__main__":
    main()
