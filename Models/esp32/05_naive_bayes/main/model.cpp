// model.cpp - Gaussian Naive Bayes inference (scikit-learn GaussianNB)
// log P(c|x) = NB_CONST[c] - 0.5 * sum_i (x_i - mean_ci)^2 / var_ci   (+ const)
// NB_CONST[c] = log prior(c) - 0.5 * sum_i log(2*pi*var_ci) is precomputed.
#include <math.h>
#include "model_api.h"
#include "model_info.h"
#include "model_data.h"

bool model_init(void) { return true; }

int model_predict(const float *x, float *score) {
    float jll[MODEL_N_CLASSES];
    for (int c = 0; c < MODEL_N_CLASSES; c++) {
        const float *mu = &NB_THETA[c * N_FEATURES_MODEL];
        const float *iv = &NB_INV_VAR[c * N_FEATURES_MODEL];
        float s = 0.0f;
        for (int i = 0; i < N_FEATURES_MODEL; i++) {
            float d = x[i] - mu[i];
            s += d * d * iv[i];
        }
        jll[c] = NB_CONST[c] - 0.5f * s;
    }
    int best = 0;
    for (int c = 1; c < MODEL_N_CLASSES; c++)
        if (jll[c] > jll[best]) best = c;
    if (score) {  // posterior probability of the winning class
        float sum = 0.0f;
        for (int c = 0; c < MODEL_N_CLASSES; c++) sum += expf(jll[c] - jll[best]);
        *score = 1.0f / sum;
    }
    return best;
}

const char *model_name(void) { return MODEL_NAME; }
size_t model_flash_bytes(void) { return sizeof(NB_THETA) + sizeof(NB_INV_VAR) + sizeof(NB_CONST); }
size_t model_arena_bytes(void) { return 0; }
