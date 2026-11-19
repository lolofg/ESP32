// m10_cnn1d.cpp - block 10: the model of ../08_cnn1d
// Includes that project's model.cpp unchanged, inside namespace m10,
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

namespace m10 {
#include "../../08_cnn1d/main/model.cpp"
#include "../../08_cnn1d/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m10

ENERGY_MODEL_ENTRY(10, "08_cnn1d", m10);
