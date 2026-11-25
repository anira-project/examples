// SPDX-License-Identifier: Apache-2.0

// Any example (ANIRA_EXAMPLE_HEADER) as a Max external, anira.<example>~ (ANIRA_EXAMPLE_MAX_NAME),
// on the Max SDK's C API: signal in, signal out (the example's channels), the example's parameters
// as attributes (@latent_0 0.3), @backend (the engines of this build, then Bypass) with the
// real-time verdict anira measured, @mix (the dry signal delayed by the latency), "trynonetheless"
// (run a backend that is not real-time here) and "info" (the verdict and the latency in the Max
// window). Every DSP start prepares the example for Max's rate and vector size, a new stream.

#include <anira/anira.hpp>
#include ANIRA_EXAMPLE_HEADER
#include <anira/examples/Backends.h>
#include <anira/examples/DryWet.h>
#include <anira/examples/Example.h>
#include <anira/examples/Weights.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <span>
#include <string>
#include <vector>

// clang-format off
#include "ext.h"
#include "ext_obex.h"
#include "z_dsp.h"
// clang-format on

namespace {

using anira::examples::Example;
static_assert(anira::examples::ExampleLike<Example>);
static_assert(Example::k_channels == 1 || Example::k_channels == 2);
constexpr std::size_t k_parameters = Example::k_parameters.size();

/// What the object holds beyond Max's own fields: the example, the backend choice, the dry/wet mix
/// and the audio as float planes.
struct State {
    anira::Context m_context{anira::ContextConfig{}};
    std::unique_ptr<Example> m_example;  // none when its weights are missing
    std::string m_error;
    anira::examples::Backends m_backends;
    anira::examples::DryWet m_dry_wet;
    std::array<std::vector<float>, Example::k_channels> m_planes;
    std::array<float*, Example::k_channels> m_channels{};
    anira::Tensor m_audio{};
    std::size_t m_block = 0;
    std::size_t m_latency = 0;
    double m_rate = 0.0;
    bool m_prepared = false;
};

/// The Max object. The attributes' values live here (Max sets them through their offsets, clipped
/// to their ranges); the audio thread reads them.
struct Object {
    t_pxobject m_object;
    State* m_state;
    std::array<float, k_parameters == 0 ? 1 : k_parameters> m_parameters;
    float m_mix;
    t_atom_long m_backend;
};

t_class* s_class = nullptr;

void* external_new(t_symbol* /*name*/, long argc, t_atom* argv) {
    auto* x = static_cast<Object*>(object_alloc(s_class));
    if (x == nullptr) { return nullptr; }
    dsp_setup(reinterpret_cast<t_pxobject*>(x), Example::k_channels);
    for (int c = 0; c < Example::k_channels; ++c) { outlet_new(x, "signal"); }
    x->m_object.z_misc |= Z_NO_INPLACE;
    x->m_state = new State();
    for (std::size_t i = 0; i < k_parameters; ++i) {
        x->m_parameters[i] = Example::k_parameters[i].m_default;
    }
    x->m_mix = 1.0F;
    x->m_backend = static_cast<t_atom_long>(x->m_state->m_backends.default_choice());
    try {
        x->m_state->m_example =
            std::make_unique<Example>(x->m_state->m_context, anira::examples::weights());
    } catch (const std::exception& error) {
        x->m_state->m_error = error.what();
        object_error(reinterpret_cast<t_object*>(x), "%s", error.what());
    }
    attr_args_process(x, static_cast<short>(argc), argv);
    return x;
}

void external_free(Object* x) {
    dsp_free(reinterpret_cast<t_pxobject*>(x));
    delete x->m_state;
}

void external_perform64(Object* x,
                        t_object* /*dsp64*/,
                        double** ins,
                        long /*numins*/,
                        double** outs,
                        long /*numouts*/,
                        long sampleframes,
                        long /*flags*/,
                        void* /*userparam*/) {
    State& s = *x->m_state;
    const auto samples = static_cast<std::size_t>(sampleframes);
    if (samples > s.m_block) {  // never: Max gives at most the vector size it prepared
        for (int c = 0; c < Example::k_channels; ++c) { std::fill_n(outs[c], samples, 0.0); }
        return;
    }
    for (std::size_t c = 0; c < s.m_channels.size(); ++c) {
        for (std::size_t i = 0; i < samples; ++i) {
            s.m_planes[c][i] = static_cast<float>(ins[c][i]);
        }
    }
    s.m_dry_wet.push(s.m_channels, samples);
    const auto choice = static_cast<std::size_t>(x->m_backend);
    if (s.m_prepared && s.m_backends.runs(choice, false)) {
        s.m_backends.select(*s.m_example, choice);
        for (std::size_t i = 0; i < k_parameters; ++i) {
            s.m_example->set_parameter(i, x->m_parameters[i]);
        }
        s.m_audio.shape[1] = static_cast<std::int64_t>(samples);
        s.m_example->process(s.m_audio, false);  // in place
        s.m_dry_wet.mix(s.m_channels, samples, x->m_mix);
    } else {
        s.m_dry_wet.dry(s.m_channels, samples);
    }
    for (std::size_t c = 0; c < s.m_channels.size(); ++c) {
        for (std::size_t i = 0; i < samples; ++i) { outs[c][i] = s.m_planes[c][i]; }
    }
}

/// The DSP chain is built (main thread, this object's perform not running): the example prepared
/// for Max's rate and vector size, a new stream.
void external_dsp64(Object* x,
                    t_object* dsp64,
                    short* /*count*/,
                    double samplerate,
                    long maxvectorsize,
                    long /*flags*/) {
    State& s = *x->m_state;
    s.m_rate = samplerate;
    s.m_block = static_cast<std::size_t>(maxvectorsize);
    s.m_prepared = false;
    s.m_latency = 0;
    for (std::size_t c = 0; c < s.m_planes.size(); ++c) {
        s.m_planes[c].assign(s.m_block, 0.0F);
        s.m_channels[c] = s.m_planes[c].data();
    }
    s.m_audio = anira::Tensor::from_host_planar<float>(
        std::span<float* const>(s.m_channels),
        std::array<std::int64_t, 2>{Example::k_channels, static_cast<std::int64_t>(s.m_block)});
    if (s.m_example) {
        try {
            s.m_example->prepare(samplerate, s.m_block);
            for (const std::string& line : s.m_backends.read(*s.m_example)) {
                object_post(reinterpret_cast<t_object*>(x), "%s", line.c_str());
            }
            s.m_latency = s.m_example->get_latency();
            s.m_prepared = true;
            object_post(reinterpret_cast<t_object*>(x),
                        "latency %zu samples (%.1f ms)",
                        s.m_latency,
                        1000.0 * static_cast<double>(s.m_latency) / samplerate);
        } catch (const std::exception& error) {
            s.m_error = error.what();
            object_error(reinterpret_cast<t_object*>(x), "%s", error.what());
        }
    }
    s.m_dry_wet.prepare(s.m_channels.size(), s.m_block, s.m_latency);
    object_method(dsp64,
                  gensym("dsp_add64"),
                  x,
                  reinterpret_cast<method>(external_perform64),
                  0,
                  nullptr);
}

void external_info(Object* x) {
    const State& s = *x->m_state;
    if (!s.m_error.empty()) {
        object_post(reinterpret_cast<t_object*>(x),
                    "the model does not run: %s",
                    s.m_error.c_str());
        return;
    }
    const std::string line = s.m_backends.verdict_line(static_cast<std::size_t>(x->m_backend));
    object_post(reinterpret_cast<t_object*>(x),
                "%s; latency %zu samples at %.0f Hz",
                line.empty() ? "plays in real time" : line.c_str(),
                s.m_latency,
                s.m_rate);
}

void external_try_nonetheless(Object* x) {
    x->m_state->m_backends.try_nonetheless(static_cast<std::size_t>(x->m_backend));
}

void external_assist(Object* /*x*/, void* /*b*/, long io, long index, char* text) {
    const char* side = Example::k_channels == 2 ? (index == 0 ? " left" : " right") : "";
    std::snprintf(text, 256, "(signal)%s %s", side, io == ASSIST_INLET ? "in" : "out");
}

}  // namespace

extern "C" C74_EXPORT void ext_main(void* /*r*/) {
    t_class* c = class_new(ANIRA_EXAMPLE_MAX_NAME,
                           reinterpret_cast<method>(external_new),
                           reinterpret_cast<method>(external_free),
                           sizeof(Object),
                           nullptr,
                           A_GIMME,
                           0);
    class_addmethod(c, reinterpret_cast<method>(external_dsp64), "dsp64", A_CANT, 0);
    class_addmethod(c, reinterpret_cast<method>(external_assist), "assist", A_CANT, 0);
    class_addmethod(c, reinterpret_cast<method>(external_info), "info", 0);
    class_addmethod(c, reinterpret_cast<method>(external_try_nonetheless), "trynonetheless", 0);

    // The example's parameters, then the target's.
    for (std::size_t i = 0; i < k_parameters; ++i) {
        const anira::examples::Parameter& p = Example::k_parameters[i];
        const std::string id(p.m_id);
        t_object* attr = attr_offset_new(
            id.c_str(),
            gensym("float32"),
            0,
            nullptr,
            nullptr,
            static_cast<long>(offsetof(Object, m_parameters) + (i * sizeof(float))));
        attr_addfilter_clip(attr, p.m_min, p.m_max, 1, 1);
        class_addattr(c, attr);
        const std::string label = "\"" + std::string(p.m_name) + "\"";
        class_attr_addattr_parse(c, id.c_str(), "label", gensym("symbol"), 0, label.c_str());
    }
    t_object* mix = attr_offset_new("mix",
                                    gensym("float32"),
                                    0,
                                    nullptr,
                                    nullptr,
                                    static_cast<long>(offsetof(Object, m_mix)));
    attr_addfilter_clip(mix, 0.0, 1.0, 1, 1);
    class_addattr(c, mix);
    class_attr_addattr_parse(c, "mix", "label", gensym("symbol"), 0, "\"Dry/Wet\"");

    const anira::examples::Backends backends;
    t_object* backend = attr_offset_new("backend",
                                        gensym("long"),
                                        0,
                                        nullptr,
                                        nullptr,
                                        static_cast<long>(offsetof(Object, m_backend)));
    attr_addfilter_clip(backend, 0, static_cast<double>(backends.choices().size() - 1), 1, 1);
    class_addattr(c, backend);
    std::string names;
    for (const anira::examples::BackendChoice& choice : backends.choices()) {
        names += (names.empty() ? "\"" : " \"") + std::string(choice.m_name) + "\"";
    }
    class_attr_addattr_parse(c, "backend", "style", gensym("symbol"), 0, "enumindex");
    class_attr_addattr_parse(c, "backend", "enumvals", gensym("atom"), 0, names.c_str());
    class_attr_addattr_parse(c, "backend", "label", gensym("symbol"), 0, "Backend");

    class_dspinit(c);
    class_register(CLASS_BOX, c);
    s_class = c;
}
