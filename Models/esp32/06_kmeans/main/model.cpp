// model.cpp - K-Means anomaly detector (unsupervised)
// K-Means was fitted on NORMAL traffic only. A sample is an anomaly (class 1)
// if its squared distance to the nearest centroid is above KM_THRESHOLD.
#include "model_api.h"
#include "model_info.h"
#include "model_data.h"

bool model_init(void) { return true; }

int model_predict(const float *x, float *score) {
    float best = 3.4e38f;
    for (int k = 0; k < KM_K; k++) {
        const float *c = &KM_CENTROIDS[k * N_FEATURES_MODEL];
        float d2 = 0.0f;
        for (int i = 0; i < N_FEATURES_MODEL; i++) {
            float d = x[i] - c[i];
            d2 += d * d;
        }
        if (d2 < best) best = d2;
    }
    if (score) *score = best;
    return best > KM_THRESHOLD ? 1 : 0;
}

const char *model_name(void) { return MODEL_NAME; }
size_t model_flash_bytes(void) { return sizeof(KM_CENTROIDS) + sizeof(float); }
size_t model_arena_bytes(void) { return 0; }
