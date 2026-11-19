// m03_xgboost.cpp - block 3: the model of ../02b_xgboost
// Includes that project's model.cpp unchanged, inside namespace m03,
// so its functions do not clash with the other models (see energy_models.h).
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "energy_models.h"

namespace m03 {
#include "../../02b_xgboost/main/model.cpp"
#include "../../02b_xgboost/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m03

ENERGY_MODEL_ENTRY(3, "02b_xgboost", m03);
