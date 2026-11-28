// MIDIView.swift — Phase 7 page: MIDI settings (sync source, receive
// channel, velocity curve, program change, MIDI out and clock out, bend and
// MPE), the MIDI monitor, MIDI Learn (scope, mode, encoder type, pickup,
// range) and the mapping lists of both scopes, with hardware profiles
// (load the standard layout, import / export .vekturmidi JSON). Functional
// styling; the hardware panel arrives in Phase 8.
import SwiftUI
import UniformTypeIdentifiers
@preconcurrency import AudioToolbox
import VekturAudioUnit
import VekturCore
import VekturDSP

public struct MIDIView: View {
    let audioUnit: VekturModularAudioUnit
    @State private var models: [UInt64: ParameterModel]
    @State private var preset: MIDIMappingSet
    @State private var global: MIDIMappingSet
    @State private var target = "vcfB.cutoff"
    @State private var request = MIDILearnRequest(parameter: "vcfB.cutoff")
    @State private var learning = false
    @State private var message: String?
    @State private var importing = false
    @State private var importScope = MIDIMappingScope.global
    @State private var exporting = false
    @State private var exportDoc = ProfileDocument(data: Data())
    @State private var filter = ""

    private static let settingIDs = ["syncSource", "receiveChannel", "velocityCurve", "programChange", "midiOut", "midiOutChannel",
                                     "midiClockOut", "pitchBendRange", "bendDown", "mpeMode", "mpeMemberChannels", "mpePitchRange", "tempo"]
        .map { "global.\($0)" }

    public init(audioUnit: VekturModularAudioUnit) {
        self.audioUnit = audioUnit
        var m: [UInt64: ParameterModel] = [:]
        for id in Self.settingIDs {
            if let spec = ParameterRegistry.spec(identifier: id), let p = audioUnit.parameterTree?.parameter(withAddress: spec.address) { m[spec.address] = ParameterModel(p) }
        }
        _models = State(initialValue: m)
        _preset = State(initialValue: audioUnit.midiMappings(.preset))
        _global = State(initialValue: audioUnit.midiMappings(.global))
    }

    public var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 16) {
                settings
                monitor
                learnCard
                mappingList(.preset, preset)
                mappingList(.global, global)
                MIDIReferenceCard(audioUnit: audioUnit)
            }
            .padding(.vertical, 4)
        }
        .onReceive(NotificationCenter.default.publisher(for: VekturModularAudioUnit.midiEventNotification, object: audioUnit)) { note in
            guard let ev = note.userInfo?["event"] as? MIDIEvent else { return }
            switch ev {
            case .learned(let m, let scope):
                learning = false
                message = "\(m.sourceDescription) → \(ParameterRegistry.spec(identifier: m.parameter)?.displayName ?? m.parameter) (\(scope == .preset ? "this preset" : "all presets"))"
                reload()
            case .programChange(let program, let msb, _, let ch, let name):
                message = "Program \(program + 1) (bank \(msb)) on ch \(ch + 1)" + (name.map { " → \($0)" } ?? " — no preset at that number")
            }
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: [.json, ProfileDocument.type]) { result in
            switch result {
            case .success(let url):
                let scoped = url.startAccessingSecurityScopedResource()
                defer { if scoped { url.stopAccessingSecurityScopedResource() } }
                do {
                    let profile = try MIDIHardwareProfile.decode(try Data(contentsOf: url))
                    audioUnit.applyMIDIProfile(profile, scope: importScope, replace: false)
                    message = "Imported “\(profile.name)” (\(profile.mappings.count) mappings)"
                    reload()
                } catch MIDIHardwareProfile.ProfileError.newerVersion(let v) {
                    message = "That profile needs a newer version of Vektur Modular (format \(v))."
                } catch {
                    message = "Not a Vektur MIDI profile."
                }
            case .failure(let e): message = e.localizedDescription
            }
        }
        .fileExporter(isPresented: $exporting, document: exportDoc, contentType: ProfileDocument.type, defaultFilename: "Vektur MIDI Profile") { result in
            if case .failure(let e) = result { message = e.localizedDescription }
        }
    }

    private func reload() {
        preset = audioUnit.midiMappings(.preset)
        global = audioUnit.midiMappings(.global)
    }

    // MARK: Settings & monitor

    private var settings: some View {
        card {
            Text("MIDI").font(.headline)
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 220), spacing: 10, alignment: .leading)], alignment: .leading, spacing: 8) {
                ForEach(Self.settingIDs, id: \.self) { id in
                    if let spec = ParameterRegistry.spec(identifier: id), let m = models[spec.address] {
                        ParamControl(model: m, label: spec.displayName.replacingOccurrences(of: "MIDI ", with: ""))
                    }
                }
            }
            Text("Sync Auto follows the host's transport when there is one, else incoming MIDI clock, else the Tempo control. Bend range, fine tune and MPE zones also follow RPN 0, RPN 1 and the MPE Configuration Message from your controller.")
                .font(.caption).foregroundStyle(.secondary)
        }
    }

    private var monitor: some View {
        card {
            TimelineView(.periodic(from: .now, by: 0.25)) { _ in
                let s = audioUnit.midiStatus()
                VStack(alignment: .leading, spacing: 4) {
                    HStack {
                        Text("Monitor").font(.subheadline.weight(.semibold))
                        Spacer()
                        Text(syncText(s)).font(.caption.monospacedDigit())
                    }
                    Text("Last: \(s.lastMessageDescription)").font(.caption.monospaced())
                    Text("In \(s.messagesIn) · Out \(s.messagesOut)").font(.caption2.monospacedDigit()).foregroundStyle(.secondary)
                }
            }
        }
    }

    private func syncText(_ s: MIDIEngineStatus) -> String {
        switch s.syncInEffect {
        case .host: return "Synced to host"
        case .midiClock: return String(format: "MIDI clock %.1f BPM%@", s.clockTempo ?? 0, s.clockRunning ? " ▶︎" : " ■")
        case .internal: return s.clockActive ? "Internal (clock ignored)" : "Internal tempo"
        }
    }

    // MARK: Learn

    private var learnCard: some View {
        card {
            Text("MIDI Learn").font(.subheadline.weight(.semibold))
            HStack {
                TextField("Filter parameters", text: $filter).textFieldStyle(.roundedBorder).frame(maxWidth: 180)
                Picker("Parameter", selection: $target) {
                    ForEach(filteredParameters, id: \.identifier) { Text("\(groupName($0.group)) · \($0.displayName)").tag($0.identifier) }
                }
                .pickerStyle(.menu)
            }
            HStack(spacing: 14) {
                Picker("Scope", selection: $request.scope) {
                    Text("This preset").tag(MIDIMappingScope.preset)
                    Text("All presets").tag(MIDIMappingScope.global)
                }.pickerStyle(.segmented).frame(maxWidth: 260)
                Picker("Mode", selection: $request.mode) {
                    Text("Absolute").tag(MIDIMapMode.absolute)
                    Text("Toggle").tag(MIDIMapMode.toggle)
                    Text("Relative").tag(MIDIMapMode.relative)
                }.pickerStyle(.menu)
                if request.mode == .relative {
                    Picker("Encoder", selection: $request.relative) {
                        ForEach(MIDIRelativeEncoding.allCases, id: \.self) { Text($0.displayName).tag($0) }
                    }.pickerStyle(.menu)
                }
                if request.mode == .absolute { Toggle("Pickup", isOn: $request.pickup).fixedSize() }
                Toggle("Any channel", isOn: $request.anyChannel).fixedSize()
            }
            HStack {
                Text("Range").font(.caption)
                Slider(value: $request.minimum, in: 0...1)
                Slider(value: $request.maximum, in: 0...1)
                Text(String(format: "%.0f–%.0f %%%@", request.minimum * 100, request.maximum * 100, request.minimum > request.maximum ? " (inverted)" : ""))
                    .font(.caption2.monospacedDigit()).frame(width: 110, alignment: .trailing)
            }
            HStack {
                Button(learning ? "Cancel" : "Learn") {
                    if learning { audioUnit.cancelMIDILearn(); learning = false; message = nil }
                    else {
                        request.parameter = target
                        audioUnit.beginMIDILearn(request)
                        learning = true
                        message = "Move a control on your MIDI device…"
                    }
                }
                .buttonStyle(.borderedProminent)
                Button("Clear this parameter") { audioUnit.clearMIDIMappings(forParameter: target); reload() }
                Spacer()
                if let message { Text(message).font(.caption).foregroundStyle(learning ? Color.orange : Color.secondary).lineLimit(2) }
            }
            HStack {
                Button("Load standard layout") { audioUnit.applyMIDIProfile(.standard, scope: .global, replace: false); reload(); message = "Standard CC layout added to all presets" }
                Menu("Import profile…") {
                    Button("Into all presets") { importScope = .global; importing = true }
                    Button("Into this preset") { importScope = .preset; importing = true }
                }
                Menu("Export profile…") {
                    Button("All-preset mappings") { export(.global) }
                    Button("This preset's mappings") { export(.preset) }
                }
            }
            .buttonStyle(.bordered)
        }
    }

    private func export(_ scope: MIDIMappingScope) {
        let profile = audioUnit.exportMIDIProfile(scope: scope, name: scope == .global ? "My controller" : "Preset mappings")
        if let data = try? profile.encoded() { exportDoc = ProfileDocument(data: data); exporting = true }
    }

    private var filteredParameters: [ParameterSpec] {
        let f = filter.trimmingCharacters(in: .whitespaces).lowercased()
        let all = ParameterRegistry.all.filter { $0.isAutomatable || $0.group == "global" }
        let list = f.isEmpty ? all : all.filter { $0.displayName.lowercased().contains(f) || $0.identifier.lowercased().contains(f) }
        // Keep the current choice selectable even when filtered out.
        if !list.contains(where: { $0.identifier == target }), let cur = ParameterRegistry.spec(identifier: target) { return [cur] + list }
        return list
    }

    private func groupName(_ g: String) -> String { ParameterRegistry.groups.first { $0.identifier == g }?.name ?? g }

    // MARK: Lists

    private func mappingList(_ scope: MIDIMappingScope, _ set: MIDIMappingSet) -> some View {
        card {
            HStack {
                Text(scope == .preset ? "This preset (\(set.mappings.count))" : "All presets (\(set.mappings.count))").font(.subheadline.weight(.semibold))
                Spacer()
                if !set.mappings.isEmpty {
                    Button("Remove all", role: .destructive) { audioUnit.setMIDIMappings(MIDIMappingSet(), scope: scope); reload() }
                        .buttonStyle(.borderless).font(.caption)
                }
            }
            if set.mappings.isEmpty {
                Text(scope == .preset ? "Mappings saved with the preset or project. They override the all-preset mappings for the same control or parameter."
                                      : "Mappings kept on this device for every preset, in the app and in every host.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            ForEach(set.mappings) { m in
                HStack {
                    Text(m.sourceDescription).font(.caption.monospaced()).frame(width: 150, alignment: .leading)
                    Image(systemName: "arrow.right").font(.caption2)
                    Text(ParameterRegistry.spec(identifier: m.parameter)?.displayName ?? m.parameter).font(.caption)
                    Spacer()
                    Text(detail(m)).font(.caption2).foregroundStyle(.secondary)
                    Button { var s = set; s.remove(id: m.id); audioUnit.setMIDIMappings(s, scope: scope); reload() } label: { Image(systemName: "xmark.circle") }
                        .buttonStyle(.borderless).accessibilityLabel("Remove mapping")
                }
            }
        }
    }

    private func detail(_ m: MIDIMapping) -> String {
        var parts: [String] = [m.mode.rawValue]
        if m.mode == .relative { parts.append(m.relative.rawValue) }
        if m.pickup { parts.append("pickup") }
        if m.minimum != 0 || m.maximum != 1 { parts.append(String(format: "%.0f–%.0f %%", m.minimum * 100, m.maximum * 100)) }
        return parts.joined(separator: " · ")
    }

    private func card<C: View>(@ViewBuilder _ content: () -> C) -> some View {
        VStack(alignment: .leading, spacing: 8, content: content)
            .padding(12)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(PanelPaint(key: "page").clipShape(RoundedRectangle(cornerRadius: 6)))   // Phase 8 panel paint
    }
}

/// .vekturmidi profile document (JSON).
struct ProfileDocument: FileDocument {
    static let type = UTType(exportedAs: "com.dmtcymatics.vekturmodular.midi-profile", conformingTo: .json)
    static var readableContentTypes: [UTType] { [type, .json] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}
