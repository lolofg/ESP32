// model.cpp - XGBoost inference (gradient-boosted trees)
// All trees are stored one after another in flat arrays (model_data.h):
//   XGB_FEATURE[n]   feature checked at node n (-1 = leaf)
//   XGB_THRESHOLD[n] go LEFT if x[feature] < threshold;  for a leaf: the leaf value
//   XGB_LEFT/RIGHT   index of the next node
// score = base score + sum of the leaf values of all trees;  score > 0 -> attack
#include <math.h>
#include "model_api.h"
#include "model_info.h"
#include "model_data.h"

bool model_init(void) { return true; }

int model_predict(const float *x, float *score) {
    float margin = XGB_BASE_SCORE;
    for (int t = 0; t < XGB_N_TREES; t++) {
        int32_t n = XGB_ROOT[t];
        while (XGB_FEATURE[n] >= 0)
            n = (x[XGB_FEATURE[n]] < XGB_THRESHOLD[n]) ? XGB_LEFT[n] : XGB_RIGHT[n];
        margin += XGB_THRESHOLD[n];               // leaf value
    }
    if (score) *score = 1.0f / (1.0f + expf(-margin));   // probability of attack
    return margin > 0.0f ? 1 : 0;
}

const char *model_name(void) { return MODEL_NAME; }
size_t model_flash_bytes(void) {
    return sizeof(XGB_ROOT) + sizeof(XGB_FEATURE) + sizeof(XGB_THRESHOLD) + sizeof(XGB_LEFT) +
           sizeof(XGB_RIGHT) + sizeof(float);
}
size_t model_arena_bytes(void) { return 0; }
