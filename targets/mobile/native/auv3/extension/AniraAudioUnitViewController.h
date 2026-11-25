// SPDX-License-Identifier: Apache-2.0

#import <CoreAudioKit/CoreAudioKit.h>

/**
 * The extension's principal class: creates the AniraAudioUnit for the host (AUAudioUnitFactory)
 * and is its plugin view, built from the audio unit's parameter tree — a slider per parameter, the
 * backend as a segmented control, "Try nonetheless" as a switch, a bar per meter. It talks to the
 * audio unit through the AUParameterTree only, the same interface the host uses.
 */
@interface AniraAudioUnitViewController : AUViewController <AUAudioUnitFactory>
@end
