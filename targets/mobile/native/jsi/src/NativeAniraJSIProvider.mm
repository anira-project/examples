#import "NativeAniraJSIProvider.h"
#import <ReactCommon/CallInvoker.h>
#import <ReactCommon/TurboModule.h>
#import <NativeAniraJSI.h>

@implementation NativeAniraJSIProvider

- (std::shared_ptr<facebook::react::TurboModule>)getTurboModule:
    (const facebook::react::ObjCTurboModule::InitParams &)params
{
  return std::make_shared<facebook::react::NativeAniraJSI>(params.jsInvoker);
}

@end
