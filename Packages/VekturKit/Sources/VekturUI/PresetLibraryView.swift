// PresetLibraryView.swift — Phase 9 preset library: the two factory banks,
// user presets, favourites, tags and recents (SwiftData metadata), the preset
// creator, and preset / project / pattern import and export.
//
// Preset *content* lives where hosts expect it (AU user presets, factory JSON);
// SwiftData stores only metadata keyed by "factory:<number>" or "user:<name>",
// so deleting the metadata store never loses a sound.
import SwiftData
import SwiftUI
import UniformTypeIdentifiers
@preconcurrency import AudioToolbox
import VekturAudioUnit
import VekturCore

@Model
public final class PresetMeta {
    @Attribute(.unique) public var key: String
    public var favourite: Bool
    public var tags: [String]
    public var lastUsed: Date?
    public var useCount: Int
    public init(key: String) { self.key = key; favourite = false; tags = []; lastUsed = nil; useCount = 0 }
}

public enum PresetMetaStore {
    /// On-disk store; falls back to memory if the container cannot be opened
    /// (metadata is a convenience — the presets themselves are unaffected).
    @MainActor public static let container: ModelContainer = {
        if let c = try? ModelContainer(for: PresetMeta.self) { return c }
        return try! ModelContainer(for: PresetMeta.self, configurations: ModelConfiguration(isStoredInMemoryOnly: true))
    }()
}

struct PresetsScreen: View {
    let audioUnit: VekturModularAudioUnit
    var body: some View { PresetLibraryView(audioUnit: audioUnit).modelContainer(PresetMetaStore.container) }
}

struct ExchangeDocument: FileDocument {
    static var readableContentTypes: [UTType] { ExchangeKind.allCases.map(\.contentType) + [.json] }
    var data: Data
    init(data: Data) { self.data = data }
    init(configuration: ReadConfiguration) throws { data = configuration.file.regularFileContents ?? Data() }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: data) }
}

extension ExchangeKind {
    var contentType: UTType { UTType(exportedAs: uniformTypeIdentifier, conformingTo: .json) }
}

private struct Entry: Identifiable, Hashable {
    enum Source: Hashable { case factory(Int), user(AUAudioUnitPreset) }
    var source: Source
    var name: String
    var category: String
    var key: String
    var id: String { key }
}

struct PresetLibraryView: View {
    let audioUnit: VekturModularAudioUnit
    @Environment(\.modelContext) private var context
    @Query private var metas: [PresetMeta]
    @State private var user: [AUAudioUnitPreset] = []
    @State private var search = ""
    @State private var category = "All"
    @State private var favouritesOnly = false
    @State private var current: String?
    @State private var saveName = ""
    @State private var genCategory = GeneratedPresetCategory.lead
    @State private var seed: UInt32 = 7
    @State private var message: String?
    @State private var exportKind: ExchangeKind?
    @State private var exportDoc = ExchangeDocument(data: Data())
    @State private var importing = false
    @State private var tagging: Entry?
    @State private var tagText = ""

    private var metaByKey: [String: PresetMeta] { Dictionary(metas.map { ($0.key, $0) }, uniquingKeysWith: { a, _ in a }) }

    private var entries: [Entry] {
        let f = FactoryPresetLibrary.all.map { Entry(source: .factory($0.presetNumber), name: $0.name, category: $0.category, key: "factory:\($0.presetNumber)") }
        let u = user.map { Entry(source: .user($0), name: $0.name, category: "User", key: "user:\($0.name)") }
        return f + u
    }

    private var categories: [String] { ["All"] + Array(Set(FactoryPresetLibrary.all.map(\.category))).sorted() + ["User"] }

    private func visible(_ list: [Entry]) -> [Entry] {
        list.filter { e in
            (category == "All" || e.category == category)
                && (!favouritesOnly || metaByKey[e.key]?.favourite == true)
                && (search.isEmpty || e.name.localizedCaseInsensitiveContains(search) || e.category.localizedCaseInsensitiveContains(search)
                    || (metaByKey[e.key]?.tags.contains { $0.localizedCaseInsensitiveContains(search) } ?? false))
        }
    }

    var body: some View {
        Form {
            Section {
                Picker("Category", selection: $category) { ForEach(categories, id: \.self) { Text($0).tag($0) } }
                Toggle("Favourites only", isOn: $favouritesOnly)
            }
            let recent = metas.filter { $0.lastUsed != nil }.sorted { $0.lastUsed! > $1.lastUsed! }.prefix(6).compactMap { m in entries.first { $0.key == m.key } }
            if !recent.isEmpty && search.isEmpty && category == "All" && !favouritesOnly {
                Section("Recent") { ForEach(recent) { row($0) } }
            }
            ForEach(Array(FactoryPresetLibrary.banks.enumerated()), id: \.offset) { b, bank in
                let list = visible(entries.filter { if case .factory(let n) = $0.source { return n / 32 == b }; return false })
                if !list.isEmpty {
                    Section { ForEach(list) { row($0) } } header: { Text("Factory · \(bank)") } footer: {
                        if b == 0 { Text("Program Change bank MSB \(b) selects these (programs 1–32).") } else { Text("Bank MSB \(b).") }
                    }
                }
            }
            Section {
                let list = visible(entries.filter { if case .user = $0.source { return true }; return false })
                if user.isEmpty { Text("No user presets yet. They also appear in your host's preset menu.").foregroundStyle(.secondary) }
                ForEach(list) { row($0) }
                HStack {
                    TextField("New preset name", text: $saveName)
                    Button("Save") { save() }.disabled(saveName.trimmingCharacters(in: .whitespaces).isEmpty)
                }
            } header: { Text("User presets") } footer: { Text("Program Change bank MSB 2 selects user presets in name order.") }
            Section("Import & export") {
                ForEach(ExchangeKind.allCases, id: \.self) { k in
                    Button("Export \(k.displayName.lowercased())…") { export(k) }
                }
                Button("Import preset, project or pattern…") { importing = true }
            }
            Section("Preset creator") {
                Picker("Category", selection: $genCategory) { ForEach(GeneratedPresetCategory.allCases) { Text($0.displayName).tag($0) } }
                Stepper("Seed \(seed)", value: $seed, in: 1...99_999)
                Button("Generate") {
                    let r = audioUnit.generatePreset(category: genCategory, seed: seed)
                    current = nil
                    message = "Generated “\(r.name)” (audition peak \(String(format: "%.1f", r.auditionLevel)) dBFS)"
                }
            }
            if let message { Section { Text(message).accessibilityAddTraits(.updatesFrequently) } }
        }
        .searchable(text: $search, prompt: "Name, category or tag")
        .onAppear(perform: reload)
        .fileExporter(isPresented: Binding(get: { exportKind != nil }, set: { if !$0 { exportKind = nil } }), document: exportDoc,
                      contentType: exportKind?.contentType ?? .json, defaultFilename: exportName) { result in
            if case .failure(let e) = result { message = "Export failed: \(e.localizedDescription)" }
        }
        .fileImporter(isPresented: $importing, allowedContentTypes: ExchangeDocument.readableContentTypes) { result in importFile(result) }
        .alert("Tags", isPresented: Binding(get: { tagging != nil }, set: { if !$0 { tagging = nil } })) {
            TextField("comma, separated", text: $tagText)
            Button("Save") { if let t = tagging { meta(t.key).tags = tagText.split(separator: ",").map { $0.trimmingCharacters(in: .whitespaces) }.filter { !$0.isEmpty } } }
            Button("Cancel", role: .cancel) {}
        }
    }

    private func row(_ e: Entry) -> some View {
        let m = metaByKey[e.key]
        return HStack {
            Button { load(e) } label: {
                VStack(alignment: .leading, spacing: 2) {
                    Text(e.name).foregroundStyle(current == e.key ? PanelTheme.amber : .primary)
                    let tags = m?.tags ?? []
                    Text(([e.category] + tags).joined(separator: " · ")).font(.caption).foregroundStyle(.secondary)
                }
                .frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
            }
            .buttonStyle(.plain)
            Button { meta(e.key).favourite.toggle() } label: { Image(systemName: m?.favourite == true ? "star.fill" : "star") }
                .buttonStyle(.borderless).accessibilityLabel(m?.favourite == true ? "Remove \(e.name) from favourites" : "Add \(e.name) to favourites")
        }
        .accessibilityElement(children: .contain)
        .contextMenu {
            Button("Load") { load(e) }
            Button("Edit tags…") { tagText = (m?.tags ?? []).joined(separator: ", "); tagging = e }
            if case .user(let p) = e.source {
                Button("Delete", role: .destructive) { try? audioUnit.deleteUserPreset(p); reload() }
            }
        }
    }

    private func meta(_ key: String) -> PresetMeta {
        if let m = metaByKey[key] { return m }
        let m = PresetMeta(key: key); context.insert(m); return m
    }

    private func load(_ e: Entry) {
        switch e.source {
        case .factory(let n): audioUnit.loadFactoryPreset(number: n)
        case .user(let p): audioUnit.currentPreset = p
        }
        let m = meta(e.key); m.lastUsed = Date(); m.useCount += 1
        current = e.key
        message = "Loaded “\(e.name)”"
    }

    private func reload() { user = audioUnit.userPresets.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending } }

    private func save() {
        let p = AUAudioUnitPreset()
        p.name = saveName.trimmingCharacters(in: .whitespaces)
        p.number = -((audioUnit.userPresets.map { abs($0.number) }.max() ?? 0) + 1)
        do { try audioUnit.saveUserPreset(p); message = "Saved “\(p.name)”"; saveName = ""; current = "user:\(p.name)" }
        catch { message = "Could not save: \(error.localizedDescription)" }
        reload()
    }

    private var exportName: String {
        let base = audioUnit.currentPreset?.name ?? "Vektur"
        return "\(base) \(exportKind?.displayName ?? "")"
    }

    private func export(_ k: ExchangeKind) {
        do {
            exportDoc = ExchangeDocument(data: try ExchangeFile.make(k, name: audioUnit.currentPreset?.name ?? "Vektur", from: audioUnit.captureState()).encoded())
            exportKind = k
        } catch { message = "Export failed: \(error.localizedDescription)" }
    }

    private func importFile(_ result: Result<URL, Error>) {
        guard case .success(let url) = result else { return }
        let ok = url.startAccessingSecurityScopedResource()
        defer { if ok { url.stopAccessingSecurityScopedResource() } }
        guard let data = try? Data(contentsOf: url) else { message = "Could not read the file"; return }
        for k in ExchangeKind.allCases {
            if let f = try? ExchangeFile.decode(data, expecting: k) {
                audioUnit.apply(f.merged(into: audioUnit.captureState()))
                current = nil
                message = "Imported \(k.displayName.lowercased()) “\(f.name)”"
                return
            }
        }
        do { _ = try ExchangeFile.decode(data, expecting: .preset) } catch { message = error.localizedDescription }
    }
}
