// energy_config.h - settings for the MQTT / CoAP energy measurement
//
// tools/analyze_comm_energy.py reads this file too (block numbers, timing),
// so keep it the same as the firmware that was flashed.
#pragma once
#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// Wiring: ESP32 GPIO -> PPK2 logic port (same as the model energy firmware;
// logic port VCC -> 3V3, GND -> GND)
// ---------------------------------------------------------------------------
#define PIN_MARKER    GPIO_NUM_8   // D0  high during one message slot
#define PIN_ID_BIT0   GPIO_NUM_1   // D1  block number, bit 0
#define PIN_ID_BIT1   GPIO_NUM_2   // D2  block number, bit 1
#define PIN_ID_BIT2   GPIO_NUM_3   // D3  block number, bit 2
#define PIN_PS        GPIO_NUM_4   // D4  high = Wi-Fi power save (modem sleep) on
#define PIN_OK        GPIO_NUM_5   // D5  high = warm-up passed and no message failed so far
#define PIN_BUSY      GPIO_NUM_6   // D6  high from "send" until the message is done
                                   //     (confirmed: reply received; unconfirmed: send returned)
#define PIN_RUNNING   GPIO_NUM_7   // D7  high while the measurement sequence runs

// ---------------------------------------------------------------------------
// Blocks (D1-D3). Every block sends MSGS_PER_BLOCK messages, one per slot.
// ---------------------------------------------------------------------------
#define BLOCK_IDLE       0   // Wi-Fi connected, nothing sent (idle reference)
#define BLOCK_MQTT_QOS0  1   // MQTT publish QoS 0 (no PUBACK)
#define BLOCK_MQTT_QOS1  2   // MQTT publish QoS 1 (wait for the broker's PUBACK)
#define BLOCK_COAP_NON   3   // CoAP POST NON + No-Response option (server sends nothing back)
#define BLOCK_COAP_CON   4   // CoAP POST CON (wait for the server's ACK + 2.04 response)
#define N_BLOCKS         5

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
#define N_ROUNDS          3      // whole sequence repeated (for repeatability)
#define MSGS_PER_BLOCK    10     // messages (slots) per block
#define SLOT_MS           1000   // one message per slot; D0 is high for SLOT_MS - SLOT_GAP_MS
#define SLOT_GAP_MS       10     // D0 low between two slots
#define BLOCK_GAP_MS      1000   // D0 low before every block
#define REPLY_TIMEOUT_MS  800    // max wait for PUBACK / CoAP reply (must be < SLOT_MS)
#define PS_SETTLE_MS      3000   // wait after switching Wi-Fi power save
#define START_DELAY_MS    3000   // after the warm-up, before the first block
#define WARMUP_MSGS       3      // messages per variant before measuring (ARP, TCP, caches)

// Wi-Fi power-save settings measured in every round (D4 = 0 / 1)
//   0: WIFI_PS_NONE       radio always on
//   1: WIFI_PS_MIN_MODEM  modem sleep, wakes up for every DTIM beacon (ESP-IDF default)
#define N_PS_MODES        2

// MQTT keep-alive in seconds: longer than the whole run, so no PINGREQ lands in a slot
#define MQTT_KEEPALIVE_S  1200
