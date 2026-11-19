// m02_random_forest.cpp - block 2: the model of ../02_random_forest
// Includes that project's model.cpp unchanged, inside namespace m02,
// so its functions do not clash with the other models (see energy_models.h).
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "energy_models.h"

namespace m02 {
#include "../../02_random_forest/main/model.cpp"
#include "../../02_random_forest/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m02

ENERGY_MODEL_ENTRY(2, "02_random_forest", m02);
