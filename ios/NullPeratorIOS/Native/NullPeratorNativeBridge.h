#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, NPTrackerAction) {
    NPTrackerActionLeft = 0,
    NPTrackerActionDown,
    NPTrackerActionRight,
    NPTrackerActionUp,
    NPTrackerActionShift,
    NPTrackerActionOption,
    NPTrackerActionEnter,
    NPTrackerActionPlay,
};

/// Objective-C++ boundary for the native core. UI2 framebuffer updates cross
/// this boundary as palette-indexed dirty regions for the Svelte canvas.
@interface NullPeratorNativeBridge : NSObject

@property(nonatomic, readonly, getter=isInitialized) BOOL initialized;
@property(nonatomic, readonly) NSString *nullPeratorVersion;
@property(nonatomic, readonly) NSString *buildHash;
@property(nonatomic, readonly) NSString *buildTime;

- (void)setAction:(NPTrackerAction)action
          pressed:(BOOL)pressed
         repeated:(BOOL)repeated;
- (void)releaseAllActions;
- (NSDictionary<NSString *, id> *)framePacketSince:(NSUInteger)sequence
    NS_SWIFT_NAME(framePacket(since:));
- (void)setBatteryPercentage:(NSInteger)percentage
                    charging:(BOOL)charging
                   available:(BOOL)available;
- (BOOL)submitMidiData:(NSData *)data timestamp:(NSTimeInterval)timestamp;
- (NSDictionary<NSString *, id> *)drainMidi;
- (void)disconnectMidiDirections:(NSUInteger)directions;
- (void)setMidiOutputConnected:(BOOL)connected;

@end

NS_ASSUME_NONNULL_END
