// SPDX-License-Identifier: Apache-2.0

#include <anira/anira.h>
#include <anira/examples/Backends.h>
#include <anira/models/core/CoreMlEngine.h>

#include <algorithm>
#include <anira/anira.hpp>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace anira::examples {

namespace {

const char* outcome_word(std::uint32_t outcome) {
    switch (outcome) {
        case ANIRA_WARMUP_OUTCOME_FIXED: return "fixed";
        case ANIRA_WARMUP_OUTCOME_STABLE: return "stable";
        case ANIRA_WARMUP_OUTCOME_CAPPED: return "capped";
        case ANIRA_WARMUP_OUTCOME_REUSED:
            return "the model's figure, measured at an earlier prepare";
        default: return "no warm-up";
    }
}

const char* verdict_word(std::uint32_t verdict) {
    switch (verdict) {
        case ANIRA_RT_COMFORTABLE: return "comfortable";
        case ANIRA_RT_MARGINAL: return "marginal";
        case ANIRA_RT_NOT_REALTIME: return "NOT real-time";
        default: return "unmeasured";
    }
}

std::string format(const char* pattern, auto... args) {
    std::array<char, 512> line{};
    std::snprintf(line.data(), line.size(), pattern, args...);
    return line.data();
}

}  // namespace

Backends::Backends(bool engines_to_choose) {
    if (!engines_to_choose) {
        m_choices.push_back({.m_name = "Automatic", .m_query = anira::PlanQuery{}});
        m_choices.push_back({.m_name = "Bypass", .m_query = std::nullopt});
        return;
    }
    const std::array<BackendChoice, 5> engines{{
        {.m_name = "ONNX Runtime",
         .m_query = anira::PlanQuery{.backend = {.engine = ANIRA_ENGINE_ONNXRUNTIME}}},
        {.m_name = "LibTorch",
         .m_query = anira::PlanQuery{.backend = {.engine = ANIRA_ENGINE_LIBTORCH}}},
        {.m_name = "LiteRT",
         .m_query = anira::PlanQuery{.backend = {.engine = ANIRA_ENGINE_LITERT}}},
        {.m_name = "TFLite",
         .m_query = anira::PlanQuery{.backend = {.engine = ANIRA_ENGINE_TFLITE}}},
        {.m_name = "ExecuTorch",
         .m_query = anira::PlanQuery{.backend = {.engine = ANIRA_ENGINE_EXECUTORCH}}},
    }};
    const std::vector<anira::BackendId> built = anira::enabled_engines();
    for (const BackendChoice& choice : engines) {
        if (!choice.m_query) { continue; }
        const auto engine = static_cast<std::uint32_t>(choice.m_query->backend.engine);
        if (std::ranges::any_of(built, [engine](const anira::BackendId& id) {
                return id.engine == engine;
            })) {
            m_choices.push_back(choice);
        }
    }
    // Core ML through the models' temporary engine, where this build has it.
    if (anira::models::core::coreml_engine() != nullptr) {
        m_choices.push_back(
            {.m_name = "Core ML",
             .m_query = anira::PlanQuery{
                 .backend = {.engine = ANIRA_ENGINE_CUSTOM,
                             .engine_id = anira::models::core::k_coreml_engine_id}}});
    }
    m_choices.push_back({.m_name = "Bypass", .m_query = std::nullopt});
}

std::size_t Backends::default_choice() const noexcept {
    const auto onnx = std::ranges::find_if(m_choices, [](const BackendChoice& choice) {
        return choice.m_query && choice.m_query->backend.engine == ANIRA_ENGINE_ONNXRUNTIME;
    });
    return onnx == m_choices.end() ? 0 : static_cast<std::size_t>(onnx - m_choices.begin());
}

void Backends::begin(std::size_t choice) noexcept {
    m_plans[choice].fill(std::nullopt);
    Verdict& verdict = m_verdicts[choice];
    verdict.m_has_plan.store(true);
    verdict.m_verdict.store(ANIRA_RT_UNMEASURED);
    verdict.m_rt_factor.store(0.0F);
    verdict.m_rt_parallel.store(1);
}

void Backends::add(std::size_t choice,
                   std::size_t handler,
                   const anira::PlanReport& report,
                   std::vector<std::string>& log) {
    Verdict& verdict = m_verdicts[choice];
    const std::optional<anira::PlanQuery>& query = m_choices[choice].m_query;
    if (!query) { return; }
    const std::optional<std::uint32_t> plan = report.find(*query);
    if (!plan) {
        verdict.m_has_plan.store(false);
        return;
    }
    m_plans[choice][handler] = plan;
    const anira_plan_info row = report.plans()[*plan];
    log.push_back(format(
        "anira: %s: budget %.3g ms (measured %.3g ms, median %.3g ms, %u warm-up inferences, %s); "
        "real-time factor %.3g of %u: %s",
        m_choices[choice].m_name,
        row.budget_ms,
        row.measured_ms,
        row.median_ms,
        row.warmup_iterations,
        outcome_word(row.warmup_outcome),
        row.rt_factor,
        row.rt_parallel,
        verdict_word(row.rt_verdict)));
    // The worse of the handlers' verdicts: the enum's order is the order of severity.
    if (row.rt_verdict >= verdict.m_verdict.load()) {
        verdict.m_verdict.store(row.rt_verdict);
        verdict.m_rt_factor.store(static_cast<float>(row.rt_factor));
        verdict.m_rt_parallel.store(row.rt_parallel);
    }
}

void Backends::end(std::size_t choice, std::vector<std::string>& log) {
    const char* name = m_choices[choice].m_name;
    if (!m_verdicts[choice].m_has_plan.load()) {
        log.push_back(format("anira: %s: no plan on this machine: it runs bypassed", name));
    } else if (held_back(choice)) {
        log.push_back(
            format("anira: %s is not real-time on this machine: bypassed until 'Try nonetheless'",
                   name));
    }
}

bool Backends::held_back(std::size_t choice) const noexcept {
    return m_verdicts[choice].m_verdict.load() == ANIRA_RT_NOT_REALTIME &&
           m_verdicts[choice].m_accepted.load() != ANIRA_RT_NOT_REALTIME;
}

bool Backends::runs(std::size_t choice, bool anyway) const noexcept {
    if (choice >= m_choices.size() || !m_choices[choice].m_query) { return false; }
    return m_verdicts[choice].m_has_plan.load() && (anyway || !held_back(choice));
}

bool Backends::waits_for_consent(std::size_t choice) const noexcept {
    return choice < m_choices.size() && m_choices[choice].m_query && held_back(choice);
}

void Backends::try_nonetheless(std::size_t choice) noexcept {
    if (choice >= m_choices.size()) { return; }
    m_verdicts[choice].m_accepted.store(m_verdicts[choice].m_verdict.load());
}

std::string Backends::verdict_line(std::size_t choice) const {
    if (choice >= m_choices.size() || !m_choices[choice].m_query) { return {}; }
    const Verdict& v = m_verdicts[choice];
    const std::string name = m_choices[choice].m_name;
    if (!v.m_has_plan.load()) { return name + ": no plan on this machine: bypassed"; }
    const float rt_factor = v.m_rt_factor.load();
    const std::uint32_t parallel = v.m_rt_parallel.load();
    const std::string inferences =
        std::to_string(parallel) + (parallel == 1 ? " parallel inference" : " parallel inferences");
    switch (v.m_verdict.load()) {
        case ANIRA_RT_NOT_REALTIME:
            return format("%s runs at %.2fx real time on this machine (%s): %s",
                          name.c_str(),
                          rt_factor,
                          inferences.c_str(),
                          held_back(choice) ? "bypassed" : "playing it anyway, it drops out");
        case ANIRA_RT_MARGINAL:
            return format("%s: marginal on this machine (%d %% headroom): may drop out under load",
                          name.c_str(),
                          static_cast<int>(std::lround(
                              100.0F * (1.0F - rt_factor / static_cast<float>(parallel)))));
        default: return {};
    }
}

}  // namespace anira::examples
