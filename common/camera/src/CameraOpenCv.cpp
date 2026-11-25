// SPDX-License-Identifier: Apache-2.0

// OpenCV's VideoCapture: Linux and Windows, and anywhere with ANIRA_EXAMPLES_CAMERA=opencv (from
// the workshop's CameraInput_opencv.cpp, without JUCE).

#include <anira/examples/Camera.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <string>
#include <thread>
#include <utility>

namespace anira::examples {

struct Camera::Impl {
    FrameCallback m_on_frame;
    ErrorCallback m_on_error;
    std::atomic<bool> m_running{false};
    std::thread m_thread;

    // Linux: V4L2 directly (OpenCV's default tries GStreamer first, whose errors do not say why),
    // MJPG for full frame rate at 1280x720 on USB webcams. Elsewhere: OpenCV's default backend.
    static void open(cv::VideoCapture& capture) {
#ifdef __linux__
        if (capture.open(0, cv::CAP_V4L2)) {
            capture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
            return;
        }
#endif
        capture.open(0);
    }

    // Capture thread: opening takes a moment and read() blocks until the next frame.
    void run() {
        cv::VideoCapture capture;
        open(capture);
#ifdef __APPLE__
        // OpenCV asks for camera permission on the first open and fails at once: try again for
        // half a minute while the dialog is up.
        for (int attempt = 0; attempt < 30 && m_running.load() && !capture.isOpened(); ++attempt) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            capture.open(0);
        }
#endif
        if (!capture.isOpened()) {
            if (m_running.load()) { m_on_error("No camera found."); }
            return;
        }
        capture.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
        cv::Mat frame;
        int failures = 0;
        while (m_running.load()) {
            if (!capture.read(frame) || frame.empty()) {
                if (++failures == 30) {
                    m_on_error("The camera stopped delivering frames.");
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            failures = 0;
            auto bgra = std::make_shared<cv::Mat>();
            cv::cvtColor(frame,
                         *bgra,
                         frame.channels() == 1 ? cv::COLOR_GRAY2BGRA : cv::COLOR_BGR2BGRA);
            Image image;
            image.m_pixels = bgra->data;
            image.m_width = bgra->cols;
            image.m_height = bgra->rows;
            image.m_stride = static_cast<int>(bgra->step);
            image.m_keep_alive = bgra;
            m_on_frame(image);
        }
    }
};

Camera::Camera(FrameCallback on_frame, ErrorCallback on_error) : m_impl(std::make_shared<Impl>()) {
    m_impl->m_on_frame = std::move(on_frame);
    m_impl->m_on_error = std::move(on_error);
}

Camera::~Camera() {
    m_impl->m_running = false;
    if (m_impl->m_thread.joinable()) { m_impl->m_thread.join(); }
}

void Camera::start() {
    if (m_impl->m_thread.joinable()) { return; }
    m_impl->m_running = true;
    m_impl->m_thread = std::thread([impl = m_impl.get()] { impl->run(); });
}

}  // namespace anira::examples
