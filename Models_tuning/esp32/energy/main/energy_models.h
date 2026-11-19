// energy_models.h - one table entry per model
//
// Every model project (../01_decision_tree, ../02_random_forest, ...) keeps its
// own model.cpp with the same function names (model_init, model_predict, ...).
// To put all of them into ONE firmware, each m*.cpp file in this folder
// includes one project's model.cpp inside its own C++ namespace and exports an
// EnergyModel entry that points to its functions. The model code itself is not
// copied or changed: re-run the notebook, rebuild this project, and the new
// models are used automatically.
#pragma once
#include <stddef.h>
#include <stdint.h>

struct EnergyModel {
    int block;                              // block number (D1-D4), see energy_config.h
    const char *project;                    // folder name, e.g. "01_decision_tree"
    bool (*init)(void);
    int (*predict)(const float *x, float *score);
    const char *(*name)(void);
    size_t (*flash_bytes)(void);
    size_t (*arena_bytes)(void);
    const uint8_t *py_pred;                 // Python predictions for the 400 test samples
};

// defined in m01_decision_tree.cpp ... m11_autoencoder.cpp
extern const EnergyModel ENERGY_MODEL_1;
extern const EnergyModel ENERGY_MODEL_2;
extern const EnergyModel ENERGY_MODEL_3;
extern const EnergyModel ENERGY_MODEL_4;
extern const EnergyModel ENERGY_MODEL_5;
extern const EnergyModel ENERGY_MODEL_6;
extern const EnergyModel ENERGY_MODEL_7;
extern const EnergyModel ENERGY_MODEL_8;
extern const EnergyModel ENERGY_MODEL_9;
extern const EnergyModel ENERGY_MODEL_10;
extern const EnergyModel ENERGY_MODEL_11;

// Builds the table entry inside a wrapper file (after model.cpp was included
// into namespace NS).
#define ENERGY_MODEL_ENTRY(BLOCK, PROJECT, NS)                                         \
    extern const EnergyModel ENERGY_MODEL_##BLOCK = {                                  \
        BLOCK, PROJECT, NS::model_init, NS::model_predict, NS::model_name,             \
        NS::model_flash_bytes, NS::model_arena_bytes, NS::TEST_PY_PRED}
