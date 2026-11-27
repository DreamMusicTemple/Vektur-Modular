// MIDIReferenceView.swift — Phase 9 MIDI CC & automation reference: every
// automatable parameter, its stable address and range, and the controllers
// currently mapped to it. Exports CSV or Markdown.
import SwiftUI
import UniformTypeIdentifiers
import VekturAudioUnit
import VekturCore

struct MIDIReferenceCard: View {
    let audioUnit: VekturModularAudioUnit
    @State private var open = false
    var body: some View {
        HStack {
            VStack(alignment: .leading) {
                Text("MIDI & automation reference").font(.headline)
                Text("\(ParameterRegistry.all.filter(\.isAutomatable).count) automatable parameters with addresses and current mappings")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            Button("Open") { open = true }.buttonStyle(.bordered)
        }
        .padding(12)
        .background(PanelPaint(key: "page").clipShape(RoundedRectangle(cornerRadius: 6)))
        .sheet(isPresented: $open) { NavigationStack { MIDIReferenceView(audioUnit: audioUnit) } }
    }
}

struct TextExportDocument: FileDocument {
    static var readableContentTypes: [UTType] { [.commaSeparatedText, .plainText] }
    var text: String
    init(text: String) { self.text = text }
    init(configuration: ReadConfiguration) throws { text = String(decoding: configuration.file.regularFileContents ?? Data(), as: UTF8.self) }
    func fileWrapper(configuration: WriteConfiguration) throws -> FileWrapper { FileWrapper(regularFileWithContents: Data(text.utf8)) }
}

struct MIDIReferenceView: View {
    let audioUnit: VekturModularAudioUnit
    @State private var query = ""
    @State private var mappedOnly = false
    @State private var exportType: UTType?
    @State private var doc = TextExportDocument(text: "")
    @Environment(\.dismiss) private var dismiss

    private var rows: [MIDIReferenceRow] { MIDIReference.rows(mappings: audioUnit.effectiveMIDIMappings) }
    private var shown: [MIDIReferenceRow] {
        rows.filter { r in (!mappedOnly || !r.assignments.isEmpty)
            && (query.isEmpty || r.name.localizedCaseInsensitiveContains(query) || r.identifier.localizedCaseInsensitiveContains(query)
                || r.assignments.contains { $0.localizedCaseInsensitiveContains(query) }) }
    }

    var body: some View {
        List {
            Toggle("Mapped only", isOn: $mappedOnly)
            ForEach(shown) { r in
                VStack(alignment: .leading, spacing: 2) {
                    HStack { Text(r.name); Spacer(); Text("#\(r.address)").font(.caption.monospacedDigit()).foregroundStyle(.secondary) }
                    Text("\(r.identifier) · \(r.range)").font(.caption.monospaced()).foregroundStyle(.secondary)
                    if !r.assignments.isEmpty { Text(r.assignments.joined(separator: ", ")).font(.caption).foregroundStyle(PanelTheme.amber) }
                }
                .accessibilityElement(children: .combine)
            }
        }
        .searchable(text: $query, prompt: "Parameter, identifier or CC")
        .navigationTitle("MIDI reference")
        .toolbar {
            ToolbarItem(placement: .cancellationAction) { Button("Done") { dismiss() } }
            ToolbarItem {
                Menu { Button("CSV (spreadsheet)") { export(.commaSeparatedText) }; Button("Markdown (print)") { export(.plainText) } }
                    label: { Label("Export", systemImage: "square.and.arrow.up") }
            }
        }
        .fileExporter(isPresented: Binding(get: { exportType != nil }, set: { if !$0 { exportType = nil } }), document: doc,
                      contentType: exportType ?? .plainText, defaultFilename: exportType == .commaSeparatedText ? "Vektur MIDI reference.csv" : "Vektur MIDI reference.md") { _ in }
    }

    private func export(_ t: UTType) {
        doc = TextExportDocument(text: t == .commaSeparatedText ? MIDIReference.csv(rows) : MIDIReference.markdown(rows))
        exportType = t
    }
}
