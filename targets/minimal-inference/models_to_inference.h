// SPDX-License-Identifier: Apache-2.0

#pragma once

// The models the minimal-inference examples run: the effects' descriptions from
// anira-project/models and their weights' directories (fetched by CMake:
// ANIRA_MINIMAL_<MODEL>_WEIGHTS).

#include <anira/models/guitar_lstm/Descriptions.h>
#include <anira/models/stateful_lstm/Descriptions.h>
#include <anira/models/steerable_nafx/Descriptions.h>

#include <array>
#include <string_view>

struct ModelToInference {
    const char* m_name;
    std::string_view m_model_json;
    const char* m_weights;
};

inline constexpr std::array<ModelToInference, 3> k_models_to_inference{{
    {.m_name = "GuitarLSTM",
     .m_model_json = anira::models::guitar_lstm::k_guitar_lstm_model_json,
     .m_weights = ANIRA_MINIMAL_GUITAR_LSTM_WEIGHTS},
    {.m_name = "steerable-nafx",
     .m_model_json = anira::models::steerable_nafx::k_steerable_nafx_model_json,
     .m_weights = ANIRA_MINIMAL_STEERABLE_NAFX_WEIGHTS},
    {.m_name = "stateful LSTM",
     .m_model_json = anira::models::stateful_lstm::k_stateful_lstm_model_json,
     .m_weights = ANIRA_MINIMAL_STATEFUL_LSTM_WEIGHTS},
}};
