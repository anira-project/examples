#import <auv3/AniraAudioUnit.h>

#import <AVFoundation/AVFoundation.h>

#include <host/ExampleEngine.h>

#include <cstddef>
#include <string>
#include <vector>

namespace {

constexpr AUAudioFrameCount k_default_max_frames = 4096;

// Everything the render block touches besides the kernel. Captured by raw
// pointer: the render block must not retain (or message) the audio unit.
struct RenderState {
    AudioBufferList* input_abl = nullptr;
    std::vector<float*> input_storage;
    std::vector<const float*> in;
    std::vector<float*> out;
    AUAudioFrameCount max_frames = 0;
};

// The one place a parameter address meets the engine. `realtime` is set for host automation on
// the render thread (every engine setter is an atomic store, safe there as well).
void set_parameter(ExampleEngine& engine, AUParameterAddress address, AUValue value, bool /*realtime*/) {
    if (address < ExampleEngine::Example::k_parameters.size()) {
        engine.set_parameter(static_cast<std::size_t>(address), value);
        return;
    }
    switch (address) {
        case AniraParameterBackend:
            engine.set_backend(static_cast<std::size_t>(value + 0.5f));
            break;
        case AniraParameterMix:
            engine.set_mix(value);
            break;
        case AniraParameterTryNonetheless:
            engine.set_try_nonetheless(value >= 0.5f);
            break;
        default:
            break;
    }
}

AUValue get_parameter(const ExampleEngine& engine, AUParameterAddress address) {
    if (address < ExampleEngine::Example::k_parameters.size()) {
        return engine.parameter(static_cast<std::size_t>(address));
    }
    if (address >= AniraParameterFirstMeter &&
        address < AniraParameterFirstMeter + ExampleEngine::k_meters) {
        return engine.meter(static_cast<std::size_t>(address - AniraParameterFirstMeter));
    }
    switch (address) {
        case AniraParameterBackend:
            return static_cast<AUValue>(engine.backend());
        case AniraParameterMix:
            return engine.mix();
        default:
            return 0.f;
    }
}

NSString* string_of(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
}

}  // namespace

@implementation AniraAudioUnit {
    ExampleEngine _kernel;
    RenderState _renderState;

    AUAudioUnitBus* _inputBus;
    AUAudioUnitBus* _outputBus;
    AUAudioUnitBusArray* _inputBusArray;
    AUAudioUnitBusArray* _outputBusArray;
    AVAudioPCMBuffer* _inputBuffer;

    double _sampleRate;
}

@synthesize parameterTree = _parameterTree;

+ (NSString*)exampleName {
    return string_of(ExampleEngine::Example::k_name);
}

- (nullable instancetype)initWithComponentDescription:(AudioComponentDescription)componentDescription
                                              options:(AudioComponentInstantiationOptions)options
                                                error:(NSError**)outError {
    self = [super initWithComponentDescription:componentDescription options:options error:outError];
    if (self == nil) {
        return nil;
    }

    AVAudioFormat* format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000.0 channels:2];
    _inputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
    _outputBus = [[AUAudioUnitBus alloc] initWithFormat:format error:outError];
    if (_inputBus == nil || _outputBus == nil) {
        return nil;
    }
    _inputBus.maximumChannelCount = 2;
    _outputBus.maximumChannelCount = 2;
    _inputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                            busType:AUAudioUnitBusTypeInput
                                                             busses:@[ _inputBus ]];
    _outputBusArray = [[AUAudioUnitBusArray alloc] initWithAudioUnit:self
                                                             busType:AUAudioUnitBusTypeOutput
                                                              busses:@[ _outputBus ]];

    self.maximumFramesToRender = k_default_max_frames;

    [self setupParameterTree];
    return self;
}

#pragma mark - Parameters & state

// The AUParameterTree is the host-facing parameter model (what JUCE's APVTS
// is on the JUCE side): the host lists, automates and displays these
// parameters, and the view binds to the same objects. The kernel's atomics
// are the single source of truth — the tree reads and writes them through
// the two implementor blocks below.
//
// State comes for free: AUAudioUnit's default `fullState` stores every
// writable parameter of the tree and restores it through the value observer,
// so host sessions and user presets work without further code. Only state
// that is *not* a parameter would need a fullState override.
- (void)setupParameterTree {
    const AudioUnitParameterOptions writable =
        kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_IsWritable;
    const AudioUnitParameterOptions meter =
        kAudioUnitParameterFlag_IsReadable | kAudioUnitParameterFlag_MeterReadOnly;
    using Example = ExampleEngine::Example;

    NSMutableArray<AUParameter*>* parameters = [NSMutableArray array];
    for (std::size_t i = 0; i < Example::k_parameters.size(); ++i) {
        const anira::examples::Parameter& p = Example::k_parameters[i];
        [parameters addObject:[AUParameterTree createParameterWithIdentifier:string_of(p.m_id)
                                                                         name:string_of(p.m_name)
                                                                      address:i
                                                                          min:p.m_min
                                                                          max:p.m_max
                                                                         unit:kAudioUnitParameterUnit_Generic
                                                                     unitName:nil
                                                                        flags:writable
                                                                 valueStrings:nil
                                                          dependentParameters:nil]];
    }

    NSMutableArray<NSString*>* backendNames = [NSMutableArray array];
    for (const anira::examples::BackendChoice& choice : _kernel.backends()) {
        [backendNames addObject:@(choice.m_name)];
    }
    [parameters addObject:[AUParameterTree createParameterWithIdentifier:@"backend"
                                                                     name:@"Backend"
                                                                  address:AniraParameterBackend
                                                                      min:0
                                                                      max:backendNames.count - 1
                                                                     unit:kAudioUnitParameterUnit_Indexed
                                                                 unitName:nil
                                                                    flags:writable
                                                             valueStrings:backendNames
                                                      dependentParameters:nil]];
    [parameters addObject:[AUParameterTree createParameterWithIdentifier:@"mix"
                                                                     name:@"Dry/Wet"
                                                                  address:AniraParameterMix
                                                                      min:0
                                                                      max:1
                                                                     unit:kAudioUnitParameterUnit_Generic
                                                                 unitName:nil
                                                                    flags:writable
                                                             valueStrings:nil
                                                      dependentParameters:nil]];
    [parameters addObject:[AUParameterTree createParameterWithIdentifier:@"trynonetheless"
                                                                     name:@"Try nonetheless"
                                                                  address:AniraParameterTryNonetheless
                                                                      min:0
                                                                      max:1
                                                                     unit:kAudioUnitParameterUnit_Boolean
                                                                 unitName:nil
                                                                    flags:writable
                                                             valueStrings:nil
                                                      dependentParameters:nil]];
    for (std::size_t i = 0; i < ExampleEngine::k_meters; ++i) {
        const anira::examples::Meter m = anira::examples::meter_info<Example>(i);
        [parameters addObject:[AUParameterTree createParameterWithIdentifier:string_of(m.m_id)
                                                                         name:string_of(m.m_name)
                                                                      address:AniraParameterFirstMeter + i
                                                                          min:m.m_min
                                                                          max:m.m_max
                                                                         unit:kAudioUnitParameterUnit_Generic
                                                                     unitName:nil
                                                                        flags:meter
                                                                 valueStrings:nil
                                                          dependentParameters:nil]];
    }

    _parameterTree = [AUParameterTree createTreeWithChildren:parameters];

    ExampleEngine* kernel = &_kernel;
    _parameterTree.implementorValueObserver = ^(AUParameter* parameter, AUValue value) {
        set_parameter(*kernel, parameter.address, value, false);
    };
    _parameterTree.implementorValueProvider = ^AUValue(AUParameter* parameter) {
        return get_parameter(*kernel, parameter.address);
    };
    _parameterTree.implementorStringFromValueCallback =
        ^NSString*(AUParameter* parameter, const AUValue* valuePtr) {
            const AUValue value = valuePtr ? *valuePtr : parameter.value;
            if (parameter.valueStrings != nil) {
                return parameter.valueStrings[static_cast<NSUInteger>(value + 0.5f)];
            }
            if (parameter.address == AniraParameterTryNonetheless) {
                return value >= 0.5f ? @"On" : @"Off";
            }
            return [NSString stringWithFormat:@"%.2f", value];
        };
}

- (BOOL)supportsUserPresets {
    return YES;
}

- (void)setShouldBypassEffect:(BOOL)shouldBypassEffect {
    [super setShouldBypassEffect:shouldBypassEffect];
    // The host bypasses: the engine keeps running, so the latency never changes; the host
    // passes the input through, delay compensated.
}

#pragma mark - Buses

- (AUAudioUnitBusArray*)inputBusses {
    return _inputBusArray;
}

- (AUAudioUnitBusArray*)outputBusses {
    return _outputBusArray;
}

- (NSArray<NSNumber*>*)channelCapabilities {
    return @[ @1, @1, @2, @2 ];
}

- (BOOL)canProcessInPlace {
    return YES;
}

- (NSTimeInterval)latency {
    return _sampleRate > 0 ? static_cast<double>(_kernel.latency()) / _sampleRate : 0;
}

#pragma mark - Rendering

- (BOOL)allocateRenderResourcesAndReturnError:(NSError**)outError {
    AVAudioFormat* format = _outputBus.format;
    if (_inputBus.format.channelCount != format.channelCount ||
        _inputBus.format.sampleRate != format.sampleRate) {
        if (outError) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                            code:kAudioUnitErr_FormatNotSupported
                                        userInfo:nil];
        }
        return NO;
    }
    if (![super allocateRenderResourcesAndReturnError:outError]) {
        return NO;
    }

    const AUAudioFrameCount maxFrames = self.maximumFramesToRender;
    const uint32_t channels = format.channelCount;

    [self willChangeValueForKey:@"latency"];
    _sampleRate = format.sampleRate;
    _kernel.prepare(_sampleRate, maxFrames);
    const bool prepared = true;
    [self didChangeValueForKey:@"latency"];
    if (!prepared) {
        if (outError) {
            *outError = [NSError errorWithDomain:NSOSStatusErrorDomain
                                            code:kAudioUnitErr_FailedInitialization
                                        userInfo:nil];
        }
        return NO;
    }

    _inputBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:_inputBus.format frameCapacity:maxFrames];
    _renderState.input_abl = _inputBuffer.mutableAudioBufferList;
    _renderState.input_storage.assign(_inputBuffer.floatChannelData,
                                      _inputBuffer.floatChannelData + channels);
    _renderState.in.assign(channels, nullptr);
    _renderState.out.assign(channels, nullptr);
    _renderState.max_frames = maxFrames;
    return YES;
}

- (void)deallocateRenderResources {
    [super deallocateRenderResources];
    _renderState = RenderState{};
    _inputBuffer = nil;
}

- (AUInternalRenderBlock)internalRenderBlock {
    ExampleEngine* kernel = &_kernel;
    RenderState* state = &_renderState;

    return ^AUAudioUnitStatus(AudioUnitRenderActionFlags* actionFlags, const AudioTimeStamp* timestamp,
                              AUAudioFrameCount frameCount, NSInteger outputBusNumber,
                              AudioBufferList* outputData, const AURenderEvent* realtimeEventListHead,
                              AURenderPullInputBlock pullInputBlock) {
        if (frameCount > state->max_frames) {
            return kAudioUnitErr_TooManyFramesToProcess;
        }
        if (pullInputBlock == nullptr) {
            return kAudioUnitErr_NoConnection;
        }

        // Host automation. Applied at the block start — threshold and gate do
        // not need sample-accurate timing.
        for (const AURenderEvent* event = realtimeEventListHead; event != nullptr;
             event = event->head.next) {
            if (event->head.eventType == AURenderEventParameter ||
                event->head.eventType == AURenderEventParameterRamp) {
                set_parameter(*kernel, event->parameter.parameterAddress, event->parameter.value, true);
            }
        }

        // Pull the input into our own buffer (the host may swap in its own).
        const UInt32 channels = static_cast<UInt32>(state->in.size());
        AudioBufferList* input = state->input_abl;
        for (UInt32 ch = 0; ch < channels; ++ch) {
            input->mBuffers[ch].mData = state->input_storage[ch];
            input->mBuffers[ch].mDataByteSize = frameCount * sizeof(float);
        }
        AudioUnitRenderActionFlags pullFlags = 0;
        const AUAudioUnitStatus status = pullInputBlock(&pullFlags, timestamp, frameCount, 0, input);
        if (status != noErr) {
            return status;
        }

        for (UInt32 ch = 0; ch < channels; ++ch) {
            // A null output buffer means "process in place".
            if (outputData->mBuffers[ch].mData == nullptr) {
                outputData->mBuffers[ch].mData = input->mBuffers[ch].mData;
            }
            state->in[ch] = static_cast<const float*>(input->mBuffers[ch].mData);
            state->out[ch] = static_cast<float*>(outputData->mBuffers[ch].mData);
        }

        kernel->process(state->in.data(), state->out.data(), frameCount, channels, channels);
        return noErr;
    };
}

@end
