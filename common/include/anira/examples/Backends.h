// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <anira/anira.h>
#include <anira/examples/Example.h>

#include <anira/anira.hpp>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace anira::examples {

/// A "Backend" choice: the name the parameter shows and the plans it stands for; no query is
/// Bypass (the dry signal, time-aligned, and no inference).
struct BackendChoice {
    const char* m_name;
    std::optional<anira::PlanQuery> m_query;
};

/// The backend choice of a target, as in anira's plugin examples: the engines this build has, then
/// Bypass. After every prepare, per choice, each handler's plan and the worst real-time verdict of
/// those plans; a choice that is not real-time on this machine stays bypassed until the user
/// agrees ("Try nonetheless"), except in an offline render, which does not run against the clock.
class Backends {
public:
    static constexpr std::size_t k_max_choices = 8;
    static constexpr std::size_t k_max_handlers = 4;

    /// `engines_to_choose`: the example has handlers whose engine the user picks; without
    /// (k_handlers 0: its models pick their engines themselves) the choices are Automatic and
    /// Bypass.
    explicit Backends(bool engines_to_choose = true);

    [[nodiscard]] const std::vector<BackendChoice>& choices() const noexcept { return m_choices; }
    /// ONNX Runtime where the build has it, else the first engine.
    [[nodiscard]] std::size_t default_choice() const noexcept;

    /// Main thread, after the example's prepare, no process() running: what prepare measured, per
    /// choice. Returns a line per choice for the log.
    template <ExampleLike E>
    std::vector<std::string> read(const E& example) {
        static_assert(E::k_handlers <= k_max_handlers);
        std::vector<std::string> log;
        for (std::size_t choice = 0; choice < m_choices.size(); ++choice) {
            begin(choice);
            if (!m_choices[choice].m_query) { continue; }
            for (std::size_t handler = 0; handler < E::k_handlers; ++handler) {
                add(choice, handler, example.plan_report(handler), log);
            }
            end(choice, log);
        }
        m_selected = k_none;
        return log;
    }

    /// Audio thread: whether `choice` runs the model, or passes the dry signal through. `anyway`
    /// (an offline render, which does not run against the clock, or the user's consent) runs a
    /// choice that is not real-time here.
    [[nodiscard]] bool runs(std::size_t choice, bool anyway) const noexcept;
    /// Audio thread: the plans of `choice` on the example's handlers (once per change).
    template <ExampleLike E>
    void select(E& example, std::size_t choice) noexcept {
        if (choice == m_selected || choice >= m_choices.size()) { return; }
        for (std::size_t handler = 0; handler < E::k_handlers; ++handler) {
            if (const auto plan = m_plans[choice][handler]) { example.set_plan(handler, *plan); }
        }
        m_selected = choice;
    }

    /// Message thread: the line about the choice's verdict (empty when it plays without a note),
    /// whether it waits for "Try nonetheless", and the click that accepts it.
    [[nodiscard]] std::string verdict_line(std::size_t choice) const;
    [[nodiscard]] bool waits_for_consent(std::size_t choice) const noexcept;
    void try_nonetheless(std::size_t choice) noexcept;

private:
    static constexpr std::size_t k_none = ~std::size_t{0};

    struct Verdict {
        std::atomic<bool> m_has_plan{true};
        std::atomic<std::uint32_t> m_verdict{ANIRA_RT_UNMEASURED};
        std::atomic<float> m_rt_factor{0.0F};
        std::atomic<std::uint32_t> m_rt_parallel{1};
        // The worst verdict the user accepted, kept across prepares: asked again only when worse.
        std::atomic<std::uint32_t> m_accepted{ANIRA_RT_UNMEASURED};
    };

    void begin(std::size_t choice) noexcept;
    void add(std::size_t choice,
             std::size_t handler,
             const anira::PlanReport& report,
             std::vector<std::string>& log);
    void end(std::size_t choice, std::vector<std::string>& log);
    [[nodiscard]] bool held_back(std::size_t choice) const noexcept;

    std::vector<BackendChoice> m_choices;
    std::array<std::array<std::optional<std::uint32_t>, k_max_handlers>, k_max_choices> m_plans{};
    std::array<Verdict, k_max_choices> m_verdicts{};
    std::size_t m_selected = k_none;
};

}  // namespace anira::examples
