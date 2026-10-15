// model.cpp - linear model inference: z = W x + b
// Used for Logistic Regression and the linear SVM (LinearSVC).
//   binary task : one output, class 1 if z > 0
//   multiclass  : one output per class (one-vs-rest), class = argmax z
// LIN_PROBA = 1 (logistic regression) turns z into a probability.
#include <math.h>
#include "model_api.h"
#include "model_info.h"
#include "model_data.h"

bool model_init(void) { return true; }

int model_predict(const float *x, float *score) {
    float z[LIN_N_OUT];
    for (int o = 0; o < LIN_N_OUT; o++) {
        const float *w = &LIN_W[o * N_FEATURES_MODEL];
        float acc = LIN_B[o];
        for (int i = 0; i < N_FEATURES_MODEL; i++) acc += w[i] * x[i];
        z[o] = acc;
    }
#if LIN_N_OUT == 1
    int pred = z[0] > 0.0f ? 1 : 0;
    if (score) *score = LIN_PROBA ? 1.0f / (1.0f + expf(-z[0])) : z[0];
    return pred;
#else
    int best = 0;
    for (int o = 1; o < LIN_N_OUT; o++)
        if (z[o] > z[best]) best = o;
    if (score) {
        if (LIN_PROBA) {  // softmax probability of the winning class
            float sum = 0.0f;
            for (int o = 0; o < LIN_N_OUT; o++) sum += expf(z[o] - z[best]);
            *score = 1.0f / sum;
        } else {
            *score = z[best];
        }
    }
    return best;
#endif
}

const char *model_name(void) { return MODEL_NAME; }
size_t model_flash_bytes(void) { return sizeof(LIN_W) + sizeof(LIN_B); }
size_t model_arena_bytes(void) { return 0; }
