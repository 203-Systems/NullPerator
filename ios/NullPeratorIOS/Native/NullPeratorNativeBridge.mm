#import "NullPeratorNativeBridge.h"
#import <AVFAudio/AVFAudio.h>

#include "Adapters/ios/gui/IOSUiPresenter.h"
#include "Adapters/ios/runtime/IOSNativeRuntime.h"
#include "ProductVersion.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dispatch/dispatch.h>
#include <fcntl.h>
#include <memory>
#include <string>
#include <unistd.h>

namespace {
NSString *Base64(const std::uint8_t *bytes, std::size_t size) {
  NSData *data = [NSData dataWithBytes:bytes length:size];
  return [data base64EncodedStringWithOptions:0];
}
} // namespace

extern "C" int NullPeratorIOSRecordPermission() {
  const auto permission = AVAudioApplication.sharedInstance.recordPermission;
  if (permission == AVAudioApplicationRecordPermissionGranted)
    return 1;
  if (permission == AVAudioApplicationRecordPermissionDenied)
    return -1;
  static std::atomic<bool> requested{false};
  if (!requested.exchange(true)) {
    dispatch_async(dispatch_get_main_queue(), ^{
      [AVAudioApplication requestRecordPermissionWithCompletionHandler:^(BOOL){
      }];
    });
  }
  return 0;
}

extern "C" bool NullPeratorIOSSetRecordingSession(bool recording) {
  AVAudioSession *session = AVAudioSession.sharedInstance;
  NSError *error = nil;
  const AVAudioSessionCategoryOptions options =
      AVAudioSessionCategoryOptionMixWithOthers |
      (recording ? AVAudioSessionCategoryOptionDefaultToSpeaker |
                       AVAudioSessionCategoryOptionAllowBluetoothHFP
                 : 0);
  if (![session setCategory:recording ? AVAudioSessionCategoryPlayAndRecord
                                        : AVAudioSessionCategoryPlayback
                         mode:AVAudioSessionModeDefault
                      options:options
                        error:&error]) {
    NSLog(@"NullPerator audio session category failed: %@", error);
    return false;
  }
  // Other active audio apps may determine the hardware rate. RemoteIO converts
  // the 44.1 kHz client format, so this preference is not required to activate.
  if (![session setPreferredSampleRate:44100 error:&error])
    NSLog(@"NullPerator preferred sample rate unavailable: %@", error);
  if (![session setActive:YES error:&error]) {
    NSLog(@"NullPerator audio session activation failed: %@", error);
    return false;
  }
  return true;
}

#include "System/System/System.h"
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <mutex>

namespace {
std::mutex sampleImportMutex;
SampleImportResult sampleImportResult;
NSString *sampleImportProject = nil;
void CompleteSampleImport(SampleImportStatus status, NSString *path = nil) {
  std::lock_guard<std::mutex> lock(sampleImportMutex);
  sampleImportResult = {};
  sampleImportResult.status = status;
  if (path != nil)
    std::snprintf(sampleImportResult.path, sizeof(sampleImportResult.path),
                  "%s", path.UTF8String);
}
UIViewController *ImportPresenter() {
  for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
    if (![scene isKindOfClass:UIWindowScene.class] ||
        scene.activationState != UISceneActivationStateForegroundActive)
      continue;
    for (UIWindow *window in ((UIWindowScene *)scene).windows) {
      if (!window.isKeyWindow)
        continue;
      UIViewController *controller = window.rootViewController;
      while (controller.presentedViewController)
        controller = controller.presentedViewController;
      return controller;
    }
  }
  return nil;
}
} // namespace

@interface TrackerSampleImporter : NSObject <UIDocumentPickerDelegate>
- (void)renameFile:(NSURL *)url
             draft:(NSString *)draft
             error:(NSString *)message;
@end
@implementation TrackerSampleImporter
- (void)documentPickerWasCancelled:
    (UIDocumentPickerViewController *)controller {
  CompleteSampleImport(SampleImportStatus::Cancelled);
}
- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
  NSURL *url = urls.firstObject;
  if (url == nil) {
    CompleteSampleImport(SampleImportStatus::Cancelled);
    return;
  }
  // The picker imports a local copy; defer the alert until its dismissal
  // completes.
  [controller
      dismissViewControllerAnimated:YES
                         completion:^{
                           [self renameFile:url
                                      draft:url.lastPathComponent
                                                .stringByDeletingPathExtension
                                      error:nil];
                         }];
}
- (void)renameFile:(NSURL *)url
             draft:(NSString *)draft
             error:(NSString *)message {
  UIViewController *presenter = ImportPresenter();
  if (presenter == nil) {
    CompleteSampleImport(SampleImportStatus::Failed);
    return;
  }
  UIAlertController *alert =
      [UIAlertController alertControllerWithTitle:@"Import sample"
                                          message:message
                                   preferredStyle:UIAlertControllerStyleAlert];
  [alert addTextFieldWithConfigurationHandler:^(UITextField *field) {
    field.text = draft;
    field.autocorrectionType = UITextAutocorrectionTypeNo;
  }];
  [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                            style:UIAlertActionStyleCancel
                                          handler:^(UIAlertAction *) {
                                            CompleteSampleImport(
                                                SampleImportStatus::Cancelled);
                                          }]];
  __block UIAlertController *inputAlert = alert;
  [alert
      addAction:
          [UIAlertAction
              actionWithTitle:@"Import"
                        style:UIAlertActionStyleDefault
                      handler:^(UIAlertAction *) {
                        NSString *name = [inputAlert.textFields.firstObject.text
                            stringByTrimmingCharactersInSet:
                                NSCharacterSet
                                    .whitespaceAndNewlineCharacterSet];
                        if ([name.pathExtension.lowercaseString
                                isEqualToString:@"wav"])
                          name = name.stringByDeletingPathExtension;
                        NSCharacterSet *invalid = [NSCharacterSet
                            characterSetWithCharactersInString:@"/\\:"];
                        if (name.length == 0 || [name hasPrefix:@"."] ||
                            [name lengthOfBytesUsingEncoding:
                                      NSUTF8StringEncoding] > 20 ||
                            [name rangeOfCharacterFromSet:invalid].location !=
                                NSNotFound ||
                            [name rangeOfCharacterFromSet:
                                      NSCharacterSet.controlCharacterSet]
                                    .location != NSNotFound) {
                          dispatch_async(dispatch_get_main_queue(), ^{
                            [self
                                renameFile:url
                                     draft:name
                                     error:
                                         @"Choose a name of 1–20 bytes without "
                                         @"slashes or a leading dot."];
                          });
                          return;
                        }
                        NSFileHandle *file =
                            [NSFileHandle fileHandleForReadingFromURL:url
                                                                error:nil];
                        NSData *header = [file readDataUpToLength:12 error:nil];
                        [file closeFile];
                        if (header.length != 12 ||
                            memcmp(header.bytes, "RIFF", 4) ||
                            memcmp((const char *)header.bytes + 8, "WAVE", 4)) {
                          CompleteSampleImport(SampleImportStatus::Failed);
                          return;
                        }
                        NSURL *documents =
                            [NSFileManager.defaultManager
                                URLsForDirectory:NSDocumentDirectory
                                       inDomains:NSUserDomainMask]
                                .firstObject;
                        NSURL *directory =
                            [documents URLByAppendingPathComponent:@"samples"
                                                       isDirectory:YES];
                        NSError *error = nil;
                        if (![NSFileManager.defaultManager
                                       createDirectoryAtURL:directory
                                withIntermediateDirectories:YES
                                                 attributes:nil
                                                      error:&error]) {
                          CompleteSampleImport(SampleImportStatus::Failed);
                          return;
                        }
                        NSString *leaf = [name stringByAppendingString:@".wav"];
                        NSURL *destination =
                            [directory URLByAppendingPathComponent:leaf];
                        NSURL *projectSamples = [[[[documents
                            URLByAppendingPathComponent:@"projects"]
                            URLByAppendingPathComponent:sampleImportProject]
                            URLByAppendingPathComponent:@"samples"]
                            URLByAppendingPathComponent:leaf];
                        if ([NSFileManager.defaultManager
                                fileExistsAtPath:projectSamples.path]) {
                          dispatch_async(dispatch_get_main_queue(), ^{
                            [self renameFile:url
                                       draft:name
                                       error:@"This project already has a "
                                             @"sample with that name. Choose "
                                             @"another name."];
                          });
                          return;
                        }
                        if (![NSFileManager.defaultManager
                                copyItemAtURL:url
                                        toURL:destination
                                        error:&error]) {
                          dispatch_async(dispatch_get_main_queue(), ^{
                            [self renameFile:url
                                       draft:name
                                       error:error.localizedDescription];
                          });
                          return;
                        }
                        const int descriptor = open(
                            destination.fileSystemRepresentation, O_RDONLY);
                        const bool synced =
                            descriptor >= 0 && fsync(descriptor) == 0;
                        if (descriptor >= 0)
                          close(descriptor);
                        const int parent =
                            open(directory.fileSystemRepresentation, O_RDONLY);
                        const bool directorySynced =
                            parent >= 0 && fsync(parent) == 0;
                        if (parent >= 0)
                          close(parent);
                        if (!synced || !directorySynced) {
                          CompleteSampleImport(SampleImportStatus::Failed);
                          return;
                        }
                        CompleteSampleImport(
                            SampleImportStatus::Imported,
                            [@"/samples/" stringByAppendingString:leaf]);
                      }]];
  [presenter presentViewController:alert animated:YES completion:nil];
}
@end

extern "C" bool NullPeratorIOSRequestSampleImport(const char *projectName) {
  NSString *project = [NSString stringWithUTF8String:projectName];
  {
    std::lock_guard<std::mutex> lock(sampleImportMutex);
    sampleImportResult = {};
  }
  dispatch_async(dispatch_get_main_queue(), ^{
    [sampleImportProject release];
    sampleImportProject = [project copy];
    static TrackerSampleImporter *importer =
        [[TrackerSampleImporter alloc] init];
    UIViewController *presenter = ImportPresenter();
    if (presenter == nil) {
      CompleteSampleImport(SampleImportStatus::Failed);
      return;
    }
    UIDocumentPickerViewController *picker =
        [[UIDocumentPickerViewController alloc]
            initForOpeningContentTypes:@[ UTTypeWAV ]
                                asCopy:YES];
    picker.delegate = importer;
    picker.allowsMultipleSelection = NO;
    [presenter presentViewController:picker animated:YES completion:nil];
    [picker release];
  });
  return true;
}
extern "C" SampleImportResult NullPeratorIOSPollSampleImport() {
  std::lock_guard<std::mutex> lock(sampleImportMutex);
  return sampleImportResult;
}

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
  return [NSString stringWithUTF8String:nullperator_product::Version];
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
