// ModulationView.swift — Phase 4A full-screen modulation page: keyboard &
// voicing, three LFOs, four macros, the 16-slot matrix and performance
// wheels. Every control is a real AUParameter (host-automatable, saved in
// state); the wheels send MIDI through the same path hosts use.
// Styling is functional; the hardware panel look arrives in Phase 8.
import SwiftUI
@preconcurrency import AudioToolbox
import VekturAudioUnit
import VekturCore

public struct ModulationView: View {
    let audioUnit: VekturModularAudioUnit
    @State private var models: [UInt64: ParameterModel]
    @State private var showEmptySlots = false

    private static let groups = ["lfo1", "lfo2", "lfo3", "macro", "matrix"]
    private static let globalExtras = ["global.glide", "global.pitchBendRange", "global.voiceMode", "global.notePriority", "global.glideMode",
                                        "global.unisonVoices", "global.unisonDetune", "global.bendDown", "global.glideConstantTime",
                                        "global.tempo", "global.mpeMode", "global.mpeMemberChannels", "global.mpePitchRange",
                                        "global.zoneMode", "global.splitPoint", "global.zoneATranspose", "global.zoneBTranspose"]

    public init(audioUnit: VekturModularAudioUnit) {
        self.audioUnit = audioUnit
        let tree = audioUnit.parameterTree
        var m: [UInt64: ParameterModel] = [:]
        let specs = Self.groups.flatMap { ParameterRegistry.specs(inGroup: $0) }
            + Self.globalExtras.compactMap { ParameterRegistry.spec(identifier: $0) }
        for spec in specs {
            if let p = tree?.parameter(withAddress: spec.address) { m[spec.address] = ParameterModel(p) }
        }
        _models = State(initialValue: m)
    }

    public var body: some View {
        HStack(alignment: .top, spacing: 12) {
            PerformanceWheelsView(audioUnit: audioUnit)
                .frame(width: 96)
            ScrollView {
                VStack(alignment: .leading, spacing: 18) {
                    ChordReadout(audioUnit: audioUnit)
                    voicingSection
                    zonesSection
                    lfoSection
                    macroSection
                    matrixSection
                }
                .padding(.vertical, 4)
            }
        }
    }

    // MARK: Sections

    private var voicingSection: some View {
        section("Keyboard & voicing") {
            grid(["global.voiceMode", "global.notePriority", "global.glide", "global.glideMode", "global.glideConstantTime",
                  "global.unisonVoices", "global.unisonDetune", "global.pitchBendRange", "global.bendDown"])
            Text("Bend down −1 follows the up range. Unison plays one key on 2–8 detuned voices; duo plays the lowest and highest keys.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private var zonesSection: some View {
        section("MPE, key zones & tempo") {
            grid(["global.mpeMode", "global.mpeMemberChannels", "global.mpePitchRange",
                  "global.zoneMode", "global.splitPoint", "global.zoneATranspose", "global.zoneBTranspose", "global.tempo"])
            Text("Zones apply in Poly mode. Use the Key Zone source in the matrix to make each zone sound different. Internal tempo is used when the host provides none.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private var lfoSection: some View {
        section("LFOs") {
            ForEach(1...3, id: \.self) { n in
                VStack(alignment: .leading, spacing: 4) {
                    Text("LFO \(n)").font(.callout.weight(.semibold))
                    grid(["shape", "rate", "tempoSync", "sync", "delay", "phase", "level"].map { "lfo\(n).\($0)" })
                }
            }
            Text("LFOs are also patchable: drag from the LFO jacks on the patch canvas.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private var macroSection: some View {
        section("Macros") { grid((1...4).map { "macro.macro\($0)" }) }
    }

    private var matrixSection: some View {
        section("Modulation matrix") {
            Toggle("Show unused slots", isOn: $showEmptySlots).font(.caption)
            ForEach(0..<ModSlotAddresses.slotCount, id: \.self) { i in
                let a = ModSlotAddresses(i)
                if let src = models[a.source], showEmptySlots || src.value.rounded() != 0 || i == firstEmptySlot {
                    ModSlotRow(index: i, models: models, audioUnit: audioUnit)
                }
            }
        }
    }

    private var firstEmptySlot: Int {
        (0..<ModSlotAddresses.slotCount).first { (models[ModSlotAddresses($0).source]?.value.rounded() ?? 0) == 0 } ?? -1
    }

    // MARK: Helpers

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
        for prefix in ["LFO 1 ", "LFO 2 ", "LFO 3 "] where name.hasPrefix(prefix) { return String(name.dropFirst(prefix.count)) }
        return name
    }
}

// MARK: - One matrix slot

private struct ModSlotRow: View {
    let index: Int
    let models: [UInt64: ParameterModel]
    let audioUnit: VekturModularAudioUnit

    private var a: ModSlotAddresses { ModSlotAddresses(index) }

    var body: some View {
        if let enable = models[a.enable], let src = models[a.source], let via = models[a.via], let dest = models[a.destination],
           let amount = models[a.amount], let offset = models[a.offset], let curve = models[a.curve],
           let polarity = models[a.polarity], let invert = models[a.invert], let slew = models[a.slew] {
            VStack(alignment: .leading, spacing: 6) {
                HStack(spacing: 8) {
                    Text("\(index + 1)").font(.caption.monospacedDigit()).frame(width: 20)
                    Toggle("", isOn: bool(enable)).labelsHidden()
                        .accessibilityLabel("Slot \(index + 1) enabled")
                    sourcePicker(src, title: "Source")
                    Image(systemName: "arrow.right").foregroundStyle(.secondary).accessibilityHidden(true)
                    DestinationMenu(model: dest)
                    Spacer(minLength: 0)
                    SlotMeter(audioUnit: audioUnit, slot: index)
                }
                HStack(spacing: 12) {
                    labeledSlider("Amount", amount)
                    labeledSlider("Offset", offset)
                }
                HStack(spacing: 10) {
                    sourcePicker(via, title: "Scale by")
                    indexed(curve, title: "Curve")
                    indexed(polarity, title: "Polarity")
                    Toggle("Invert", isOn: bool(invert)).toggleStyle(.button).font(.caption)
                    labeledSlider("Slew", slew).frame(maxWidth: 200)
                }
            }
            .padding(8)
            .background(RoundedRectangle(cornerRadius: 8).stroke(Color.secondary.opacity(0.25)))
            .opacity(enable.value >= 0.5 ? 1 : 0.55)
            .accessibilityElement(children: .contain)
            .accessibilityLabel("Modulation slot \(index + 1)")
        }
    }

    private func bool(_ m: ParameterModel) -> Binding<Bool> {
        Binding(get: { m.value >= 0.5 }, set: { m.value = $0 ? 1 : 0 })
    }

    private func sourcePicker(_ m: ParameterModel, title: String) -> some View {
        Picker(title, selection: Binding(get: { Int(m.value.rounded()) }, set: { m.value = Float($0) })) {
            ForEach(ModSource.allCases) { Text($0.displayName).tag($0.rawValue) }
        }
        .pickerStyle(.menu)
        .accessibilityLabel("Slot \(index + 1) \(title.lowercased())")
    }

    private func indexed(_ m: ParameterModel, title: String) -> some View {
        Picker(title, selection: Binding(get: { Int(m.value.rounded()) }, set: { m.value = Float($0) })) {
            ForEach(Array((m.parameter.valueStrings ?? []).enumerated()), id: \.offset) { Text($0.element).tag($0.offset) }
        }
        .pickerStyle(.menu)
        .accessibilityLabel("Slot \(index + 1) \(title.lowercased())")
    }

    private func labeledSlider(_ title: String, _ m: ParameterModel) -> some View {
        HStack(spacing: 6) {
            Text(title).font(.caption).frame(width: 46, alignment: .leading)
            Slider(value: Binding(get: { m.value }, set: { m.value = $0 }),
                   in: m.parameter.minValue...m.parameter.maxValue)
                .accessibilityLabel("Slot \(index + 1) \(title.lowercased())")
                .accessibilityValue(m.displayString)
            Text(m.displayString).font(.caption2.monospaced()).frame(width: 60, alignment: .trailing)
        }
    }
}

// Destination chooser: virtual destinations plus every modulatable parameter, by module.
private struct DestinationMenu: View {
    @Bindable var model: ParameterModel

    var body: some View {
        let current = ModDestination(code: model.value)
        Menu {
            Button("None") { model.value = 0 }
            ForEach(Array(ModDestination.catalogue.enumerated()), id: \.offset) { _, entry in
                Menu(entry.group) {
                    ForEach(entry.destinations, id: \.self) { d in
                        Button(d.displayName) { model.value = d.code }
                    }
                }
            }
        } label: {
            Text(current.displayName).lineLimit(1)
                .padding(.horizontal, 8).padding(.vertical, 4)
                .background(RoundedRectangle(cornerRadius: 6).fill(Color.accentColor.opacity(current == .none ? 0.05 : 0.15)))
        }
        .accessibilityLabel("Destination")
        .accessibilityValue(current.displayName)
    }
}

// Generic control for one parameter (toggle, picker or slider).
struct ParamControl: View {
    @Bindable var model: ParameterModel
    let label: String

    var body: some View {
        HStack(spacing: 6) {
            Text(label).font(.caption).frame(width: 96, alignment: .leading).lineLimit(1)
            control
                .accessibilityLabel(model.parameter.displayName)
                .accessibilityValue(model.displayString)
            Text(model.displayString).font(.caption2.monospaced()).frame(width: 64, alignment: .trailing).lineLimit(1)
        }
    }

    @ViewBuilder private var control: some View {
        let p = model.parameter
        if p.unit == .boolean {
            Toggle("", isOn: Binding(get: { model.value >= 0.5 }, set: { model.value = $0 ? 1 : 0 })).labelsHidden()
            Spacer(minLength: 0)
        } else if let strings = p.valueStrings {
            Picker("", selection: Binding(get: { Int(model.value.rounded()) }, set: { model.value = Float($0) })) {
                ForEach(strings.indices, id: \.self) { Text(strings[$0]).tag($0) }
            }
            .labelsHidden().pickerStyle(.menu)
            Spacer(minLength: 0)
        } else if p.flags.contains(.flag_DisplayLogarithmic) {
            Slider(value: Binding(get: { log(max(model.value, p.minValue) / p.minValue) / log(p.maxValue / p.minValue) },
                                  set: { model.value = p.minValue * exp($0 * log(p.maxValue / p.minValue)) }), in: 0...1)
        } else {
            Slider(value: $model.value, in: p.minValue...p.maxValue)
        }
    }
}

// MARK: - Pitch bend & mod wheel

/// Spring-loaded pitch wheel and a mod wheel, sent as MIDI (bend, CC 1).
struct PerformanceWheelsView: View {
    let audioUnit: VekturModularAudioUnit
    @State private var bend: Double = 0     // −1 … 1
    @State private var mod: Double = 0      // 0 … 1

    var body: some View {
        HStack(spacing: 8) {
            wheel(title: "Bend", value: bend, bipolar: true)
                .gesture(DragGesture(minimumDistance: 0)
                    .onChanged { g in setBend(-Double(g.translation.height) / 120) }
                    .onEnded { _ in withAnimation(.spring(duration: 0.2)) { setBend(0) } })
                .accessibilityAdjustableAction { dir in setBend(dir == .increment ? min(bend + 0.25, 1) : max(bend - 0.25, -1)) }
            wheel(title: "Mod", value: mod, bipolar: false)
                .gesture(DragGesture(minimumDistance: 0).onChanged { g in
                    setMod(min(max(1 - Double(g.location.y) / 180, 0), 1))
                })
                .accessibilityAdjustableAction { dir in setMod(dir == .increment ? min(mod + 0.1, 1) : max(mod - 0.1, 0)) }
        }
    }

    private func wheel(title: String, value: Double, bipolar: Bool) -> some View {
        VStack(spacing: 4) {
            GeometryReader { g in
                ZStack {
                    RoundedRectangle(cornerRadius: 6).fill(Color.black.opacity(0.85))
                    let y = bipolar ? g.size.height * (0.5 - value * 0.45) : g.size.height * (0.95 - value * 0.9)
                    Rectangle().fill(Color.white).frame(height: 3).position(x: g.size.width / 2, y: y)
                }
            }
            .frame(height: 180)
            Text(title).font(.caption2)
        }
        .accessibilityElement()
        .accessibilityLabel(title == "Bend" ? "Pitch bend" : "Mod wheel")
        .accessibilityValue(String(format: "%.0f percent", value * 100))
    }

    private func setBend(_ v: Double) {
        bend = min(max(v, -1), 1)
        let raw = Int((bend * 8192).rounded()) + 8192
        let clamped = min(max(raw, 0), 16383)
        send([0xE0, UInt8(clamped & 0x7F), UInt8(clamped >> 7)])
    }

    private func setMod(_ v: Double) {
        mod = v
        send([0xB0, 1, UInt8((v * 127).rounded())])
    }

    private func send(_ bytes: [UInt8]) {
        guard let schedule = audioUnit.scheduleMIDIEventBlock else { return }
        bytes.withUnsafeBufferPointer { schedule(AUEventSampleTimeImmediate, 0, bytes.count, $0.baseAddress!) }
    }
}

// MARK: - Live readouts (Phase 4B)

/// Bipolar bar showing a slot's live output on the newest voice (~30 Hz).
private struct SlotMeter: View {
    let audioUnit: VekturModularAudioUnit
    let slot: Int

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1.0 / 30.0)) { _ in
            let v = Double(max(-1, min(1, audioUnit.modSlotValue(slot))))
            GeometryReader { g in
                ZStack(alignment: .leading) {
                    Capsule().fill(Color.secondary.opacity(0.15))
                    Rectangle().fill(Color.accentColor)
                        .frame(width: abs(v) * g.size.width / 2)
                        .offset(x: v >= 0 ? g.size.width / 2 : g.size.width / 2 * (1 + v))
                    Rectangle().fill(Color.secondary).frame(width: 1).offset(x: g.size.width / 2)
                }
            }
            .frame(width: 80, height: 8)
            .accessibilityElement()
            .accessibilityLabel("Slot \(slot + 1) live value")
            .accessibilityValue(String(format: "%.0f percent", v * 100))
        }
    }
}

/// Names the chord formed by the keys currently held.
private struct ChordReadout: View {
    let audioUnit: VekturModularAudioUnit

    var body: some View {
        TimelineView(.periodic(from: .now, by: 1.0 / 15.0)) { _ in
            let notes = audioUnit.heldNotes()
            let name = ChordAnalyzer.name(of: notes)
            HStack(spacing: 10) {
                Text("Chord").font(.caption).foregroundStyle(.secondary)
                Text(name.isEmpty ? (notes.isEmpty ? "—" : "?") : name)
                    .font(.title3.monospaced().weight(.semibold))
                Text(notes.map { Self.noteName($0) }.joined(separator: " "))
                    .font(.caption.monospaced()).foregroundStyle(.secondary).lineLimit(1)
            }
            .accessibilityElement(children: .combine)
            .accessibilityLabel("Chord \(name.isEmpty ? "none" : name)")
        }
    }

    static func noteName(_ n: UInt8) -> String {
        let names = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
        return "\(names[Int(n) % 12])\(Int(n) / 12 - 1)"
    }
}
