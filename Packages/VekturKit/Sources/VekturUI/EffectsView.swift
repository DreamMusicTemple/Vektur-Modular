// EffectsView.swift — Phase 6 page: eight effect slots on the insert chain
// and two send buses, bus routing and levels, latency readout, per-type
// controls (named and formatted by the effect catalogue), dynamics meters and
// user impulse responses. Every control is an AUParameter. Functional styling;
// the hardware panel look arrives in Phase 8.
import SwiftUI
import UniformTypeIdentifiers
@preconcurrency import AudioToolbox
import VekturAudioUnit
import VekturCore
import VekturDSP

public struct EffectsView: View {
    let audioUnit: VekturModularAudioUnit
    @State private var models: [UInt64: ParameterModel]

    public init(audioUnit: VekturModularAudioUnit) {
        self.audioUnit = audioUnit
        var m: [UInt64: ParameterModel] = [:]
        let tree = audioUnit.parameterTree
        for spec in ((1...8).map { "fx\($0)" } + ["fxBus"]).flatMap({ ParameterRegistry.specs(inGroup: $0) }) {
            if let p = tree?.parameter(withAddress: spec.address) { m[spec.address] = ParameterModel(p) }
        }
        _models = State(initialValue: m)
    }

    public var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                busSection
                ForEach(0..<FxSlotAddresses.slotCount, id: \.self) { s in
                    EffectSlotCard(slot: s, audioUnit: audioUnit, models: models)
                }
            }
            .padding(.vertical, 4)
        }
    }

    private var busSection: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Text("Effects").font(.headline)
                Spacer()
                TimelineView(.periodic(from: .now, by: 0.5)) { _ in
                    let samples = audioUnit.effectsLatencySamples
                    let sr = audioUnit.outputBusses[0].format.sampleRate
                    Text(samples == 0 ? "No added latency" : String(format: "Latency %d samples (%.1f ms), compensated", samples, Double(samples) / max(sr, 1) * 1000))
                        .font(.caption.monospacedDigit()).foregroundStyle(.secondary)
                }
            }
            grid(["enabled", "insertMode", "dry", "output", "sendA", "sendAPosition", "sendAMode", "returnA",
                  "sendB", "sendBPosition", "sendBMode", "returnB"].map { "fxBus.\($0)" })
            Text("Slots on the insert chain process in order (or side by side in Parallel, averaged). Sends feed their own chains, tapped before or after the inserts. Every path is delay-compensated, so parallel and send signals stay phase-aligned.")
                .font(.caption).foregroundStyle(.secondary)
        }
        .padding(12)
        .background(PanelPaint(key: "page").clipShape(RoundedRectangle(cornerRadius: 6)))   // Phase 8 panel paint
    }

    private func grid(_ ids: [String]) -> some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: 220), spacing: 10, alignment: .leading)], alignment: .leading, spacing: 8) {
            ForEach(ids, id: \.self) { id in
                if let spec = ParameterRegistry.spec(identifier: id), let m = models[spec.address] {
                    ParamControl(model: m, label: spec.displayName.replacingOccurrences(of: "Effects ", with: ""))
                }
            }
        }
    }
}

// MARK: - One slot

private struct EffectSlotCard: View {
    let slot: Int
    let audioUnit: VekturModularAudioUnit
    let models: [UInt64: ParameterModel]
    @State private var importing = false
    @State private var irMessage: String?

    private var a: FxSlotAddresses { FxSlotAddresses(slot) }
    private var typeID: Int { Int(models[a.type]?.value.rounded() ?? 0) }

    var body: some View {
        let type = FxCatalogue.type(typeID)
        VStack(alignment: .leading, spacing: 8) {
            HStack(spacing: 10) {
                Text("FX \(slot + 1)").font(.callout.weight(.semibold)).frame(width: 44, alignment: .leading)
                typeMenu(type)
                if typeID != 0 {
                    if let on = models[a.enabled] {
                        Toggle("On", isOn: Binding(get: { on.value >= 0.5 }, set: { on.value = $0 ? 1 : 0 })).labelsHidden()
                            .accessibilityLabel("FX \(slot + 1) on")
                    }
                    if let bus = models[a.bus] {
                        Picker("Bus", selection: Binding(get: { Int(bus.value.rounded()) }, set: { bus.value = Float($0) })) {
                            Text("Insert").tag(0); Text("Send A").tag(1); Text("Send B").tag(2)
                        }
                        .pickerStyle(.segmented).frame(maxWidth: 240)
                    }
                    if type.family == "Dynamics" { GainReductionMeter(audioUnit: audioUnit, slot: slot) }
                }
                Spacer()
            }
            if typeID != 0 {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 220), spacing: 10, alignment: .leading)], alignment: .leading, spacing: 8) {
                    if let m = models[a.mix] { ParamControl(model: m, label: "Mix") }
                    if let m = models[a.output] { ParamControl(model: m, label: "Output") }
                    ForEach(type.parameters) { p in
                        if let m = models[a.parameter(p.index)] { FxParamControl(model: m, descriptor: p, slot: slot, audioUnit: audioUnit) }
                    }
                }
                if typeID == 9 { irRow }
            }
        }
        .padding(12)
        .background(PanelPaint(key: "fx\(slot)").opacity(typeID == 0 ? 0.7 : 1).clipShape(RoundedRectangle(cornerRadius: 6)))
        .fileImporter(isPresented: $importing, allowedContentTypes: [.audio]) { result in
            switch result {
            case .success(let url):
                do { let r = try audioUnit.loadImpulseResponse(from: url, slot: slot); irMessage = "Loaded \(r.name) (\(String(format: "%.2f", Double(r.frames) / r.sampleRate)) s)" }
                catch { irMessage = "Could not load the file: \(error.localizedDescription)" }
            case .failure(let e): irMessage = e.localizedDescription
            }
        }
    }

    private func typeMenu(_ current: FxTypeDescriptor) -> some View {
        Menu {
            Button("Empty") { audioUnit.setEffectType(slot: slot, type: 0) }
            ForEach(FxCatalogue.families, id: \.name) { family in
                Section(family.name) {
                    ForEach(family.types) { t in Button(t.name) { audioUnit.setEffectType(slot: slot, type: t.id) } }
                }
            }
        } label: {
            Text(current.id == 0 ? "Empty — choose an effect" : current.name).frame(minWidth: 160, alignment: .leading)
        }
        .accessibilityLabel("FX \(slot + 1) effect type, \(current.name)")
    }

    @ViewBuilder private var irRow: some View {
        HStack {
            Button { importing = true } label: { Label("Load IR…", systemImage: "waveform.badge.plus") }
            if let ref = audioUnit.impulseResponses.first(where: { $0.slot == slot }) {
                Text(ref.name).font(.caption)
                if audioUnit.missingImpulseResponses.contains(ref) {
                    Text("file missing on this device — using the built-in IR").font(.caption).foregroundStyle(.orange)
                }
                Button(role: .destructive) { audioUnit.clearImpulseResponse(slot: slot) } label: { Image(systemName: "xmark.circle") }
                    .accessibilityLabel("Remove user impulse response")
            }
            if let msg = irMessage { Text(msg).font(.caption).foregroundStyle(.secondary) }
        }
        .buttonStyle(.bordered)
    }
}

// MARK: - A generic slot parameter, presented by the type's descriptor

private struct FxParamControl: View {
    @Bindable var model: ParameterModel
    let descriptor: FxParamDescriptor
    let slot: Int
    let audioUnit: VekturModularAudioUnit

    var body: some View {
        HStack(spacing: 6) {
            Text(descriptor.name).font(.caption).frame(width: 96, alignment: .leading).lineLimit(1)
            control
            Text(audioUnit.formatEffectParameter(slot: slot, index: descriptor.index, normalised: model.value))
                .font(.caption2.monospaced()).frame(width: 72, alignment: .trailing).lineLimit(1)
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(descriptor.name)
        .accessibilityValue(audioUnit.formatEffectParameter(slot: slot, index: descriptor.index, normalised: model.value))
    }

    @ViewBuilder private var control: some View {
        if descriptor.isToggle {
            Toggle("", isOn: Binding(get: { model.value >= 0.5 }, set: { model.value = $0 ? 1 : 0 })).labelsHidden()
            Spacer(minLength: 0)
        } else if descriptor.isChoice, descriptor.choices.count > 1 {
            let n = descriptor.choices.count - 1
            Picker("", selection: Binding(get: { Int((model.value * Float(n)).rounded()) }, set: { model.value = Float($0) / Float(n) })) {
                ForEach(descriptor.choices.indices, id: \.self) { Text(descriptor.choices[$0]).tag($0) }
            }
            .labelsHidden().pickerStyle(.menu)
            Spacer(minLength: 0)
        } else {
            Slider(value: $model.value, in: 0...1)
        }
    }
}

private struct GainReductionMeter: View {
    let audioUnit: VekturModularAudioUnit
    let slot: Int
    var body: some View {
        TimelineView(.periodic(from: .now, by: 1.0 / 20.0)) { _ in
            let gr = audioUnit.effectMeter(slot: slot)
            HStack(spacing: 4) {
                Text("GR").font(.caption2)
                GeometryReader { g in
                    ZStack(alignment: .trailing) {
                        Capsule().fill(Color.secondary.opacity(0.2))
                        Capsule().fill(Color.orange).frame(width: g.size.width * CGFloat(min(1, -gr / 24)))
                    }
                }
                .frame(width: 80, height: 6)
                Text(String(format: "%.1f dB", gr)).font(.caption2.monospacedDigit()).frame(width: 52, alignment: .trailing)
            }
            .accessibilityElement(children: .ignore)
            .accessibilityLabel("Gain reduction \(String(format: "%.1f", -gr)) decibels")
        }
    }
}
