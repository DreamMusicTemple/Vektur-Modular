// DiagnosticsConsoleView.swift — PHASE 1 bring-up console, shared by the
// standalone app and the AUv3 view. It exercises the real parameter tree, MIDI
// path and telemetry. The vintage hardware interface replaces it in Phase 8.
import SwiftUI
@preconcurrency import AudioToolbox
import VekturAudioUnit
import VekturCore
import VekturDSP

public struct DiagnosticsConsoleView: View {
    let audioUnit: VekturModularAudioUnit
    let engineStatus: String?
    @State private var telemetry: TelemetryModel
    @State private var params: [ParameterModel]
    @State private var showAbout = false
    @State private var expanded: Set<String> = ["patchbay", "global", "vco1", "vcfB", "eg2"]
    @State private var page = Page.canvas
    enum Page: String, CaseIterable { case canvas = "Patch canvas", modulation = "Modulation", sequencing = "Sequencing", effects = "Effects", midi = "MIDI", console = "Console" }

    private func expandedBinding(_ id: String) -> Binding<Bool> {
        Binding(get: { expanded.contains(id) },
                set: { if $0 { expanded.insert(id) } else { expanded.remove(id) } })
    }

    public init(audioUnit: VekturModularAudioUnit, engineStatus: String? = nil) {
        self.audioUnit = audioUnit
        self.engineStatus = engineStatus
        _telemetry = State(initialValue: TelemetryModel.shared(kernel: audioUnit.kernel,
                                                        sampleRate: { audioUnit.outputBusses[0].format.sampleRate }))
        let tree = audioUnit.parameterTree
        _params = State(initialValue: ParameterRegistry.all.compactMap { spec in
            tree?.parameter(withAddress: spec.address).map(ParameterModel.init)
        })
    }

    public var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            header
            meters
            Picker("View", selection: $page) { ForEach(Page.allCases, id: \.self) { Text($0.rawValue).tag($0) } }
                .pickerStyle(.segmented)
            if page == .canvas {
                PatchCanvasView(audioUnit: audioUnit)
                    .frame(minHeight: 560)
                TestKeyboardView(audioUnit: audioUnit).frame(height: 90)
            } else if page == .effects {
                EffectsView(audioUnit: audioUnit)
                    .frame(minHeight: 560)
                TestKeyboardView(audioUnit: audioUnit).frame(height: 90)
            } else if page == .midi {
                MIDIView(audioUnit: audioUnit)
                    .frame(minHeight: 560)
                TestKeyboardView(audioUnit: audioUnit).frame(height: 90)
            } else if page == .sequencing {
                SequencerView(audioUnit: audioUnit)
                    .frame(minHeight: 560)
                TestKeyboardView(audioUnit: audioUnit).frame(height: 90)
            } else if page == .modulation {
                ModulationView(audioUnit: audioUnit)
                    .frame(minHeight: 560)
                TestKeyboardView(audioUnit: audioUnit).frame(height: 90)
            } else {
                consolePage
            }
        }
        .padding()
        .onAppear { telemetry.start() }
        .onDisappear { telemetry.stop() }
        .sheet(isPresented: $showAbout) { AboutView() }
    }

    // Only the console page scrolls; the canvas owns its drag/pinch gestures.
    @ViewBuilder private var consolePage: some View {
        ScrollView { VStack(alignment: .leading, spacing: 14) {
            DisclosureGroup("Patch bay", isExpanded: expandedBinding("patchbay")) {
                PatchBayView(audioUnit: audioUnit)
            }
            .font(.subheadline.weight(.semibold))
            ForEach(Array(ParameterRegistry.groups.enumerated()), id: \.offset) { _, group in
                let rows = params.filter { $0.parameter.identifier.hasPrefix(group.identifier + ".") }
                DisclosureGroup(group.name, isExpanded: expandedBinding(group.identifier)) {
                    ForEach(rows, id: \.parameter.address) { ParameterRow(model: $0) }
                }
                .font(.subheadline.weight(.semibold))
            }
            TestKeyboardView(audioUnit: audioUnit)
                .frame(height: 110)
            HStack {
                Button("All Notes Off") { audioUnit.kernel.post(VK_CMD_ALL_NOTES_OFF) }
                Button("Panic", role: .destructive) { audioUnit.kernel.post(VK_CMD_PANIC) }
                Spacer()
                Button("About") { showAbout = true }
            }
            .buttonStyle(.bordered)
            Text("Development console: patch bay and every parameter of the modular engine. The vintage hardware panels replace this console in Phase 8.")
                .font(.caption).foregroundStyle(.secondary)
        } }
    }

    private var header: some View {
        HStack(alignment: .firstTextBaseline) {
            Text(ProductInfo.productName.uppercased()).font(.system(.title2, design: .monospaced).weight(.bold))
            Text(ProductInfo.tagline).font(.caption).foregroundStyle(.secondary)
            Spacer()
            if let engineStatus { Text(engineStatus).font(.caption.monospaced()) }
        }
    }

    private var meters: some View {
        VStack(alignment: .leading, spacing: 6) {
            MeterBar(label: "L", level: telemetry.peakL)
            MeterBar(label: "R", level: telemetry.peakR)
            HStack(spacing: 16) {
                Label("\(telemetry.activeVoices) voices", systemImage: "pianokeys")
                Label(String(format: "DSP %.1f%%", telemetry.dspLoad * 100), systemImage: "cpu")
                if telemetry.clipped { Label("SAFETY CLAMP", systemImage: "exclamationmark.triangle").foregroundStyle(.orange) }
                if telemetry.recoveredNonFinite { Label("NaN recovered", systemImage: "exclamationmark.octagon").foregroundStyle(.red) }
                if telemetry.eventsDropped { Label("events dropped", systemImage: "tray.full").foregroundStyle(.orange) }
            }
            .font(.caption.monospaced())
        }
    }
}

private struct ParameterRow: View {
    @Bindable var model: ParameterModel

    var body: some View {
        HStack {
            Text(shortName).font(.callout).frame(width: 150, alignment: .leading)
            control
                .accessibilityLabel(model.parameter.displayName)
                .accessibilityValue(model.displayString)
            Text(model.displayString).font(.caption.monospaced()).frame(width: 86, alignment: .trailing)
        }
    }

    // "VCO 1 Waveform" → "Waveform" inside the VCO 1 group.
    private var shortName: String {
        let n = model.parameter.displayName
        for prefix in ["VCO 1 ", "VCO 2 ", "VCO 3 ", "VCO 4 ", "VCO 5 ", "VCO 6 ", "VCO 7 ", "VCO 8 ", "VCO 9 ", "Filter A ", "Filter B ",
                       "EG 1 ", "EG 2 ", "VCA 2 ", "VCA ", "Noise ", "Ring Mod ", "S&H ", "OTA ", "Comb ", "Formant ", "Bank ", "VCBPF ",
                       "Mixer ", "Switch ", "Gate ", "Slew ", "Follower ", "VP ", "LFO 1 ", "LFO 2 ", "LFO 3 "]
            where n.hasPrefix(prefix) { return String(n.dropFirst(prefix.count)) }
        return n
    }

    @ViewBuilder private var control: some View {
        let p = model.parameter
        if p.unit == .boolean {
            Toggle("", isOn: Binding(get: { model.value >= 0.5 }, set: { model.value = $0 ? 1 : 0 })).labelsHidden()
            Spacer()
        } else if let strings = p.valueStrings {
            Picker("", selection: Binding(get: { Int(model.value.rounded()) }, set: { model.value = Float($0) })) {
                ForEach(strings.indices, id: \.self) { Text(strings[$0]).tag($0) }
            }
            .labelsHidden()
            .pickerStyle(.menu)
            Spacer()
        } else if p.flags.contains(.flag_DisplayLogarithmic), p.minValue > 0 {
            // Logarithmic taper: slider moves through log(value).
            let lo = log(Double(p.minValue)), hi = log(Double(p.maxValue))
            Slider(value: Binding(get: { (log(Double(model.value)) - lo) / (hi - lo) },
                                  set: { model.value = Float(exp(lo + $0 * (hi - lo))) }), in: 0...1)
        } else if p.unit == .relativeSemiTones {
            Slider(value: $model.value, in: p.minValue...p.maxValue, step: 1)
        } else {
            Slider(value: $model.value, in: p.minValue...p.maxValue)
        }
    }
}

private struct MeterBar: View {
    let label: String
    let level: Float
    var body: some View {
        HStack {
            Text(label).font(.caption.monospaced()).frame(width: 14)
            GeometryReader { g in
                let db = 20 * log10(max(Double(level), 1e-5))
                let norm = min(max((db + 60) / 66, 0), 1)   // -60…+6 dBFS
                ZStack(alignment: .leading) {
                    RoundedRectangle(cornerRadius: 2).fill(.quaternary)
                    RoundedRectangle(cornerRadius: 2).fill(db > 0 ? Color.red : Color.green)
                        .frame(width: g.size.width * norm)
                }
            }
            .frame(height: 8)
        }
        .accessibilityElement()
        .accessibilityLabel("\(label) output level")
    }
}
