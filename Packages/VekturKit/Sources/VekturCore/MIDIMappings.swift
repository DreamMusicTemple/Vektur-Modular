// MIDIMappings.swift — MIDI Learn mappings and hardware profiles (Phase 7).
//
// A mapping ties one MIDI source (CC, 14-bit CC, NRPN, pitch bend, channel
// pressure or a note) to one parameter, by the parameter's stable
// identifier (never its address, so mappings survive any future re-layout of
// the tree). Mappings live in two scopes:
//   • preset — saved in the instrument state (`midi` section) and recalled
//     with the preset or project;
//   • global — saved on the device (shared by the app and the AUv3), for a
//     controller you always use the same way.
// The kernel receives the merge of both (preset wins).
//
// A hardware profile is a named, portable set of mappings, exchanged as
// JSON (`.vekturmidi`). Unknown keys are ignored so older builds can read
// newer profiles.
import Foundation

public enum MIDISourceKind: String, Codable, Sendable, CaseIterable {
    case cc, cc14, nrpn, pitchBend, channelPressure, note

    /// VKMidiSourceKind
    public var engineCode: UInt8 {
        switch self {
        case .cc: return 0
        case .cc14: return 1
        case .nrpn: return 2
        case .pitchBend: return 3
        case .channelPressure: return 4
        case .note: return 5
        }
    }
    public init?(engineCode: UInt8) {
        guard let k = Self.allCases.first(where: { $0.engineCode == engineCode }) else { return nil }
        self = k
    }
    /// Valid source numbers.
    public var numberRange: ClosedRange<Int> {
        switch self {
        case .cc, .note: return 0...127
        case .cc14: return 0...31
        case .nrpn: return 0...16383
        case .pitchBend, .channelPressure: return 0...0
        }
    }
    public var displayName: String {
        switch self {
        case .cc: return "CC"
        case .cc14: return "CC (14-bit)"
        case .nrpn: return "NRPN"
        case .pitchBend: return "Pitch Bend"
        case .channelPressure: return "Pressure"
        case .note: return "Note"
        }
    }
}

public enum MIDIMapMode: String, Codable, Sendable, CaseIterable {
    case absolute, toggle, relative
    public var engineCode: UInt8 { switch self { case .absolute: return 0; case .toggle: return 1; case .relative: return 2 } }
}

/// Encodings used by endless encoders in relative mode.
public enum MIDIRelativeEncoding: String, Codable, Sendable, CaseIterable {
    case twosComplement, binaryOffset, signedBit
    public var engineCode: UInt8 { switch self { case .twosComplement: return 0; case .binaryOffset: return 1; case .signedBit: return 2 } }
    public var displayName: String {
        switch self {
        case .twosComplement: return "Two's complement (1 / 127)"
        case .binaryOffset: return "Binary offset (65 / 63)"
        case .signedBit: return "Signed bit (1 / 65)"
        }
    }
}

public enum MIDIMappingScope: String, Codable, Sendable, CaseIterable { case preset, global }

public struct MIDIMapping: Codable, Sendable, Hashable, Identifiable {
    public static let maxCount = 256
    /// Default relative step: 128 detents per full sweep.
    public static let defaultSensitivity = 1.0 / 128.0

    public var id: UUID
    public var source: MIDISourceKind
    /// 0…15, nil = any channel.
    public var channel: Int?
    public var number: Int
    /// Stable parameter identifier (e.g. "vcfB.cutoff").
    public var parameter: String
    /// Normalised range 0…1 along the parameter's taper. minimum > maximum inverts.
    public var minimum: Double
    public var maximum: Double
    public var mode: MIDIMapMode
    public var relative: MIDIRelativeEncoding
    /// Absolute mode: ignore the control until it reaches the parameter's value.
    public var pickup: Bool
    /// Relative mode: normalised change per encoder step.
    public var sensitivity: Double

    public init(id: UUID = UUID(), source: MIDISourceKind, channel: Int? = nil, number: Int, parameter: String,
                minimum: Double = 0, maximum: Double = 1, mode: MIDIMapMode = .absolute,
                relative: MIDIRelativeEncoding = .twosComplement, pickup: Bool = false,
                sensitivity: Double = MIDIMapping.defaultSensitivity) {
        self.id = id; self.source = source; self.channel = channel; self.number = number; self.parameter = parameter
        self.minimum = minimum; self.maximum = maximum; self.mode = mode; self.relative = relative
        self.pickup = pickup; self.sensitivity = sensitivity
    }

    enum CodingKeys: String, CodingKey { case id, source, channel, number, parameter, minimum, maximum, mode, relative, pickup, sensitivity }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = (try? c.decodeIfPresent(UUID.self, forKey: .id)) ?? UUID()
        source = try c.decode(MIDISourceKind.self, forKey: .source)
        channel = try? c.decodeIfPresent(Int.self, forKey: .channel)
        number = (try? c.decodeIfPresent(Int.self, forKey: .number)) ?? 0
        parameter = try c.decode(String.self, forKey: .parameter)
        minimum = (try? c.decodeIfPresent(Double.self, forKey: .minimum)) ?? 0
        maximum = (try? c.decodeIfPresent(Double.self, forKey: .maximum)) ?? 1
        mode = (try? c.decodeIfPresent(MIDIMapMode.self, forKey: .mode)) ?? .absolute
        relative = (try? c.decodeIfPresent(MIDIRelativeEncoding.self, forKey: .relative)) ?? .twosComplement
        pickup = (try? c.decodeIfPresent(Bool.self, forKey: .pickup)) ?? false
        sensitivity = (try? c.decodeIfPresent(Double.self, forKey: .sensitivity)) ?? Self.defaultSensitivity
    }

    /// The MIDI source, independent of target (used to merge scopes).
    public struct SourceKey: Hashable, Sendable { public var source: MIDISourceKind; public var channel: Int?; public var number: Int }
    public var sourceKey: SourceKey { SourceKey(source: source, channel: channel, number: number) }

    public var spec: ParameterSpec? { ParameterRegistry.spec(identifier: parameter) }

    /// Nil when the target or the source is invalid; otherwise a clamped copy.
    public func sanitized() -> MIDIMapping? {
        var m = self
        if source == .pitchBend || source == .channelPressure { m.number = 0 }
        guard spec != nil, source.numberRange.contains(m.number) else { return nil }
        if let ch = channel, !(0...15).contains(ch) { return nil }
        func unit(_ v: Double, _ d: Double) -> Double { v.isFinite ? min(max(v, 0), 1) : d }
        m.minimum = unit(minimum, 0); m.maximum = unit(maximum, 1)
        m.sensitivity = sensitivity.isFinite && sensitivity > 0 ? min(sensitivity, 1) : Self.defaultSensitivity
        return m
    }

    /// "CC 74 · Ch 1", "NRPN 3:9 · Any", "Note C1 · Ch 10".
    public var sourceDescription: String {
        let ch = channel.map { "Ch \($0 + 1)" } ?? "Any"
        switch source {
        case .cc: return "CC \(number) · \(ch)"
        case .cc14: return "CC \(number)/\(number + 32) · \(ch)"
        case .nrpn: return "NRPN \(number >> 7):\(number & 127) · \(ch)"
        case .pitchBend, .channelPressure: return "\(source.displayName) · \(ch)"
        case .note: return "Note \(Self.noteName(number)) · \(ch)"
        }
    }

    public static func noteName(_ n: Int) -> String {
        let names = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"]
        return "\(names[((n % 12) + 12) % 12])\(n / 12 - 1)"
    }
}

/// An ordered list of mappings (one scope).
public struct MIDIMappingSet: Codable, Sendable, Equatable {
    public var mappings: [MIDIMapping]

    public init(mappings: [MIDIMapping] = []) { self.mappings = mappings }

    public var isDefault: Bool { mappings.isEmpty }

    enum CodingKeys: String, CodingKey { case mappings }
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        // One malformed entry must not discard the rest.
        let raw = (try? c.decodeIfPresent([FailableMapping].self, forKey: .mappings)) ?? []
        mappings = raw.compactMap(\.value)
    }

    /// Valid entries only, at most `MIDIMapping.maxCount`, no exact duplicates.
    public func sanitized() -> MIDIMappingSet {
        var seen = Set<String>(), out: [MIDIMapping] = []
        for m in mappings {
            guard let s = m.sanitized() else { continue }
            let key = "\(s.source.rawValue)/\(s.channel ?? -1)/\(s.number)/\(s.parameter)"
            guard seen.insert(key).inserted else { continue }
            out.append(s)
            if out.count == MIDIMapping.maxCount { break }
        }
        return MIDIMappingSet(mappings: out)
    }

    /// Learn: the new mapping replaces whatever controlled the same parameter in this scope.
    public mutating func assign(_ m: MIDIMapping) {
        mappings.removeAll { $0.parameter == m.parameter }
        mappings.append(m)
        if mappings.count > MIDIMapping.maxCount { mappings.removeFirst(mappings.count - MIDIMapping.maxCount) }
    }

    public mutating func remove(id: UUID) { mappings.removeAll { $0.id == id } }
    public mutating func removeAll(forParameter p: String) { mappings.removeAll { $0.parameter == p } }

    /// What the engine receives: preset mappings, then global ones whose
    /// parameter and source the preset does not already use.
    public static func merged(preset: MIDIMappingSet, global: MIDIMappingSet) -> [MIDIMapping] {
        let p = preset.sanitized().mappings, g = global.sanitized().mappings
        let params = Set(p.map(\.parameter)), sources = Set(p.map(\.sourceKey))
        let rest = g.filter { !params.contains($0.parameter) && !sources.contains($0.sourceKey) }
        return Array((p + rest).prefix(MIDIMapping.maxCount))
    }
}

private struct FailableMapping: Decodable {
    let value: MIDIMapping?
    init(from decoder: Decoder) throws { value = try? MIDIMapping(from: decoder) }
}

/// A portable controller profile (JSON).
public struct MIDIHardwareProfile: Codable, Sendable, Equatable {
    public static let formatIdentifier = "com.dreammusictemple.vekturmodular.midi-profile"
    public static let currentVersion = 1
    public static let fileExtension = "vekturmidi"

    public var format: String
    public var version: Int
    public var name: String
    /// Optional controller name (e.g. the MIDI source's display name).
    public var device: String?
    public var mappings: [MIDIMapping]

    public init(name: String, device: String? = nil, mappings: [MIDIMapping]) {
        format = Self.formatIdentifier; version = Self.currentVersion
        self.name = name; self.device = device; self.mappings = mappings
    }

    /// `empty`: the file had mappings but none targets a parameter of this build.
    public enum ProfileError: Error, Equatable { case notAProfile, newerVersion(Int), empty }

    /// Decodes and validates a profile file. Invalid mappings are dropped.
    public static func decode(_ data: Data) throws -> MIDIHardwareProfile {
        struct Header: Decodable { var format: String?; var version: Int? }
        let h = try JSONDecoder().decode(Header.self, from: data)
        guard h.format == formatIdentifier else { throw ProfileError.notAProfile }
        let v = h.version ?? 1
        guard v <= currentVersion else { throw ProfileError.newerVersion(v) }
        let b = try JSONDecoder().decode(MIDIHardwareProfile.self, from: data)
        let maps = MIDIMappingSet(mappings: b.mappings).sanitized().mappings
        guard !maps.isEmpty || b.mappings.isEmpty else { throw ProfileError.empty }
        return MIDIHardwareProfile(name: String(b.name.prefix(64)), device: b.device.map { String($0.prefix(128)) }, mappings: maps)
    }

    public func encoded() throws -> Data {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        return try e.encode(self)
    }

    enum CodingKeys: String, CodingKey { case format, version, name, device, mappings }
    public func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(format, forKey: .format)
        try c.encode(version, forKey: .version)
        try c.encode(name, forKey: .name)
        try c.encodeIfPresent(device, forKey: .device)
        try c.encode(mappings, forKey: .mappings)
    }
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        format = try c.decode(String.self, forKey: .format)
        version = (try? c.decodeIfPresent(Int.self, forKey: .version)) ?? 1
        name = (try? c.decodeIfPresent(String.self, forKey: .name)) ?? "Imported"
        device = try? c.decodeIfPresent(String.self, forKey: .device)
        mappings = ((try? c.decodeIfPresent([FailableMapping].self, forKey: .mappings)) ?? []).compactMap(\.value)
    }

    /// The factory controller layout, using the General MIDI "sound controller"
    /// numbers most keyboards send from their knobs.
    public static let standard = MIDIHardwareProfile(name: "Standard (GM sound controllers)", mappings: [
        MIDIMapping(source: .cc, number: 74, parameter: "vcfB.cutoff"),       // brightness
        MIDIMapping(source: .cc, number: 71, parameter: "vcfB.resonance"),    // harmonic content
        MIDIMapping(source: .cc, number: 73, parameter: "eg2.attack"),        // attack time
        MIDIMapping(source: .cc, number: 75, parameter: "eg2.decay"),         // decay time
        MIDIMapping(source: .cc, number: 72, parameter: "eg2.release"),       // release time
        MIDIMapping(source: .cc, number: 76, parameter: "lfo1.rate"),         // vibrato rate
        MIDIMapping(source: .cc, number: 5, parameter: "global.glide"),       // portamento time
        MIDIMapping(source: .cc, number: 7, parameter: "global.masterVolume", pickup: true),   // volume
    ])
}
