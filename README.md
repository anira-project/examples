# anira examples

The models of [anira-project/models](https://github.com/anira-project/models) in applications:
plugins, standalone apps and the engines' own APIs. `models/` is a submodule; the weights come from
Hugging Face at build time.

## An example

An example is one header, `effects/<name>/Example.h`, that maps a model wrapper onto every target
(`anira::examples::Example`, checked by the concept in `common/include/anira/examples/Example.h`):
its name, bus (mono or stereo), parameters and anira handlers. A model that changes the sound takes
four lines (`EffectExample<Wrapper>`); an example may also have meters (`k_meters`, `meter(i)`: the
VAD's speech probability), which every target shows; `rave-funk-drum-latents` shows an example of its own (the
RAVE encoder and decoder with latent offsets, as anira's RAVE example). One CMake call,
`anira_example()` (`cmake/examples.cmake`), builds it on each target.

| Example | Model | Weights |
|---|---|---|
| `steerable-nafx` | steerable-nafx, a TCN effect | compiled in |
| `guitar-lstm` | GuitarLSTM, an amp model | compiled in |
| `stateful-lstm` | the stateful LSTM | compiled in |
| `silero-vad` | Silero VAD as a speech gate, with the speech probability as a meter | compiled in |
| `rave-funk-drum`, `rave-djembe`, `rave-magnetic-resonator-piano` | RAVE, timbre transfer (CC BY-NC 4.0) | in the bundle |
| `rave-funk-drum-latents` | RAVE funk drum as encoder and decoder, latent offsets | in the bundle |

## Targets

| Target | What | Option |
|---|---|---|
| `targets/juce-plugin` | JUCE: VST3, AU, Standalone | `ANIRA_EXAMPLES_WITH_JUCE` |
| `targets/clap-plugin` | CLAP, and VST3 and AUv2 through clap-wrapper | `ANIRA_EXAMPLES_WITH_CLAP` |
| `targets/standalone` | an app: audio devices through the model (miniaudio), a page in a CHOC WebView; "Audio…" picks input, output and period | `ANIRA_EXAMPLES_WITH_STANDALONE` |
| `targets/max-external` | a Max external per example, `anira.<example>~`: the parameters, `@backend` and `@mix` as attributes, `trynonetheless`, `info` (macOS, Windows) | `ANIRA_EXAMPLES_WITH_MAX` |
| `targets/mobile` | a React Native app (Android, iOS): one example (app.json) between microphone and output, and the same example as an iOS AUv3; its own build (`targets/mobile/README.md`) | — |
| `targets/minimal-inference` | each engine's own API on the effects' weights, without anira | `ANIRA_EXAMPLES_WITH_MINIMAL_INFERENCE` |

Every plugin and app has the example's parameters, a Backend choice (the engines of the build, then
Bypass) with the real-time verdict anira measured at prepare ("Try nonetheless" runs a backend that
is not real-time on this machine), and a dry/wet mix with the dry signal delayed by the reported
latency. An offline render (a bounce) waits for late blocks instead of missing them.

The small effects are on by default (`ANIRA_EXAMPLES_WITH_EFFECTS`); the RAVE examples are
`ANIRA_EXAMPLES_WITH_RAVE` (50 to 130 MB of weights per graph and engine).

## Build

```bash
git clone --recursive https://github.com/anira-project/examples.git && cd examples
just build                      # the debug preset: ONNX Runtime + LiteRT
cmake --preset all && cmake --build --preset all   # with the RAVE examples
```

Requirements: CMake ≥ 3.25, Ninja, clang, `just`. Fetched by CMake: anira, tanh-lib and
nlohmann_json (through models), JUCE 8.0.14, clap 1.2.10, clap-helpers and clap-wrapper 0.18.0
(which downloads the VST3 and AudioUnit SDKs), CHOC, miniaudio 0.11.24 and the Max SDK
(max-sdk-base); on Linux the standalone
needs GTK 3 and WebKitGTK 4.1. Offline builds take local copies as models does
(`FETCHCONTENT_SOURCE_DIR_*`, `ANIRA_MODELS_LOCAL_DIR`, `ANIRA_MODELS_CACHE_DIR`, see models'
README).

## Licence

The sources are [Apache-2.0](LICENSE). A plugin built with JUCE falls under JUCE's licence (AGPL-3.0
or a JUCE licence). Model weights keep their upstream licences (each model's catalog in models).
