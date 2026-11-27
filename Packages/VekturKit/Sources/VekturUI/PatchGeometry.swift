// PatchGeometry.swift — where modules and jacks sit on the workspace.
// Pure functions of the layout, so cables stay attached at any zoom: cables
// and panels are both drawn in workspace coordinates inside one transform.
import CoreGraphics
import VekturAudioUnit
import VekturCore

enum PatchGeometry {
    static let panelWidth: CGFloat = 170
    static let headerHeight: CGFloat = 30
    static let rowHeight: CGFloat = 24
    static let jackRadius: CGFloat = 7
    static let gridColumns = 6
    static let gridGap: CGFloat = 28

    static func panelHeight(_ m: RackModule) -> CGFloat {
        headerHeight + CGFloat(max(m.inputs.count, m.outputs.count, 1)) * rowHeight + 8
    }

    /// Saved position, or a tidy default grid in rack order.
    static func origin(of m: RackModule, layout: PatchLayout) -> CGPoint {
        if let p = layout.positions[m.key] { return CGPoint(x: p.x, y: p.y) }
        let modules = RackCatalog.patchableModules
        let i = modules.firstIndex { $0.key == m.key } ?? 0
        let col = i % gridColumns, row = i / gridColumns
        // Row height = tallest panel in that row, so the default grid never overlaps.
        var y: CGFloat = gridGap
        for r in 0..<row {
            let rowModules = modules[(r * gridColumns)..<min((r + 1) * gridColumns, modules.count)]
            y += (rowModules.map(panelHeight).max() ?? 100) + gridGap
        }
        return CGPoint(x: gridGap + CGFloat(col) * (panelWidth + gridGap), y: y)
    }

    static func frame(of m: RackModule, layout: PatchLayout) -> CGRect {
        CGRect(origin: origin(of: m, layout: layout), size: CGSize(width: panelWidth, height: panelHeight(m)))
    }

    /// Jack centre in workspace coordinates. Inputs on the left edge, outputs on the right.
    static func jack(_ ref: PortRef, input: Bool, layout: PatchLayout) -> CGPoint? {
        guard let m = RackCatalog.module(key: ref.module) else { return nil }
        let ports = input ? m.inputs : m.outputs
        guard let idx = ports.firstIndex(where: { $0.key == ref.port }) else { return nil }
        let o = origin(of: m, layout: layout)
        return CGPoint(x: o.x + (input ? 12 : panelWidth - 12),
                       y: o.y + headerHeight + CGFloat(idx) * rowHeight + rowHeight / 2)
    }

    static func workspaceBounds(layout: PatchLayout) -> CGRect {
        RackCatalog.patchableModules.reduce(CGRect.null) { $0.union(frame(of: $1, layout: layout)) }.insetBy(dx: -gridGap, dy: -gridGap)
    }

    /// Cable path with gravity sag proportional to its length.
    static func cablePath(from a: CGPoint, to b: CGPoint) -> CGPath {
        let p = CGMutablePath()
        let sag = min(160, hypot(b.x - a.x, b.y - a.y) * 0.25 + 20)
        p.move(to: a)
        p.addCurve(to: b, control1: CGPoint(x: a.x + 40, y: a.y + sag), control2: CGPoint(x: b.x - 40, y: b.y + sag))
        return p
    }

    /// Distance from a point to a cable (sampled), for tap selection.
    static func distance(_ pt: CGPoint, toCableFrom a: CGPoint, to b: CGPoint) -> CGFloat {
        let sag = min(160, hypot(b.x - a.x, b.y - a.y) * 0.25 + 20)
        let c1 = CGPoint(x: a.x + 40, y: a.y + sag), c2 = CGPoint(x: b.x - 40, y: b.y + sag)
        var best = CGFloat.greatestFiniteMagnitude
        for i in 0...32 {
            let t = CGFloat(i) / 32, u = 1 - t
            let x = u * u * u * a.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * b.x
            let y = u * u * u * a.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * b.y
            best = min(best, hypot(pt.x - x, pt.y - y))
        }
        return best
    }
}
