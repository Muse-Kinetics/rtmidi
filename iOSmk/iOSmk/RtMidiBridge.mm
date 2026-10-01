#import "RtMidiBridge.h"
#import "RtMidi.h"
#include <memory>
#include <chrono>

@implementation RtMidiBridge {
    std::unique_ptr<RtMidiOut> _out;
    std::unique_ptr<RtMidiIn> _in;
    RtMidiReceiveBlock _receiveHandler;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        try {
            _out = std::make_unique<RtMidiOut>();
            _in = std::make_unique<RtMidiIn>();
        } catch (RtMidiError &e) {
            NSLog(@"RtMidiBridge init failed: %s", e.getMessage().c_str());
        }
    }
    return self;
}

- (NSArray<NSString *> *)outputPortNames {
    NSMutableArray<NSString *> *names = [NSMutableArray array];
    if (!_out) return names;
    unsigned int n = _out->getPortCount();
    for (unsigned int i = 0; i < n; ++i) {
        [names addObject:[NSString stringWithUTF8String:_out->getPortName(i).c_str()]];
    }
    return names;
}

- (NSArray<NSString *> *)inputPortNames {
    NSMutableArray<NSString *> *names = [NSMutableArray array];
    if (!_in) return names;
    unsigned int n = _in->getPortCount();
    for (unsigned int i = 0; i < n; ++i) {
        [names addObject:[NSString stringWithUTF8String:_in->getPortName(i).c_str()]];
    }
    return names;
}

- (BOOL)openOutputPortAtIndex:(NSUInteger)index error:(NSError **)error {
    if (!_out) return NO;
    try {
        if (_out->isPortOpen()) _out->closePort();
        _out->openPort((unsigned int)index);
        return YES;
    } catch (RtMidiError &e) {
        if (error) {
            *error = [NSError errorWithDomain:@"RtMidiBridge" code:1
                userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithUTF8String:e.getMessage().c_str()]}];
        }
        return NO;
    }
}

- (BOOL)openInputPortAtIndex:(NSUInteger)index error:(NSError **)error {
    if (!_in) return NO;
    try {
        if (_in->isPortOpen()) _in->closePort();
        _in->openPort((unsigned int)index);
        // Do NOT ignore sysex, timing, or sensing -- matches shemeshg's
        // #366 reproducer and this session's macOS receivers exactly.
        _in->ignoreTypes(false, false, false);
        __weak RtMidiBridge *weakSelf = self;
        _in->setCallback([](double deltatime, std::vector<unsigned char> *message, void *userData) {
            RtMidiBridge *strongSelf = (__bridge RtMidiBridge *)userData;
            if (!strongSelf || !message || message->empty()) return;
            NSData *data = [NSData dataWithBytes:message->data() length:message->size()];
            RtMidiReceiveBlock handler = strongSelf->_receiveHandler;
            if (handler) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    handler(data, deltatime);
                });
            }
        }, (__bridge void *)self);
        (void)weakSelf;
        return YES;
    } catch (RtMidiError &e) {
        if (error) {
            *error = [NSError errorWithDomain:@"RtMidiBridge" code:2
                userInfo:@{NSLocalizedDescriptionKey: [NSString stringWithUTF8String:e.getMessage().c_str()]}];
        }
        return NO;
    }
}

- (void)closeOutputPort {
    if (_out && _out->isPortOpen()) _out->closePort();
}

- (void)closeInputPort {
    if (_in && _in->isPortOpen()) _in->closePort();
}

- (void)setReceiveHandler:(RtMidiReceiveBlock)handler {
    _receiveHandler = [handler copy];
}

- (int)sendBytes:(NSData *)bytes {
    if (!_out) return -1;
    std::vector<unsigned char> msg((const unsigned char *)bytes.bytes,
                                    (const unsigned char *)bytes.bytes + bytes.length);
    return _out->sendMessage(&msg);
}

- (double)drainAndMeasure {
    if (!_out) return 0.0;
    auto start = std::chrono::steady_clock::now();
    _out->drain();
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(end - start).count();
}

@end
