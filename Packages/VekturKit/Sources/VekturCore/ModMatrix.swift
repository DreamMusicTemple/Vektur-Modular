// ModMatrix.swift — Swift view of the 16-slot modulation matrix (Phase 4A).
//
// The matrix lives entirely in ordinary parameters (module 60), so it is
// saved, restored, automated (amount/offset) and preset-recalled like every
// other control. This file only names the fields and the permanent source and
// destination encodings shared with the engine (modules/ModMatrix.hpp).
import Foundation

/// Modulation sources. Raw values are PERMANENT (stored in saved state).
public enum ModSource: Int, CaseIterable, Sendable, Identifiable {
    case none = 0, lfo1, lfo2, lfo3, eg1, eg2, velocity, keyTrack
    case modWheel, aftertouch, polyAftertouch, pitchBend, breath, expression
    case macro1, macro2, macro3, macro4, randomPerNote, voiceSpread, constant
    case mpeTimbre, keyZone   // Phase 4B (appended)
    case seqRowA, seqRowB, seqRowC   // Phase 5 (appended): analogue sequencer rows, stepped, unipolar

    public var id: Int { rawValue }

    public var displayName: String {
        // Matches tools/params.py MOD_SOURCES (the parameter's value strings).
        ParameterRegistry.spec(identifier: "matrix.slot1Source")?.valueStrings?[rawValue] ?? "\(rawValue)"
    }

    /// Natural range: bipolar (−1…1) or unipolar (0…1).
    public var isBipolar: Bool {
        switch self {
        case .lfo1, .lfo2, .lfo3, .keyTrack, .pitchBend, .randomPerNote, .voiceSpread: return true
        default: return false
        }
    }
}

/// Where a slot sends its value. Encoded in the slot's `Dest` parameter:
/// 0 = none, 1…16383 = parameter address, 16384+ = virtual destinations.
public enum ModDestination: Hashable, Sendable {
    case none
    case parameter(UInt64)
    case pitch        // all oscillators, 100 % = ±1 octave
    case amplitude    // voice level, −100 % = silent, +100 % = ×2
    case pan          // voice pan, 100 % = full width
    case allCutoffs   // every filter cutoff / centre frequency at once

    public static let pitchCode: Float = 16384, amplitudeCode: Float = 16385, panCode: Float = 16386, allCutoffsCode: Float = 16387

    public init(code: Float) {
        switch code.rounded() {
        case Self.pitchCode: self = .pitch
        case Self.amplitudeCode: self = .amplitude
        case Self.panCode: self = .pan
        case Self.allCutoffsCode: self = .allCutoffs
        case let c where c >= 1 && c < 16384: self = .parameter(UInt64(c))
        default: self = .none
        }
    }

    public var code: Float {
        switch self {
        case .none: return 0
        case .parameter(let a): return Float(a)
        case .pitch: return Self.pitchCode
        case .amplitude: return Self.amplitudeCode
        case .pan: return Self.panCode
        case .allCutoffs: return Self.allCutoffsCode
        }
    }

    public var displayName: String {
        switch self {
        case .none: return "None"
        case .parameter(let a): return ParameterRegistry.spec(address: a)?.displayName ?? "Parameter \(a)"
        case .pitch: return "Pitch (all oscillators)"
        case .amplitude: return "Voice Level"
        case .pan: return "Voice Pan"
        case .allCutoffs: return "All Filter Cutoffs"
        }
    }

    /// Valid targets: virtual destinations plus every modulatable parameter, by module.
    public static var catalogue: [(group: String, destinations: [ModDestination])] {
        var out: [(group: String, destinations: [ModDestination])] = [(group: "Voice", destinations: [.pitch, .amplitude, .pan, .allCutoffs])]
        for group in ParameterRegistry.groups {
            let d = ParameterRegistry.specs(inGroup: group.identifier).filter(\.isModulatable).map { ModDestination.parameter($0.address) }
            if !d.isEmpty { out.append((group: group.name, destinations: d)) }
        }
        return out
    }
}

/// Field addresses of one matrix slot.
public struct ModSlotAddresses: Sendable, Hashable {
    public static let slotCount = 16
    public static let moduleBase: UInt64 = 60 * 256
    public let index: Int   // 0…15

    public init(_ index: Int) { self.index = min(max(index, 0), Self.slotCount - 1) }

    private var base: UInt64 { Self.moduleBase + UInt64(index) * 16 }
    public var enable: UInt64 { base + 0 }
    public var source: UInt64 { base + 1 }
    public var via: UInt64 { base + 2 }
    public var destination: UInt64 { base + 3 }
    public var amount: UInt64 { base + 4 }
    public var offset: UInt64 { base + 5 }
    public var curve: UInt64 { base + 6 }
    public var polarity: UInt64 { base + 7 }
    public var invert: UInt64 { base + 8 }
    public var slew: UInt64 { base + 9 }
    public var all: [UInt64] { [enable, source, via, destination, amount, offset, curve, polarity, invert, slew] }
}
