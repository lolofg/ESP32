// m04_logistic_regression.cpp - block 4: the model of ../03_logistic_regression
// Includes that project's model.cpp unchanged, inside namespace m04,
// so its functions do not clash with the other models (see energy_models.h).
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "energy_models.h"

namespace m04 {
#include "../../03_logistic_regression/main/model.cpp"
#include "../../03_logistic_regression/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m04

ENERGY_MODEL_ENTRY(4, "03_logistic_regression", m04);
