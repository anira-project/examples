// SPDX-License-Identifier: Apache-2.0

#ifndef ANIRA_EXAMPLES_MINIMAL_MODEL_H
#define ANIRA_EXAMPLES_MINIMAL_MODEL_H

// What the minimal-inference examples read off a model's description (anira-project/models) before
// they call an engine directly: the path of the entry for their engine and the tensor shapes that
// engine is fed.
// The shapes are the model file's tensor specs, read back from the anira::ModelConfig: the
// extents in the spec's (canonical) order, permuted by the entry's layout when it has one (a
// TensorFlow export holds [batch, time, channel] where the spec says [batch, channel, time]).

#include <anira/anira.hpp>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "models_to_inference.h"

struct MinimalModel {
    std::string m_path;
    std::vector<std::vector<int64_t>> m_input_shapes;
    std::vector<std::vector<int64_t>> m_output_shapes;
    std::vector<size_t> m_input_sizes;   ///< elements per input
    std::vector<size_t> m_output_sizes;  ///< elements per output
};

namespace minimal_model_detail {

inline std::vector<int64_t> engine_shape(const anira::ModelConfig& config,
                                         uint32_t entry,
                                         const anira::SpecView& spec) {
    std::vector<int64_t> canonical;
    canonical.reserve(spec.ndim());
    for (uint32_t axis = 0; axis < spec.ndim(); ++axis) {
        canonical.push_back(spec.axis(axis).extent);
    }
    // Per position of the entry's tensor, the spec axis it holds, or ANIRA_AXIS_INSERT for an
    // axis of extent 1 the spec does not have; empty when the entry keeps the spec's order.
    const std::vector<uint32_t> layout = config.tensor_layout(entry, spec.name());
    if (layout.empty()) { return canonical; }
    std::vector<int64_t> permuted;
    permuted.reserve(layout.size());
    for (const uint32_t axis : layout) {
        permuted.push_back(axis == ANIRA_AXIS_INSERT ? 1 : canonical.at(axis));
    }
    return permuted;
}

inline size_t elements_of(const std::vector<int64_t>& shape) {
    size_t elements = 1;
    for (const int64_t extent : shape) { elements *= static_cast<size_t>(extent); }
    return elements;
}

}  // namespace minimal_model_detail

/// The first entry of the model's description for `engine`, its path in the model's weights, or
/// nothing when the description has none.
inline std::optional<MinimalModel> minimal_model(const ModelToInference& info,
                                                 anira::EngineKind engine) {
    const anira::ModelConfig config =
        anira::ModelConfig::from_json(info.m_model_json, info.m_weights);
    for (uint32_t entry = 0; entry < config.model_count(); ++entry) {
        if (config.model_engine(entry).kind != engine) { continue; }
        MinimalModel model;
        std::filesystem::path path(std::string(config.model_path(entry)));
        if (path.is_relative()) { path = std::filesystem::path(info.m_weights) / path; }
        model.m_path = path.string();
        for (uint32_t i = 0; i < config.input_count(); ++i) {
            model.m_input_shapes.push_back(
                minimal_model_detail::engine_shape(config, entry, config.input_spec(i)));
            model.m_input_sizes.push_back(
                minimal_model_detail::elements_of(model.m_input_shapes.back()));
        }
        for (uint32_t i = 0; i < config.output_count(); ++i) {
            model.m_output_shapes.push_back(
                minimal_model_detail::engine_shape(config, entry, config.output_spec(i)));
            model.m_output_sizes.push_back(
                minimal_model_detail::elements_of(model.m_output_shapes.back()));
        }
        return model;
    }
    return std::nullopt;
}

#endif  // ANIRA_EXAMPLES_MINIMAL_MODEL_H
