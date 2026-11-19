// main.cpp - energy measurement firmware (all models in one build)
//
// Meant to run with the ESP32 powered by the Power Profiler Kit 2 (USB cable
// unplugged). The PPK2 records the current; the logic pins below tell it what
// the board is doing at every moment:
//
//   D0  marker     high while a block runs (the part that is measured)
//   D1-D4 block    0 = busy-loop baseline, 1-11 = model, 12 = preprocessing only
//   D5  test OK    high when the self-test passed and every block gave the
//                  expected predictions
//   D6  pipeline   0 = model only (input already preprocessed),
//                  1 = preprocessing + model (raw features in)
//   D7  running    high from the first block until the last one is done
//
// Sequence after power-on:
//   1. init all models + self-test (400 embedded samples, compared with Python)
//   2. wait START_DELAY_MS
//   3. N_ROUNDS times: for every block -> set D1-D4/D6, idle IDLE_MS (CPU
//      waiting), marker high, run 400 x PASSES inferences, marker low
//   4. print a timing summary (only visible if the USB cable is connected) and
//      stay idle
//
// tools/analyze_energy.py turns the PPK2 export into energy per inference.
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "sdkconfig.h"

#include "energy_config.h"
#include "energy_models.h"
// preprocessing and the 400 raw test samples are the same in every project,
// so they are taken from the first one
#include "../../01_decision_tree/main/preprocess.h"
#include "../../01_decision_tree/main/test_data.h"  // N_TEST, TEST_X, TEST_Y

#define CPU_MHZ CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ

// ------------------------------------------------------------------ models --
struct ModelPlan {
    const EnergyModel *m;
    uint32_t passes_model;      // passes over the 400 samples, model only
    uint32_t passes_pipeline;   // passes over the 400 samples, preprocessing + model
};

#define PLAN_ENTRY(BLOCK, PROJECT, PASSES_MODEL, PASSES_PIPE) \
    {&ENERGY_MODEL_##BLOCK, PASSES_MODEL, PASSES_PIPE},
static const ModelPlan PLAN[] = {ENERGY_MODELS(PLAN_ENTRY)};
static const int N_MODELS = sizeof(PLAN) / sizeof(PLAN[0]);

static float X_PRE[N_TEST][N_FEATURES];   // test samples after preprocessing (RAM)
static int32_t SELFTEST_SUM[16];          // sum of the device predictions per model (400 samples)
static bool all_ok = true;

// keeps the compiler from removing the loops (results are "used")
static volatile int32_t g_sink_i;
static volatile float g_sink_f;

// ------------------------------------------------------------------- pins --
static void pins_init(void) {
    const gpio_num_t pins[] = {PIN_MARKER,  PIN_ID_BIT0, PIN_ID_BIT1,  PIN_ID_BIT2,
                               PIN_ID_BIT3, PIN_TEST_OK, PIN_PIPELINE, PIN_RUNNING};
    uint64_t mask = 0;
    for (gpio_num_t p : pins) mask |= 1ULL << p;
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = mask;
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    for (gpio_num_t p : pins) gpio_set_level(p, 0);
}

static void set_block(int block, bool pipeline) {
    gpio_set_level(PIN_ID_BIT0, (block >> 0) & 1);
    gpio_set_level(PIN_ID_BIT1, (block >> 1) & 1);
    gpio_set_level(PIN_ID_BIT2, (block >> 2) & 1);
    gpio_set_level(PIN_ID_BIT3, (block >> 3) & 1);
    gpio_set_level(PIN_PIPELINE, pipeline ? 1 : 0);
}

static void fail(const char *what) {
    if (all_ok) printf("CHECK FAILED: %s -> D5 (test OK) goes low\n", what);
    all_ok = false;
    gpio_set_level(PIN_TEST_OK, 0);
}

// -------------------------------------------------------------- workloads --
// The model is called through a function pointer, like main.cpp in the model
// projects calls model_predict() in another file (no inlining in both cases).
static int32_t run_model_only(const EnergyModel *m, uint32_t passes) {
    int32_t sum = 0;
    float score;
    for (uint32_t p = 0; p < passes; p++)
        for (int i = 0; i < N_TEST; i++) sum += m->predict(X_PRE[i], &score);
    return sum;
}

static int32_t run_pipeline(const EnergyModel *m, uint32_t passes) {
    int32_t sum = 0;
    float x[N_FEATURES], score;
    for (uint32_t p = 0; p < passes; p++)
        for (int i = 0; i < N_TEST; i++) {
            preprocess(TEST_X[i], x);
            sum += m->predict(x, &score);
        }
    return sum;
}

static float run_preprocess_only(uint32_t passes) {
    float acc = 0.0f, x[N_FEATURES];
    for (uint32_t p = 0; p < passes; p++)
        for (int i = 0; i < N_TEST; i++) {
            preprocess(TEST_X[i], x);
            for (int f = 0; f < N_FEATURES; f++) acc += x[f];
        }
    return acc;
}

static void run_busy(uint32_t ms) {
    const int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    while (esp_timer_get_time() < end) {
    }
}

// --------------------------------------------------------------- self-test --
static void self_test(void) {
    for (int i = 0; i < N_TEST; i++) preprocess(TEST_X[i], X_PRE[i]);

    printf("\n%-24s %-20s %8s %8s %10s %8s\n", "project", "model", "acc %", "agree %",
           "flash B", "arena B");
    for (int k = 0; k < N_MODELS; k++) {
        const EnergyModel *m = PLAN[k].m;
        int correct = 0, agree = 0;
        int32_t sum = 0;
        for (int i = 0; i < N_TEST; i++) {
            float score;
            int pred = m->predict(X_PRE[i], &score);
            sum += pred;
            if (pred == TEST_Y[i]) correct++;
            if (pred == m->py_pred[i]) agree++;
        }
        SELFTEST_SUM[m->block] = sum;
        double agr = 100.0 * agree / N_TEST;
        printf("%-24s %-20s %8.2f %8.2f %10u %8u\n", m->project, m->name(),
               100.0 * correct / N_TEST, agr, (unsigned)m->flash_bytes(),
               (unsigned)m->arena_bytes());
        if (agr < MIN_AGREEMENT_PCT) fail("self-test agreement with Python too low");
        vTaskDelay(1);
    }
}

// -------------------------------------------------------------- one block --
struct BlockResult {
    int block;
    bool pipeline;
    uint32_t n_inferences;   // 0 for the baseline
    int64_t us;              // measured length of the block
};

static BlockResult run_block(int block, bool pipeline, const ModelPlan *plan) {
    BlockResult r = {block, pipeline, 0, 0};
    set_block(block, pipeline);
    vTaskDelay(pdMS_TO_TICKS(IDLE_MS));   // idle gap (also the idle reference)

    int32_t expected = 0, got = 0;
    gpio_set_level(PIN_MARKER, 1);
    const int64_t t0 = esp_timer_get_time();
    if (block == BLOCK_BASELINE) {
        run_busy(BASELINE_MS);
    } else if (block == BLOCK_PREPROCESS) {
        g_sink_f = run_preprocess_only(PASSES_PREPROCESS);
        r.n_inferences = PASSES_PREPROCESS * N_TEST;
    } else {
        const uint32_t passes = pipeline ? plan->passes_pipeline : plan->passes_model;
        got = pipeline ? run_pipeline(plan->m, passes) : run_model_only(plan->m, passes);
        expected = (int32_t)passes * SELFTEST_SUM[block];
        r.n_inferences = passes * N_TEST;
    }
    r.us = esp_timer_get_time() - t0;
    gpio_set_level(PIN_MARKER, 0);

    g_sink_i = got;
    if (got != expected) fail("a block gave different predictions than the self-test");
    return r;
}

// -------------------------------------------------------------------- main --
#define MAX_BLOCKS (2 + 2 * 11)
static BlockResult results[N_ROUNDS][MAX_BLOCKS];

extern "C" void app_main(void) {
    pins_init();
    vTaskDelay(pdMS_TO_TICKS(3000));   // time for the serial monitor to reconnect after flashing
    printf("\n=== Energy measurement firmware: %d models, %d rounds ===\n", N_MODELS, N_ROUNDS);

    for (int k = 0; k < N_MODELS; k++) {
        if (!PLAN[k].m->init()) {
            printf("model_init() failed for %s\n", PLAN[k].m->project);
            fail("model init");
        }
    }
    self_test();
    if (all_ok) gpio_set_level(PIN_TEST_OK, 1);
    printf("Self-test %s. Free heap: %u bytes. Measurement starts in %d ms.\n",
           all_ok ? "PASSED" : "FAILED", (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
           START_DELAY_MS);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(START_DELAY_MS));

    // ---- measurement (no printing in here) ----
    int n_blocks = 0;
    gpio_set_level(PIN_RUNNING, 1);
    for (int round = 0; round < N_ROUNDS; round++) {
        int b = 0;
        results[round][b++] = run_block(BLOCK_BASELINE, false, nullptr);
        results[round][b++] = run_block(BLOCK_PREPROCESS, false, nullptr);
        for (int k = 0; k < N_MODELS; k++)
            results[round][b++] = run_block(PLAN[k].m->block, false, &PLAN[k]);
#if RUN_PIPELINE
        for (int k = 0; k < N_MODELS; k++)
            results[round][b++] = run_block(PLAN[k].m->block, true, &PLAN[k]);
#endif
        n_blocks = b;
    }
    set_block(0, false);
    vTaskDelay(pdMS_TO_TICKS(IDLE_MS));   // closing idle gap
    gpio_set_level(PIN_RUNNING, 0);

    // ---- summary (only seen when the USB cable is connected) ----
    printf("\nBLOCK SUMMARY (time per block, averaged over %d rounds)\n", N_ROUNDS);
    printf("%5s %-24s %8s %12s %12s %14s\n", "block", "name", "pipeline", "inferences",
           "block ms", "us/inference");
    for (int b = 0; b < n_blocks; b++) {
        const BlockResult &r = results[0][b];
        double us = 0;
        for (int round = 0; round < N_ROUNDS; round++) us += (double)results[round][b].us;
        us /= N_ROUNDS;
        const char *name = r.block == BLOCK_BASELINE     ? "baseline (busy loop)"
                           : r.block == BLOCK_PREPROCESS ? "preprocessing only"
                                                         : "";
        for (int k = 0; k < N_MODELS; k++)
            if (PLAN[k].m->block == r.block) name = PLAN[k].m->project;
        printf("%5d %-24s %8d %12u %12.1f %14.3f\n", r.block, name, r.pipeline ? 1 : 0,
               (unsigned)r.n_inferences, us / 1000.0,
               r.n_inferences ? us / r.n_inferences : 0.0);
        // machine-readable copy
        printf("BLOCK,%d,%s,%d,%u,%.1f\n", r.block, name, r.pipeline ? 1 : 0,
               (unsigned)r.n_inferences, us);
    }
    printf("Checks: %s\nDone - power-cycle the board to measure again.\n",
           all_ok ? "all OK" : "FAILED (see above)");

    while (true) vTaskDelay(pdMS_TO_TICKS(1000));
}
