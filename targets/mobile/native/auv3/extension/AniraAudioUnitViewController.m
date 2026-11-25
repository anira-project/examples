// SPDX-License-Identifier: Apache-2.0

#import "AniraAudioUnitViewController.h"

#import <auv3/AniraAudioUnit.h>

@implementation AniraAudioUnitViewController {
    AniraAudioUnit* _audioUnit;
    AUParameterObserverToken _observerToken;
    CADisplayLink* _displayLink;
    UIStackView* _stack;
    NSMutableDictionary<NSNumber*, UIControl*>* _controls;     // address -> control
    NSMutableDictionary<NSNumber*, UILabel*>* _values;         // address -> value label
    NSMutableDictionary<NSNumber*, UIProgressView*>* _meters;  // address -> bar
}

#pragma mark - AUAudioUnitFactory

// Called by the host, possibly before the view is loaded and not necessarily on the main thread.
- (AUAudioUnit*)createAudioUnitWithComponentDescription:(AudioComponentDescription)desc
                                                  error:(NSError**)error {
    _audioUnit = [[AniraAudioUnit alloc] initWithComponentDescription:desc error:error];
    dispatch_async(dispatch_get_main_queue(), ^{
        if (self.isViewLoaded) {
            [self connectToAudioUnit];
        }
    });
    return _audioUnit;
}

#pragma mark - View

- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithRed:0.063 green:0.063 blue:0.078 alpha:1];
    self.preferredContentSize = CGSizeMake(360, 300);

    _stack = [[UIStackView alloc] init];
    _stack.axis = UILayoutConstraintAxisVertical;
    _stack.spacing = 14;
    _stack.translatesAutoresizingMaskIntoConstraints = NO;
    [self.view addSubview:_stack];
    UILayoutGuide* guide = self.view.layoutMarginsGuide;
    [NSLayoutConstraint activateConstraints:@[
        [_stack.leadingAnchor constraintEqualToAnchor:guide.leadingAnchor constant:8],
        [_stack.trailingAnchor constraintEqualToAnchor:guide.trailingAnchor constant:-8],
        [_stack.centerYAnchor constraintEqualToAnchor:self.view.centerYAnchor],
    ]];

    if (_audioUnit != nil) {
        [self connectToAudioUnit];
    }
}

- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated];
    _displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(updateMeters)];
    _displayLink.preferredFramesPerSecond = 30;
    [_displayLink addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated];
    [_displayLink invalidate];
    _displayLink = nil;
}

- (void)dealloc {
    if (_observerToken != NULL) {
        [_audioUnit.parameterTree removeParameterObserver:_observerToken];
    }
}

#pragma mark - Building the view from the parameter tree

- (void)connectToAudioUnit {
    if (_observerToken != NULL) {
        return;
    }
    _controls = [NSMutableDictionary dictionary];
    _values = [NSMutableDictionary dictionary];
    _meters = [NSMutableDictionary dictionary];

    [_stack addArrangedSubview:[self labelWithText:[NSString stringWithFormat:@"anira · %@", AniraAudioUnit.exampleName]
                                              size:17
                                            weight:UIFontWeightSemibold]];
    for (AUParameter* parameter in _audioUnit.parameterTree.allParameters) {
        const NSNumber* address = @(parameter.address);
        UILabel* name = [self labelWithText:parameter.displayName size:13 weight:UIFontWeightRegular];
        [name.widthAnchor constraintEqualToConstant:110].active = YES;

        if ((parameter.flags & kAudioUnitParameterFlag_MeterReadOnly) != 0) {
            UIProgressView* bar = [[UIProgressView alloc] initWithProgressViewStyle:UIProgressViewStyleDefault];
            bar.trackTintColor = [UIColor colorWithWhite:1 alpha:0.12];
            _meters[address] = bar;
            [_stack addArrangedSubview:[self rowWithViews:@[ name, bar ]]];
        } else if (parameter.valueStrings != nil) {
            UISegmentedControl* segments = [[UISegmentedControl alloc] initWithItems:parameter.valueStrings];
            segments.tag = (NSInteger)parameter.address;
            [segments addTarget:self action:@selector(controlChanged:) forControlEvents:UIControlEventValueChanged];
            _controls[address] = segments;
            [_stack addArrangedSubview:segments];
        } else if (parameter.unit == kAudioUnitParameterUnit_Boolean) {
            UISwitch* toggle = [[UISwitch alloc] init];
            toggle.tag = (NSInteger)parameter.address;
            [toggle addTarget:self action:@selector(controlChanged:) forControlEvents:UIControlEventValueChanged];
            _controls[address] = toggle;
            [_stack addArrangedSubview:[self rowWithViews:@[ name, [[UIView alloc] init], toggle ]]];
        } else {
            UISlider* slider = [[UISlider alloc] init];
            slider.minimumValue = parameter.minValue;
            slider.maximumValue = parameter.maxValue;
            slider.tag = (NSInteger)parameter.address;
            [slider addTarget:self action:@selector(controlChanged:) forControlEvents:UIControlEventValueChanged];
            UILabel* value = [self labelWithText:@"" size:13 weight:UIFontWeightRegular];
            [value.widthAnchor constraintEqualToConstant:40].active = YES;
            _controls[address] = slider;
            _values[address] = value;
            [_stack addArrangedSubview:[self rowWithViews:@[ name, slider, value ]]];
        }
    }

    // Host -> view: automation, preset recall and state restore all arrive here.
    __weak typeof(self) weakSelf = self;
    _observerToken = [_audioUnit.parameterTree
        tokenByAddingParameterObserver:^(AUParameterAddress address, AUValue value) {
            dispatch_async(dispatch_get_main_queue(), ^{
                [weakSelf updateControls];
            });
        }];
    [self updateControls];
}

- (void)updateControls {
    for (NSNumber* address in _controls) {
        AUParameter* parameter = [_audioUnit.parameterTree parameterWithAddress:address.unsignedLongLongValue];
        UIControl* control = _controls[address];
        if ([control isKindOfClass:UISlider.class]) {
            ((UISlider*)control).value = parameter.value;
            _values[address].text = [NSString stringWithFormat:@"%.2f", parameter.value];
        } else if ([control isKindOfClass:UISwitch.class]) {
            ((UISwitch*)control).on = parameter.value >= 0.5f;
        } else if ([control isKindOfClass:UISegmentedControl.class]) {
            ((UISegmentedControl*)control).selectedSegmentIndex = (NSInteger)(parameter.value + 0.5f);
        }
    }
}

- (void)updateMeters {
    for (NSNumber* address in _meters) {
        AUParameter* parameter = [_audioUnit.parameterTree parameterWithAddress:address.unsignedLongLongValue];
        const float range = parameter.maxValue - parameter.minValue;
        _meters[address].progress = range > 0 ? (parameter.value - parameter.minValue) / range : 0;
    }
}

- (void)controlChanged:(UIControl*)control {
    AUParameter* parameter = [_audioUnit.parameterTree parameterWithAddress:(AUParameterAddress)control.tag];
    AUValue value = 0;
    if ([control isKindOfClass:UISlider.class]) {
        value = ((UISlider*)control).value;
        _values[@(parameter.address)].text = [NSString stringWithFormat:@"%.2f", value];
    } else if ([control isKindOfClass:UISwitch.class]) {
        value = ((UISwitch*)control).on ? 1 : 0;
    } else if ([control isKindOfClass:UISegmentedControl.class]) {
        value = (AUValue)((UISegmentedControl*)control).selectedSegmentIndex;
    }
    [parameter setValue:value originator:_observerToken];
}

#pragma mark - Helpers

- (UILabel*)labelWithText:(NSString*)text size:(CGFloat)size weight:(UIFontWeight)weight {
    UILabel* label = [[UILabel alloc] init];
    label.text = text;
    label.textColor = [UIColor colorWithWhite:0.95 alpha:1];
    label.font = [UIFont systemFontOfSize:size weight:weight];
    return label;
}

- (UIStackView*)rowWithViews:(NSArray<UIView*>*)views {
    UIStackView* row = [[UIStackView alloc] initWithArrangedSubviews:views];
    row.axis = UILayoutConstraintAxisHorizontal;
    row.alignment = UIStackViewAlignmentCenter;
    row.spacing = 12;
    return row;
}

@end
