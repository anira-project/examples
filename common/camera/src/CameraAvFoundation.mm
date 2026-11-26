// SPDX-License-Identifier: Apache-2.0

// Apple platforms: AVFoundation's video data output, BGRA frames as they come (from the
// workshop's CameraInput.mm, without JUCE).

#include <anira/examples/Camera.h>

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>

#include <memory>
#include <string>
#include <utility>

namespace anira::examples {
struct FrameSink {
    virtual ~FrameSink() = default;
    virtual void deliver(CMSampleBufferRef sample_buffer) = 0;
};
}  // namespace anira::examples

@interface AniraCameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, assign) anira::examples::FrameSink* owner;
@end

namespace anira::examples {

struct Camera::Impl : FrameSink {
    FrameCallback m_on_frame;
    ErrorCallback m_on_error;
    AVCaptureSession* m_session = nil;
    AniraCameraDelegate* m_delegate = nil;
    dispatch_queue_t m_queue = nullptr;

    void open() {
        AVCaptureDevice* device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        if (device == nil) {
            m_on_error("No camera found.");
            return;
        }
        NSError* error = nil;
        AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device
                                                                            error:&error];
        if (input == nil) {
            m_on_error(std::string("Cannot open the camera: ") +
                       error.localizedDescription.UTF8String);
            return;
        }
        m_session = [[AVCaptureSession alloc] init];
        if ([m_session canSetSessionPreset:AVCaptureSessionPreset1280x720]) {
            m_session.sessionPreset = AVCaptureSessionPreset1280x720;
        }
        if ([m_session canAddInput:input]) { [m_session addInput:input]; }
        AVCaptureVideoDataOutput* output = [[AVCaptureVideoDataOutput alloc] init];
        output.videoSettings =
            @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA)};
        output.alwaysDiscardsLateVideoFrames = YES;
        m_delegate = [[AniraCameraDelegate alloc] init];
        m_delegate.owner = this;
        m_queue = dispatch_queue_create("org.anira-project.examples.camera", DISPATCH_QUEUE_SERIAL);
        [output setSampleBufferDelegate:m_delegate queue:m_queue];
        if ([m_session canAddOutput:output]) { [m_session addOutput:output]; }
        // startRunning blocks until the session is up: off the caller's thread.
        AVCaptureSession* running = m_session;
        dispatch_async(m_queue, ^{ [running startRunning]; });
    }

    void close() {
        if (m_session == nil) { return; }
        [m_session stopRunning];
        for (AVCaptureOutput* output in m_session.outputs) {
            if ([output isKindOfClass:[AVCaptureVideoDataOutput class]]) {
                [(AVCaptureVideoDataOutput*)output setSampleBufferDelegate:nil queue:nil];
            }
        }
        dispatch_sync(m_queue, ^{});  // drain a callback in flight
        m_delegate.owner = nullptr;
        m_session = nil;
        m_delegate = nil;
    }

    void deliver(CMSampleBufferRef sample_buffer) override {
        CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sample_buffer);
        if (pixels == nullptr) { return; }
        // The frame keeps the pixel buffer, locked, while anyone holds it.
        CVPixelBufferRetain(pixels);
        CVPixelBufferLockBaseAddress(pixels, kCVPixelBufferLock_ReadOnly);
        Image image;
        image.m_pixels = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(pixels));
        image.m_width = static_cast<int>(CVPixelBufferGetWidth(pixels));
        image.m_height = static_cast<int>(CVPixelBufferGetHeight(pixels));
        image.m_stride = static_cast<int>(CVPixelBufferGetBytesPerRow(pixels));
        image.m_keep_alive = std::shared_ptr<const void>(pixels, [](const void* p) {
            auto* buffer = static_cast<CVPixelBufferRef>(const_cast<void*>(p));
            CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
            CVPixelBufferRelease(buffer);
        });
        m_on_frame(image);
    }
};

}  // namespace anira::examples

@implementation AniraCameraDelegate
- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
    if (self.owner != nullptr) { self.owner->deliver(sampleBuffer); }
}
@end

namespace anira::examples {

Camera::Camera(FrameCallback on_frame, ErrorCallback on_error) : m_impl(std::make_shared<Impl>()) {
    m_impl->m_on_frame = std::move(on_frame);
    m_impl->m_on_error = std::move(on_error);
}

Camera::~Camera() {
    m_impl->close();
}

void Camera::start() {
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
        case AVAuthorizationStatusAuthorized: m_impl->open(); return;
        case AVAuthorizationStatusNotDetermined: {
            // The answer may arrive after this Camera is gone.
            std::weak_ptr<Impl> weak = m_impl;
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                     completionHandler:^(BOOL granted) {
                                         dispatch_async(dispatch_get_main_queue(), ^{
                                             if (auto impl = weak.lock()) {
                                                 if (granted) {
                                                     impl->open();
                                                 } else {
                                                     impl->m_on_error("Camera access denied.");
                                                 }
                                             }
                                         });
                                     }];
            return;
        }
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
            m_impl->m_on_error(
                "Camera access denied: allow it in System Settings > Privacy & Security > Camera.");
            return;
    }
}

}  // namespace anira::examples
