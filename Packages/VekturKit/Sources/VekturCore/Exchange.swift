// Exchange.swift — Phase 9 import/export formats. All are JSON (UTF-8) with a
// "format" tag and the schema version, so a later build can migrate them.
//   .vekturpreset   one sound: parameters (setup excluded), patch, preset MIDI map
//   .vekturproject  everything: the complete InstrumentState incl. clips, appearance
//   .vekturpattern  the step sequencer and arpeggiator settings only
// MIDI clips export as Standard MIDI Files (MIDIFileWriter, VekturAudioUnit).
import Foundation

public enum ExchangeKind: String, Codable, Sendable, CaseIterable {
    case preset = "vektur.preset", project = "vektur.project", pattern = "vektur.pattern"
    public var fileExtension: String {
        switch self { case .preset: return "vekturpreset"; case .project: return "vekturproject"; case .pattern: return "vekturpattern" }
    }
    public var uniformTypeIdentifier: String { "com.dmtcymatics.vekturmodular.\(fileExtension.dropFirst(6))" }
    public var displayName: String { switch self { case .preset: return "Preset"; case .project: return "Project"; case .pattern: return "Pattern" } }
}

public enum ExchangeError: Error, Equatable, LocalizedError {
    case notVektur, wrongKind(expected: ExchangeKind, found: String), newerSchema(Int)
    public var errorDescription: String? {
        switch self {
        case .notVektur: return "This file is not a Vektur Modular file."
        case .wrongKind(let e, let f): return "Expected a \(e.displayName.lowercased()) file, found “\(f)”."
        case .newerSchema(let v): return "This file was written by a newer version (schema \(v)). Update the app to open it."
        }
    }
}

public struct ExchangeFile: Codable, Sendable, Equatable {
    public var format: String
    public var schemaVersion: Int
    public var writtenBy: String
    public var name: String
    public var state: InstrumentState

    /// Parameter groups that make up a pattern.
    public static let patternPrefixes = ["seq.", "arp."]

    public static func make(_ kind: ExchangeKind, name: String, from s: InstrumentState) -> ExchangeFile {
        var out = s
        switch kind {
        case .project: break
        case .preset:
            out.parameters = s.parameters.filter { !FactoryPresetLibrary.isSetupParameter($0.key) }
            out.clips = []
            out.appearance = AppearanceState()
            out.preset = PresetReference(kind: .user, name: name)
        case .pattern:
            out = InstrumentState()
            out.parameters = s.parameters.filter { k, _ in patternPrefixes.contains { k.hasPrefix($0) } }
        }
        return ExchangeFile(format: kind.rawValue, schemaVersion: InstrumentState.currentSchemaVersion,
                            writtenBy: ProductInfo.marketingVersion, name: name, state: out)
    }

    public func encoded() throws -> Data {
        let e = JSONEncoder(); e.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try e.encode(self)
    }

    public static func decode(_ data: Data, expecting kind: ExchangeKind) throws -> ExchangeFile {
        struct Head: Decodable { var format: String?; var schemaVersion: Int? }
        guard let head = try? JSONDecoder().decode(Head.self, from: data), let f = head.format, f.hasPrefix("vektur.") else { throw ExchangeError.notVektur }
        guard f == kind.rawValue else { throw ExchangeError.wrongKind(expected: kind, found: f) }
        if let v = head.schemaVersion, v > InstrumentState.currentSchemaVersion { throw ExchangeError.newerSchema(v) }
        return try JSONDecoder().decode(ExchangeFile.self, from: data)
    }

    /// The state to apply on import, merged into the current one so a preset
    /// keeps the player's setup and a pattern changes only the sequencer.
    public func merged(into current: InstrumentState) -> InstrumentState {
        switch ExchangeKind(rawValue: format) {
        case .project?: return state.sanitized()
        case .pattern?:
            var s = current
            for k in s.parameters.keys where Self.patternPrefixes.contains(where: { k.hasPrefix($0) }) { s.parameters[k] = nil }
            for (k, v) in state.parameters where Self.patternPrefixes.contains(where: { k.hasPrefix($0) }) { s.parameters[k] = v }
            return s.sanitized()
        default:
            var s = state
            for (k, v) in current.parameters where FactoryPresetLibrary.isSetupParameter(k) { s.parameters[k] = v }
            s.clips = current.clips
            s.appearance = current.appearance
            s.patch.layout = current.patch.layout
            return s.sanitized()
        }
    }
}

// MARK: - MIDI CC reference

/// Every automatable parameter with its address and any MIDI assignment —
/// the "MIDI CC reference" screen and its CSV / Markdown export.
public struct MIDIReferenceRow: Sendable, Identifiable, Hashable {
    public var identifier: String
    public var name: String
    public var group: String
    public var address: UInt64
    public var range: String
    public var assignments: [String]
    public var id: String { identifier }
}

public enum MIDIReference {
    public static func rows(mappings: [MIDIMapping]) -> [MIDIReferenceRow] {
        let byParam = Dictionary(grouping: mappings, by: \.parameter)
        return ParameterRegistry.all.filter(\.isAutomatable).map { s in
            let a = (byParam[s.identifier] ?? []).map { m -> String in
                let ch = m.channel.map { "ch \($0 + 1)" } ?? "any ch"
                return "\(m.source.displayName) \(m.number) (\(ch))"
            }
            return MIDIReferenceRow(identifier: s.identifier, name: s.displayName, group: s.group, address: s.address,
                                    range: "\(format(s.minValue))…\(format(s.maxValue))\(unitSymbol(s.unit))",
                                    assignments: a)
        }
    }

    public static func csv(_ rows: [MIDIReferenceRow]) -> String {
        func q(_ s: String) -> String { "\"" + s.replacingOccurrences(of: "\"", with: "\"\"") + "\"" }
        var out = "identifier,name,group,address,range,midi\n"
        for r in rows { out += [r.identifier, r.name, r.group, String(r.address), r.range, r.assignments.joined(separator: "; ")].map(q).joined(separator: ",") + "\n" }
        return out
    }

    public static func markdown(_ rows: [MIDIReferenceRow]) -> String {
        var out = "# Vektur Modular — MIDI & automation reference\n\n\(rows.count) automatable parameters. Addresses are stable across versions.\n"
        for (g, list) in Dictionary(grouping: rows, by: \.group).sorted(by: { $0.key < $1.key }) {
            out += "\n## \(g)\n\n| Parameter | Identifier | Address | Range | MIDI |\n|---|---|---:|---|---|\n"
            for r in list { out += "| \(r.name) | `\(r.identifier)` | \(r.address) | \(r.range) | \(r.assignments.isEmpty ? "—" : r.assignments.joined(separator: "<br>")) |\n" }
        }
        return out
    }

    private static func unitSymbol(_ u: ParameterUnit) -> String {
        switch u {
        case .decibels: return " dB"; case .cents: return " ct"; case .semitones: return " st"; case .hertz: return " Hz"
        case .seconds: return " s"; case .milliseconds: return " ms"; case .percent: return " %"; case .octaves: return " oct"; case .degrees: return "°"
        default: return ""
        }
    }

    private static func format(_ v: Float) -> String { v == v.rounded() ? String(Int(v)) : String(format: "%.2f", v) }
}
