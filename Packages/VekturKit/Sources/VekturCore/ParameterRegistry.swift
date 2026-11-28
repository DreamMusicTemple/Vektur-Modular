// ParameterRegistry.swift — the one list of every host-visible parameter.
//
// The list itself is GENERATED from tools/params.py into
// ParameterRegistry+Generated.swift, together with the C address constants
// (VekturParams.h) and the C++ range table (ParamTable.hpp), so Swift and the
// DSP kernel can never disagree. Do not hand-edit generated files.
//
// Rules (see docs/ARCHITECTURE.md §6):
//  • Identifiers and addresses are permanent once shipped. Retired parameters
//    stay listed with isAutomatable = false rather than being deleted.
//  • Only parameters that are actually connected to DSP are listed.
//  • ParameterRegistryStabilityTests pins every shipped (identifier, address).
import Foundation

public enum ParameterRegistry {

    public static let all: [ParameterSpec] = generated

    /// Parameter-tree groups (identifier, display name) in display order.
    public static let groups: [(identifier: String, name: String)] = generatedGroups.map { ($0.0, $0.1) }

    /// Voice counts selectable by the polyphony parameter (mirrors kPolyphonyTable in Kernel.hpp).
    public static let polyphonyChoices: [Int] = [1, 2, 4, 6, 8, 12, 16, 24, 32]

    /// Frequently referenced identifiers.
    public enum ID {
        public static let masterVolume = "global.masterVolume"
        public static let masterTune = "global.masterTune"
        public static let polyphony = "global.polyphony"
        public static let pitchBendRange = "global.pitchBendRange"
        public static let engineMode = "global.engineMode"
        public static let instability = "global.instability"
    }

    private static let byIdentifier: [String: ParameterSpec] =
        Dictionary(uniqueKeysWithValues: all.map { ($0.identifier, $0) })
    private static let byAddress: [UInt64: ParameterSpec] =
        Dictionary(uniqueKeysWithValues: all.map { ($0.address, $0) })

    public static func spec(identifier: String) -> ParameterSpec? { byIdentifier[identifier] }
    public static func spec(address: UInt64) -> ParameterSpec? { byAddress[address] }
    public static func specs(inGroup group: String) -> [ParameterSpec] { all.filter { $0.group == group } }

    /// Default value for every registered parameter, keyed by identifier.
    public static var defaults: [String: Double] {
        Dictionary(uniqueKeysWithValues: all.map { ($0.identifier, Double($0.defaultValue)) })
    }
}
