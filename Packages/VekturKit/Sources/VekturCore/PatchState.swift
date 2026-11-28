// PatchState.swift — the user's patch cables, as saved in presets, projects
// and AUv3 state.
//
// Cables refer to modules and ports by their stable KEYS ("vco4", "fm"), never
// by index, so a patch keeps working when later versions add modules or ports.
// Visual properties (colour, lock) live here too; module positions and zoom
// arrive with the patch canvas (Phase 3B) as an additive `layout` section.
import Foundation

public struct PortRef: Codable, Sendable, Hashable, CustomStringConvertible {
    public var module: String
    public var port: String
    public init(module: String, port: String) { self.module = module; self.port = port }
    public var description: String { "\(module).\(port)" }
}

public struct CableState: Codable, Sendable, Equatable, Identifiable {
    /// 0 … 255, unique within a patch. Also the cable's telemetry slot.
    public var id: Int
    public var from: PortRef          // an output
    public var to: PortRef            // an input
    /// Per-cable attenuverter, −2 … +2 (1 = unity).
    public var gain: Double
    /// Display colour name (UI vocabulary, e.g. "red"); nil = automatic by signal type.
    public var colour: String?
    /// Locked cables cannot be removed or moved by accident.
    public var locked: Bool

    public init(id: Int, from: PortRef, to: PortRef, gain: Double = 1, colour: String? = nil, locked: Bool = false) {
        self.id = id; self.from = from; self.to = to; self.gain = gain; self.colour = colour; self.locked = locked
    }

    enum CodingKeys: String, CodingKey { case id, from, to, gain, colour, locked }
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        id = try c.decode(Int.self, forKey: .id)
        from = try c.decode(PortRef.self, forKey: .from)
        to = try c.decode(PortRef.self, forKey: .to)
        gain = try c.decodeIfPresent(Double.self, forKey: .gain) ?? 1
        colour = try c.decodeIfPresent(String.self, forKey: .colour)
        locked = try c.decodeIfPresent(Bool.self, forKey: .locked) ?? false
    }
}

/// Workspace layout of the patch canvas (Phase 3B). Purely visual: changing it
/// never recompiles the audio graph.
public struct PatchLayout: Codable, Sendable, Equatable {
    public struct Position: Codable, Sendable, Equatable { public var x: Double; public var y: Double
        public init(x: Double, y: Double) { self.x = x; self.y = y } }

    public var positions: [String: Position]   // module key → top-left in workspace points
    public var zoom: Double                    // 1 = 100 %
    public var panX: Double
    public var panY: Double
    public var cableOpacity: Double            // 0.1 … 1.0
    public var cableThickness: Double          // points, 1 … 8
    public var showNormals: Bool               // draw default (normalled) connections

    public init(positions: [String: Position] = [:], zoom: Double = 1, panX: Double = 0, panY: Double = 0,
                cableOpacity: Double = 0.8, cableThickness: Double = 3, showNormals: Bool = true) {
        self.positions = positions; self.zoom = zoom; self.panX = panX; self.panY = panY
        self.cableOpacity = cableOpacity; self.cableThickness = cableThickness; self.showNormals = showNormals
    }

    public static let maxZoom = 5.0, minZoom = 0.1

    enum CodingKeys: String, CodingKey { case positions, zoom, panX, panY, cableOpacity, cableThickness, showNormals }
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        positions = (try? c.decodeIfPresent([String: Position].self, forKey: .positions)) ?? [:]
        zoom = (try? c.decodeIfPresent(Double.self, forKey: .zoom)) ?? 1
        panX = (try? c.decodeIfPresent(Double.self, forKey: .panX)) ?? 0
        panY = (try? c.decodeIfPresent(Double.self, forKey: .panY)) ?? 0
        cableOpacity = (try? c.decodeIfPresent(Double.self, forKey: .cableOpacity)) ?? 0.8
        cableThickness = (try? c.decodeIfPresent(Double.self, forKey: .cableThickness)) ?? 3
        showNormals = (try? c.decodeIfPresent(Bool.self, forKey: .showNormals)) ?? true
    }

    public func sanitized() -> PatchLayout {
        func fin(_ v: Double, _ d: Double, _ lo: Double, _ hi: Double) -> Double { v.isFinite ? min(max(v, lo), hi) : d }
        var l = self
        l.zoom = fin(zoom, 1, Self.minZoom, Self.maxZoom)
        l.panX = fin(panX, 0, -1e6, 1e6); l.panY = fin(panY, 0, -1e6, 1e6)
        l.cableOpacity = fin(cableOpacity, 0.8, 0.1, 1)
        l.cableThickness = fin(cableThickness, 3, 1, 8)
        l.positions = positions.filter { $0.value.x.isFinite && $0.value.y.isFinite && abs($0.value.x) < 1e5 && abs($0.value.y) < 1e5 }
        return l
    }
}

public struct PatchState: Codable, Sendable, Equatable {
    public static let maxCables = 128
    public static let maxCableID = 255

    public var cables: [CableState]
    public var layout: PatchLayout

    public init(cables: [CableState] = [], layout: PatchLayout = PatchLayout()) { self.cables = cables; self.layout = layout }

    enum CodingKeys: String, CodingKey { case cables, layout }
    public init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        cables = try c.decodeIfPresent([CableState].self, forKey: .cables) ?? []
        layout = (try? c.decodeIfPresent(PatchLayout.self, forKey: .layout)) ?? PatchLayout()
    }

    /// True when there is nothing worth saving (no cables, default layout).
    public var isDefault: Bool { cables.isEmpty && layout == PatchLayout() }

    public static var empty: PatchState { PatchState() }

    /// Smallest unused cable id, or nil when the patch is full.
    public var nextCableID: Int? {
        guard cables.count < Self.maxCables else { return nil }
        let used = Set(cables.map(\.id))
        return (0...Self.maxCableID).first { !used.contains($0) }
    }

    /// Structural clean-up of untrusted input: drops cables with invalid or
    /// duplicate ids, clamps gains, enforces the cable limit. Whether the
    /// module/port keys exist is checked by the engine when the patch is applied.
    public func sanitized() -> PatchState {
        var seen = Set<Int>()
        var out: [CableState] = []
        for var c in cables {
            guard (0...Self.maxCableID).contains(c.id), !seen.contains(c.id), out.count < Self.maxCables else { continue }
            guard !c.from.module.isEmpty, !c.from.port.isEmpty, !c.to.module.isEmpty, !c.to.port.isEmpty else { continue }
            c.gain = c.gain.isFinite ? min(max(c.gain, -2), 2) : 1
            seen.insert(c.id)
            out.append(c)
        }
        return PatchState(cables: out, layout: layout.sanitized())
    }
}
