// m08_kmeans.cpp - block 8: the model of ../06_kmeans
// Includes that project's model.cpp unchanged, inside namespace m08,
// so its functions do not clash with the other models (see energy_models.h).
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "energy_models.h"

namespace m08 {
#include "../../06_kmeans/main/model.cpp"
#include "../../06_kmeans/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m08

ENERGY_MODEL_ENTRY(8, "06_kmeans", m08);
