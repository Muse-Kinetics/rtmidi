import SwiftUI

struct ContentView: View {
    @StateObject private var midi = MidiController()

    // Pickers bind to these non-optional sentinels (-1 == none selected)
    // rather than directly to midi.selectedOutputIndex/selectedInputIndex
    // (Int?). Picker selection bound to an Optional, with .tag(Int?.some)/
    // .tag(Int?.none), is a known AttributeGraph crash trigger on-device --
    // confirmed here by bisection: a trivial view with no Picker ran fine
    // on this exact device/SDK combo, crash came back the instant this
    // ContentView (Form+Picker+NavigationStack+@StateObject) was restored.
    @State private var outputSelection = -1
    @State private var inputSelection = -1

    // In-app "screensaver" -- there's no way for a sandboxed app to blank
    // the actual display or override iOS's own lock/auto-lock behavior
    // (confirmed separately: a locked device refuses app launch outright,
    // FBSOpenApplicationErrorDomain error 7 "Locked" -- that's an OS-level
    // policy, not something in our control). This is a black overlay
    // covering the UI after 5 minutes of inactivity instead.
    //
    // IMPORTANT: activity is registered by calling registerActivity()
    // explicitly from each control's own action/onChange below, NOT via a
    // catch-all gesture on the view hierarchy. The first version attached
    // a DragGesture(minimumDistance: 0) via .simultaneousGesture to detect
    // "any touch" -- that broke every Button/Picker permanently (not just
    // during the overlay's fade): a zero-distance drag recognizer attached
    // anywhere in the tree conflicts with Form/List's underlying
    // UITableView gesture arbitration, a well-known SwiftUI gotcha. No
    // competing gesture recognizer == no interference, at the cost of one
    // explicit call per control.
    private static let idleTimeout: TimeInterval = 5 * 60
    @State private var lastActivity = Date()
    @State private var isBlanked = false
    @State private var idleCheckTimer = Timer.publish(every: 5, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            NavigationStack {
                GeometryReader { geo in
                    HStack(spacing: 0) {
                        controlsArea
                            .frame(width: geo.size.width / 2)

                        Divider()

                        VStack(spacing: 0) {
                            monitorArea
                                .frame(height: geo.size.height * 2 / 3)
                            Divider()
                            statusLogArea
                                .frame(height: geo.size.height / 3)
                        }
                        .frame(width: geo.size.width / 2)
                    }
                }
                .navigationTitle("RtMidi Tester")
                .toolbar {
                    // The actual fix for "have to close/reopen the app to see
                    // a swapped USB device" -- always visible, not buried at
                    // the bottom of a scrolling form. refreshPorts() also
                    // auto-reopens any previously-open port by name if it's
                    // still present after the swap.
                    ToolbarItem(placement: .navigationBarTrailing) {
                        Button {
                            registerActivity()
                            midi.refreshPorts()
                        } label: {
                            Label("Refresh", systemImage: "arrow.clockwise")
                        }
                    }
                }
            }

            // No animation on show/hide -- deliberately instant, not a
            // .transition(.opacity) fade. A fading view still intercepts
            // touches at intermediate opacity in SwiftUI (opacity affects
            // paint, not hit-testing, unless explicitly disabled), which
            // would reintroduce a milder version of the same "can't touch
            // anything" symptom for the ~0.3s the animation is in flight.
            if isBlanked {
                Color.black
                    .ignoresSafeArea()
                    .onTapGesture { registerActivity() }
                    .overlay(
                        Text("Tap to wake")
                            .font(.caption)
                            .foregroundStyle(.white.opacity(0.25))
                    )
            }
        }
        .onReceive(idleCheckTimer) { _ in
            if !isBlanked && Date().timeIntervalSince(lastActivity) >= Self.idleTimeout {
                isBlanked = true
            }
        }
        // Incoming MIDI counts as activity too -- this is a monitor tool;
        // blanking mid-stream while real data is actively arriving would
        // defeat the point, even if nobody has touched the screen.
        .onChange(of: midi.receivedCount) { _ in registerActivity() }
        .onAppear {
            // Crash #2 on-device (first launch, crash.ips): AttributeGraph
            // precondition failure from AppSceneDelegate.sceneDidBecomeActive
            // -> PlatformSceneCache.setPhase -> AG::Graph::value_set, with
            // midi.start()'s @Published mutations as the trigger -- even
            // .onAppear can race with the scene's own first-activation graph
            // transaction. Deferring one run-loop turn past it fixes this
            // class of crash; see also the note on MidiController.bridge.
            DispatchQueue.main.async { midi.start() }
        }
    }

    private func registerActivity() {
        lastActivity = Date()
        if isBlanked { isBlanked = false }
    }

    // MARK: - Controls (left half)

    private var controlsArea: some View {
        Form {
            Section("Output port\(midi.openedOutputPortName.map { " — open: \($0)" } ?? "")") {
                Picker("Output", selection: $outputSelection) {
                    Text("None").tag(-1)
                    ForEach(Array(midi.outputPorts.enumerated()), id: \.offset) { i, name in
                        Text(name).tag(i)
                    }
                }
                .onChange(of: outputSelection) { newValue in
                    registerActivity()
                    // Auto-open on selection -- no separate "Open" button.
                    midi.selectedOutputIndex = newValue >= 0 ? newValue : nil
                    if newValue >= 0 { midi.openSelectedOutput() }
                }
            }

            Section("Input port\(midi.openedInputPortName.map { " — open: \($0)" } ?? "")") {
                Picker("Input", selection: $inputSelection) {
                    Text("None").tag(-1)
                    ForEach(Array(midi.inputPorts.enumerated()), id: \.offset) { i, name in
                        Text(name).tag(i)
                    }
                }
                .onChange(of: inputSelection) { newValue in
                    registerActivity()
                    // Auto-open on selection -- no separate "Open" button.
                    midi.selectedInputIndex = newValue >= 0 ? newValue : nil
                    if newValue >= 0 { midi.openSelectedInput() }
                }
            }

            Section("Send test patterns") {
                // Same byte patterns as the macOS harness
                // (~/rtmidi-sandbox/366) so results compare directly.
                Button("Empty SysEx (F0 F7)") { registerActivity(); midi.sendEmptySysex() }
                Button("Small SysEx (6 bytes)") { registerActivity(); midi.sendSmallSysex() }
                Button("300-byte SysEx") { registerActivity(); midi.send300ByteSysex() }
                Button("Note On") { registerActivity(); midi.sendNoteOn() }
                Button("Note Off") { registerActivity(); midi.sendNoteOff() }
                Button("Identity Request") { registerActivity(); midi.sendIdentityRequest() }
                Button("Run drain() timing test") { registerActivity(); midi.runDrainTimingTest() }
            }
        }
    }

    // MARK: - Status log (right half, bottom third -- app/connection status, NOT received MIDI)

    private var statusLogArea: some View {
        VStack(spacing: 0) {
            Text("Status")
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(.horizontal)
                .padding(.top, 4)

            List(midi.log.reversed()) { line in
                Text(line.text)
                    .font(.system(.caption2, design: .monospaced))
            }
            .listStyle(.plain)
        }
    }

    // MARK: - MIDI monitor (right half, top two-thirds -- received messages only, separate from status)

    private var monitorArea: some View {
        VStack(spacing: 0) {
            HStack {
                Text("MIDI Monitor")
                    .font(.headline)
                Text("\(midi.receivedCount) received")
                    .font(.caption)
                    .foregroundStyle(.secondary)
                Spacer()
                Button("Clear") { registerActivity(); midi.clearMonitor() }
                    .font(.caption)
            }
            .padding(.horizontal)
            .padding(.vertical, 6)

            if midi.midiMonitor.isEmpty {
                Spacer()
                Text("No MIDI received yet. Select an input port on the left, then send from the attached device.")
                    .font(.footnote)
                    .foregroundStyle(.secondary)
                    .multilineTextAlignment(.center)
                    .padding()
                Spacer()
            } else {
                List(midi.midiMonitor.reversed()) { line in
                    HStack(alignment: .firstTextBaseline) {
                        Text(line.timestamp.formatted(date: .omitted, time: .standard))
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                            .frame(width: 70, alignment: .leading)
                        VStack(alignment: .leading, spacing: 1) {
                            Text(line.summary)
                                .font(.system(.footnote, design: .monospaced))
                                .bold()
                            Text(line.hex)
                                .font(.system(.caption2, design: .monospaced))
                                .foregroundStyle(.secondary)
                                .lineLimit(2)
                        }
                    }
                }
                .listStyle(.plain)
            }
        }
    }
}

#Preview {
    ContentView()
}
