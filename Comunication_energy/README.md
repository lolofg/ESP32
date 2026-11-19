# Comunication_energy – energy and latency of MQTT and CoAP

This firmware sends the same detection-result message (102 bytes of JSON) with four
protocol variants, once with Wi-Fi power saving off and once with modem sleep, and
marks every message on the PPK2 logic pins. `tools/analyze_comm_energy.py` turns
the PPK2 recording into **energy per message** and **latency per message**.

| Block (D1–D3) | Variant | What "done" means |
|---|---|---|
| 0 | Idle, Wi-Fi connected | nothing is sent (the idle reference) |
| 1 | MQTT QoS 0 | the publish call returned (handed to TCP) |
| 2 | MQTT QoS 1 | the broker's PUBACK arrived |
| 3 | CoAP NON + No-Response | `coap_send()` returned (the server sends nothing back) |
| 4 | CoAP CON | the server's ACK with the 2.04 response arrived |

CoAP NON uses the No-Response option (RFC 7967, value 2: "no 2.xx answer"), so it is
really "send and forget", like MQTT QoS 0. Without it, the server would still send a
NON response.

## Wiring

This is the same as for the model energy measurement:

| ESP32-S3 | PPK2 | Meaning |
|---|---|---|
| 5V / GND | VOUT / GND | supply (Source meter, 5000 mV) |
| GPIO8 | D0 | one message slot (high ~990 ms) |
| GPIO1–GPIO3 | D1–D3 | block number (table above) |
| GPIO4 | D4 | Wi-Fi power save: 0 = off, 1 = modem sleep |
| GPIO5 | D5 | OK: warm-up passed, no message failed |
| GPIO6 | D6 | busy: from send until done |
| GPIO7 | D7 | sequence running |
| 3V3 / GND | logic VCC / GND | reference for D0–D7 |

**Wi-Fi draws short current peaks of several hundred mA.** Connect the PPK2's second
USB cable (the "USB POWER" port) as well, so the PPK2 can supply them. If it can't,
the ESP32 may reset (brown-out), and you would see the sequence start again in the
recording.

## What happens after power-on (about 6.5 minutes)

1. After 6 s, the results of the **previous** run are printed. They're kept in flash, so
   you can read them over USB later.
2. The board connects to Wi-Fi, MQTT and CoAP, then sends 3 warm-up messages per
   variant. If all of them succeed, D5 goes high.
3. It runs 3 rounds. In each round, for each power-save mode, it waits 3 s, then runs
   the 5 blocks. Each block has 1 s of idle followed by 10 messages, one per 1 s slot.
4. The latencies are saved in flash and printed, then the board stays idle.

## How to measure

1. **On the PC:**
   - Start the broker: `docker compose up -d` in `docker/mqtt_broker`.
   - Start the CoAP server: `.venv/bin/python coap_server.py` in `Comunication_testing`.
   - UDP 5683 must be open in ufw. You already did that.
2. **Flash** with USB connected and the PPK2 output off:
   `pio run -t upload --upload-port /dev/ttyACMx`
3. **Check (optional):** unplug and replug USB, then open `pio device monitor --port …`.
   It should show `warm-up … 3/3 OK` for all four variants and `Warm-up PASSED`.
   Close the monitor before step 4.
4. **Unplug USB.** In the Power Profiler app, set **10 kS/s**, Source meter at 5000 mV.
   Press **Start**, then turn on **Enable power output**.
5. Wait until **D7 goes low** (about 6.5 min after power-on), then stop and export:
   Timestamp, Current, and "Digital logic pins (single string field)".
   Save as `results/comm_recording.csv`.
6. **Exact latencies (optional, recommended):** turn the PPK2 output off. Type the
   monitor command, plug in USB, and press Enter right away. The board waits 6 s after
   power-on before it prints, so there's time:
   ```
   pio device monitor --port /dev/ttyACMx | tee results/serial_output.txt
   ```
   It first prints the latencies of the previous run (`LAT,...` lines), which is the one
   you just recorded. Stop the monitor (Ctrl+C) once `===== end =====` has appeared.
   The board then starts a new run on USB power; just unplug it. Don't press RST: on the
   native USB port that disconnects the monitor.
7. **Analyse:**
   ```
   python tools/analyze_comm_energy.py results/comm_recording.csv --latency-log results/serial_output.txt
   ```
   You can leave out `--latency-log`. The latency then comes from D6, which only has a
   resolution of 0.1 ms at 10 kS/s, so QoS 0 and NON show about 0 ms.

The script writes `results/comm_energy_results.csv` (one row per power-save mode and
protocol), `results/comm_energy_messages.csv` (every message) and `results/comm_energy.png`.

To look at the recording like in the app, use the model project's plotting script
with `--no-labels`:
```
python ../Models_tuning/esp32/tools/plot_recording.py results/comm_recording.csv --start 60 --end 75 --no-labels
```

## How the energy is calculated

For each slot (D0 high), the script works out the average power and the slot length.
From that it subtracts the average power of the idle slots (block 0) in the same round
and power-save mode:

    energy per message = (P_slot − P_idle) × T_slot

This counts everything the message causes: sending, the broker's or server's answer,
TCP ACKs, and the radio staying awake afterwards. It doesn't count the board's normal
"Wi-Fi connected" power. The 1 s slot is long enough for the radio to settle again,
even with modem sleep, where the board only wakes up at DTIM beacons (about every
100–300 ms).

## Settings

All the settings are in `main/energy_config.h`: number of rounds, messages per block,
slot length, reply timeout (800 ms), and the MQTT keep-alive (1200 s, longer than the
run, so no ping lands in a slot). The analysis script reads the same file.
