// m11_autoencoder.cpp - block 11: the model of ../09_autoencoder
// Includes that project's model.cpp unchanged, inside namespace m11,
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

namespace m11 {
#include "../../09_autoencoder/main/model.cpp"
#include "../../09_autoencoder/main/test_data.h"  // for TEST_PY_PRED (Python predictions)
}  // namespace m11

ENERGY_MODEL_ENTRY(11, "09_autoencoder", m11);
