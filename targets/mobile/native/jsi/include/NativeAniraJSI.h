// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "AppSpecsJSI.h"

// tanh-lib keeps its classes in an inline ABI namespace, which a forward declaration
// cannot name: the header itself.
#include <tanh/audio-io/AudioDeviceManager.h>

#include <memory>
#include <string>

class ExampleHost;

namespace facebook::react {

/**
 * NativeAniraJSI — a minimal TurboModule bridging React Native and C++.
 *
 * The JS side sends control messages and polls JSON snapshots (state,
 * timelines). The audio path is pure C++: the device's duplex callback
 * (ExampleHost) runs the app's example with no JS involvement.
 */
class NativeAniraJSI : public NativeAniraJSICxxSpec<NativeAniraJSI> {
public:
    explicit NativeAniraJSI(std::shared_ptr<CallInvoker> js_invoker);
    ~NativeAniraJSI();
    NativeAniraJSI(const NativeAniraJSI&) = delete;
    NativeAniraJSI& operator=(const NativeAniraJSI&) = delete;
    NativeAniraJSI(NativeAniraJSI&&) = delete;
    NativeAniraJSI& operator=(NativeAniraJSI&&) = delete;

    jsi::String startAudio(jsi::Runtime& rt);
    void stopAudio(jsi::Runtime& rt);
    jsi::String getState(jsi::Runtime& rt);
    void setParameter(jsi::Runtime& rt, double index, double value);
    void setBackend(jsi::Runtime& rt, double index);
    void setMix(jsi::Runtime& rt, double value);
    void tryNonetheless(jsi::Runtime& rt);
    jsi::String getTimeline(jsi::Runtime& rt, double maxCount);

private:
    void stop();

    std::unique_ptr<thl::AudioDeviceManager> m_audio_device_manager;
    std::unique_ptr<ExampleHost> m_host;

    bool m_audio_running = false;
};

}  // namespace facebook::react
