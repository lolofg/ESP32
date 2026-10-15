#!/usr/bin/env python3
"""
evaluate_all.py - flash and test ALL models on the ESP32, one after another.

For every project folder in esp32/ (01_decision_tree, 02_random_forest, ...):
  1. build + upload it with PlatformIO       (same as the -> button in VS Code)
  2. test it with serial_eval.py             (2000 samples over USB)
At the end it prints a summary table from results/device_results.csv.

Usage (from the Models folder, with the venv active, serial monitor CLOSED):
    python esp32/tools/evaluate_all.py
    python esp32/tools/evaluate_all.py --port /dev/ttyACM0
    python esp32/tools/evaluate_all.py --only 07_mlp 08_cnn1d     # just some models
    python esp32/tools/evaluate_all.py --no-upload                # only test what is on the board
"""
import argparse
import csv
import glob
import shutil
import subprocess
import sys
import time
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent          # esp32/tools
ESP32_DIR = TOOLS_DIR.parent                          # esp32
RESULTS_FILE = ESP32_DIR.parent / "results" / "device_results.csv"


def find_pio():
    """Find PlatformIO's command-line tool (installed by the VS Code extension)."""
    pio = shutil.which("pio") or shutil.which("platformio")
    if pio:
        return pio
    default = Path.home() / ".platformio" / "penv" / "bin" / "pio"
    if default.exists():
        return str(default)
    sys.exit("PlatformIO (pio) not found - is the PlatformIO extension installed in VS Code?")


def find_port():
    """Guess the ESP32's serial port (the first /dev/ttyACM* or /dev/ttyUSB*)."""
    ports = sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*"))
    if not ports:
        sys.exit("No serial port found - is the ESP32 plugged in? (or pass --port)")
    return ports[0]


def print_summary():
    """Print the most important columns of device_results.csv as a table."""
    if not RESULTS_FILE.exists():
        return
    rows = list(csv.DictReader(open(RESULTS_FILE, newline="")))
    print("\n" + "=" * 92)
    print(f"{'project':<24}{'accuracy %':>11}{'agree %':>9}{'inference us':>14}"
          f"{'per request us':>16}{'flash B':>9}{'RAM B':>8}")
    print("-" * 92)
    for r in rows:
        print(f"{r['project']:<24}{float(r['accuracy_pct']):>11.2f}{float(r['agreement_pct']):>9.2f}"
              f"{r.get('inference_us', ''):>14}{float(r['latency_mean_us']):>16.1f}"
              f"{r.get('model_flash_bytes', ''):>9}{r.get('model_ram_bytes', ''):>8}")
    print("=" * 92)
    print(f"full results: {RESULTS_FILE}")


def main():
    ap = argparse.ArgumentParser(description="Flash and test all models on the ESP32.")
    ap.add_argument("--port", default=None, help="serial port, e.g. /dev/ttyACM0 (default: auto)")
    ap.add_argument("--only", nargs="*", default=None, help="only these project folders")
    ap.add_argument("--no-upload", action="store_true", help="skip building/uploading")
    ap.add_argument("--n", type=int, default=None, help="only use the first N test samples")
    args = ap.parse_args()

    port = args.port or find_port()
    pio = None if args.no_upload else find_pio()

    projects = sorted(p for p in ESP32_DIR.glob("0*_*") if (p / "platformio.ini").exists())
    if args.only:
        projects = [p for p in projects if p.name in args.only]
    print(f"port: {port}")
    print(f"projects ({len(projects)}): {', '.join(p.name for p in projects)}\n")

    failed = []
    start_all = time.time()
    for i, project in enumerate(projects, start=1):
        print(f"\n##### [{i}/{len(projects)}] {project.name} #####")

        # 1) build + upload
        if not args.no_upload:
            upload = subprocess.run([pio, "run", "-d", str(project), "-t", "upload",
                                     "--upload-port", port])
            if upload.returncode != 0:
                print(f"!! build/upload of {project.name} failed - skipping it")
                failed.append(project.name)
                continue
            time.sleep(1)                       # give the USB port a moment to come back

        # 2) test with serial_eval.py
        command = [sys.executable, str(TOOLS_DIR / "serial_eval.py"),
                   "--project", str(project), "--port", port]
        if args.n:
            command += ["--n", str(args.n)]
        test = subprocess.run(command)
        if test.returncode != 0:
            print(f"!! test of {project.name} failed")
            failed.append(project.name)

    print(f"\nfinished in {(time.time() - start_all) / 60:.1f} minutes")
    if failed:
        print(f"FAILED: {', '.join(failed)}")
    print_summary()


if __name__ == "__main__":
    main()
