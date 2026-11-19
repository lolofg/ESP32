// m09_mlp.cpp - block 9: the model of ../07_mlp
// Includes that project's model.cpp unchanged, inside namespace m09,
// so its functions do not clash with the other models (see energy_models.h).
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
// TFLite Micro headers must be included before the namespace below
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "energy_models.h"

namespace m09 {
#include "../../07_mlp/main/model.cpp"
#include "../../07_mlp/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m09

ENERGY_MODEL_ENTRY(9, "07_mlp", m09);
