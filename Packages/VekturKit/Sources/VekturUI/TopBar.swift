// TopBar.swift — the instrument's top strip, shared by the app and the AUv3,
// matching the web version's header: brushed-aluminium bar, DMT Cymatics logo
// and VEKTUR MODULAR wordmark, then transport (play / rec / tempo), wear,
// engine-load and MIDI readouts, panic, keyboard and help.
import SwiftUI
@preconcurrency import AudioToolbox
import VekturAudioUnit
import VekturCore

/// Transport the bar can drive. The standalone app supplies one (internal /
/// Ableton Link clock); inside a host the host owns the transport, so it is nil.
@MainActor
public protocol TransportControlling: AnyObject {
    var tempo: Double { get }
    var isPlaying: Bool { get }
    func setTempo(_ bpm: Double)
    func setPlaying(_ playing: Bool)
}

struct TopBar: View {
    let audioUnit: VekturModularAudioUnit
    let transport: TransportControlling?
    let compact: Bool
    @Binding var showKeyboard: Bool
    let showsKeyboardToggle: Bool
    let help: () -> Void
    @State private var telemetry: TelemetryModel
    @State private var recorder: RecordingService
    @State private var wear: Double
    @State private var playing = false
    @State private var tempoText = "120"
    @State private var midiText = "midi —"
    @State private var midiFlash = false

    init(audioUnit: VekturModularAudioUnit, transport: TransportControlling?, compact: Bool, showKeyboard: Binding<Bool>,
         showsKeyboardToggle: Bool, help: @escaping () -> Void) {
        self.audioUnit = audioUnit; self.transport = transport; self.compact = compact
        _showKeyboard = showKeyboard; self.showsKeyboardToggle = showsKeyboardToggle; self.help = help
        _telemetry = State(initialValue: TelemetryModel.shared(kernel: audioUnit.kernel, sampleRate: { audioUnit.outputBusses[0].format.sampleRate }))
        _recorder = State(initialValue: RecordingService.shared(for: audioUnit))
        _wear = State(initialValue: audioUnit.appearance.wear)
    }

    var body: some View {
        ScrollView(.horizontal, showsIndicators: false) {
            HStack(spacing: compact ? 8 : 14) {
                brand
                Group {
                    if let transport {
                        LampButton(title: "play", on: playing, colour: PanelTheme.lampGreen) { playing.toggle(); transport.setPlaying(playing) }
                            .keyboardShortcut(.space, modifiers: [])
                    } else {
                        Text("host transport").font(PanelTheme.legendFont(12)).foregroundStyle(Color(white: 0.2))
                    }
                    LampButton(title: recorder.isRecording ? recTime : "rec", on: recorder.isRecording, colour: PanelTheme.lampRed) {
                        recorder.isRecording ? recorder.stop() : recorder.start()
                    }
                    .accessibilityHint("Records the output to a file in Recordings")
                    if let transport, !compact {
                        field("tempo") {
                            TextField("tempo", text: $tempoText).frame(width: 58).multilineTextAlignment(.trailing)
                                .font(.system(size: 15, weight: .medium).monospacedDigit())
                                .onSubmit { if let v = Double(tempoText) { transport.setTempo(v); tempoText = Self.fmt(transport.tempo) } }
                            #if os(iOS)
                                .keyboardType(.decimalPad)
                            #endif
                        }
                    }
                }
                if !compact {
                    field("wear") {
                        Slider(value: $wear, in: 0...1).frame(width: 96).tint(Color(white: 0.13))
                            .accessibilityLabel("Panel wear").accessibilityValue("\(Int(wear * 100)) %")
                    }
                }
                readout(String(format: "%.0f %%", telemetry.dspLoad * 100)).help("Engine load (share of the audio deadline)")
                    .accessibilityLabel("Engine load \(Int(telemetry.dspLoad * 100)) percent")
                if !compact { readout(midiText).foregroundStyle(midiFlash ? PanelTheme.amber : PanelTheme.amber.opacity(0.75)).help("Last MIDI input") }
                LampButton(title: "panic", on: false, colour: PanelTheme.lampRed, showsLamp: false, action: panic)
                    .accessibilityHint("Stops all sound")
                if showsKeyboardToggle {
                    LampButton(title: "keyboard", on: showKeyboard, colour: PanelTheme.lampGreen) { withAnimation(.snappy) { showKeyboard.toggle() } }
                        .accessibilityLabel(showKeyboard ? "Hide keyboard" : "Show keyboard")
                }
                LampButton(title: "?", on: false, colour: PanelTheme.amber, showsLamp: false, action: help).accessibilityLabel("Help for this screen")
            }
            .padding(.horizontal, compact ? 8 : 14).padding(.vertical, 7)
        }
        .background(AluminiumStrip())
        .onAppear {
            telemetry.start()
            if let transport { playing = transport.isPlaying; tempoText = Self.fmt(transport.tempo) }
        }
        .onDisappear { telemetry.stop() }
        .onChange(of: wear) { _, w in var a = audioUnit.appearance; a.wear = w; audioUnit.setAppearance(a) }
        .onReceive(NotificationCenter.default.publisher(for: VekturModularAudioUnit.appearanceDidChange, object: audioUnit)) { _ in
            if abs(audioUnit.appearance.wear - wear) > 0.001 { wear = audioUnit.appearance.wear }
        }
        .onReceive(NotificationCenter.default.publisher(for: VekturModularAudioUnit.midiEventNotification, object: audioUnit)) { note in
            if let ev = note.userInfo?["event"] as? MIDIEvent, case .programChange(let p, _, _, let ch, _) = ev { midiText = "pc \(p + 1) · ch \(ch + 1)"; flash() }
        }
        .task {
            // Held notes are the cheapest live view of MIDI input.
            while !Task.isCancelled {
                let held = audioUnit.heldNotes()
                if !held.isEmpty { midiText = held.count == 1 ? Self.noteName(held[0]) : (ChordAnalyzer.name(of: held).isEmpty ? "\(held.count) notes" : ChordAnalyzer.name(of: held)) }
                try? await Task.sleep(for: .milliseconds(120))
            }
        }
    }

    private var brand: some View {
        HStack(spacing: 10) {
            BrandLogo(size: compact ? 30 : 44)
            VStack(alignment: .leading, spacing: 1) {
                Text("VEKTUR MODULAR").font(PanelTheme.legendFont(compact ? 15 : 18, weight: .bold)).tracking(1.1).foregroundStyle(Color(white: 0.07))
                if !compact { Text("DMT Cymatics").font(PanelTheme.legendFont(11, weight: .medium)).tracking(0.5).foregroundStyle(Color(white: 0.2)) }
            }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel("Vektur Modular by DMT Cymatics")
        .padding(.trailing, compact ? 0 : 6)
    }

    private func field<C: View>(_ label: String, @ViewBuilder _ content: () -> C) -> some View {
        HStack(spacing: 6) {
            Text(label).font(PanelTheme.legendFont(12, weight: .semibold)).foregroundStyle(Color(white: 0.07))
            content()
                .padding(.horizontal, 6).frame(height: 34)
                .background(RoundedRectangle(cornerRadius: 4).fill(Color(white: 0.08)).overlay(RoundedRectangle(cornerRadius: 4).stroke(Color(white: 0.33))))
                .foregroundStyle(PanelTheme.legendNew)
        }
    }

    private func readout(_ s: String) -> some View {
        Text(s).font(.system(size: 13, weight: .medium).monospacedDigit()).foregroundStyle(PanelTheme.amber)
            .padding(.horizontal, 9).frame(minWidth: 76, minHeight: 30)
            .background(RoundedRectangle(cornerRadius: 3).fill(Color(white: 0.055)).shadow(color: .black, radius: 1.5, y: 1))
    }

    private var recTime: String { let s = Int(recorder.seconds); return String(format: "%02d:%02d", s / 60, s % 60) }

    private func flash() { midiFlash = true; Task { try? await Task.sleep(for: .milliseconds(150)); midiFlash = false } }

    /// All Sound Off + All Notes Off + reset controllers on all 16 channels.
    private func panic() {
        guard let schedule = audioUnit.scheduleMIDIEventBlock else { return }
        for ch in 0..<16 {
            for cc: UInt8 in [120, 123, 121] {
                let b: [UInt8] = [0xB0 | UInt8(ch), cc, 0]
                b.withUnsafeBufferPointer { schedule(AUEventSampleTimeImmediate, 0, 3, $0.baseAddress!) }
            }
        }
        midiText = "panic"
    }

    static func fmt(_ v: Double) -> String { v == v.rounded() ? String(Int(v)) : String(format: "%.1f", v) }
    static func noteName(_ n: UInt8) -> String {
        let names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
        return "\(names[Int(n) % 12])\(Int(n) / 12 - 1)"
    }
}

/// Dark hardware push button with a status lamp (the web build's .tbtn).
struct LampButton: View {
    let title: String
    let on: Bool
    var colour: Color
    var showsLamp = true
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 7) {
                if showsLamp {
                    Circle().fill(on ? colour : Color(red: 0.23, green: 0.07, blue: 0.06)).frame(width: 9, height: 9)
                        .shadow(color: on ? colour : .clear, radius: 4)
                }
                Text(title).font(PanelTheme.legendFont(13, weight: .semibold)).tracking(0.5).monospacedDigit()
            }
            .foregroundStyle(PanelTheme.legendNew)
            .padding(.horizontal, 12).frame(minWidth: 44, minHeight: 40)
            .background(RoundedRectangle(cornerRadius: 5)
                .fill(LinearGradient(colors: on && showsLamp ? [Color(red: 0.23, green: 0.08, blue: 0.07), Color(red: 0.1, green: 0.03, blue: 0.02)]
                                                             : [Color(white: 0.165), Color(white: 0.07)], startPoint: .top, endPoint: .bottom))
                .overlay(RoundedRectangle(cornerRadius: 5).stroke(on && showsLamp ? Color(red: 0.49, green: 0.16, blue: 0.13) : Color(white: 0.27))))
            .contentShape(Rectangle())
        }
        .buttonStyle(.plain)
        .accessibilityValue(showsLamp ? (on ? "on" : "off") : "")
    }
}

/// Brushed aluminium with a fine vertical grain (the web build's .top).
struct AluminiumStrip: View {
    var body: some View {
        Canvas { gc, size in
            let r = CGRect(origin: .zero, size: size)
            gc.fill(Path(r), with: .linearGradient(Gradient(stops: [
                .init(color: Color(red: 0.81, green: 0.82, blue: 0.83), location: 0), .init(color: Color(red: 0.62, green: 0.635, blue: 0.647), location: 0.48),
                .init(color: Color(red: 0.55, green: 0.56, blue: 0.573), location: 0.52), .init(color: Color(red: 0.70, green: 0.71, blue: 0.72), location: 1)]),
                startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
            var x: CGFloat = 0
            while x < size.width { gc.fill(Path(CGRect(x: x, y: 0, width: 1, height: size.height)), with: .color(.white.opacity(0.06))); x += 3 }
            gc.fill(Path(CGRect(x: 0, y: size.height - 2, width: size.width, height: 2)), with: .color(Color(white: 0.23)))
        }
        .shadow(color: .black.opacity(0.6), radius: 9, y: 4)
        .accessibilityHidden(true)
    }
}
