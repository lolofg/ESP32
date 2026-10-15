// model.cpp - Random Forest inference (scikit-learn RandomForestClassifier)
// All trees are stored in one set of flat arrays (model_data.h). Like
// scikit-learn, the class probabilities of the reached leaves are averaged
// over the trees (soft voting) and the class with the highest mean wins.
#include "model_api.h"
#include "model_info.h"
#include "model_data.h"

bool model_init(void) { return true; }

int model_predict(const float *x, float *score) {
    float proba[MODEL_N_CLASSES] = {0};
    for (int t = 0; t < RF_N_TREES; t++) {
        int32_t n = RF_ROOT[t];
        while (RF_FEATURE[n] >= 0)
            n = (x[RF_FEATURE[n]] <= RF_THRESHOLD[n]) ? RF_LEFT[n] : RF_RIGHT[n];
        const float *leaf = &RF_LEAF_PROBA[(int32_t)RF_LEFT[n] * MODEL_N_CLASSES];
        for (int c = 0; c < MODEL_N_CLASSES; c++) proba[c] += leaf[c];
    }
    int best = 0;
    for (int c = 1; c < MODEL_N_CLASSES; c++)
        if (proba[c] > proba[best]) best = c;
    if (score) *score = proba[best] / RF_N_TREES;
    return best;
}

const char *model_name(void) { return MODEL_NAME; }
size_t model_flash_bytes(void) {
    return sizeof(RF_ROOT) + sizeof(RF_FEATURE) + sizeof(RF_THRESHOLD) + sizeof(RF_LEFT) +
           sizeof(RF_RIGHT) + sizeof(RF_LEAF_PROBA);
}
size_t model_arena_bytes(void) { return 0; }
