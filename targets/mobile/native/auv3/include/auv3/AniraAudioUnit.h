// SPDX-License-Identifier: Apache-2.0

#pragma once

#import <AudioToolbox/AudioToolbox.h>

NS_ASSUME_NONNULL_BEGIN

/**
 * Parameter addresses of the AniraAudioUnit's AUParameterTree — the contract between the audio
 * unit, its view and the host, persisted in host sessions and presets. The example's parameters
 * are 0 to N - 1 (in the order of its k_parameters), then the target's, then the example's meters
 * (read-only).
 */
typedef NS_ENUM(AUParameterAddress, AniraParameter) {
    AniraParameterBackend = 1000,         ///< the inference backend (indexed; the last is Bypass)
    AniraParameterMix = 1001,             ///< dry/wet, 0..1
    AniraParameterTryNonetheless = 1002,  ///< run a backend that is not real-time here
    AniraParameterFirstMeter = 2000,      ///< meters: 2000 + the meter's index
};

/**
 * AniraAudioUnit — the app's example (any example of the examples repository) as an AUv3 effect,
 * the way every target runs it. Pure Objective-C interface; the C++ (anira, the example and its
 * engine) stays behind it in libanira_mobile.a.
 */
@interface AniraAudioUnit : AUAudioUnit
/** The example's name, for the view's title. */
@property(class, nonatomic, readonly) NSString* exampleName;
@end

NS_ASSUME_NONNULL_END
