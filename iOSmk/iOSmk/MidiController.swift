import Foundation
import AVFoundation

struct LogLine: Identifiable {
    let id = UUID()
    let timestamp: Date
    let text: String
}

struct MidiMonitorLine: Identifiable {
    let id = UUID()
    let timestamp: Date
    let summary: String
    let hex: String
    let byteCount: Int
}

@MainActor
final class MidiController: ObservableObject {
    // Lazily constructed in start(), NOT as an eagerly-initialized stored
    // property -- RtMidiBridge's init constructs RtMidiOut/RtMidiIn, which
    // calls CoreMIDI's MIDIClientCreate(). That has a documented run-loop-
    // binding sensitivity (the same Apple quirk behind the macOS #262
    // getPortCount() crash investigated earlier this session: "notifyProc
    // will always be called on the run loop which was current when
    // MIDIClientCreate was first called"). A stored-property default
    // initializer runs synchronously as part of @StateObject's own
    // SwiftUI-graph-tied construction; isolated by bisection (a trivial
    // view, and a structurally-identical Form/Picker/NavigationStack view
    // with zero MidiController involvement, both ran fine on this exact
    // device -- only the version actually constructing RtMidiBridge
    // crashed, every time, 5/5). Deferring construction to start() (called
    // from .onAppear, one run-loop turn removed via DispatchQueue.main.async)
    // keeps CoreMIDI client creation off that critical path.
    private var bridge: RtMidiBridge?

    @Published var outputPorts: [String] = []
    @Published var inputPorts: [String] = []
    @Published var selectedOutputIndex: Int? = nil
    @Published var selectedInputIndex: Int? = nil
    @Published var openedOutputPortName: String? = nil
    @Published var openedInputPortName: String? = nil

    /// Status/send-confirmation log -- everything EXCEPT received MIDI,
    /// which goes to midiMonitor instead. Mixing the two was the original
    /// complaint: impossible to tell whether anything was actually being
    /// received versus just app status chatter.
    @Published var log: [LogLine] = []

    /// Received MIDI only, newest first when displayed -- a dedicated
    /// monitor view, separate from app status.
    @Published var midiMonitor: [MidiMonitorLine] = []
    @Published var receivedCount = 0

    /// Call once from the view's .onAppear -- see the note on `bridge` above.
    func start() {
        let b = RtMidiBridge()
        bridge = b
        b.setReceiveHandler { [weak self] data, deltaTime in
            guard let self else { return }
            self.handleReceived(data, deltaTime: deltaTime)
        }

        // CoreMIDI on iOS wants an active audio session -- without this,
        // MIDI I/O can be unreliable, especially across app foreground/
        // background transitions. Not something RtMidi itself sets up
        // (confirmed by reading RtMidi.cpp -- no AVAudioSession code
        // anywhere in it), so the app has to.
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, options: [.mixWithOthers])
            try session.setActive(true)
        } catch {
            appendLog("AVAudioSession setup failed: \(error.localizedDescription)")
        }

        refreshPorts()
    }

    /// Re-queries CoreMIDI for the current port list (e.g. after swapping
    /// which USB-C device is attached) and, if a previously-opened port's
    /// NAME still exists in the new list, reopens it automatically at its
    /// (possibly new) index -- device swaps renumber ports, so tracking by
    /// name rather than index is what makes "just hit Refresh" actually work
    /// without having to re-pick from the dropdown every time.
    func refreshPorts() {
        guard let bridge else { return }
        outputPorts = bridge.outputPortNames()
        inputPorts = bridge.inputPortNames()
        appendLog("Ports refreshed: \(outputPorts.count) out, \(inputPorts.count) in")

        if let name = openedOutputPortName, let idx = outputPorts.firstIndex(of: name) {
            selectedOutputIndex = idx
            openSelectedOutput()
        } else if openedOutputPortName != nil {
            appendLog("Previously-open output \"\(openedOutputPortName!)\" is gone")
            openedOutputPortName = nil
        }

        if let name = openedInputPortName, let idx = inputPorts.firstIndex(of: name) {
            selectedInputIndex = idx
            openSelectedInput()
        } else if openedInputPortName != nil {
            appendLog("Previously-open input \"\(openedInputPortName!)\" is gone")
            openedInputPortName = nil
        }
    }

    func openSelectedOutput() {
        guard let i = selectedOutputIndex, let bridge, outputPorts.indices.contains(i) else { return }
        do {
            try bridge.openOutputPort(at: UInt(i))
            openedOutputPortName = outputPorts[i]
            appendLog("Opened output port \(i): \(outputPorts[i])")
        } catch {
            appendLog("Failed to open output port \(i): \(error.localizedDescription)")
        }
    }

    func openSelectedInput() {
        guard let i = selectedInputIndex, let bridge, inputPorts.indices.contains(i) else { return }
        do {
            try bridge.openInputPort(at: UInt(i))
            openedInputPortName = inputPorts[i]
            appendLog("Opened input port \(i): \(inputPorts[i])")
        } catch {
            appendLog("Failed to open input port \(i): \(error.localizedDescription)")
        }
    }

    // Same test vectors used throughout this session's macOS harness
    // (~/rtmidi-sandbox/366), so results are directly comparable.
    func sendEmptySysex() { send([0xF0, 0xF7], label: "empty-sysex") }

    func sendSmallSysex() { send([0xF0, 0x00, 0x01, 0x5F, 0x7E, 0xF7], label: "small-sysex") }

    func send300ByteSysex() {
        var bytes: [UInt8] = [0xF0, 0x00, 0x01, 0x5F, 0x7E]
        while bytes.count < 299 { bytes.append(UInt8(bytes.count & 0x7F)) }
        bytes.append(0xF7)
        send(bytes, label: "300B-sysex")
    }

    func sendNoteOn() { send([0x90, 0x50, 0x60], label: "note-on") }
    func sendNoteOff() { send([0x80, 0x50, 0x60], label: "note-off") }

    /// Universal Non-Realtime SysEx, sub-ID1 0x06 (General Information),
    /// sub-ID2 0x01 (Identity Request). Device ID 0x7F = broadcast/"all
    /// call", standard for discovery when the responder's own ID isn't
    /// known yet. Replies are decoded in summarize() below.
    func sendIdentityRequest() { send([0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7], label: "identity-request") }

    /// 8x3000-byte SysEx fragments back-to-back, no inter-fragment delay,
    /// then drain() -- the exact drain() backpressure measurement done on
    /// macOS (~7.76s there). Compares the fork's flow-controlled CoreMIDI
    /// path on iOS against that baseline.
    func runDrainTimingTest() {
        guard selectedOutputIndex != nil, let bridge else {
            appendLog("Select and open an output port first")
            return
        }
        var totalSent = 0
        let sendStart = Date()
        for f in 0..<8 {
            var bytes: [UInt8] = [0xF0, 0x00, 0x01, 0x5F, 0x7E, UInt8(f)]
            while bytes.count < 2999 { bytes.append(UInt8(bytes.count & 0x7F)) }
            bytes.append(0xF7)
            let rc = bridge.sendBytes(Data(bytes))
            if rc >= 0 { totalSent += Int(rc) }
        }
        let sendMs = Date().timeIntervalSince(sendStart) * 1000
        appendLog("sendMessage() loop: \(String(format: "%.3f", sendMs))ms, \(totalSent) bytes accepted")

        let drainSeconds = bridge.drainAndMeasure()
        appendLog("drain(): \(String(format: "%.3f", drainSeconds * 1000))ms (macOS baseline: ~7758ms)")
    }

    func clearMonitor() {
        midiMonitor.removeAll()
        receivedCount = 0
    }

    private func send(_ bytes: [UInt8], label: String) {
        guard let bridge else {
            appendLog("Not started yet")
            return
        }
        let rc = bridge.sendBytes(Data(bytes))
        appendLog("SENT \(label) (\(bytes.count) bytes), sendMessage() returned \(rc)")
    }

    private func handleReceived(_ data: Data, deltaTime: Double) {
        receivedCount += 1
        let hex = data.map { String(format: "%02X", $0) }.joined(separator: " ")
        midiMonitor.append(MidiMonitorLine(timestamp: Date(), summary: Self.summarize(data), hex: hex, byteCount: data.count))
        if midiMonitor.count > 500 { midiMonitor.removeFirst(midiMonitor.count - 500) }
    }

    /// Minimal human-readable label for the monitor view -- not a full MIDI
    /// parser, just enough to tell message types apart at a glance.
    private static func summarize(_ data: Data) -> String {
        guard let status = data.first else { return "empty" }
        switch status {
        case 0xF0: return parseIdentityReply(data) ?? "SysEx (\(data.count)B)"
        case 0x80...0x8F: return "Note Off ch\((status & 0x0F) + 1)"
        case 0x90...0x9F: return "Note On ch\((status & 0x0F) + 1)"
        case 0xA0...0xAF: return "Poly Aftertouch ch\((status & 0x0F) + 1)"
        case 0xB0...0xBF: return "Control Change ch\((status & 0x0F) + 1)"
        case 0xC0...0xCF: return "Program Change ch\((status & 0x0F) + 1)"
        case 0xD0...0xDF: return "Channel Aftertouch ch\((status & 0x0F) + 1)"
        case 0xE0...0xEF: return "Pitch Bend ch\((status & 0x0F) + 1)"
        case 0xF8: return "Timing Clock"
        case 0xFA: return "Start"
        case 0xFB: return "Continue"
        case 0xFC: return "Stop"
        case 0xFE: return "Active Sensing"
        default: return String(format: "Status 0x%02X", status)
        }
    }

    /// Universal Non-Realtime Identity Reply: F0 7E <devID> 06 02
    /// <manufacturer: 1 byte, or 00 + 2 more bytes> <family LSB/MSB>
    /// <member LSB/MSB> <4 bytes software revision> F7. Returns nil for
    /// anything else (falls back to the generic "SysEx (NB)" label).
    private static func parseIdentityReply(_ data: Data) -> String? {
        let bytes = [UInt8](data)
        guard bytes.count >= 6,
              bytes[0] == 0xF0, bytes[1] == 0x7E, bytes[3] == 0x06, bytes[4] == 0x02
        else { return nil }

        let deviceId = bytes[2]
        var idx = 5
        let manufacturer: [UInt8]
        if idx < bytes.count, bytes[idx] == 0x00 {
            guard bytes.count >= idx + 3 else { return nil }
            manufacturer = Array(bytes[idx...idx + 2])
            idx += 3
        } else {
            guard idx < bytes.count else { return nil }
            manufacturer = [bytes[idx]]
            idx += 1
        }
        guard bytes.count >= idx + 9 else { return nil } // family(2) + member(2) + rev(4) + F7

        let family = Int(bytes[idx]) | (Int(bytes[idx + 1]) << 7)
        let member = Int(bytes[idx + 2]) | (Int(bytes[idx + 3]) << 7)
        let rev = bytes[(idx + 4)..<(idx + 8)].map { String($0) }.joined(separator: ".")

        let manuHex = manufacturer.map { String(format: "%02X", $0) }.joined(separator: " ")
        // 00 01 5F is KMI Music, Inc.'s registered manufacturer ID -- used
        // throughout this session's SysEx test vectors (~/rtmidi-sandbox/366).
        let manuName = manufacturer == [0x00, 0x01, 0x5F] ? "KMI" : manuHex

        return "ID Reply: \(manuName) family=\(family) member=\(member) rev=\(rev) (devID \(deviceId))"
    }

    private func appendLog(_ text: String) {
        log.append(LogLine(timestamp: Date(), text: text))
        if log.count > 500 { log.removeFirst(log.count - 500) }
    }
}
