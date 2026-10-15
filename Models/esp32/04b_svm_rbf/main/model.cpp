// model.cpp - kernel SVM (RBF) inference
//   z = b + sum_i alpha_i * exp(-gamma * ||x - sv_i||^2)
//   z > 0 -> attack (class 1)
// The support vectors (SVM_SV) are training samples stored in flash.
// Every prediction compares x with ALL of them, so the time grows with SVM_N_SV.
#include <math.h>
#include "model_api.h"
#include "model_info.h"
#include "model_data.h"

bool model_init(void) { return true; }

int model_predict(const float *x, float *score) {
    float z = SVM_B;
    for (int s = 0; s < SVM_N_SV; s++) {
        const float *sv = &SVM_SV[s * N_FEATURES_MODEL];
        float d2 = 0.0f;                          // squared distance between x and this support vector
        for (int i = 0; i < N_FEATURES_MODEL; i++) {
            float d = x[i] - sv[i];
            d2 += d * d;
        }
        z += SVM_ALPHA[s] * expf(-SVM_GAMMA * d2);
    }
    if (score) *score = z;
    return z > 0.0f ? 1 : 0;
}

const char *model_name(void) { return MODEL_NAME; }
size_t model_flash_bytes(void) { return sizeof(SVM_SV) + sizeof(SVM_ALPHA) + 2 * sizeof(float); }
size_t model_arena_bytes(void) { return 0; }
