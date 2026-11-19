// m01_decision_tree.cpp - block 1: the model of ../01_decision_tree
// Includes that project's model.cpp unchanged, inside namespace m01,
// so its functions do not clash with the other models (see energy_models.h).
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include "energy_models.h"

namespace m01 {
#include "../../01_decision_tree/main/model.cpp"
#include "../../01_decision_tree/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m01

ENERGY_MODEL_ENTRY(1, "01_decision_tree", m01);
