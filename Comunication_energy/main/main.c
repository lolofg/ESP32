// main.c - energy measurement of MQTT and CoAP on the ESP32-S3 (Wi-Fi)
//
// Meant to run powered by the Power Profiler Kit 2 (USB cable unplugged).
// The logic pins tell the PPK2 what happens (see energy_config.h):
//   D0      high during one message slot (SLOT_MS - SLOT_GAP_MS)
//   D1-D3   block: 0 idle, 1 MQTT QoS 0, 2 MQTT QoS 1, 3 CoAP NON, 4 CoAP CON
//   D4      Wi-Fi power save: 0 = off (WIFI_PS_NONE), 1 = modem sleep (WIFI_PS_MIN_MODEM)
//   D5      OK: warm-up passed and no message has failed
//   D6      busy: from "send" until the message is done
//   D7      running: high from the first block until the last one is done
//
// Sequence after power-on:
//   1. print the results of the previous run (kept in flash) - visible over USB
//   2. Wi-Fi, MQTT connect, CoAP setup, warm-up (a few messages per protocol)
//   3. N_ROUNDS x { for each power-save mode: { for each block: idle gap,
//      then MSGS_PER_BLOCK slots with one message each } }
//   4. save the latencies to flash (NVS), print them, idle forever
//
// Every message is the same detection-result JSON (constant length, 102 bytes), so the
// protocols are compared with exactly the same payload.
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mqtt_client.h"

#include "energy_config.h"
#include "wifi.h"
#include "coap_send.h"
#include "secrets.h"

static const char *TAG = "comm";

static const char *const BLOCK_NAMES[N_BLOCKS] = {
    "idle (Wi-Fi connected)", "MQTT QoS 0", "MQTT QoS 1", "CoAP NON", "CoAP CON"};
static const char *const PS_NAMES[N_PS_MODES] = {"power save off", "modem sleep"};
static const wifi_ps_type_t PS_MODES[N_PS_MODES] = {WIFI_PS_NONE, WIFI_PS_MIN_MODEM};

// ------------------------------------------------------------------ pins --
static void pins_init(void)
{
    const gpio_num_t pins[] = {PIN_MARKER, PIN_ID_BIT0, PIN_ID_BIT1, PIN_ID_BIT2,
                               PIN_PS,     PIN_OK,      PIN_BUSY,    PIN_RUNNING};
    gpio_config_t cfg = {0};
    for (int i = 0; i < 8; i++) cfg.pin_bit_mask |= 1ULL << pins[i];
    cfg.mode = GPIO_MODE_OUTPUT;
    gpio_config(&cfg);
    for (int i = 0; i < 8; i++) gpio_set_level(pins[i], 0);
}

static void set_block(int block)
{
    gpio_set_level(PIN_ID_BIT0, (block >> 0) & 1);
    gpio_set_level(PIN_ID_BIT1, (block >> 1) & 1);
    gpio_set_level(PIN_ID_BIT2, (block >> 2) & 1);
}

// ------------------------------------------------------------------ MQTT --
static esp_mqtt_client_handle_t mqtt = NULL;
static volatile bool mqtt_connected = false;
static QueueHandle_t puback_queue = NULL;   // msg_ids confirmed by the broker (QoS 1)

static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        mqtt_connected = false;
        break;
    case MQTT_EVENT_PUBLISHED: {          // PUBACK received (QoS 1)
        int id = event->msg_id;
        xQueueSend(puback_queue, &id, 0);
        break;
    }
    default:
        break;
    }
}

static bool mqtt_start(void)
{
    puback_queue = xQueueCreate(16, sizeof(int));
    esp_mqtt_client_config_t config = {
        .broker.address.uri = MQTT_BROKER_URI,
        .session.keepalive = MQTT_KEEPALIVE_S,
    };
    mqtt = esp_mqtt_client_init(&config);
    esp_mqtt_client_register_event(mqtt, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    esp_mqtt_client_start(mqtt);
    for (int i = 0; i < 150 && !mqtt_connected; i++) vTaskDelay(pdMS_TO_TICKS(100));
    return mqtt_connected;
}

// QoS 0: done when the publish call returns (handed to TCP).
// QoS 1: done when the broker's PUBACK for this message arrives.
static bool mqtt_send(const char *text, int qos, int timeout_ms)
{
    if (!mqtt_connected) return false;
    int id = esp_mqtt_client_publish(mqtt, MQTT_TOPIC, text, 0, qos, 0);
    if (id < 0) return false;
    if (qos == 0) return true;

    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    int got;
    while (true) {
        int64_t left_ms = (deadline - esp_timer_get_time()) / 1000;
        if (left_ms <= 0) return false;
        if (xQueueReceive(puback_queue, &got, pdMS_TO_TICKS(left_ms)) == pdTRUE && got == id)
            return true;
    }
}

// --------------------------------------------------------------- payload --
// Same size every time: numbers padded with spaces to a fixed width (valid JSON).
static void make_payload(char *buf, size_t len, uint32_t seq)
{
    snprintf(buf, len,
             "{\"dev\":\"esp32s3-01\",\"model\":\"DecisionTree\",\"class\":\"attack\","
             "\"score\":0.997,\"seq\":%5" PRIu32 ",\"ts\":%10" PRIu32 "}",
             seq % 100000, (uint32_t)(esp_timer_get_time() / 1000) % 1000000000u);
}

// One message of the given block. Returns true when it was delivered
// (confirmed variants) / handed to the network (unconfirmed variants).
static bool send_message(int block, const char *text)
{
    switch (block) {
    case BLOCK_MQTT_QOS0: return mqtt_send(text, 0, REPLY_TIMEOUT_MS);
    case BLOCK_MQTT_QOS1: return mqtt_send(text, 1, REPLY_TIMEOUT_MS);
    case BLOCK_COAP_NON:  return coap_client_send(text, false, REPLY_TIMEOUT_MS);
    case BLOCK_COAP_CON:  return coap_client_send(text, true, REPLY_TIMEOUT_MS);
    default:              return true;    // idle block: nothing to send
    }
}

// --------------------------------------------------------------- results --
// latency in us from "send" to "done" for every message; -1 = failed
typedef struct {
    uint32_t magic;
    uint32_t payload_bytes;
    int32_t latency_us[N_ROUNDS][N_PS_MODES][N_BLOCKS][MSGS_PER_BLOCK];
} results_t;
#define RESULTS_MAGIC 0xC0E7E001u
static results_t results;

static void print_results(const results_t *r, const char *title)
{
    printf("\n===== %s =====\n", title);
    printf("payload: %" PRIu32 " bytes\n", r->payload_bytes);
    printf("%-16s %-24s %6s %6s %12s %12s %12s\n", "power save", "protocol", "sent", "failed",
           "mean us", "min us", "max us");
    for (int ps = 0; ps < N_PS_MODES; ps++)
        for (int b = 1; b < N_BLOCKS; b++) {
            int n = 0, failed = 0;
            int64_t sum = 0;
            int32_t lo = INT32_MAX, hi = 0;
            for (int round = 0; round < N_ROUNDS; round++)
                for (int m = 0; m < MSGS_PER_BLOCK; m++) {
                    int32_t v = r->latency_us[round][ps][b][m];
                    if (v < 0) { failed++; continue; }
                    n++; sum += v;
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
            printf("%-16s %-24s %6d %6d %12.1f %12" PRId32 " %12" PRId32 "\n", PS_NAMES[ps],
                   BLOCK_NAMES[b], n + failed, failed, n ? (double)sum / n : 0.0,
                   n ? lo : 0, n ? hi : 0);
        }
    // every single message, machine-readable
    for (int round = 0; round < N_ROUNDS; round++)
        for (int ps = 0; ps < N_PS_MODES; ps++)
            for (int b = 1; b < N_BLOCKS; b++)
                for (int m = 0; m < MSGS_PER_BLOCK; m++)
                    printf("LAT,%d,%d,%d,%d,%" PRId32 "\n", round + 1, ps, b, m,
                           r->latency_us[round][ps][b][m]);
    printf("===== end =====\n\n");
}

static void results_save(void)
{
    nvs_handle_t h;
    if (nvs_open("commenergy", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "results", &results, sizeof(results));
    nvs_commit(h);
    nvs_close(h);
}

static void results_print_previous(void)
{
    static results_t prev;
    nvs_handle_t h;
    size_t len = sizeof(prev);
    if (nvs_open("commenergy", NVS_READONLY, &h) != ESP_OK) {
        printf("(no results from a previous run in flash)\n");
        return;
    }
    if (nvs_get_blob(h, "results", &prev, &len) == ESP_OK && len == sizeof(prev) &&
        prev.magic == RESULTS_MAGIC)
        print_results(&prev, "Latencies of the PREVIOUS run (saved in flash)");
    else
        printf("(no results from a previous run in flash)\n");
    nvs_close(h);
}

// ------------------------------------------------------------- one block --
static void wait_until(int64_t t_us)
{
    while (esp_timer_get_time() < t_us) {
        int64_t left_ms = (t_us - esp_timer_get_time()) / 1000;
        vTaskDelay(left_ms > 1 ? pdMS_TO_TICKS(left_ms - 1) : 1);
    }
}

static bool all_ok = true;

static void run_block(int round, int ps, int block, uint32_t *seq)
{
    char payload[160];
    set_block(block);
    vTaskDelay(pdMS_TO_TICKS(BLOCK_GAP_MS));      // idle gap before the block

    int64_t slot_start = esp_timer_get_time();
    for (int m = 0; m < MSGS_PER_BLOCK; m++) {
        make_payload(payload, sizeof(payload), (*seq)++);
        coap_client_poll();                       // nothing left over from earlier

        gpio_set_level(PIN_MARKER, 1);            // ---- slot starts ----
        int32_t latency = 0;
        if (block != BLOCK_IDLE) {
            gpio_set_level(PIN_BUSY, 1);
            int64_t t0 = esp_timer_get_time();
            bool ok = send_message(block, payload);
            int64_t t1 = esp_timer_get_time();
            gpio_set_level(PIN_BUSY, 0);
            latency = ok ? (int32_t)(t1 - t0) : -1;
            if (!ok && all_ok) {
                all_ok = false;
                gpio_set_level(PIN_OK, 0);
            }
        }
        results.latency_us[round][ps][block][m] = latency;

        wait_until(slot_start + (int64_t)(SLOT_MS - SLOT_GAP_MS) * 1000);
        gpio_set_level(PIN_MARKER, 0);            // ---- slot ends ----
        slot_start += (int64_t)SLOT_MS * 1000;
        wait_until(slot_start);
    }
}

// ------------------------------------------------------------------ main --
void app_main(void)
{
    pins_init();
    vTaskDelay(pdMS_TO_TICKS(6000));              // time to plug in USB and open the monitor
    printf("\n=== MQTT / CoAP energy measurement ===\n");

    esp_err_t err = nvs_flash_init();             // (wifi_connect() also does this)
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    results_print_previous();

    // ---- connect everything ----
    wifi_connect();
    bool ok = mqtt_start();
    printf("MQTT %s\n", ok ? "connected" : "NOT connected - is the broker running?");
    bool coap_ok = coap_client_start();
    printf("CoAP %s\n", coap_ok ? "ready" : "NOT ready - check COAP_SERVER_URI");

    // ---- warm-up: a few messages per protocol, with power save off ----
    esp_wifi_set_ps(WIFI_PS_NONE);
    char payload[160];
    uint32_t seq = 0;
    for (int b = 1; b < N_BLOCKS; b++) {
        int good = 0;
        for (int i = 0; i < WARMUP_MSGS; i++) {
            make_payload(payload, sizeof(payload), seq++);
            if (send_message(b, payload)) good++;
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        printf("warm-up %-12s %d/%d OK\n", BLOCK_NAMES[b], good, WARMUP_MSGS);
        if (good < WARMUP_MSGS) ok = false;
    }
    printf("payload (%u bytes): %s\n", (unsigned)strlen(payload), payload);
    results.magic = RESULTS_MAGIC;
    results.payload_bytes = strlen(payload);
    all_ok = ok && coap_ok;
    gpio_set_level(PIN_OK, all_ok);
    printf("Warm-up %s. Measurement starts in %d ms (~%d s in total).\n",
           all_ok ? "PASSED" : "FAILED (D5 stays low)", START_DELAY_MS,
           N_ROUNDS * N_PS_MODES *
               (PS_SETTLE_MS + N_BLOCKS * (BLOCK_GAP_MS + MSGS_PER_BLOCK * SLOT_MS)) / 1000);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(START_DELAY_MS));

    // ---- measurement: no printing in here ----
    esp_log_level_set("*", ESP_LOG_ERROR);        // keep Wi-Fi/MQTT logs quiet as well
    gpio_set_level(PIN_RUNNING, 1);
    for (int round = 0; round < N_ROUNDS; round++)
        for (int ps = 0; ps < N_PS_MODES; ps++) {
            esp_wifi_set_ps(PS_MODES[ps]);
            gpio_set_level(PIN_PS, ps);
            set_block(BLOCK_IDLE);
            vTaskDelay(pdMS_TO_TICKS(PS_SETTLE_MS));
            for (int b = 0; b < N_BLOCKS; b++) run_block(round, ps, b, &seq);
        }
    set_block(BLOCK_IDLE);
    vTaskDelay(pdMS_TO_TICKS(BLOCK_GAP_MS));
    gpio_set_level(PIN_RUNNING, 0);
    esp_log_level_set("*", ESP_LOG_INFO);

    // ---- results ----
    results_save();
    print_results(&results, "Latencies of THIS run");
    printf("%s\nDone. The latencies are also saved in flash and printed at the next boot.\n",
           all_ok ? "All messages OK." : "Some messages FAILED (latency -1).");
    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
