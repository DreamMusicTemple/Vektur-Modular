// ParameterTreeBuilder.swift — builds the AUParameterTree from ParameterRegistry.
@preconcurrency import AudioToolbox
import VekturCore

enum ParameterTreeBuilder {

    static func makeTree() -> AUParameterTree {
        // One group per module ("global", "vco1", "vcfA", …). Group identifiers
        // are permanent too: some hosts show and store them.
        let groups = ParameterRegistry.groups.map { g in
            AUParameterTree.createGroup(withIdentifier: g.identifier, name: g.name,
                                        children: ParameterRegistry.specs(inGroup: g.identifier).map(makeParameter))
        }
        return AUParameterTree.createTree(withChildren: groups)
    }

    private static func makeParameter(_ spec: ParameterSpec) -> AUParameter {
        var flags: AudioUnitParameterOptions = [.flag_IsReadable, .flag_IsWritable]
        if spec.isAutomatable { flags.insert(.flag_CanRamp) } else { flags.insert(.flag_NonRealTime) }
        if spec.taper == .log { flags.insert(.flag_DisplayLogarithmic) }
        let p = AUParameterTree.createParameter(
            withIdentifier: spec.identifier,
            name: spec.displayName,
            address: spec.address,
            min: spec.minValue,
            max: spec.maxValue,
            unit: auUnit(spec.unit),
            unitName: nil,
            flags: flags,
            valueStrings: spec.valueStrings,
            dependentParameters: nil)
        p.value = spec.defaultValue
        return p
    }

    private static func auUnit(_ u: ParameterUnit) -> AudioUnitParameterUnit {
        switch u {
        case .generic: return .generic
        case .decibels: return .decibels
        case .cents: return .cents
        case .semitones: return .relativeSemiTones
        case .indexed: return .indexed
        case .hertz: return .hertz
        case .seconds: return .seconds
        case .milliseconds: return .milliseconds
        case .percent: return .percent
        case .boolean: return .boolean
        case .octaves: return .octaves
        case .degrees: return .degrees
        }
    }
}
