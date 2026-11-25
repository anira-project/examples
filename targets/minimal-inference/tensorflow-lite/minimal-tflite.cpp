/* ==========================================================================

Minimal TensorFlow Lite example from https://www.tensorflow.org/lite/guide/inference
Licence: Apache 2.0

========================================================================== */

#include <tensorflow/lite/c_api.h>

#include <array>
#include <cstdio>
#include <iostream>
#include <optional>
#include <utility>
#include <vector>

#include "../minimal_model.h"

#define TFLITE_MINIMAL_CHECK(x)                                  \
    if (!(x)) {                                                  \
        fprintf(stderr, "Error at %s:%d\n", __FILE__, __LINE__); \
        exit(1);                                                 \
    }

void minimal_inference(const MinimalModel& info) {
    std::cout << "Minimal TensorFlow-Lite example:" << '\n';
    std::cout << "-----------------------------------------" << '\n';
    std::cout << "Using model: " << info.m_path << '\n';

    // Load model
    TfLiteModel* m_model;
    m_model = TfLiteModelCreateFromFile(info.m_path.c_str());

    // Create the interpreter
    TfLiteInterpreterOptions* m_options;
    TfLiteInterpreter* m_interpreter;
    m_options = TfLiteInterpreterOptionsCreate();
    TfLiteInterpreterOptionsSetNumThreads(m_options, 1);
    m_interpreter = TfLiteInterpreterCreate(m_model, m_options);

    // This is necessary when we have dynamic input shapes, it should be done before allocating
    // tensors obviously
    for (size_t i = 0; i < info.m_input_shapes.size(); i++) {
        std::vector<int> input_shape;
        std::vector<int64_t> input_shape64 = info.m_input_shapes[i];
        for (size_t j = 0; j < input_shape64.size(); j++) {
            input_shape.push_back((int)input_shape64[j]);
        }
        TfLiteInterpreterResizeInputTensor(m_interpreter,
                                           i,
                                           input_shape.data(),
                                           static_cast<int32_t>(input_shape.size()));
    }

    // Allocate memory for all tensors
    TfLiteInterpreterAllocateTensors(m_interpreter);

    // Fill an Buffer with some data
    std::vector<float> input(info.m_input_sizes[0]);
    for (int i = 0; i < info.m_input_sizes[0]; ++i) { input[i] = i * 0.000001f; }

    // Get input tensors and prepare input data
    std::vector<TfLiteTensor*> m_inputs;
    std::vector<std::vector<float>> m_input_data;

    m_inputs.resize(info.m_input_shapes.size());
    m_input_data.resize(info.m_input_shapes.size());
    for (size_t i = 0; i < info.m_input_shapes.size(); i++) {
        m_input_data[i].resize(info.m_input_sizes[i]);
        m_inputs[i] = TfLiteInterpreterGetInputTensor(m_interpreter, i);
        if (i == 0) { m_input_data[i].swap(input); }
        TfLiteTensorCopyFromBuffer(m_inputs[i],
                                   m_input_data[i].data(),
                                   info.m_input_sizes[i] * sizeof(float));
    }

    for (int i = 0; i < m_inputs.size(); ++i) {
        std::cout << "Input shape " << i << ": [";
        for (int j = 0; j < TfLiteTensorNumDims(m_inputs[i]); ++j) {
            std::cout << TfLiteTensorDim(m_inputs[i], j);
            if (j < TfLiteTensorNumDims(m_inputs[i]) - 1) { std::cout << ", "; }
        }
        std::cout << "]" << '\n';
    }

    // Execute inference
    TfLiteInterpreterInvoke(m_interpreter);

    // Get output tensors
    std::vector<const TfLiteTensor*> m_outputs;

    m_outputs.resize(info.m_output_shapes.size());
    for (size_t i = 0; i < info.m_output_shapes.size(); i++) {
        m_outputs[i] = TfLiteInterpreterGetOutputTensor(m_interpreter, i);
    }

    for (int i = 0; i < m_outputs.size(); ++i) {
        std::cout << "Output shape " << i << ": [";
        for (int j = 0; j < TfLiteTensorNumDims(m_outputs[i]); ++j) {
            std::cout << TfLiteTensorDim(m_outputs[i], j);
            if (j < TfLiteTensorNumDims(m_outputs[i]) - 1) { std::cout << ", "; }
        }
        std::cout << "]" << '\n';
    }

    // Extract the output tensor data
    std::vector<std::vector<float>> m_output_data;
    m_output_data.resize(info.m_output_shapes.size());

    for (size_t i = 0; i < info.m_output_shapes.size(); i++) {
        float* output_data = (float*)TfLiteTensorData(m_outputs[i]);
        for (size_t j = 0; j < info.m_output_sizes[i]; j++) {
            m_output_data[i].resize(info.m_output_sizes[i]);
            for (size_t j = 0; j < info.m_output_sizes[i]; j++) {
                m_output_data[i][j] = output_data[j];
            }
        }
    }

    // Print output data
    for (int i = 0; i < m_output_data.size(); i++) {
        for (int j = 0; j < m_output_data[i].size(); j++) {
            std::cout << "Output data [" << i << "][" << j << "]: " << m_output_data[i][j] << '\n';
        }
    }

    // Dispose of the model and interpreter objects.
    TfLiteInterpreterDelete(m_interpreter);
    TfLiteInterpreterOptionsDelete(m_options);
    TfLiteModelDelete(m_model);
}

int main() {
    // The effects' descriptions (anira-project/models) and their weights, read back with anira's
    // API (minimal_model.h): the path of the entry for this example's engine and the shapes the
    // engine is fed. A description without an entry for the engine is skipped.
    for (const ModelToInference& info : k_models_to_inference) {
        const std::optional<MinimalModel> model = minimal_model(info, ANIRA_ENGINE_TFLITE);
        if (!model.has_value()) {
            std::cout << info.m_name << ": no entry for this engine" << '\n';
            continue;
        }
        minimal_inference(*model);
    }

    return 0;
}
