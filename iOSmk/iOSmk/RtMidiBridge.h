// Thin Objective-C wrapper around RtMidiIn/RtMidiOut so Swift can drive the
// same CoreMIDI backend used throughout the macOS rtmidi test harness
// (~/rtmidi-sandbox/366, /262) -- this is the iOS side of the same work.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef void (^RtMidiReceiveBlock)(NSData *bytes, double deltaTime);

@interface RtMidiBridge : NSObject

- (NSArray<NSString *> *)outputPortNames NS_SWIFT_NAME(outputPortNames());
- (NSArray<NSString *> *)inputPortNames NS_SWIFT_NAME(inputPortNames());

/// Opens a regular (non-virtual) output port by index. Closes any
/// previously open output port first. Imported into Swift as a throwing
/// function (openOutputPort(at:)) via the standard BOOL+NSError** -> throws
/// bridging convention.
- (BOOL)openOutputPortAtIndex:(NSUInteger)index error:(NSError **)error NS_SWIFT_NAME(openOutputPort(at:));

/// Opens a regular (non-virtual) input port by index and starts delivering
/// received messages to the receive handler. Closes any previously open
/// input port first.
- (BOOL)openInputPortAtIndex:(NSUInteger)index error:(NSError **)error NS_SWIFT_NAME(openInputPort(at:));

- (void)closeOutputPort NS_SWIFT_NAME(closeOutputPort());
- (void)closeInputPort NS_SWIFT_NAME(closeInputPort());

/// Called from RtMidi's own input thread -- hop to the main thread before
/// touching UI state.
- (void)setReceiveHandler:(nullable RtMidiReceiveBlock)handler NS_SWIFT_NAME(setReceiveHandler(_:));

/// Returns the number of bytes accepted, or a negative value on failure
/// (RtMidi's own int sendMessage() contract, fork-only -- this vendors
/// Muse-Kinetics/rtmidi's sysex-send-flowcontrol branch, not upstream).
- (int)sendBytes:(NSData *)bytes NS_SWIFT_NAME(sendBytes(_:));

/// Blocks until every in-flight asynchronous CoreMIDI SysEx send this
/// backend queued has actually completed. Returns the elapsed time in
/// seconds, so the caller can compare against the macOS drain() timing
/// already measured in this session's harness (~7.76s for 8x3000-byte
/// fragments sent back-to-back).
- (double)drainAndMeasure NS_SWIFT_NAME(drainAndMeasure());

@end

NS_ASSUME_NONNULL_END
