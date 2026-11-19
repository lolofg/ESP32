// m07_naive_bayes.cpp - block 7: the model of ../05_naive_bayes
// Includes that project's model.cpp unchanged, inside namespace m07,
// so its functions do not clash with the other models (see energy_models.h).
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "energy_models.h"

namespace m07 {
#include "../../05_naive_bayes/main/model.cpp"
#include "../../05_naive_bayes/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m07

ENERGY_MODEL_ENTRY(7, "05_naive_bayes", m07);
