// PanelKit.swift — Phase 8 hardware panel language (docs/design/HARDWARE_AESTHETIC.md):
// matte black painted panels with a satin sheen and faint orange peel, white
// silkscreen legends, brushed-aluminium rails with screws, a tolex cabinet,
// and seeded wear (scratches, edge rub, smudges, legend yellowing) scaled by
// the Wear control. Everything is vector / procedural — no photographic
// textures. Wear never lowers legend contrast below WCAG AA (the legend
// yellowing is capped; see `PanelTheme.legend(wear:)`).
import SwiftUI
import VekturCore

public enum PanelTheme {
    public static let room = Color(red: 0.07, green: 0.067, blue: 0.063)
    public static let tolex = Color(white: 0.045)
    public static let paint = Color(white: 0.086)
    public static let legendNew = Color(red: 0.945, green: 0.941, blue: 0.918)
    public static let legend2 = Color(red: 0.66, green: 0.66, blue: 0.635)
    public static let cream = Color(red: 0.937, green: 0.902, blue: 0.812)
    public static let lampRed = Color(red: 1.0, green: 0.23, blue: 0.18)
    public static let lampGreen = Color(red: 0.25, green: 0.87, blue: 0.52)
    public static let amber = Color(red: 1.0, green: 0.70, blue: 0.25)
    public static let alu = Color(red: 0.73, green: 0.735, blue: 0.74)

    /// Silkscreen white yellowing with wear. At wear 1 the colour is
    /// (0.91, 0.87, 0.75): contrast 15.3:1 on the panel paint (AA needs 4.5:1).
    public static func legend(wear: Double) -> Color {
        let w = min(max(wear, 0), 1) * 0.18
        return Color(red: 0.945 - 0.2 * w, green: 0.941 - 0.4 * w, blue: 0.918 - 0.95 * w)
    }

    /// Condensed silkscreen face.
    public static func legendFont(_ size: CGFloat, weight: Font.Weight = .semibold) -> Font {
        .system(size: size, weight: weight).width(.condensed)
    }

    public static let hp: CGFloat = 22
    public static let panelHeight: CGFloat = 430
    public static let railHeight: CGFloat = 26
}

// MARK: - Environment

private struct PanelWearKey: EnvironmentKey { static let defaultValue = AppearanceState.defaultWear }
private struct TiltKey: EnvironmentKey { static let defaultValue = true }
public extension EnvironmentValues {
    /// Wear amount 0 (factory new) … 1 (well-used studio unit) for panels below.
    var panelWear: Double { get { self[PanelWearKey.self] } set { self[PanelWearKey.self] = newValue } }
    var panelTiltHighlights: Bool { get { self[TiltKey.self] } set { self[TiltKey.self] = newValue } }
}

// MARK: - Deterministic noise for surfaces

struct SeededNoise {
    private var s: UInt32
    init(_ seed: UInt32) { s = seed == 0 ? 1 : seed }
    mutating func next() -> Double { s = s &* 1664525 &+ 1013904223; return Double(s) / 4294967296.0 }
}

// MARK: - Surfaces

/// Painted panel: base paint, satin sheen, orange peel, then seeded wear.
public struct PanelPaint: View {
    var key: String
    @Environment(\.panelWear) private var wear
    @Environment(\.panelTilt) private var tilt
    public init(key: String) { self.key = key }

    public var body: some View {
        Canvas(rendersAsynchronously: false) { gc, size in
            let rect = CGRect(origin: .zero, size: size)
            gc.fill(Path(rect), with: .color(PanelTheme.paint))
            // Satin sheen: soft light from the top.
            gc.fill(Path(rect), with: .linearGradient(Gradient(colors: [.white.opacity(0.045), .clear, .black.opacity(0.18)]),
                                                     startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
            // Tilt-reactive highlight (Phase 10): a soft band of light that
            // slides across the satin paint as the device tilts.
            if tilt != .zero {
                let cx = size.width * (0.5 + 0.45 * tilt.x), cy = size.height * (0.35 + 0.4 * tilt.y)
                gc.fill(Path(rect), with: .radialGradient(Gradient(colors: [.white.opacity(0.07), .clear]), center: CGPoint(x: cx, y: cy),
                                                          startRadius: 0, endRadius: max(size.width, size.height) * 0.55))
            }
            // Orange peel: a sparse field of faint specks.
            var n = SeededNoise(WearPattern.seed(key) ^ 0x9E3779B9)
            let specks = min(6000, Int(size.width * size.height / 160))
            for _ in 0..<specks {
                let p = CGPoint(x: n.next() * size.width, y: n.next() * size.height)
                gc.fill(Path(ellipseIn: CGRect(x: p.x, y: p.y, width: 1.1, height: 1.1)), with: .color(.white.opacity(0.03 + 0.03 * n.next())))
            }
            // Edges: a lighter left bevel and a dark right shadow.
            gc.fill(Path(CGRect(x: 0, y: 0, width: 1, height: size.height)), with: .color(.white.opacity(0.06)))
            gc.fill(Path(CGRect(x: size.width - 1, y: 0, width: 1, height: size.height)), with: .color(.black.opacity(0.8)))
            drawWear(gc, size: size)
        }
        .accessibilityHidden(true)
    }

    private func drawWear(_ gc: GraphicsContext, size: CGSize) {
        guard wear > 0.001 else { return }
        let p = WearPattern.make(key: key, width: size.width, height: size.height)
        let count = Int((Double(p.scratches.count) * min(1, wear * 1.4)).rounded())
        for s in p.scratches.prefix(count) {
            var path = Path(); path.move(to: CGPoint(x: s.x, y: s.y)); path.addLine(to: CGPoint(x: s.x + s.dx, y: s.y + s.dy))
            gc.stroke(path, with: .color(Color(white: 0.81).opacity(s.opacity * wear)), lineWidth: s.width)
        }
        for r in p.rubs {   // paint rub-through on the edges
            gc.fill(Path(ellipseIn: CGRect(x: r.x - r.rx, y: r.y - r.ry, width: r.rx * 2, height: r.ry * 2)), with: .color(Color(white: 0.54).opacity(0.5 * wear)))
        }
        for m in p.smudges {
            gc.fill(Path(ellipseIn: CGRect(x: m.x - m.r, y: m.y - m.r, width: m.r * 2, height: m.r * 2)), with: .color(.white.opacity(0.035 * wear)))
        }
    }
}

/// Brushed-aluminium rack rail with screws; dust gathers with wear.
public struct RackRail: View {
    @Environment(\.panelWear) private var wear
    public init() {}
    public var body: some View {
        Canvas { gc, size in
            let rect = CGRect(origin: .zero, size: size)
            gc.fill(Path(rect), with: .linearGradient(Gradient(stops: [
                .init(color: Color(white: 0.875), location: 0), .init(color: Color(white: 0.66), location: 0.35),
                .init(color: Color(white: 0.57), location: 0.6), .init(color: Color(white: 0.77), location: 1)]),
                startPoint: .zero, endPoint: CGPoint(x: 0, y: size.height)))
            // Fine horizontal grain.
            var n = SeededNoise(77)
            var x = 0.0
            while x < size.width {
                let w = 1 + n.next() * 2
                gc.fill(Path(CGRect(x: x, y: 0, width: 0.6, height: size.height)), with: .color(.white.opacity(0.05 + 0.08 * n.next())))
                x += w
            }
            // Screws every 44 pt.
            var sx = 11.0
            while sx < size.width {
                let c = CGRect(x: sx - 4, y: size.height / 2 - 4, width: 8, height: 8)
                gc.fill(Path(ellipseIn: c.insetBy(dx: -0.6, dy: -0.6)), with: .color(Color(white: 0.43)))
                gc.fill(Path(ellipseIn: c), with: .radialGradient(Gradient(colors: [Color(white: 0.9), Color(white: 0.5)]),
                                                                center: CGPoint(x: c.minX + 3, y: c.minY + 3), startRadius: 0, endRadius: 6))
                var slot = Path(); slot.move(to: CGPoint(x: c.minX + 1.5, y: c.midY)); slot.addLine(to: CGPoint(x: c.maxX - 1.5, y: c.midY))
                gc.stroke(slot, with: .color(Color(white: 0.25)), lineWidth: 1)
                sx += 44
            }
            // Dust in the rail channel.
            if wear > 0.01 {
                var d = SeededNoise(4242)
                for _ in 0..<Int(size.width / 6 * wear) {
                    gc.fill(Path(ellipseIn: CGRect(x: d.next() * size.width, y: d.next() * size.height, width: 1.4, height: 1.4)),
                            with: .color(Color(red: 0.3, green: 0.27, blue: 0.22).opacity(0.6 * wear)))
                }
            }
            gc.fill(Path(CGRect(x: 0, y: size.height - 1, width: size.width, height: 1)), with: .color(.black.opacity(0.5)))
        }
        .frame(height: PanelTheme.railHeight)
        .accessibilityHidden(true)
    }
}

/// Tolex cabinet covering.
public struct TolexBackground: View {
    public init() {}
    public var body: some View {
        Canvas { gc, size in
            gc.fill(Path(CGRect(origin: .zero, size: size)), with: .color(PanelTheme.tolex))
            var n = SeededNoise(1234)
            for _ in 0..<Int(size.width * size.height / 30) {
                gc.fill(Path(CGRect(x: n.next() * size.width, y: n.next() * size.height, width: 1.2, height: 1.2)), with: .color(.white.opacity(0.06 * n.next())))
            }
        }
        .accessibilityHidden(true)
    }
}

struct ScrewHead: View {
    var angle: Double
    var body: some View {
        Circle().fill(RadialGradient(colors: [Color(white: 0.93), Color(white: 0.47), Color(white: 0.27)], center: .init(x: 0.35, y: 0.35), startRadius: 0, endRadius: 6))
            .overlay(Rectangle().fill(Color(white: 0.2)).frame(height: 1).padding(.horizontal, 1).rotationEffect(.degrees(angle)))
            .frame(width: 9, height: 9).shadow(color: .black, radius: 0.5, y: 1)
            .accessibilityHidden(true)
    }
}

// MARK: - Taper helpers

enum Taper {
    static func norm(_ spec: ParameterSpec, _ v: Float) -> Double {
        let lo = Double(spec.minValue), hi = Double(spec.maxValue), x = Double(v)
        guard hi > lo else { return 0 }
        if spec.taper == .log && lo > 0 { return min(max(log(max(x, lo) / lo) / log(hi / lo), 0), 1) }
        return min(max((x - lo) / (hi - lo), 0), 1)
    }
    static func value(_ spec: ParameterSpec, _ n: Double) -> Float {
        let lo = Double(spec.minValue), hi = Double(spec.maxValue), t = min(max(n, 0), 1)
        let v = spec.taper == .log && lo > 0 ? lo * pow(hi / lo, t) : lo + t * (hi - lo)
        return spec.sanitize(v)
    }
}
