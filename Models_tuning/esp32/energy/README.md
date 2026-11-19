# energy – energy per inference with the Power Profiler Kit 2

This project is one firmware that contains all 11 tuned models. It runs them one
after another in measured blocks and uses GPIO pins to tell the PPK2 which block
is running. `tools/analyze_energy.py` then turns the PPK2 recording into energy
per inference for every model.

The model code is not copied. Each `main/m*.cpp` includes `../<project>/main/model.cpp`
inside its own namespace. When you re-run the notebook, rebuild this project and
it picks up the new models. Keep this folder inside `esp32/`, next to the model projects.

## Wiring

| ESP32-S3 | PPK2 | Meaning |
|---|---|---|
| 5V pin | VOUT | supply (PPK2 in **Source meter** mode, 5000 mV) |
| GND | GND | ground |
| GPIO8 | D0 | **marker**: high while a block is running (not GPIO0, see Notes) |
| GPIO1–GPIO4 | D1–D4 | block number (binary, D1 = bit 0) |
| GPIO5 | D5 | self-test passed and every block gave the expected predictions |
| GPIO6 | D6 | 0 = model only, 1 = preprocessing + model |
| GPIO7 | D7 | high while the whole sequence runs |
| 3V3 | logic port **VCC** | reference voltage for D0–D7 |
| GND | logic port **GND** | |

Block numbers: 0 = busy-loop baseline (no model), 1–11 = models in folder order
(1 = 01_decision_tree … 11 = 09_autoencoder), 12 = preprocessing only.

The **USB cable must be unplugged while you measure**. Otherwise the board takes
its power from USB and the PPK2 reading is wrong.

## What the firmware does after power-on

1. It initialises all 11 models and runs the self-test (400 samples, compared with
   Python). If everything passes, D5 goes high.
2. It waits 3 s.
3. It runs 3 rounds. In each round, every block gets 1 s of idle with the CPU waiting
   (this is the idle reference), then the marker goes high and the block runs for about 1 s:
   - the baseline busy loop
   - preprocessing only
   - the 11 models on preprocessed input
   - the 11 models on raw input (preprocessing + model)
4. It stops. A full run takes about 2.5 minutes.

Each model block runs the 400 test samples *PASSES* times. The pass counts in
`main/energy_config.h` are chosen so that every block lasts about 1 s. The analysis
script reads the same file, so it knows how many inferences each block contains.

## How to measure

1. **Build and flash** with the USB cable connected and the PPK2 output OFF:
   `pio run -t upload`. This is the arrow button in VS Code.
2. *Optional check:* run `pio device monitor`. You should see the self-test table, and
   after about 2.5 minutes a `BLOCK SUMMARY` with the length of every block.
   All blocks should be roughly 0.5–2 s.
3. **Unplug the USB cable.**
4. In the Power Profiler app:
   - choose Source meter and set the supply to 5000 mV
   - turn on the digital channels in the chart options so you can watch D0–D7
   - sampling at 100 kS/s works; 10 kS/s is enough because the blocks are about 1 s long, and it keeps the file small
   - press **Start** to record, then switch **Enable power output** ON
5. Wait until D7 goes low (about 2.5 min), then stop the recording.
6. Click **Export**, select the whole recording, and tick *Timestamp*, *Current* and
   *Digital logic pins (single string field)*. Save it, for example as
   `results/ppk2_recording.csv`.
7. Analyse it (from `Models_tuning`, with the venv active):

   ```
   python esp32/tools/analyze_energy.py results/ppk2_recording.csv --voltage 5.0
   ```

   This writes `results/energy_results.csv` (one row per model and input type,
   averaged over the 3 rounds), `results/energy_windows.csv` (every block) and
   `results/energy_per_inference.png`.

## Reading the results

- **Energy per inference – total**: average power during the block × time per
  inference. This is the whole board at the 5 V input, including the regulator and LEDs.
- **above idle**: only the extra energy compared with the CPU waiting in `vTaskDelay`.
- **above busy-loop baseline**: the extra energy compared with the CPU spinning
  without doing useful work. Expect this to be small: at 240 MHz the power is almost the
  same whatever the CPU computes, so the energy mostly follows the inference time.
- **at the 3.3 V rail – estimate**: current × 3.3 V × time. The board's regulator is
  linear (current in ≈ current out), so this estimates what the ESP32 module itself uses.

## Notes

- D0 is on GPIO8, not GPIO0. GPIO0 is a boot pin: the PPK2 logic input holds it low
  at reset, and the board then starts in download mode ("waiting for download") instead
  of running the firmware.
- If a tuned model gets much faster or slower, change its pass counts in
  `energy_config.h` so its blocks stay about 0.5–2 s. Then reflash. The script uses
  the new numbers automatically.
- TFLite Micro is pinned to 1.4.1 (`main/idf_component.yml`), the same version
  the single-model projects were built with.
- `evaluate_all.py` ignores this folder, because it only looks at folders starting with `0`.
