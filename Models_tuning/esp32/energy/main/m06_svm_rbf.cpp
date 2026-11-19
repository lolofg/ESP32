// m06_svm_rbf.cpp - block 6: the model of ../04b_svm_rbf
// Includes that project's model.cpp unchanged, inside namespace m06,
// so its functions do not clash with the other models (see energy_models.h).
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "energy_models.h"

namespace m06 {
#include "../../04b_svm_rbf/main/model.cpp"
#include "../../04b_svm_rbf/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m06

ENERGY_MODEL_ENTRY(6, "04b_svm_rbf", m06);
