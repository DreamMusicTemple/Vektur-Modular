// ParameterSpec.swift — declarative description of one host-visible parameter.
import Foundation

/// Unit vocabulary kept independent of AudioToolbox so VekturCore stays portable.
/// VekturAudioUnit maps these onto `AudioUnitParameterUnit`.
public enum ParameterUnit: String, Sendable, Codable {
    case generic, decibels, cents, semitones, indexed, hertz, seconds, milliseconds, percent, boolean, octaves, degrees
}

/// How a control maps position to value (hosts use this for display).
public enum ParameterTaper: String, Sendable, Codable { case linear, log }

public struct ParameterSpec: Sendable, Hashable {
    /// Stable string identifier, used as the key in saved state and as the
    /// AUParameter identifier. Never rename once shipped.
    public let identifier: String
    /// Stable numeric AU address: module * VK_PARAMS_PER_MODULE + index.
    /// Never renumber once shipped — hosts store automation by address.
    public let address: UInt64
    public let displayName: String
    public let unit: ParameterUnit
    public let minValue: Float
    public let maxValue: Float
    public let defaultValue: Float
    /// Render-side smoothing time. 0 for discrete parameters.
    public let smoothingMs: Float
    /// Labels for indexed parameters (count must equal max - min + 1).
    public let valueStrings: [String]?
    /// Whether hosts may automate this parameter.
    public let isAutomatable: Bool
    public let taper: ParameterTaper
    /// Identifier of the parameter-tree group (module) this belongs to.
    public let group: String
    /// Whether the modulation matrix may target this parameter (per voice).
    public let isModulatable: Bool

    public init(identifier: String, address: UInt64, displayName: String, unit: ParameterUnit,
                minValue: Float, maxValue: Float, defaultValue: Float, smoothingMs: Float,
                valueStrings: [String]? = nil, isAutomatable: Bool = true,
                taper: ParameterTaper = .linear, group: String = "global", isModulatable: Bool = false) {
        self.identifier = identifier
        self.address = address
        self.displayName = displayName
        self.unit = unit
        self.minValue = minValue
        self.maxValue = maxValue
        self.defaultValue = defaultValue
        self.smoothingMs = smoothingMs
        self.valueStrings = valueStrings
        self.isAutomatable = isAutomatable
        self.taper = taper
        self.group = group
        self.isModulatable = isModulatable
    }

    /// Clamp to range; non-finite input becomes the default; indexed values are rounded.
    public func sanitize(_ value: Double) -> Float {
        guard value.isFinite else { return defaultValue }
        var v = Float(min(max(value, Double(minValue)), Double(maxValue)))
        if unit == .indexed || unit == .boolean || identifier.hasSuffix(".dest") { v = v.rounded() }
        return v
    }
}
