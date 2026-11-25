// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.hpp>
#include ANIRA_EXAMPLE_HEADER         // IWYU pragma: export
#include <anira/examples/Backends.h>  // IWYU pragma: export
#include <anira/examples/DryWet.h>    // IWYU pragma: export
#include <anira/examples/Example.h>   // IWYU pragma: export

#include <array>
#include <atomic>
#include <clap/helpers/plugin.hh>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace anira::examples::clap_plugin {

static_assert(ExampleLike<Example>);
static_assert(Example::k_channels == 1 || Example::k_channels == 2);

/// Any example (ANIRA_EXAMPLE_HEADER) as a CLAP plugin, the way anira's CLAP example runs its
/// model: the example's parameters, Backend (the engines of this build, then Bypass), Try
/// nonetheless (on: run a backend that is not real-time here) and Mix, the dry signal delayed by
/// the latency; the example's meters as read-only parameters. clap-wrapper makes a VST3 and an AUv2
/// of it.
class Plugin : public clap::helpers::Plugin<clap::helpers::MisbehaviourHandler::Terminate,
                                            clap::helpers::CheckingLevel::Maximal> {
public:
    explicit Plugin(const clap_host* host);

    static const clap_plugin_descriptor* descriptor() noexcept;

protected:
    // clap-helpers' overrides, protected as in clap::helpers::Plugin.
    bool activate(double sample_rate,
                  std::uint32_t min_frames,
                  std::uint32_t max_frames) noexcept override;
    clap_process_status process(const clap_process* process) noexcept override;

    bool implementsParams() const noexcept override { return true; }
    bool isValidParamId(clap_id id) const noexcept override;
    std::uint32_t paramsCount() const noexcept override;
    bool paramsInfo(std::uint32_t index, clap_param_info* info) const noexcept override;
    bool paramsValue(clap_id id, double* value) noexcept override;
    bool paramsValueToText(clap_id id,
                           double value,
                           char* display,
                           std::uint32_t size) noexcept override;
    bool paramsTextToValue(clap_id id, const char* display, double* value) noexcept override;
    void paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept override;

    bool implementsAudioPorts() const noexcept override { return true; }
    std::uint32_t audioPortsCount(bool is_input) const noexcept override;
    bool audioPortsInfo(std::uint32_t index,
                        bool is_input,
                        clap_audio_port_info* info) const noexcept override;

    bool implementsLatency() const noexcept override { return true; }
    std::uint32_t latencyGet() const noexcept override { return m_latency; }

    // clap.render: an offline render runs the Backend whatever its verdict and waits for late
    // blocks, since it does not run against the clock.
    bool implementsRender() const noexcept override { return true; }
    bool renderHasHardRealtimeRequirement() noexcept override { return false; }
    bool renderSetMode(clap_plugin_render_mode mode) noexcept override;

    bool implementsState() const noexcept override { return true; }
    bool stateSave(const clap_ostream* stream) noexcept override;
    bool stateLoad(const clap_istream* stream) noexcept override;

private:
    // The parameters: the example's (ids 0 to N - 1), then the target's.
    static constexpr std::size_t k_example_params = Example::k_parameters.size();
    static constexpr clap_id k_mix = 1000;
    static constexpr clap_id k_backend = 1001;
    static constexpr clap_id k_try_nonetheless = 1002;
    static constexpr std::size_t k_params = k_example_params + 3;
    // The example's meters: read-only parameters (ids 2000 on), the host polls them.
    static constexpr clap_id k_first_meter = 2000;
    static constexpr std::size_t k_meters = meter_count<Example>();

    /// The index of a parameter id in m_values; none for an unknown id.
    static std::optional<std::size_t> index_of(clap_id id) noexcept;
    static clap_id id_of(std::size_t index) noexcept;
    /// Whether `id` is one of the example's meters.
    static bool is_meter(clap_id id) noexcept {
        if constexpr (k_meters == 0) {
            return false;
        } else {
            return id >= k_first_meter && id - k_first_meter < k_meters;
        }
    }
    void handle(const clap_event_header_t* event) noexcept;
    /// Audio thread: the example on the block in m_audio with the chosen plan and the parameters.
    void run(std::size_t samples, bool offline, std::size_t choice) noexcept;

    anira::Context m_context{anira::ContextConfig{}};
    std::unique_ptr<Example> m_example;  // none when its weights are missing
    bool m_prepared = false;
    Backends m_backends;

    std::array<std::atomic<double>, k_params> m_values{};
    std::atomic<bool> m_offline{false};
    std::uint32_t m_latency = 0;

    DryWet m_dry_wet;
    std::array<float*, Example::k_channels> m_channels{};
    anira::Tensor m_audio{};
};

}  // namespace anira::examples::clap_plugin
