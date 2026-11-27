// Screens.swift — Phase 8 screens that are new in this phase: Performance,
// Arpeggiator, Generative, Mixer & Output, Presets and Settings. The other
// screens (rack, patch canvas, sequencer, effects, modulation, MIDI,
// diagnostics) are their own files. All use the hardware panel language.
import SwiftUI
import UniformTypeIdentifiers
@preconcurrency import AudioToolbox
import VekturAudioUnit
import VekturCore
import VekturDSP

// MARK: - A generic panel for one parameter group

/// One painted panel holding every parameter of the given groups, styled by
/// parameter type (used for the arpeggiator, generative engine and buses).
struct GroupPanel: View {
    let title: String
    let identifiers: [String]
    let models: PanelModels
    var key: String
    var lamp: @MainActor () -> Bool = { false }
    @Environment(\.panelWear) private var wear

    var body: some View {
        let specs = identifiers.compactMap { ParameterRegistry.spec(identifier: $0) }
        VStack(spacing: 10) {
            HStack {
                Text(title).font(PanelTheme.legendFont(15, weight: .bold)).tracking(1.3).foregroundStyle(PanelTheme.legend(wear: wear))
                TimelineView(.periodic(from: .now, by: 0.1)) { _ in PanelLED(on: lamp()) }
            }
            .accessibilityElement(children: .combine).accessibilityAddTraits(.isHeader)
            FlowGrid(minWidth: 70) {
                ForEach(specs.filter { ControlStyle.for($0, inSliderBank: false) != .knob }, id: \.address) { s in
                    if let c = models.model(s.identifier) { PanelControl(model: c.0, spec: c.1) }
                }
            }
            FlowGrid(minWidth: 70) {
                ForEach(specs.filter { ControlStyle.for($0, inSliderBank: false) == .knob }, id: \.address) { s in
                    if let c = models.model(s.identifier) { PanelKnob(model: c.0, spec: c.1, showsValue: true) }
                }
            }
        }
        .padding(14)
        .background(PanelPaint(key: key))
        .overlay(alignment: .topLeading) { ScrewHead(angle: 20).padding(5) }
        .overlay(alignment: .topTrailing) { ScrewHead(angle: 110).padding(5) }
    }
}

/// Rails + tolex around arbitrary panel content.
struct Cabinet<Content: View>: View {
    var title: String
    @ViewBuilder var content: Content
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title).font(PanelTheme.legendFont(12, weight: .semibold)).tracking(1).foregroundStyle(Color(white: 0.55)).padding(.leading, 10)
                .accessibilityAddTraits(.isHeader)
            VStack(spacing: 0) { RackRail(); content; RackRail() }
                .padding(.horizontal, 16).padding(.vertical, 10)
                .background(TolexBackground().clipShape(RoundedRectangle(cornerRadius: 10)))
                .shadow(color: .black.opacity(0.6), radius: 14, y: 10)
        }
    }
}

private func ids(_ groups: [String]) -> [String] { groups.flatMap { ParameterRegistry.specs(inGroup: $0).map(\.identifier) } }

// MARK: - Arpeggiator & Generative

struct ArpeggiatorScreen: View {
    let audioUnit: VekturModularAudioUnit
    @State private var models: PanelModels
    init(audioUnit: VekturModularAudioUnit) { self.audioUnit = audioUnit; _models = State(initialValue: PanelModels(audioUnit: audioUnit, identifiers: ids(["arp"]) + ["global.tempo"])) }
    var body: some View {
        ScrollView {
            Cabinet(title: "arpeggiator") {
                GroupPanel(title: "arpeggiator", identifiers: ids(["arp"]) + ["global.tempo"], models: models, key: "arp",
                           lamp: { let s = audioUnit.sequencerStatus(); return s.arpRunning && Int(s.beat * 4) % 2 == 0 })
            }
            .padding()
        }
        .background(PanelTheme.room)
    }
}

struct GenerativeScreen: View {
    let audioUnit: VekturModularAudioUnit
    @State private var models: PanelModels
    @State private var chords: [String] = []
    init(audioUnit: VekturModularAudioUnit) { self.audioUnit = audioUnit; _models = State(initialValue: PanelModels(audioUnit: audioUnit, identifiers: ids(["gen"]))) }
    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                Cabinet(title: "generative engine") {
                    GroupPanel(title: "generative", identifiers: ids(["gen"]), models: models, key: "gen", lamp: { audioUnit.sequencerStatus().generativeRunning })
                }
                HStack {
                    Button("Show this phrase's chords") { chords = audioUnit.generativeChords() }.buttonStyle(.bordered)
                    Text(chords.isEmpty ? "" : chords.joined(separator: "  ·  ")).font(PanelTheme.legendFont(16, weight: .semibold)).foregroundStyle(PanelTheme.amber)
                }
                TimelineView(.periodic(from: .now, by: 0.25)) { _ in
                    let s = audioUnit.sequencerStatus()
                    Text(s.generativeRunning ? "Phrase \((s.generativePhrase ?? 0) + 1), bar \((s.generativeBar ?? 0) + 1)" : "Set Generate to Run and press play (or follow the host).")
                        .font(.footnote).foregroundStyle(PanelTheme.legend2)
                }
            }
            .padding()
        }
        .background(PanelTheme.room)
    }
}

// MARK: - Performance

struct PerformanceScreen: View {
    let audioUnit: VekturModularAudioUnit
    @State private var models: PanelModels
    @State private var telemetry: TelemetryModel
    @State private var octave = 3
    @State private var bend: Double = 0.5
    @State private var mod: Double = 0

    init(audioUnit: VekturModularAudioUnit) {
        self.audioUnit = audioUnit
        _models = State(initialValue: PanelModels(audioUnit: audioUnit, identifiers: ["macro.macro1", "macro.macro2", "macro.macro3", "macro.macro4", "global.glide", "global.voiceMode", "global.masterVolume"]))
        _telemetry = State(initialValue: TelemetryModel.shared(kernel: audioUnit.kernel, sampleRate: { audioUnit.outputBusses[0].format.sampleRate }))
    }

    var body: some View {
        VStack(spacing: 14) {
            HStack(alignment: .top, spacing: 18) {
                Cabinet(title: "macros") {
                    HStack(spacing: 18) {
                        ForEach(1...4, id: \.self) { k in
                            if let c = models.model("macro.macro\(k)") { PanelKnob(model: c.0, spec: c.1, legend: "macro \(k)", size: 54, showsValue: true) }
                        }
                    }
                    .padding(14).background(PanelPaint(key: "perf-macros"))
                }
                Cabinet(title: "voice") {
                    HStack(spacing: 14) {
                        if let c = models.model("global.voiceMode") { PanelControl(model: c.0, spec: c.1) }
                        if let c = models.model("global.glide") { PanelKnob(model: c.0, spec: c.1, showsValue: true) }
                        if let c = models.model("global.masterVolume") { PanelKnob(model: c.0, spec: c.1, legend: "volume", showsValue: true) }
                    }
                    .padding(14).background(PanelPaint(key: "perf-voice"))
                }
                VStack {
                    VUMeter(level: telemetry.peakL, label: "left")
                    VUMeter(level: telemetry.peakR, label: "right")
                }
                TimelineView(.periodic(from: .now, by: 0.15)) { _ in
                    let notes = audioUnit.heldNotes()
                    Text(notes.count >= 2 ? ChordAnalyzer.name(of: notes) : notes.first.map { MIDIMapping.noteName(Int($0)) } ?? "—")
                        .font(PanelTheme.legendFont(34, weight: .bold)).foregroundStyle(PanelTheme.amber).frame(minWidth: 120)
                        .accessibilityLabel("Chord").accessibilityValue(notes.isEmpty ? "none" : ChordAnalyzer.name(of: notes))
                }
            }
            HStack(spacing: 10) {
                PerformanceWheel(label: "bend", value: $bend, springs: true) { n in let v = Int(n * 16383); send([0xE0, UInt8(v & 127), UInt8(v >> 7)]) }
                PerformanceWheel(label: "mod", value: $mod, springs: false) { n in send([0xB0, 1, UInt8(n * 127)]) }
                VStack {
                    Stepper("Octave C\(octave)", value: $octave, in: 0...7).labelsHidden()
                    Text("C\(octave)").font(PanelTheme.legendFont(13)).foregroundStyle(PanelTheme.amber)
                }
                TestKeyboardView(audioUnit: audioUnit, lowestNote: 12 * (octave + 1), octaves: 3)
            }
            .frame(height: 170)
        }
        .padding()
        .background(PanelTheme.room)
        .onAppear { telemetry.start() }
        .onDisappear { telemetry.stop() }
    }

    private func send(_ b: [UInt8]) {
        guard let schedule = audioUnit.scheduleMIDIEventBlock else { return }
        b.withUnsafeBufferPointer { schedule(AUEventSampleTimeImmediate, 0, b.count, $0.baseAddress!) }
    }
}

struct PerformanceWheel: View {
    var label: String
    @Binding var value: Double
    var springs: Bool
    var send: (Double) -> Void
    @State private var start: Double?
    var body: some View {
        VStack(spacing: 4) {
            GeometryReader { g in
                ZStack(alignment: .top) {
                    RoundedRectangle(cornerRadius: 5).fill(.black).overlay(RoundedRectangle(cornerRadius: 5).stroke(Color(white: 0.2), lineWidth: 2))
                    RoundedRectangle(cornerRadius: 4)
                        .fill(LinearGradient(colors: [Color(white: 0.35), Color(white: 0.17), Color(white: 0.35)], startPoint: .top, endPoint: .bottom))
                        .frame(height: 30).padding(.horizontal, 3)
                        .offset(y: (1 - value) * (g.size.height - 30))
                }
                .contentShape(Rectangle())
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { d in if start == nil { start = value }; value = min(max(start! - d.translation.height / (g.size.height - 30), 0), 1); send(value) }
                    .onEnded { _ in start = nil; if springs { value = 0.5; send(0.5) } })
            }
            .frame(width: 34)
            LegendText(text: label)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(label == "bend" ? "Pitch bend" : "Modulation wheel")
        .accessibilityValue(String(format: "%.0f %%", value * 100))
        .accessibilityAdjustableAction { dir in value = min(max(value + (dir == .increment ? 0.1 : -0.1), 0), 1); send(value) }
    }
}

// MARK: - Mixer & output

struct OutputScreen: View {
    let audioUnit: VekturModularAudioUnit
    @State private var models: PanelModels
    @State private var telemetry: TelemetryModel
    private static let mix = ["vco1.level", "vco2.level", "vco3.level", "vco1.subLevel", "noise.level", "ringMod.level"]
    private static let bus = ["fxBus.dry", "fxBus.returnA", "fxBus.returnB", "fxBus.output"]
    private static let out = ["global.masterVolume", "global.stereoSpread", "global.instability", "global.masterTune"]

    init(audioUnit: VekturModularAudioUnit) {
        self.audioUnit = audioUnit
        _models = State(initialValue: PanelModels(audioUnit: audioUnit, identifiers: Self.mix + Self.bus + Self.out))
        _telemetry = State(initialValue: TelemetryModel.shared(kernel: audioUnit.kernel, sampleRate: { audioUnit.outputBusses[0].format.sampleRate }))
    }

    var body: some View {
        ScrollView {
            Cabinet(title: "mixer · output") {
                HStack(alignment: .top, spacing: 0) {
                    strip("voice mix", Self.mix, key: "out-mix")
                    strip("effects", Self.bus, key: "out-fx")
                    VStack(spacing: 12) {
                        Text("output").font(PanelTheme.legendFont(15, weight: .bold)).foregroundStyle(PanelTheme.legendNew)
                        HStack { VUMeter(level: telemetry.peakL, label: "left"); VUMeter(level: telemetry.peakR, label: "right") }
                        HStack(alignment: .top) {
                            if let c = models.model("global.masterVolume") { PanelFader(model: c.0, spec: c.1, legend: "master") }
                            VStack { ForEach(Self.out.dropFirst(), id: \.self) { id in if let c = models.model(id) { PanelKnob(model: c.0, spec: c.1, showsValue: true) } } }
                        }
                        HStack(spacing: 14) {
                            PanelLED(on: telemetry.clipped).accessibilityLabel("Clip")
                            Text(String(format: "dsp %.0f %% · %d voices", telemetry.dspLoad * 100, telemetry.activeVoices))
                                .font(.caption.monospacedDigit()).foregroundStyle(PanelTheme.legend2)
                        }
                    }
                    .padding(14).frame(width: 300).background(PanelPaint(key: "out-main"))
                }
            }
            .padding()
            RecordingPanel(audioUnit: audioUnit).padding(.horizontal)
        }
        .background(PanelTheme.room)
        .onAppear { telemetry.start() }
        .onDisappear { telemetry.stop() }
    }

    private func strip(_ title: String, _ list: [String], key: String) -> some View {
        VStack(spacing: 10) {
            Text(title).font(PanelTheme.legendFont(15, weight: .bold)).foregroundStyle(PanelTheme.legendNew)
            HStack(alignment: .top, spacing: 4) { ForEach(list, id: \.self) { id in if let c = models.model(id) { PanelFader(model: c.0, spec: c.1) } } }
        }
        .padding(14).frame(minHeight: PanelTheme.panelHeight, alignment: .top).background(PanelPaint(key: key))
    }
}

// Presets: see PresetLibraryView.swift (Phase 9).

// MARK: - Settings

struct SettingsScreen: View {
    let audioUnit: VekturModularAudioUnit
    @State private var appearance: AppearanceState
    @State private var layout: PatchLayout
    @State private var showAbout = false

    init(audioUnit: VekturModularAudioUnit) {
        self.audioUnit = audioUnit
        _appearance = State(initialValue: audioUnit.appearance)
        _layout = State(initialValue: audioUnit.currentPatch.layout)
    }

    var body: some View {
        Form {
            Section {
                Slider(value: $appearance.wear, in: 0...1) { Text("Wear") } minimumValueLabel: { Text("new") } maximumValueLabel: { Text("used") }
                    .accessibilityValue(String(format: "%.0f %%", appearance.wear * 100))
                Toggle("Tilt-reactive metal highlights", isOn: $appearance.tiltHighlights)
                ForEach(PanelLayout.standard.modules) { m in
                    HStack {
                        Text(m.title)
                        Spacer()
                        if appearance.moduleWear[m.key] != nil {
                            Slider(value: Binding(get: { appearance.moduleWear[m.key] ?? appearance.wear }, set: { appearance.moduleWear[m.key] = $0 }), in: 0...1).frame(maxWidth: 200)
                            Button("Rack") { appearance.moduleWear[m.key] = nil }.buttonStyle(.borderless)
                        } else {
                            Button("Own wear") { appearance.moduleWear[m.key] = appearance.wear }.buttonStyle(.borderless)
                        }
                    }
                }
            } header: { Text("Panels") } footer: { Text("Wear adds scratches, edge rub, dust and yellowed legends. Legends always stay readable. Saved with the project.") }
            Section("Cables") {
                Slider(value: $layout.cableOpacity, in: 0.1...1) { Text("Opacity") }
                Slider(value: $layout.cableThickness, in: 1...8) { Text("Thickness") }
                Toggle("Show default (normalled) connections on the canvas", isOn: $layout.showNormals)
            }
            PerformanceSettingsSection(audioUnit: audioUnit)
            Section("Help") {
                NavigationLink("Manual (38 chapters)") { ManualView() }
                Button("About Vektur Modular") { showAbout = true }
            }
        }
        .onChange(of: appearance) { _, a in audioUnit.setAppearance(a) }
        .onChange(of: layout) { _, l in audioUnit.updateLayout(l) }
        .sheet(isPresented: $showAbout) { AboutView() }
    }
}
