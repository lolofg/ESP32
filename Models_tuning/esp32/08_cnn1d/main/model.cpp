// model.cpp - neural network inference with TensorFlow Lite Micro (int8)
// Used for the MLP, the 1D-CNN and the autoencoder.
// model_data.h (generated) contains:
//   g_model[]              the .tflite flatbuffer (full int8 quantisation)
//   TENSOR_ARENA_SIZE      working memory for TFLite Micro
//   MODEL_OP_COUNT / MODEL_REGISTER_OPS(r)   exactly the ops the model needs
//   NN_IS_AUTOENCODER, AE_THRESHOLD          for the autoencoder
#include <math.h>
#include <stdio.h>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "model_api.h"
#include "model_info.h"
#include "model_data.h"

namespace {
const tflite::Model *tfl_model = nullptr;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr;
TfLiteTensor *output = nullptr;
alignas(16) uint8_t tensor_arena[TENSOR_ARENA_SIZE];

inline int8_t quantize(float v, float scale, int zero_point) {
    long q = lroundf(v / scale) + zero_point;  // same rounding as the notebook
    if (q < -128) q = -128;
    if (q > 127) q = 127;
    return (int8_t)q;
}
}  // namespace

bool model_init(void) {
    tfl_model = tflite::GetModel(g_model);
    if (tfl_model->version() != TFLITE_SCHEMA_VERSION) {
        printf("Model schema %lu != supported %d\n", (unsigned long)tfl_model->version(),
               TFLITE_SCHEMA_VERSION);
        return false;
    }
    static tflite::MicroMutableOpResolver<MODEL_OP_COUNT> resolver;
    MODEL_REGISTER_OPS(resolver);

    static tflite::MicroInterpreter static_interpreter(tfl_model, resolver, tensor_arena,
                                                       TENSOR_ARENA_SIZE);
    interpreter = &static_interpreter;
    if (interpreter->AllocateTensors() != kTfLiteOk) {
        printf("AllocateTensors() failed - increase TENSOR_ARENA_SIZE in model_data.h\n");
        return false;
    }
    input = interpreter->input(0);
    output = interpreter->output(0);
    if (input->type != kTfLiteInt8 || output->type != kTfLiteInt8) {
        printf("Expected an int8 model\n");
        return false;
    }
    return true;
}

int model_predict(const float *x, float *score) {
    const float in_scale = input->params.scale;
    const int in_zp = input->params.zero_point;
    for (int i = 0; i < N_FEATURES_MODEL; i++) input->data.int8[i] = quantize(x[i], in_scale, in_zp);

    if (interpreter->Invoke() != kTfLiteOk) return -1;

    const float out_scale = output->params.scale;
    const int out_zp = output->params.zero_point;
#if NN_IS_AUTOENCODER
    // reconstruction error (mean squared error) between input and output
    float err = 0.0f;
    for (int i = 0; i < N_FEATURES_MODEL; i++) {
        float r = (output->data.int8[i] - out_zp) * out_scale;
        float d = r - x[i];
        err += d * d;
    }
    err /= N_FEATURES_MODEL;
    if (score) *score = err;
    return err > AE_THRESHOLD ? 1 : 0;
#else
    int best = 0;
    for (int c = 1; c < MODEL_N_CLASSES; c++)
        if (output->data.int8[c] > output->data.int8[best]) best = c;
    if (score) *score = (output->data.int8[best] - out_zp) * out_scale;  // softmax prob
    return best;
#endif
}

const char *model_name(void) { return MODEL_NAME; }
size_t model_flash_bytes(void) { return g_model_len; }
size_t model_arena_bytes(void) { return interpreter ? interpreter->arena_used_bytes() : 0; }
