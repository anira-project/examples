// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace anira::examples {

/// One camera frame: BGRA bytes, unmirrored. `m_pixels` stays valid while a copy of
/// `m_keep_alive` exists, so a consumer may keep the frame beyond the call.
struct Image {
    const std::uint8_t* m_pixels = nullptr;
    int m_width = 0;
    int m_height = 0;
    int m_stride = 0;  ///< bytes per row
    std::shared_ptr<const void> m_keep_alive;

    [[nodiscard]] bool is_valid() const noexcept {
        return m_pixels != nullptr && m_width > 0 && m_height > 0;
    }
};

/// The default camera's frames, at about 1280 x 720. Apple platforms: AVFoundation; elsewhere, or
/// with ANIRA_EXAMPLES_CAMERA=opencv: OpenCV's VideoCapture (V4L2 on Linux).
class Camera {
public:
    using FrameCallback = std::function<void(const Image&)>;        ///< capture thread
    using ErrorCallback = std::function<void(const std::string&)>;  ///< any thread

    Camera(FrameCallback on_frame, ErrorCallback on_error);
    /// Stops capture: no frame callback runs after it returns.
    ~Camera();
    Camera(const Camera&) = delete;
    Camera& operator=(const Camera&) = delete;
    Camera(Camera&&) = delete;
    Camera& operator=(Camera&&) = delete;

    /// Asks for camera permission if needed, then opens the default camera.
    void start();

    struct Impl;

private:
    std::shared_ptr<Impl> m_impl;
};

}  // namespace anira::examples
