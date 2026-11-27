// StateMigrator.swift — upgrades raw state JSON from any older schema to current.
//
// Migrations run on the untyped JSON object *before* typed decoding, so a step
// can rename keys, split sections or convert units without the old shape having
// to exist as a Swift type. Each step upgrades exactly one version.
import Foundation

public enum StateMigrationError: Error, Equatable {
    case notAnObject
    case missingStep(from: Int)
}

public struct StateMigrator: Sendable {
    public typealias Step = @Sendable ([String: JSONValue]) throws -> [String: JSONValue]

    /// steps[n] upgrades schema n → n+1.
    let steps: [Int: Step]
    let current: Int

    public init(current: Int = InstrumentState.currentSchemaVersion, steps: [Int: Step] = StateMigrator.shippedSteps) {
        self.current = current
        self.steps = steps
    }

    /// Every migration that has shipped. Never edit a shipped step; add a new one.
    public static let shippedSteps: [Int: Step] = [
        // 0 → 1: pre-release development builds wrote no schemaVersion and kept
        // parameters under "params". Normalise to the v1 layout.
        0: { obj in
            var o = obj
            if o["parameters"] == nil, let legacy = o["params"] { o["parameters"] = legacy }
            o.removeValue(forKey: "params")
            o["schemaVersion"] = .number(1)
            return o
        },
    ]

    public struct Result: Sendable {
        public let object: [String: JSONValue]
        public let originalVersion: Int
        /// True when the state came from a newer build: loaded best-effort,
        /// unknown content preserved, nothing discarded.
        public let isFromNewerVersion: Bool
    }

    public func migrate(_ object: [String: JSONValue]) throws -> Result {
        let original = object["schemaVersion"]?.numberValue.map { Int($0) } ?? 0
        if original > current {
            return Result(object: object, originalVersion: original, isFromNewerVersion: true)
        }
        var o = object
        var v = original
        while v < current {
            guard let step = steps[v] else { throw StateMigrationError.missingStep(from: v) }
            o = try step(o)
            v += 1
            o["schemaVersion"] = .number(Double(v))
        }
        return Result(object: o, originalVersion: original, isFromNewerVersion: false)
    }
}
