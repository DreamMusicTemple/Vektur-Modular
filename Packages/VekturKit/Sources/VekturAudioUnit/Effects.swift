// Effects.swift — Phase 6 Swift surface of the effects engine: catalogue,
// per-type parameter naming and display, slot type changes with defaults,
// structural rebuilds, latency reporting, meters and user impulse responses
// (stored by content hash in the shared IR library).
@preconcurrency import AVFoundation
import CryptoKit
import Foundation
import os
import VekturCore
import VekturDSP

// MARK: - Catalogue

public struct FxParamDescriptor: Sendable, Hashable, Identifiable {
    public var index: Int                // 0…11 (slot parameter p1…p12)
    public var name: String
    public var unit: UInt8               // VKFxUnit
    public var isChoice: Bool
    public var isToggle: Bool
    public var isLog: Bool
    public var structural: Bool
    public var choices: [String]
    public var defaultNormalised: Float
    public var id: Int { index }
}

public struct FxTypeDescriptor: Sendable, Hashable, Identifiable {
    public var id: Int                   // permanent type number
    public var name: String
    public var family: String
    public var defaultMix: Float
    public var parameters: [FxParamDescriptor]
}

public enum FxCatalogue {
    public static let types: [FxTypeDescriptor] = (0..<Int(vk_fx_type_count())).map { t in
        var info = VKFxTypeInfo()
        _ = vk_fx_type_info(UInt32(t), &info)
        var params: [FxParamDescriptor] = []
        for k in 0..<Int(VK_FX_PARAMS) {
            var p = VKFxParamInfo()
            guard vk_fx_param_info(UInt32(t), UInt32(k), &p), let cname = p.name else { continue }
            let choices = (0..<Int(p.choiceCount)).map { String(cString: vk_fx_choice_name(UInt32(t), UInt32(k), UInt32($0))) }
            params.append(FxParamDescriptor(index: k, name: String(cString: cname), unit: p.unit, isChoice: p.isChoice, isToggle: p.isToggle,
                                            isLog: p.isLog, structural: p.structural, choices: choices, defaultNormalised: p.defaultNormalised))
        }
        return FxTypeDescriptor(id: t, name: info.name.map { String(cString: $0) } ?? "", family: info.family.map { String(cString: $0) } ?? "",
                                defaultMix: info.defaultMix, parameters: params)
    }
    public static func type(_ id: Int) -> FxTypeDescriptor { types.indices.contains(id) ? types[id] : types[0] }
    /// Families in display order with their types (Empty excluded).
    public static var families: [(name: String, types: [FxTypeDescriptor])] {
        var order: [String] = []
        for t in types.dropFirst() where !order.contains(t.family) { order.append(t.family) }
        return order.map { f in (f, types.filter { $0.family == f }) }
    }
}

/// Parameter addresses of one effect slot (modules 39–46) and the buses (47).
public struct FxSlotAddresses: Sendable, Hashable {
    public static let slotCount = Int(VK_FX_SLOTS)
    public let slot: Int
    public init(_ slot: Int) { self.slot = min(max(slot, 0), Self.slotCount - 1) }
    private var base: UInt64 { UInt64(39 + slot) * 256 }
    public var type: UInt64 { base }
    public var enabled: UInt64 { base + 1 }
    public var mix: UInt64 { base + 2 }
    public var output: UInt64 { base + 3 }
    public var bus: UInt64 { base + 4 }
    public func parameter(_ index: Int) -> UInt64 { base + 5 + UInt64(min(max(index, 0), 11)) }

    /// (slot, generic parameter index) for a slot parameter address.
    public static func decode(_ address: UInt64) -> (slot: Int, index: Int?)? {
        let module = Int(address / 256), i = Int(address % 256)
        guard (39...46).contains(module), i <= 16 else { return nil }
        return (module - 39, i >= 5 ? i - 5 : nil)
    }
}

// MARK: - IR library

/// User impulse responses, stored once by SHA-256 of the file bytes. The
/// library lives in the app-group container when it exists (shared by the
/// app and the AUv3 extension), else in this process's Application Support.
public enum ImpulseResponseLibrary {
    public static let appGroup = "group.com.dmtcymatics.vekturmodular"

    public static var directory: URL {
        let fm = FileManager.default
        let root = fm.containerURL(forSecurityApplicationGroupIdentifier: appGroup)
            ?? (try? fm.url(for: .applicationSupportDirectory, in: .userDomainMask, appropriateFor: nil, create: true))
            ?? fm.temporaryDirectory
        let dir = root.appendingPathComponent("VekturModular/ImpulseResponses", isDirectory: true)
        try? fm.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir
    }

    public static func sha256(of data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }

    /// Copies a file into the library; returns its hash and library URL.
    public static func store(fileAt url: URL) throws -> (sha256: String, url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        let data = try Data(contentsOf: url)
        let hash = sha256(of: data)
        let ext = url.pathExtension.isEmpty ? "wav" : url.pathExtension.lowercased()
        let dest = directory.appendingPathComponent("\(hash).\(ext)")
        if !FileManager.default.fileExists(atPath: dest.path) { try data.write(to: dest, options: .atomic) }
        return (hash, dest)
    }

    public static func url(forHash hash: String) -> URL? {
        let dir = directory
        let names = (try? FileManager.default.contentsOfDirectory(atPath: dir.path)) ?? []
        return names.first { $0.hasPrefix(hash + ".") }.map { dir.appendingPathComponent($0) }
    }
}

public enum ImpulseResponseError: Error, Equatable {
    case unreadable(String)
    case empty
    case tooLong(seconds: Double)
}

// MARK: - Audio unit API

extension VekturModularAudioUnit {

    /// Sets a slot's effect type and its defaults (mix, p1…p12) through the
    /// parameter tree, so hosts and the UI see the change, then rebuilds.
    public func setEffectType(slot: Int, type: Int) {
        let a = FxSlotAddresses(slot)
        let t = FxCatalogue.type(type)
        guard let tree = parameterTree else { return }
        tree.parameter(withAddress: a.type)?.setValue(Float(t.id), originator: nil)
        tree.parameter(withAddress: a.mix)?.setValue(t.defaultMix, originator: nil)
        for k in 0..<12 {
            let d = t.parameters.first { $0.index == k }?.defaultNormalised ?? 0.5
            tree.parameter(withAddress: a.parameter(k))?.setValue(d, originator: nil)
        }
        updateEffects()
    }

    /// Rebuilds slots whose type / structural setting / IR changed. Safe to call from any non-render thread.
    @discardableResult
    public func updateEffects() -> Int {
        let n = effectsLock.withLockUnchecked { Int(vk_kernel_update_effects(kernel.raw)) }
        scheduleLatencyCheck()
        return n
    }

    public var effectsLatencySamples: Int { Int(vk_kernel_effects_latency(kernel.raw)) }
    public func activeEffectType(slot: Int) -> Int { Int(vk_kernel_effect_active_type(kernel.raw, UInt32(FxSlotAddresses(slot).slot))) }
    /// Gain reduction in dB (≤ 0) for dynamics effects; 0 otherwise.
    public func effectMeter(slot: Int) -> Float { vk_kernel_effect_meter(kernel.raw, UInt32(FxSlotAddresses(slot).slot)) }

    /// Display string of a slot parameter for the slot's current type.
    public func formatEffectParameter(slot: Int, index: Int, normalised: Float) -> String {
        let type = UInt32(kernel.get(FxSlotAddresses(slot).type).rounded())
        var buf = [CChar](repeating: 0, count: 32)
        _ = buf.withUnsafeMutableBufferPointer { vk_fx_format_value(type, UInt32(index), normalised, $0.baseAddress, 32) }
        return buf.withUnsafeBufferPointer { String(cString: $0.baseAddress!) }
    }

    // MARK: Impulse responses

    public var impulseResponses: [ImpulseResponseReference] { stateSnapshot.effects.impulseResponses }

    /// References whose file is not in this device's library (e.g. a project
    /// moved from another device): the slot falls back to its built-in IR.
    public var missingImpulseResponses: [ImpulseResponseReference] {
        impulseResponses.filter { ImpulseResponseLibrary.url(forHash: $0.sha256) == nil }
    }

    /// Loads an audio file as the slot's user IR, copies it into the library,
    /// records it in the state and selects "User IR" on the slot.
    @discardableResult
    public func loadImpulseResponse(from url: URL, slot: Int) throws -> ImpulseResponseReference {
        let stored = try ImpulseResponseLibrary.store(fileAt: url)
        let (frames, rate, channels) = try installImpulseResponse(fileAt: stored.url, slot: slot)
        let ref = ImpulseResponseReference(slot: FxSlotAddresses(slot).slot, sha256: stored.sha256, name: url.lastPathComponent,
                                           frames: frames, sampleRate: rate, channels: channels)
        mutateState { s in
            s.effects.impulseResponses.removeAll { $0.slot == ref.slot }
            s.effects.impulseResponses.append(ref)
            s.effects = s.effects.sanitized()
        }
        // Select "User IR" (the last choice of the structural Impulse parameter).
        if let p = parameterTree?.parameter(withAddress: FxSlotAddresses(slot).parameter(0)),
           Int(kernel.get(FxSlotAddresses(slot).type).rounded()) == 9 { p.setValue(1, originator: nil) }
        updateEffects()
        return ref
    }

    public func clearImpulseResponse(slot: Int) {
        effectsLock.withLockUnchecked { vk_kernel_clear_effect_ir(kernel.raw, UInt32(FxSlotAddresses(slot).slot)) }
        mutateState { $0.effects.impulseResponses.removeAll { $0.slot == slot } }
        updateEffects()
    }

    /// Reads an audio file into the kernel for `slot`. Returns (frames, rate, channels).
    func installImpulseResponse(fileAt url: URL, slot: Int) throws -> (Int, Double, Int) {
        let file: AVAudioFile
        do { file = try AVAudioFile(forReading: url, commonFormat: .pcmFormatFloat32, interleaved: false) }
        catch { throw ImpulseResponseError.unreadable(error.localizedDescription) }
        let rate = file.processingFormat.sampleRate
        let frames = AVAudioFrameCount(min(file.length, AVAudioFramePosition(rate * 10)))
        guard frames > 0 else { throw ImpulseResponseError.empty }
        guard let buf = AVAudioPCMBuffer(pcmFormat: file.processingFormat, frameCapacity: frames) else { throw ImpulseResponseError.empty }
        do { try file.read(into: buf, frameCount: frames) } catch { throw ImpulseResponseError.unreadable(error.localizedDescription) }
        guard let ch = buf.floatChannelData, buf.frameLength > 0 else { throw ImpulseResponseError.empty }
        let channels = Int(buf.format.channelCount)
        let left = ch[0], right = channels > 1 ? ch[1] : nil
        effectsLock.withLockUnchecked {
            vk_kernel_set_effect_ir(kernel.raw, UInt32(FxSlotAddresses(slot).slot), left, right, buf.frameLength, rate)
        }
        return (Int(buf.frameLength), rate, min(channels, 2))
    }

    /// Re-installs the IRs a restored state refers to (missing files are skipped).
    func restoreImpulseResponses(_ effects: EffectsState) {
        for s in 0..<FxSlotAddresses.slotCount {
            if let ref = effects.impulseResponse(forSlot: s), let url = ImpulseResponseLibrary.url(forHash: ref.sha256) {
                do { _ = try installImpulseResponse(fileAt: url, slot: s) }
                catch { effectsLog.error("IR \(ref.name) could not be restored: \(String(describing: error))") }
            } else {
                effectsLock.withLockUnchecked { vk_kernel_clear_effect_ir(kernel.raw, UInt32(s)) }
            }
        }
    }
}

let effectsLog = Logger(subsystem: "com.dmtcymatics.vekturmodular", category: "Effects")
