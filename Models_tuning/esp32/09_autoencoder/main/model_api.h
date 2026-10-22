// model_api.h - the interface every model in this repo implements.
// main.cpp only talks to the model through these functions, so the same
// benchmark firmware is used for all models (fair comparison).
#pragma once
#include <stddef.h>
#include <stdint.h>

// Called once at boot. Returns false if the model could not be set up
// (e.g. TFLite Micro tensor arena too small).
bool model_init(void);

// x      : preprocessed feature vector (length N_FEATURES, already log1p + standardised)
// score  : optional output. Classifiers: confidence/probability of the predicted class.
//          Anomaly detectors: anomaly score (distance / reconstruction error).
// returns: predicted class index (0 = normal for the binary task).
int model_predict(const float *x, float *score);

const char *model_name(void);
size_t model_flash_bytes(void);  // size of the model parameters stored in flash
size_t model_arena_bytes(void);  // RAM used by the model at run time (0 = only stack)
