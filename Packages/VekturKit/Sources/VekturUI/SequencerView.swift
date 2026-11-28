// SequencerView.swift — Phase 5 page: arpeggiator, 16-step analogue-style
// sequencer, generative engine, note capture with MIDI export, and the
// generative preset creator. Every knob is a real AUParameter (automatable,
// saved with the project). Styling is functional; the hardware panel look
// (slider banks, LEDs, jacks) arrives in Phase 8.
import SwiftUI
import UniformTypeIdentifiers
@preconcurrency import AudioToolbox
import VekturAudioUnit
import VekturCore
import VekturDSP

public struct SequencerView: View {
    let audioUnit: VekturModularAudioUnit
    @State private var models: [UInt64: ParameterModel]
    @State private var recorder: CaptureRecorder
    @State private var chords: [String] = []
    @State private var exportDocument: MIDIDocument?
    @State private var exportName = "Vektur"
    @State private var showExporter = false
    @State private var presetCategory = GeneratedPresetCategory.bass
    @State private var presetSeed: UInt32 = 1
    @State private var presetReport: GeneratedPresetReport?
    @State private var clips: [NoteClip] = []

    public init(audioUnit: VekturModularAudioUnit) {
        self.audioUnit = audioUnit
        var m: [UInt64: ParameterModel] = [:]
        let tree = audioUnit.parameterTree
        for spec in ["arp", "seq", "gen"].flatMap({ ParameterRegistry.specs(inGroup: $0) }) + ["global.tempo"].compactMap({ ParameterRegistry.spec(identifier: $0) }) {
            if let p = tree?.parameter(withAddress: spec.address) { m[spec.address] = ParameterModel(p) }
        }
        _models = State(initialValue: m)
        _recorder = State(initialValue: CaptureRecorder(audioUnit: audioUnit))
    }

    public var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                clockSection
                arpSection
                sequencerSection
                generativeSection
                captureSection
                presetSection
            }
            .padding(.vertical, 4)
        }
        .onAppear { refreshChords(); clips = audioUnit.clips }
        .fileExporter(isPresented: $showExporter, document: exportDocument, contentType: .midi, defaultFilename: exportName) { _ in }
    }

    // MARK: Sections

    private var clockSection: some View {
        TimelineView(.periodic(from: .now, by: 1.0 / 15.0)) { _ in
            let st = audioUnit.sequencerStatus()
            HStack(spacing: 14) {
                Label(st.hostPlaying ? "Host playing" : "Free clock", systemImage: st.hostPlaying ? "play.fill" : "metronome")
                Text(String(format: "Bar %d · beat %.2f", Int(st.beat / 4) + 1, st.beat.truncatingRemainder(dividingBy: 4) + 1)).monospacedDigit()
                if let m = model("global.tempo") { ParamControl(model: m, label: "Tempo").frame(maxWidth: 320) }
                Spacer()
                led(st.arpRunning, "Arp"); led(st.sequencerRunning, "Seq"); led(st.generativeRunning, "Gen"); led(st.capturing, "Rec", .red)
            }
            .font(.caption)
        }
    }

    private var arpSection: some View {
        section("Arpeggiator") {
            grid(["mode", "pattern", "octaves", "octaveMode", "rate", "gate", "swing", "restart",
                  "velocityMode", "fixedVelocity", "probability", "ratchet", "ratchetChance", "seed"].map { "arp.\($0)" })
            Text("Rhythm").font(.callout.weight(.semibold))
            grid(["rhythm", "euclidSteps", "euclidPulses", "euclidRotate"].map { "arp.\($0)" })
            Text("Pattern chain").font(.callout.weight(.semibold))
            grid(["chain", "chainPattern2", "chainPattern3", "chainPattern4", "chainPasses", "seqTranspose"].map { "arp.\($0)" })
            Text("Latch keeps the chord after you let go; a new hand replaces it. Auto restart follows the host bar grid while it plays, and the first key otherwise. The sustain pedal holds keys in the arp.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private var sequencerSection: some View {
        section("Step sequencer") {
            grid(["run", "length", "direction", "rate", "swing", "gate", "pitchRange", "scale", "root",
                  "transpose", "playNotes", "rowBVelocity", "seed"].map { "seq.\($0)" })
            TimelineView(.periodic(from: .now, by: 1.0 / 30.0)) { _ in
                let current = audioUnit.sequencerStatus().sequencerStep
                ScrollView(.horizontal) {
                    HStack(alignment: .top, spacing: 6) {
                        rowLegend
                        ForEach(0..<16, id: \.self) { s in StepColumn(step: s, models: models, isCurrent: current == s) }
                    }
                    .padding(.vertical, 4)
                }
            }
            Text("Row A plays notes (quantised to the scale); rows A, B and C are also the Seq Row A/B/C sources in the modulation matrix. Tie holds the previous note; Skip removes the step from the cycle. Key mode runs while a key is held and transposes from C4.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private var rowLegend: some View {
        VStack(alignment: .trailing, spacing: 6) {
            Text(" ").font(.caption2)
            ForEach(["A", "B", "C"], id: \.self) { Text($0).font(.caption.weight(.semibold)).frame(height: 70) }
            Text("Gate").font(.caption2).frame(height: 24)
            Text("Prob").font(.caption2).frame(height: 24)
            Text("Ratchet").font(.caption2).frame(height: 24)
        }
    }

    private var generativeSection: some View {
        section("Generative engine") {
            grid(["mode", "style", "scale", "root", "density", "complexity", "range", "register",
                  "phraseBars", "rate", "evolve", "humanise", "followKeys", "swing", "gate", "seed"].map { "gen.\($0)" })
            HStack {
                Button { if let m = model("gen.seed") { m.value = Float(Int.random(in: 0...65535)) }; refreshChords() } label: { Label("New seed", systemImage: "dice") }
                Button { refreshChords() } label: { Label("Show chords", systemImage: "music.note.list") }
                Button { let c = audioUnit.generativePhrase(); audioUnit.storeClip(c); clips = audioUnit.clips } label: { Label("Keep phrase", systemImage: "tray.and.arrow.down") }
                Button { export(audioUnit.generativePhrase(repeat: 0)) } label: { Label("Export phrase (.mid)", systemImage: "square.and.arrow.up") }
            }
            .buttonStyle(.bordered).font(.caption)
            if !chords.isEmpty {
                Text("Progression: " + chords.joined(separator: " → ")).font(.callout.monospaced())
                    .accessibilityLabel("Chord progression " + chords.joined(separator: ", "))
            }
            Text("Phrases are locked to the bar grid and reproducible from the seed; Evolve re-rolls bars gradually on each repeat. Edits take effect at the next phrase. Hold to Play runs while a key is held and transposes to it.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private var captureSection: some View {
        section("Capture") {
            HStack {
                Button { recorder.isRecording ? finishTake() : recorder.start() } label: {
                    Label(recorder.isRecording ? "Stop" : "Record", systemImage: recorder.isRecording ? "stop.fill" : "record.circle")
                }
                .tint(recorder.isRecording ? .red : .accentColor)
                Text(recorder.isRecording ? "\(recorder.noteCount) notes" : "Records keys, arp, sequencer and generative notes on the beat grid.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            .buttonStyle(.bordered)
            ForEach(clips) { clip in
                HStack {
                    Text(clip.name).font(.callout)
                    Text("\(clip.notes.count) notes · \(Int(clip.lengthBeats / 4)) bars · \(Int(clip.tempo)) BPM").font(.caption).foregroundStyle(.secondary)
                    Spacer()
                    Button { export(clip) } label: { Image(systemName: "square.and.arrow.up") }.accessibilityLabel("Export \(clip.name) as MIDI")
                    Button(role: .destructive) { audioUnit.removeClip(id: clip.id); clips = audioUnit.clips } label: { Image(systemName: "trash") }
                        .accessibilityLabel("Delete \(clip.name)")
                }
            }
        }
    }

    private var presetSection: some View {
        section("Preset creator") {
            HStack(spacing: 12) {
                Picker("Category", selection: $presetCategory) {
                    ForEach(GeneratedPresetCategory.allCases) { Text($0.displayName).tag($0) }
                }
                .pickerStyle(.menu)
                Stepper(value: Binding(get: { Int(presetSeed) }, set: { presetSeed = UInt32(max(0, min($0, 999_999))) }), in: 0...999_999) {
                    Text("Seed \(presetSeed)").monospacedDigit()
                }
                .frame(maxWidth: 220)
                Button { presetSeed = UInt32.random(in: 0...999_999); generate() } label: { Label("Surprise me", systemImage: "dice") }
                Button { generate() } label: { Label("Generate", systemImage: "wand.and.stars") }
            }
            .buttonStyle(.bordered)
            if let r = presetReport {
                Text("Loaded “\(r.name)” — audition \(String(format: "%.1f", r.auditionLevel)) dBFS\(r.attempts > 1 ? ", \(r.attempts) candidates tried" : "")\(r.usedFallback ? ", safe voice used" : "")")
                    .font(.caption)
            }
            Text("Builds a complete sound from the seed within musical ranges for the category, auditions it, and never returns a silent or clipping result. The same category and seed always rebuild the same sound. Arp, sequencer, generative and performance settings are kept; the patch returns to the normalled voice.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    // MARK: Actions

    private func generate() { presetReport = audioUnit.generatePreset(category: presetCategory, seed: presetSeed) }

    private func refreshChords() { chords = audioUnit.generativeChords() }

    private func finishTake() {
        if let clip = recorder.stop(), !clip.notes.isEmpty { audioUnit.storeClip(clip); clips = audioUnit.clips }
    }

    private func export(_ clip: NoteClip) {
        exportDocument = MIDIDocument(data: MIDIFileWriter.data(for: clip))
        exportName = clip.name
        showExporter = true
    }

    // MARK: Helpers

    private func model(_ identifier: String) -> ParameterModel? {
        ParameterRegistry.spec(identifier: identifier).flatMap { models[$0.address] }
    }

    private func led(_ on: Bool, _ label: String, _ colour: Color = .green) -> some View {
        HStack(spacing: 3) {
            Circle().fill(on ? colour : Color.secondary.opacity(0.25)).frame(width: 8, height: 8)
            Text(label)
        }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(label) \(on ? "running" : "stopped")")
    }

    private func section<C: View>(_ title: String, @ViewBuilder _ content: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 8) {
            Text(title).font(.headline)
            content()
        }
        .padding(12)
        .background(PanelPaint(key: "page").clipShape(RoundedRectangle(cornerRadius: 6)))   // Phase 8 panel paint
    }

    private func grid(_ identifiers: [String]) -> some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 220), spacing: 10, alignment: .leading)], alignment: .leading, spacing: 8) {
            ForEach(identifiers, id: \.self) { id in
                if let spec = ParameterRegistry.spec(identifier: id), let m = models[spec.address] {
                    ParamControl(model: m, label: shortLabel(spec.displayName))
                }
            }
        }
    }

    private func shortLabel(_ name: String) -> String {
        for prefix in ["Arp ", "Seq ", "Generate "] where name.hasPrefix(prefix) { return String(name.dropFirst(prefix.count)) }
        return name
    }
}

// MARK: - One sequencer step (three row sliders, gate, probability, ratchet)

private struct StepColumn: View {
    let step: Int
    let models: [UInt64: ParameterModel]
    let isCurrent: Bool

    private func m(_ field: UInt64) -> ParameterModel? { models[UInt64(VK_P_SEQ_STEP1_A) + UInt64(step) * 8 + field] }

    var body: some View {
        VStack(spacing: 6) {
            Circle().fill(isCurrent ? Color.red : Color.secondary.opacity(0.25)).frame(width: 9, height: 9)
                .accessibilityHidden(true)
            ForEach(0..<3, id: \.self) { r in
                if let model = m(UInt64(r)) { VerticalSlider(model: model, label: "Step \(step + 1) row \(["A", "B", "C"][r])").frame(width: 26, height: 70) }
            }
            if let gate = m(3) { GateButton(model: gate, step: step).frame(height: 24) }
            if let prob = m(4) {
                Text("\(Int(prob.value))").font(.caption2.monospacedDigit()).frame(height: 24)
                    .gesture(DragGesture(minimumDistance: 2).onChanged { prob.value = min(100, max(0, prob.value - Float($0.translation.height) * 0.05)) })
                    .accessibilityLabel("Step \(step + 1) probability").accessibilityValue("\(Int(prob.value)) percent")
                    .accessibilityAdjustableAction { dir in prob.value = min(100, max(0, prob.value + (dir == .increment ? 10 : -10))) }
            }
            if let rat = m(5) {
                Button { rat.value = Float((Int(rat.value.rounded()) + 1) % 4) } label: { Text("\(Int(rat.value.rounded()) + 1)×").font(.caption2) }
                    .buttonStyle(.plain).frame(height: 24)
                    .accessibilityLabel("Step \(step + 1) ratchet").accessibilityValue("\(Int(rat.value.rounded()) + 1) times")
            }
            Text("\(step + 1)").font(.caption2).foregroundStyle(.secondary)
        }
        .padding(.horizontal, 2)
        .background(RoundedRectangle(cornerRadius: 4).fill(isCurrent ? Color.red.opacity(0.08) : .clear))
    }
}

private struct GateButton: View {
    @Bindable var model: ParameterModel
    let step: Int
    private static let symbols = ["–", "●", "⌒", "↷"]
    private static let names = ["Off", "On", "Tie", "Skip"]
    var body: some View {
        let i = min(max(Int(model.value.rounded()), 0), 3)
        Button { model.value = Float((i + 1) % 4) } label: { Text(Self.symbols[i]).frame(width: 24) }
            .buttonStyle(.bordered).controlSize(.mini)
            .accessibilityLabel("Step \(step + 1) gate").accessibilityValue(Self.names[i])
    }
}

/// Slider-bank style vertical fader (drag or VoiceOver adjust).
struct VerticalSlider: View {
    @Bindable var model: ParameterModel
    let label: String

    var body: some View {
        GeometryReader { geo in
            let p = model.parameter
            let frac = CGFloat((model.value - p.minValue) / max(p.maxValue - p.minValue, 1e-6))
            ZStack(alignment: .bottom) {
                RoundedRectangle(cornerRadius: 3).fill(Color.secondary.opacity(0.2))
                RoundedRectangle(cornerRadius: 3).fill(Color.accentColor.opacity(0.7)).frame(height: max(2, geo.size.height * frac))
            }
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0).onChanged { g in
                let f = Float(1 - min(max(g.location.y / geo.size.height, 0), 1))
                model.value = p.minValue + f * (p.maxValue - p.minValue)
            })
        }
        .accessibilityElement()
        .accessibilityLabel(label)
        .accessibilityValue(model.displayString)
        .accessibilityAdjustableAction { dir in
            let p = model.parameter, stepSize = (p.maxValue - p.minValue) / 20
            model.value = min(p.maxValue, max(p.minValue, model.value + (dir == .increment ? stepSize : -stepSize)))
        }
    }
}

// MARK: - Capture recorder

@MainActor
@Observable
final class CaptureRecorder {
    private(set) var isRecording = false
    private(set) var noteCount = 0
    @ObservationIgnored private let audioUnit: VekturModularAudioUnit
    @ObservationIgnored private var events: [(beat: Double, note: UInt8, velocity: UInt8, source: UInt8)] = []
    @ObservationIgnored private var task: Task<Void, Never>?
    @ObservationIgnored private var takes = 0

    init(audioUnit: VekturModularAudioUnit) { self.audioUnit = audioUnit }

    func start() {
        _ = audioUnit.drainCapturedEvents()
        events.removeAll(); noteCount = 0
        audioUnit.setCapturing(true)
        isRecording = true
        task = Task { [weak self] in   // same pattern as TelemetryModel: inherits the main actor
            while !Task.isCancelled {
                self?.drain()
                try? await Task.sleep(for: .milliseconds(50))
            }
        }
    }

    func stop() -> NoteClip? {
        guard isRecording else { return nil }
        audioUnit.setCapturing(false)
        task?.cancel(); task = nil
        isRecording = false
        drain()
        takes += 1
        return NoteClip(name: "Take \(takes)", tempo: audioUnit.generatorTempo, events: events)
    }

    private func drain() {
        let new = audioUnit.drainCapturedEvents()
        events.append(contentsOf: new)
        noteCount += new.filter { $0.velocity > 0 }.count
    }
}

// MARK: - MIDI file document (for .fileExporter)

struct MIDIDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.midi] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}
