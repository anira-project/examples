// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/models/core/ModelSource.h>

#include <anira/anira.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace anira::examples {

/// A parameter of an example, beside the ones every target has (backend, dry/wet).
struct Parameter {
    std::string_view m_id;
    std::string_view m_name;
    float m_min = 0.0F;
    float m_max = 1.0F;
    float m_default = 0.0F;
};

/// A value an example shows (a speech probability, a level), read on any thread.
struct Meter {
    std::string_view m_id;
    std::string_view m_name;
    float m_min = 0.0F;
    float m_max = 1.0F;
};

/// How long an offline render (a bounce, which does not run against the clock) waits for a block.
inline constexpr double k_offline_timeout_ms = 10'000.0;

/// What a target needs of an example (an example header defines `anira::examples::Example`):
///
///   - k_name, k_channels (the plugin's bus: 1 or 2, in and out), k_parameters (std::array of
///     Parameter), k_handlers (the anira handlers it runs, each with its plan report);
///   - a constructor from the context and the weights (main thread; throws on missing weights);
///   - prepare(sample_rate, max_block) and reset() on the main thread, no process() running;
///   - process(audio, offline): audio thread, in place on [k_channels, n] planar float; `offline`
///     (a bounce) waits for a late block instead of missing it;
///   - set_parameter(index, value): audio thread, before process();
///   - get_latency() (host samples, fixed after prepare), plan_report(handler) and
///     set_plan(handler, plan) (audio thread), the backend choice;
///   - optionally k_meters (std::array of Meter) and meter(index), any thread: values a target
///     shows (MetersLike; meter_count and meter_value below work for every example).
template <class E>
concept ExampleLike =
    requires(E& e, const E& ce, const anira::Tensor& audio, std::size_t i, std::uint32_t plan) {
        { E::k_name } -> std::convertible_to<std::string_view>;
        { E::k_channels } -> std::convertible_to<int>;
        { E::k_handlers } -> std::convertible_to<std::size_t>;
        { E::k_parameters.size() } -> std::convertible_to<std::size_t>;
        e.prepare(double{}, std::size_t{});
        e.reset();
        { e.process(audio, bool{}) } noexcept -> std::same_as<anira_status>;
        { e.set_parameter(i, float{}) } noexcept;
        { ce.get_latency() } -> std::convertible_to<std::size_t>;
        { ce.plan_report(i) } -> std::same_as<anira::PlanReport>;
        { e.set_plan(i, plan) } noexcept;
    };

template <class E>
concept MetersLike = requires(const E& e, std::size_t i) {
    { E::k_meters.size() } -> std::convertible_to<std::size_t>;
    { e.meter(i) } noexcept -> std::convertible_to<float>;
};

/// The example's meters (none for one without).
template <class E>
constexpr std::size_t meter_count() noexcept {
    if constexpr (MetersLike<E>) {
        return E::k_meters.size();
    } else {
        return 0;
    }
}
template <class E>
constexpr Meter meter_info(std::size_t index) noexcept {
    if constexpr (MetersLike<E>) {
        return E::k_meters[index];
    } else {
        return {};
    }
}
template <class E>
float meter_value(const E& example, std::size_t index) noexcept {
    if constexpr (MetersLike<E>) {
        return example.meter(index);
    } else {
        return 0.0F;
    }
}

/// An example of one model wrapper that changes the sound in place (core::AudioEffect): its
/// handler, without parameters of its own. An example header derives from it and names the
/// plugin.
template <class Model>
class EffectExample {
public:
    static constexpr std::size_t k_handlers = 1;

    template <class... Args>
    EffectExample(anira::Context& context,
                  const anira::models::core::ModelSource& weights,
                  Args&&... args)
        : m_model(context, weights, std::forward<Args>(args)...) {}

    void prepare(double sample_rate, std::size_t max_block) {
        m_model.prepare(sample_rate, max_block);
    }
    void reset() { m_model.reset(); }

    anira_status process(const anira::Tensor& audio, bool offline) noexcept {
        return offline ? m_model.process_wait(audio, audio, k_offline_timeout_ms)
                       : m_model.process(audio, audio);
    }
    void set_parameter(std::size_t /*index*/, float /*value*/) noexcept {}

    [[nodiscard]] std::size_t get_latency() const noexcept { return m_model.get_latency(); }
    [[nodiscard]] anira::PlanReport plan_report(std::size_t /*handler*/) const {
        return m_model.plan_report();
    }
    void set_plan(std::size_t /*handler*/, std::uint32_t plan) noexcept { m_model.set_plan(plan); }

protected:
    Model m_model;
};

}  // namespace anira::examples
