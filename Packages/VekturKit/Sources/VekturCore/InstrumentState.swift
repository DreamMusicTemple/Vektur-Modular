// InstrumentState.swift — the complete, versioned, serialisable instrument state.
//
// One type serves AUv3 fullState, user presets, factory presets and standalone
// project files, so there is exactly one format to migrate.
//
// Phase 1 carries the global parameters. Later phases add typed sections
// (patch graph, modulation matrix, arpeggiator, generative engine, effects,
// MIDI mappings, key zones, workspace) as optional properties that decode with
// `decodeIfPresent`, plus a schema bump and a migration step when a change is
// not purely additive. Sections this build does not understand are kept in
// `preservedSections` and written back unchanged.
import Foundation

public struct PresetReference: Codable, Sendable, Equatable {
    public enum Kind: String, Codable, Sendable { case factory, user, generated, none }
    public var kind: Kind
    public var name: String
    /// Factory: bank-local index. User: host/AU preset number (negative).
    public var number: Int?
    /// Generated presets: the seed that reproduces them.
    public var seed: UInt64?

    public init(kind: Kind, name: String, number: Int? = nil, seed: UInt64? = nil) {
        self.kind = kind; self.name = name; self.number = number; self.seed = seed
    }
}

public struct InstrumentState: Codable, Sendable, Equatable {
    /// Bump when the format changes; add a step to `StateMigrator`.
    public static let currentSchemaVersion = 1

    public var schemaVersion: Int
    /// Marketing version of the build that wrote this state (diagnostics only).
    public var writtenBy: String
    /// Parameter values keyed by stable identifier. Unknown identifiers are kept
    /// (a newer build may have written them) but never applied.
    public var parameters: [String: Double]
    public var preset: PresetReference?
    /// User patch cables (Phase 3). Absent in older saves → empty (normalled voice).
    public var patch: PatchState
    /// Captured note clips (Phase 5): generative phrases and recorded
    /// performances kept with the project. Absent in older saves → none.
    public var clips: [NoteClip]
    /// Effects state that is not a parameter (Phase 6): user IR references.
    public var effects: EffectsState
    /// MIDI Learn mappings saved with this preset/project (Phase 7). Global
    /// (device-wide) mappings are stored outside the state.
    public var midi: MIDIMappingSet
    /// Panel appearance — Wear amounts, tilt highlights (Phase 8).
    public var appearance: AppearanceState
    /// Sections this build does not model, preserved verbatim for round-tripping.
    public var preservedSections: [String: JSONValue]

    public init(schemaVersion: Int = InstrumentState.currentSchemaVersion,
                writtenBy: String = ProductInfo.marketingVersion,
                parameters: [String: Double] = ParameterRegistry.defaults,
                preset: PresetReference? = nil,
                patch: PatchState = .empty,
                clips: [NoteClip] = [],
                effects: EffectsState = EffectsState(),
                midi: MIDIMappingSet = MIDIMappingSet(),
                appearance: AppearanceState = AppearanceState(),
                preservedSections: [String: JSONValue] = [:]) {
        self.schemaVersion = schemaVersion
        self.writtenBy = writtenBy
        self.parameters = parameters
        self.preset = preset
        self.patch = patch
        self.clips = clips
        self.effects = effects
        self.midi = midi
        self.appearance = appearance
        self.preservedSections = preservedSections
    }

    public static var `default`: InstrumentState { InstrumentState() }

    // Known top-level keys; anything else lands in preservedSections.
    enum CodingKeys: String, CodingKey, CaseIterable {
        case schemaVersion, writtenBy, parameters, preset, patch, clips, effects, midi, appearance
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        schemaVersion = try c.decodeIfPresent(Int.self, forKey: .schemaVersion) ?? 0
        writtenBy = try c.decodeIfPresent(String.self, forKey: .writtenBy) ?? "unknown"
        parameters = try c.decodeIfPresent([String: Double].self, forKey: .parameters) ?? [:]
        preset = try? c.decodeIfPresent(PresetReference.self, forKey: .preset)
        // A malformed patch section must not make the whole state unloadable.
        patch = (try? c.decodeIfPresent(PatchState.self, forKey: .patch)) ?? .empty
        clips = (try? c.decodeIfPresent([NoteClip].self, forKey: .clips)) ?? []
        effects = (try? c.decodeIfPresent(EffectsState.self, forKey: .effects)) ?? EffectsState()
        midi = (try? c.decodeIfPresent(MIDIMappingSet.self, forKey: .midi)) ?? MIDIMappingSet()
        appearance = (try? c.decodeIfPresent(AppearanceState.self, forKey: .appearance)) ?? AppearanceState()

        let all = try decoder.singleValueContainer().decode([String: JSONValue].self)
        let known = Set(CodingKeys.allCases.map(\.rawValue))
        preservedSections = all.filter { !known.contains($0.key) }
    }

    public func encode(to encoder: Encoder) throws {
        // Preserved sections first, so typed keys always win on collision.
        var dyn = encoder.container(keyedBy: DynamicKey.self)
        for (k, v) in preservedSections { try dyn.encode(v, forKey: DynamicKey(k)) }
        var c = encoder.container(keyedBy: CodingKeys.self)
        try c.encode(schemaVersion, forKey: .schemaVersion)
        try c.encode(writtenBy, forKey: .writtenBy)
        try c.encode(parameters, forKey: .parameters)
        try c.encodeIfPresent(preset, forKey: .preset)
        if !patch.isDefault { try c.encode(patch, forKey: .patch) }
        if !clips.isEmpty { try c.encode(clips, forKey: .clips) }
        if !effects.isDefault { try c.encode(effects, forKey: .effects) }
        if !midi.isDefault { try c.encode(midi, forKey: .midi) }
        if !appearance.isDefault { try c.encode(appearance, forKey: .appearance) }
    }

    /// Returns a copy where every registered parameter is present, finite and in range.
    /// Unknown identifiers are retained for forward compatibility.
    public func sanitized() -> InstrumentState {
        var copy = self
        for spec in ParameterRegistry.all {
            let raw = parameters[spec.identifier] ?? Double(spec.defaultValue)
            copy.parameters[spec.identifier] = Double(spec.sanitize(raw))
        }
        copy.patch = patch.sanitized()
        copy.clips = Array(clips.prefix(NoteClip.maxClips)).map { $0.sanitized() }
        copy.effects = effects.sanitized()
        copy.midi = midi.sanitized()
        copy.appearance = appearance.sanitized()
        return copy
    }

    /// Value to apply for a registered parameter (always valid).
    public func value(for spec: ParameterSpec) -> Float {
        spec.sanitize(parameters[spec.identifier] ?? Double(spec.defaultValue))
    }
}

struct DynamicKey: CodingKey {
    var stringValue: String
    var intValue: Int? { nil }
    init(_ s: String) { stringValue = s }
    init?(stringValue: String) { self.stringValue = stringValue }
    init?(intValue: Int) { return nil }
}
