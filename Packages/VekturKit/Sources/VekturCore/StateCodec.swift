// StateCodec.swift — the only way state is turned into bytes and back.
//
// Decoding never trusts its input: size-limited, migrated, then sanitised.
// Callers get a typed error and are expected to fall back to their current
// (or default) state rather than crash or half-apply.
import Foundation

public enum StateCodecError: Error, Equatable {
    case empty
    case tooLarge(bytes: Int)
    case malformed(String)
    case migrationFailed(String)
}

public enum StateCodec {
    /// Hard ceiling for any imported state blob. Patches with large graphs and
    /// generative clips stay far below this; anything bigger is rejected.
    public static let maxBytes = 8 * 1024 * 1024

    /// Key under which the state blob lives inside AUAudioUnit.fullState.
    public static let fullStateKey = "com.dmtcymatics.vekturmodular.state"

    public static func encode(_ state: InstrumentState) throws -> Data {
        let enc = JSONEncoder()
        enc.outputFormatting = [.sortedKeys]
        enc.nonConformingFloatEncodingStrategy = .throw
        return try enc.encode(state.sanitized())
    }

    public struct Decoded: Sendable {
        public let state: InstrumentState
        public let originalSchemaVersion: Int
        public let isFromNewerVersion: Bool
    }

    public static func decode(_ data: Data, migrator: StateMigrator = StateMigrator()) throws -> Decoded {
        guard !data.isEmpty else { throw StateCodecError.empty }
        guard data.count <= maxBytes else { throw StateCodecError.tooLarge(bytes: data.count) }

        let raw: JSONValue
        do { raw = try JSONDecoder().decode(JSONValue.self, from: data) }
        catch { throw StateCodecError.malformed(String(describing: error)) }
        guard let object = raw.objectValue else { throw StateCodecError.malformed("top level is not an object") }

        let migrated: StateMigrator.Result
        do { migrated = try migrator.migrate(object) }
        catch { throw StateCodecError.migrationFailed(String(describing: error)) }

        let state: InstrumentState
        do {
            let bytes = try JSONEncoder().encode(JSONValue.object(migrated.object))
            state = try JSONDecoder().decode(InstrumentState.self, from: bytes)
        } catch { throw StateCodecError.malformed(String(describing: error)) }

        return Decoded(state: state.sanitized(),
                       originalSchemaVersion: migrated.originalVersion,
                       isFromNewerVersion: migrated.isFromNewerVersion)
    }
}
