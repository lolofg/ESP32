// energy_config.h - settings for the energy measurement firmware
//
// tools/analyze_energy.py reads THIS file too (the pass counts and the block
// numbers), so the firmware and the analysis always agree. Edit it here only.
#pragma once
#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// Wiring: ESP32 GPIO -> PPK2 logic port (also connect the logic port's VCC to
// the ESP32's 3V3 pin and its GND to GND)
// ---------------------------------------------------------------------------
#define PIN_MARKER    GPIO_NUM_8   // D0  high while a measured block is running
                                   //     (not GPIO0: a boot pin - the PPK2 input holds it
                                   //      low at reset and the board starts in download mode)
#define PIN_ID_BIT0   GPIO_NUM_1   // D1  block number, bit 0
#define PIN_ID_BIT1   GPIO_NUM_2   // D2  block number, bit 1
#define PIN_ID_BIT2   GPIO_NUM_3   // D3  block number, bit 2
#define PIN_ID_BIT3   GPIO_NUM_4   // D4  block number, bit 3
#define PIN_TEST_OK   GPIO_NUM_5   // D5  high = self-test passed and all checks OK
#define PIN_PIPELINE  GPIO_NUM_6   // D6  high = block runs preprocessing + model
#define PIN_RUNNING   GPIO_NUM_7   // D7  high while the measurement sequence runs

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
#define START_DELAY_MS  3000   // wait after the self-test before the first block
#define IDLE_MS         1000   // idle gap (CPU waiting in vTaskDelay) before every block
#define BASELINE_MS     1000   // length of the busy-loop baseline block
#define N_ROUNDS        3      // the whole sequence is repeated this many times
#define RUN_PIPELINE    1      // 1 = also measure "preprocessing + model" for every model

// ---------------------------------------------------------------------------
// Blocks. Every model block runs the 400 embedded test samples PASSES times,
// so one block = 400 x PASSES inferences. The pass counts are chosen so that
// a block takes about 1 s (from the latencies in results/device_results.csv).
// If you change a model a lot, adjust its numbers so the blocks stay ~0.5-2 s.
// ---------------------------------------------------------------------------
#define BLOCK_BASELINE    0    // busy loop, no model (CPU active, doing nothing useful)
#define BLOCK_PREPROCESS 12    // preprocessing only (log1p + z-score), no model
#define PASSES_PREPROCESS 200

// X(block number, project folder, passes model only, passes preprocessing + model)
#define ENERGY_MODELS(X)                            \
    X( 1, "01_decision_tree",       1600, 180)     \
    X( 2, "02_random_forest",        250, 110)     \
    X( 3, "02b_xgboost",             110,  70)     \
    X( 4, "03_logistic_regression", 1400, 170)     \
    X( 5, "04_svm",                 4500, 190)     \
    X( 6, "04b_svm_rbf",               5,   5)     \
    X( 7, "05_naive_bayes",          650, 150)     \
    X( 8, "06_kmeans",                50,  40)     \
    X( 9, "07_mlp",                   11,  10)     \
    X(10, "08_cnn1d",                  3,   3)     \
    X(11, "09_autoencoder",           33,  28)

// self-test: a model passes if its predictions agree with Python on at least this share
#define MIN_AGREEMENT_PCT 99.5
