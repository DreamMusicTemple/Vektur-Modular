// EffectsState.swift — the part of the effects setup that is not a parameter
// (Phase 6). Slot types, routing and every effect setting are ordinary
// parameters (modules 39–47); what remains is which user impulse response
// each convolution slot uses. IRs are referenced by content hash (SHA-256)
// and live as files in the shared IR library, never inline in the state.
import Foundation

public struct ImpulseResponseReference: Codable, Sendable, Equatable, Hashable {
    public var slot: Int            // 0…7
    public var sha256: String       // lowercase hex of the file's bytes
    public var name: String         // display name (original file name)
    public var frames: Int
    public var sampleRate: Double
    public var channels: Int

    public init(slot: Int, sha256: String, name: String, frames: Int, sampleRate: Double, channels: Int) {
        self.slot = slot; self.sha256 = sha256; self.name = name; self.frames = frames; self.sampleRate = sampleRate; self.channels = channels
    }
}

public struct EffectsState: Codable, Sendable, Equatable {
    public static let slotCount = 8
    public var impulseResponses: [ImpulseResponseReference]

    public init(impulseResponses: [ImpulseResponseReference] = []) { self.impulseResponses = impulseResponses }

    public var isDefault: Bool { impulseResponses.isEmpty }

    public func impulseResponse(forSlot slot: Int) -> ImpulseResponseReference? { impulseResponses.first { $0.slot == slot } }

    /// One reference per valid slot, plausible hashes and sizes only.
    public func sanitized() -> EffectsState {
        var seen = Set<Int>()
        var out: [ImpulseResponseReference] = []
        for r in impulseResponses where (0..<Self.slotCount).contains(r.slot) && !seen.contains(r.slot) {
            let hex = r.sha256.lowercased()
            guard hex.count == 64, hex.allSatisfy({ $0.isHexDigit }) else { continue }
            guard r.sampleRate.isFinite, r.sampleRate >= 8000, r.sampleRate <= 768_000 else { continue }
            seen.insert(r.slot)
            out.append(ImpulseResponseReference(slot: r.slot, sha256: hex, name: String(r.name.prefix(128)),
                                                frames: max(0, min(r.frames, 768_000 * 10)), sampleRate: r.sampleRate,
                                                channels: min(max(r.channels, 1), 2)))
        }
        return EffectsState(impulseResponses: out.sorted { $0.slot < $1.slot })
    }
}
