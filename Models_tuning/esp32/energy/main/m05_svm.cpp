// m05_svm.cpp - block 5: the model of ../04_svm
// Includes that project's model.cpp unchanged, inside namespace m05,
// so its functions do not clash with the other models (see energy_models.h).
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "energy_models.h"

namespace m05 {
#include "../../04_svm/main/model.cpp"
#include "../../04_svm/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m05

ENERGY_MODEL_ENTRY(5, "04_svm", m05);
