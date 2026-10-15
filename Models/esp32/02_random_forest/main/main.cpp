// main.cpp - benchmark firmware (identical for every model folder)
//
// 1) Self-test: runs the model on the test samples embedded in test_data.h and
//    prints accuracy, agreement with the Python model, latency, memory.
// 2) Serial mode: afterwards it waits for lines of comma-separated RAW features
//    on the USB serial port and answers with  PRED,<class>,<score>,<latency_us>
//    (used by esp32/tools/serial_eval.py to test the full test set).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_cpu.h"
#include "driver/uart.h"
#include "soc/soc_caps.h"
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include "driver/usb_serial_jtag.h"  // native USB port on ESP32-S3/C3
#endif
#include "sdkconfig.h"

#include "model_api.h"
#include "preprocess.h"
#include "model_info.h"   // MODEL_NAME, MODEL_N_CLASSES, MODEL_CLASS_NAMES (generated)
#include "test_data.h"    // N_TEST, TEST_X, TEST_Y, TEST_PY_PRED (generated)

static_assert(N_FEATURES_MODEL == N_FEATURES, "model and preprocessing disagree on #features");

#define CPU_MHZ CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ
#define UART_PORT UART_NUM_0
#define SERIAL_LINE_MAX 1024

static inline uint32_t cycles_now(void) { return (uint32_t)esp_cpu_get_cycle_count(); }

static void run_self_test(size_t heap_before_init, size_t heap_after_init, int64_t init_us) {
    static uint16_t confusion[MODEL_N_CLASSES][MODEL_N_CLASSES];
    memset(confusion, 0, sizeof(confusion));
    float x[N_FEATURES];
    int correct = 0, agree = 0;
    uint64_t cyc_pre_total = 0, cyc_inf_total = 0;
    uint32_t cyc_inf_max = 0, cyc_inf_min = UINT32_MAX;

    for (int i = 0; i < N_TEST; i++) {
        float score = 0.0f;
        uint32_t c0 = cycles_now();
        preprocess(TEST_X[i], x);
        uint32_t c1 = cycles_now();
        int pred = model_predict(x, &score);
        uint32_t c2 = cycles_now();

        uint32_t inf = c2 - c1;
        cyc_pre_total += (c1 - c0);
        cyc_inf_total += inf;
        if (inf > cyc_inf_max) cyc_inf_max = inf;
        if (inf < cyc_inf_min) cyc_inf_min = inf;

        if (pred == TEST_Y[i]) correct++;
        if (pred == TEST_PY_PRED[i]) agree++;
        if (pred >= 0 && pred < MODEL_N_CLASSES) confusion[TEST_Y[i]][pred]++;
        if ((i & 63) == 0) vTaskDelay(1);  // keep the watchdog happy
    }

    double acc = 100.0 * correct / N_TEST;
    double agr = 100.0 * agree / N_TEST;
    double pre_us = (double)cyc_pre_total / N_TEST / CPU_MHZ;
    double inf_us = (double)cyc_inf_total / N_TEST / CPU_MHZ;

    printf("\n================ %s ================\n", model_name());
    printf("Test samples            : %d\n", N_TEST);
    printf("Accuracy (vs. labels)   : %.2f %%\n", acc);
    printf("Agreement with Python   : %.2f %%  (should be ~100%%)\n", agr);
    printf("Preprocessing / sample  : %.2f us\n", pre_us);
    printf("Inference / sample      : %.2f us (min %.2f, max %.2f)\n", inf_us,
           (double)cyc_inf_min / CPU_MHZ, (double)cyc_inf_max / CPU_MHZ);
    printf("Model init time         : %lld us\n", (long long)init_us);
    printf("Model parameters (flash): %u bytes\n", (unsigned)model_flash_bytes());
    printf("Model RAM (arena)       : %u bytes\n", (unsigned)model_arena_bytes());
    printf("Heap used by init       : %d bytes\n", (int)heap_before_init - (int)heap_after_init);
    printf("Free heap now / minimum : %u / %u bytes\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    printf("CPU clock               : %d MHz\n", CPU_MHZ);

    printf("\nConfusion matrix (rows = true, cols = predicted)\n");
    for (int r = 0; r < MODEL_N_CLASSES; r++) {
        printf("%-12.12s", MODEL_CLASS_NAMES[r]);
        for (int c = 0; c < MODEL_N_CLASSES; c++) printf(" %5u", confusion[r][c]);
        printf("\n");
    }
    // one machine-readable line (parsed by tools/serial_eval.py)
    printf("\nRESULT,%s,%d,%.3f,%.3f,%.3f,%.3f,%u,%u,%d,%u\n", model_name(), N_TEST, acc, agr,
           pre_us, inf_us, (unsigned)model_flash_bytes(), (unsigned)model_arena_bytes(),
           (int)heap_before_init - (int)heap_after_init,
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
    printf("==========================================\n");
}

// Parse "v1,v2,...,vN" into raw[]. Returns number of values parsed.
static int parse_csv_line(char *line, float *raw) {
    int n = 0;
    char *p = line;
    while (*p && n < N_FEATURES) {
        char *end;
        float v = strtof(p, &end);
        if (end == p) break;
        raw[n++] = v;
        p = end;
        while (*p == ',' || *p == ' ' || *p == ';' || *p == '\t') p++;
    }
    return n;
}

static void handle_line(char *line) {
    float raw[N_FEATURES], x[N_FEATURES];
    if (line[0] == '\0' || line[0] == '#') return;
    if (strcmp(line, "TEST") == 0) { run_self_test(0, 0, 0); return; }
    int n = parse_csv_line(line, raw);
    if (n != N_FEATURES) {
        printf("ERR,expected %d values got %d\n", N_FEATURES, n);
        return;
    }
    float score = 0.0f;
    uint32_t c0 = cycles_now();
    preprocess(raw, x);
    int pred = model_predict(x, &score);
    uint32_t c1 = cycles_now();
    printf("PRED,%d,%.6f,%.2f\n", pred, score, (double)(c1 - c0) / CPU_MHZ);
}

static void serial_loop(void) {
    uart_driver_install(UART_PORT, 2 * SERIAL_LINE_MAX, 0, 0, NULL, 0);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
    // also listen on the native USB port (ESP32-S3 boards with a second USB connector)
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&usb_cfg);
#endif
    printf("READY: send %d comma-separated raw features per line\n", N_FEATURES);
    static char line[SERIAL_LINE_MAX];
    int len = 0;
    uint8_t buf[256];
    while (true) {
        // take whatever has arrived on either port, without waiting
        int got = uart_read_bytes(UART_PORT, buf, sizeof(buf), 0);
#if SOC_USB_SERIAL_JTAG_SUPPORTED
        if (got <= 0) got = usb_serial_jtag_read_bytes(buf, sizeof(buf), 0);
#endif
        if (got <= 0) {
            vTaskDelay(1);  // nothing yet: sleep one tick (also keeps the watchdog happy)
            continue;
        }
        for (int i = 0; i < got; i++) {
            char ch = (char)buf[i];
            if (ch == '\r') continue;
            if (ch != '\n') {
                if (len < SERIAL_LINE_MAX - 1) line[len++] = ch;
                continue;
            }
            line[len] = '\0';
            len = 0;
            handle_line(line);
        }
    }
}

extern "C" void app_main(void) {
    vTaskDelay(pdMS_TO_TICKS(2000));  // time to open the serial monitor
    size_t heap0 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    int64_t t0 = esp_timer_get_time();
    bool ok = model_init();
    int64_t init_us = esp_timer_get_time() - t0;
    size_t heap1 = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    if (!ok) {
        printf("model_init() FAILED - see messages above\n");
        while (true) vTaskDelay(pdMS_TO_TICKS(1000));
    }
    run_self_test(heap0, heap1, init_us);
    serial_loop();
}
