#import "NullPeratorNativeBridge.h"

#include "Adapters/ios/gui/IOSUiPresenter.h"
#include "Adapters/ios/runtime/IOSNativeRuntime.h"
#include "Application/Model/ProjectVersion.h"

#include <algorithm>
#include <cstdint>
#include <dispatch/dispatch.h>
#include <memory>
#include <string>

namespace {
NSString *Base64(const std::uint8_t *bytes, std::size_t size) {
  NSData *data = [NSData dataWithBytes:bytes length:size];
  return [data base64EncodedStringWithOptions:0];
}
} // namespace

@implementation NullPeratorNativeBridge {
  std::unique_ptr<IOSNativeRuntime> _runtime;
  dispatch_queue_t _runtimeQueue;
  dispatch_source_t _runtimeTimer;
  BOOL _initialized;
}

- (instancetype)init {
  self = [super init];
  if (self == nil) return nil;
  NSURL *documents = [[[NSFileManager alloc] init]
      URLsForDirectory:NSDocumentDirectory
             inDomains:NSUserDomainMask].firstObject;
  const char *path = documents.fileSystemRepresentation;
  _runtime = std::make_unique<IOSNativeRuntime>(
      path == nullptr ? std::string() : std::string(path));
  _initialized = _runtime->Init();
  _runtimeQueue = dispatch_queue_create("io.nullperator.runtime",
                                        DISPATCH_QUEUE_SERIAL);
  if (_initialized) {
    _runtimeTimer = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, 0U, 0U, _runtimeQueue);
    IOSNativeRuntime *runtime = _runtime.get();
    dispatch_source_set_timer(
        _runtimeTimer, dispatch_time(DISPATCH_TIME_NOW, 0),
        NSEC_PER_SEC / 60U, NSEC_PER_MSEC);
    dispatch_source_set_event_handler(_runtimeTimer, ^{
      runtime->Tick();
    });
    dispatch_resume(_runtimeTimer);
  }
  return self;
}

- (void)dealloc {
  if (_runtimeTimer != nil) dispatch_source_cancel(_runtimeTimer);
  if (_runtimeQueue != nil) {
    IOSNativeRuntime *runtime = _runtime.get();
    dispatch_sync(_runtimeQueue, ^{
      if (runtime != nullptr) runtime->Shutdown();
    });
  }
  _runtime.reset();
  [super dealloc];
}

- (BOOL)isInitialized { return _initialized; }

- (NSString *)nullPeratorVersion {
  return [NSString stringWithUTF8String:PROJECT_NUMBER];
}

- (NSString *)buildHash {
  return [NSString stringWithUTF8String:IOSNativeRuntime::BuildHash()];
}

- (NSString *)buildTime {
  return [NSString stringWithUTF8String:IOSNativeRuntime::BuildTime()];
}

- (void)setAction:(NPTrackerAction)action
          pressed:(BOOL)pressed
         repeated:(BOOL)repeated {
  if (!_initialized) return;
  IOSNativeRuntime *runtime = _runtime.get();
  const auto nativeAction = static_cast<std::uint8_t>(action);
  dispatch_sync(_runtimeQueue, ^{
    runtime->SetAction(nativeAction, pressed, repeated);
  });
}

- (void)releaseAllActions {
  if (!_initialized) return;
  IOSNativeRuntime *runtime = _runtime.get();
  dispatch_sync(_runtimeQueue, ^{
    runtime->ReleaseAllActions();
  });
}

- (NSDictionary<NSString *, id> *)framePacketSince:(NSUInteger)sequence {
  IOSUiFramePacket packet;
  IOSUiFramePacket *output = &packet;
  __block bool changed = false;
  if (_initialized && _runtime != nullptr) {
    IOSNativeRuntime *runtime = _runtime.get();
    const auto afterSequence = static_cast<std::uint32_t>(sequence);
    dispatch_sync(_runtimeQueue, ^{
      changed = runtime->DrainFrame(afterSequence, *output);
    });
  }
  if (!changed) {
    return @{
      @"version" : @1,
      @"sequence" : @(packet.sequence),
      @"changed" : @NO,
    };
  }

  NSMutableArray<NSDictionary<NSString *, id> *> *regions =
      [NSMutableArray arrayWithCapacity:packet.regions.size()];
  for (const IOSUiFrameRegion &region : packet.regions) {
    [regions addObject:@{
      @"x" : @(region.bounds.x),
      @"y" : @(region.bounds.y),
      @"width" : @(region.bounds.width),
      @"height" : @(region.bounds.height),
      @"indices" : Base64(region.indices.data(), region.indices.size()),
    }];
  }
  return @{
    @"version" : @1,
    @"sequence" : @(packet.sequence),
    @"changed" : @YES,
    @"width" : @(IOSNativeRuntime::FrameWidth),
    @"height" : @(IOSNativeRuntime::FrameHeight),
    @"palette" : Base64(packet.palette.data(), packet.palette.size()),
    @"regions" : regions,
  };
}

- (void)setBatteryPercentage:(NSInteger)percentage
                    charging:(BOOL)charging
                   available:(BOOL)available {
  if (_runtime == nullptr) return;
  const auto clamped = static_cast<std::uint8_t>(
      std::clamp<NSInteger>(percentage, 0, 100));
  IOSNativeRuntime *runtime = _runtime.get();
  dispatch_sync(_runtimeQueue, ^{
    runtime->SetBattery(clamped, charging, available);
  });
}

- (BOOL)submitMidiData:(NSData *)data timestamp:(NSTimeInterval)timestamp {
  if (!_initialized || _runtime == nullptr || data.length == 0) return NO;
  IOSNativeRuntime *runtime = _runtime.get();
  __block bool accepted = false;
  dispatch_sync(_runtimeQueue, ^{
    accepted = runtime->SubmitMidi(
        static_cast<const std::uint8_t *>(data.bytes), data.length, timestamp);
  });
  return accepted;
}

- (NSDictionary<NSString *, id> *)drainMidi {
  IOSNativeRuntime::MidiDrain drain;
  IOSNativeRuntime::MidiDrain *output = &drain;
  if (_initialized && _runtime != nullptr) {
    IOSNativeRuntime *runtime = _runtime.get();
    dispatch_sync(_runtimeQueue, ^{ *output = runtime->DrainMidi(); });
  }
  NSMutableArray<NSDictionary<NSString *, id> *> *packets =
      [NSMutableArray arrayWithCapacity:drain.packets.size()];
  for (const IOSNativeRuntime::MidiPacket &packet : drain.packets) {
    NSMutableArray<NSNumber *> *bytes =
        [NSMutableArray arrayWithCapacity:packet.length];
    for (std::uint8_t index = 0U; index < packet.length; ++index)
      [bytes addObject:@(packet.bytes[index])];
    [packets addObject:@{
      @"sequence" : @(packet.sequence),
      @"bytes" : bytes,
    }];
  }
  return @{
    @"packets" : packets,
    @"droppedNormal" : @(drain.droppedNormal),
    @"droppedRealtime" : @(drain.droppedRealtime),
  };
}

- (void)disconnectMidiDirections:(NSUInteger)directions {
  if (!_initialized || _runtime == nullptr) return;
  IOSNativeRuntime *runtime = _runtime.get();
  dispatch_sync(_runtimeQueue, ^{
    runtime->DisconnectMidi(static_cast<std::uint32_t>(directions));
  });
}

- (void)setMidiOutputConnected:(BOOL)connected {
  if (!_initialized || _runtime == nullptr) return;
  IOSNativeRuntime *runtime = _runtime.get();
  dispatch_sync(_runtimeQueue, ^{
    runtime->SetMidiOutputConnected(connected);
  });
}

@end
