// PanelControls.swift — Phase 8 panel controls, each bound to a real
// AUParameter through ParameterModel (host automation and presets flow back).
//   PanelFader   vertical slider: black slot, white cap with index line, tick scale
//   PanelKnob    small knob: knurled skirt, silver cap, black pointer
//   PanelLever   toggle / 2–4 position lever switch with position legends
//   PanelRotary  rotary selector for longer lists (drag, or tap for a menu)
//   PanelLED, PanelJack, VUMeter, GateButton
//
// Accessibility: every control is one element with the parameter's full
// name, its formatted value, and adjustable actions (VoiceOver swipe up /
// down, Switch Control, Full Keyboard Access arrows). Double-tap resets to
// the default. Drags are vertical; hold a second finger / Shift-drag
// (macOS) for fine adjustment.
import SwiftUI
#if os(macOS)
import AppKit
#endif
import VekturAudioUnit
import VekturCore

// MARK: - Shared drag behaviour

private struct ParameterDrag: ViewModifier {
    @Bindable var model: ParameterModel
    let spec: ParameterSpec
    var travel: CGFloat
    @State private var start: Double?

    func body(content: Content) -> some View {
        content
            .contentShape(Rectangle())
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { g in
                    if start == nil { start = Taper.norm(spec, model.value) }
                    let k: Double = NSEventModifierFine.isActive ? 0.15 : 1
                    model.value = Taper.value(spec, start! - Double(g.translation.height / travel) * k)
                }
                .onEnded { _ in start = nil })
            .simultaneousGesture(TapGesture(count: 2).onEnded { model.value = spec.defaultValue })
            .accessibilityElement(children: .ignore)
            .accessibilityLabel(spec.displayName)
            .accessibilityValue(model.displayString)
            .accessibilityAdjustableAction { dir in
                let steps: Double = spec.valueStrings != nil || spec.unit == .boolean ? 1 / max(1, Double(spec.maxValue - spec.minValue)) : 0.02
                let n = Taper.norm(spec, model.value)
                switch dir {
                case .increment: model.value = Taper.value(spec, n + steps)
                case .decrement: model.value = Taper.value(spec, n - steps)
                @unknown default: break
                }
            }
            .accessibilityAction(named: "Reset to default") { model.value = spec.defaultValue }
    }
}

/// Shift (macOS) for fine drags; on touch devices fine control is the
/// VoiceOver adjustable action or the larger Performance controls.
enum NSEventModifierFine {
    @MainActor static var isActive: Bool {
        #if os(macOS)
        return NSEvent.modifierFlags.contains(.shift)
        #else
        return false
        #endif
    }
}

struct ValueBubble: View {
    let text: String
    var body: some View {
        Text(text).font(.system(size: 11, weight: .semibold).monospacedDigit()).foregroundStyle(.black)
            .padding(.horizontal, 5).padding(.vertical, 2)
            .background(PanelTheme.cream, in: RoundedRectangle(cornerRadius: 3))
            .fixedSize()
    }
}

struct LegendText: View {
    let text: String
    var size: CGFloat = 10.5
    var secondary = true
    @Environment(\.panelWear) private var wear
    var body: some View {
        Text(text).font(PanelTheme.legendFont(size, weight: .medium))
            .foregroundStyle(secondary ? PanelTheme.legend2 : PanelTheme.legend(wear: wear))
            .multilineTextAlignment(.center).lineLimit(2).minimumScaleFactor(0.8)
            .frame(maxWidth: 76)
            .accessibilityHidden(true)
    }
}

// MARK: - Fader

public struct PanelFader: View {
    @Bindable var model: ParameterModel
    let spec: ParameterSpec
    var legend: String?
    var height: CGFloat = 170
    @State private var dragging = false
    @Environment(\.panelWear) private var wear

    public init(model: ParameterModel, spec: ParameterSpec, legend: String? = nil, height: CGFloat = 170) {
        self.model = model; self.spec = spec; self.legend = legend; self.height = height
    }

    public var body: some View {
        let n = Taper.norm(spec, model.value)
        VStack(spacing: 4) {
            ZStack(alignment: .bottom) {
                // Tick scale on both sides.
                Canvas { gc, size in
                    for i in 0...10 {
                        let y = 15 + (size.height - 30) * CGFloat(i) / 10
                        let w: CGFloat = i % 5 == 0 ? 7 : 5
                        gc.fill(Path(CGRect(x: 0, y: y, width: w, height: 1)), with: .color(PanelTheme.legend2.opacity(0.8)))
                        gc.fill(Path(CGRect(x: size.width - w, y: y, width: w, height: 1)), with: .color(PanelTheme.legend2.opacity(0.8)))
                    }
                }
                RoundedRectangle(cornerRadius: 3).fill(.black).frame(width: 7)
                    .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color(white: 0.17), lineWidth: 1))
                    .frame(maxHeight: .infinity)
                cap.offset(y: -CGFloat(n) * (height - 30))
            }
            .frame(width: 30, height: height)
            .overlay(alignment: .top) { if dragging { ValueBubble(text: model.displayString).offset(y: -22) } }
            .modifier(ParameterDrag(model: model, spec: spec, travel: height - 30))
            .simultaneousGesture(DragGesture(minimumDistance: 0).onChanged { _ in dragging = true }.onEnded { _ in dragging = false })
            LegendText(text: legend ?? PanelLegend.text(for: spec))
        }
    }

    private var cap: some View {
        RoundedRectangle(cornerRadius: 3)
            .fill(LinearGradient(colors: [Color(white: 0.985), Color(red: 0.906, green: 0.898, blue: 0.863), Color(red: 0.81, green: 0.8, blue: 0.75)], startPoint: .top, endPoint: .bottom))
            .overlay(Rectangle().fill(.black).frame(height: 2).padding(.horizontal, 3))
            .overlay(Ellipse().fill(Color(red: 0.47, green: 0.43, blue: 0.31).opacity(0.25 * wear)).padding(4))   // fingerprints
            .frame(width: 26, height: 30)
            .shadow(color: .black.opacity(0.7), radius: 2, y: 3)
    }
}

// MARK: - Knob

public struct PanelKnob: View {
    @Bindable var model: ParameterModel
    let spec: ParameterSpec
    var legend: String?
    var size: CGFloat = 38
    var showsValue = false
    @State private var dragging = false

    public init(model: ParameterModel, spec: ParameterSpec, legend: String? = nil, size: CGFloat = 38, showsValue: Bool = false) {
        self.model = model; self.spec = spec; self.legend = legend; self.size = size; self.showsValue = showsValue
    }

    public var body: some View {
        let n = Taper.norm(spec, model.value)
        VStack(spacing: 4) {
            KnobFace(normalised: n, size: size)
                .overlay(alignment: .top) { if dragging { ValueBubble(text: model.displayString).offset(y: -24) } }
                .modifier(ParameterDrag(model: model, spec: spec, travel: 200))
                .simultaneousGesture(DragGesture(minimumDistance: 0).onChanged { _ in dragging = true }.onEnded { _ in dragging = false })
            if showsValue {
                Text(model.displayString).font(PanelTheme.legendFont(10.5)).foregroundStyle(PanelTheme.amber).lineLimit(1).accessibilityHidden(true)
            }
            LegendText(text: legend ?? PanelLegend.text(for: spec))
        }
    }
}

struct KnobFace: View {
    var normalised: Double
    var size: CGFloat
    var body: some View {
        ZStack {
            // Scale ticks, 270° sweep.
            Canvas { gc, sz in
                let c = CGPoint(x: sz.width / 2, y: sz.height / 2), r1 = sz.width / 2 - 1, r2 = r1 - 4
                for i in 0...10 {
                    let a = (-225 + 27 * Double(i)) * .pi / 180
                    var p = Path(); p.move(to: CGPoint(x: c.x + cos(a) * r2, y: c.y + sin(a) * r2)); p.addLine(to: CGPoint(x: c.x + cos(a) * r1, y: c.y + sin(a) * r1))
                    gc.stroke(p, with: .color(PanelTheme.legend2), lineWidth: 1)
                }
            }
            .frame(width: size + 12, height: size + 12)
            Circle().fill(AngularGradient(colors: Array(repeating: [Color(white: 0.23), Color(white: 0.08)], count: 30).flatMap { $0 }, center: .center))
                .frame(width: size, height: size).shadow(color: .black.opacity(0.8), radius: 2.5, y: 3)
            Circle().fill(RadialGradient(colors: [Color(white: 0.98), Color(white: 0.79), Color(white: 0.55), Color(white: 0.42)], center: .init(x: 0.35, y: 0.3), startRadius: 0, endRadius: size * 0.45))
                .frame(width: size - 10, height: size - 10)
                .overlay(alignment: .top) { Capsule().fill(Color(white: 0.05)).frame(width: 2, height: (size - 10) * 0.42).padding(.top, 2) }
                .rotationEffect(.degrees(-135 + normalised * 270))
        }
        .frame(width: size + 12, height: size + 12)
    }
}

// MARK: - Lever and rotary switches

public struct PanelLever: View {
    @Bindable var model: ParameterModel
    let spec: ParameterSpec
    var legend: String?

    public init(model: ParameterModel, spec: ParameterSpec, legend: String? = nil) { self.model = model; self.spec = spec; self.legend = legend }

    private var positions: [String] { spec.valueStrings ?? ["off", "on"] }
    private var index: Int { Int((model.value - spec.minValue).rounded()) }

    public var body: some View {
        let n = positions.count, i = min(max(index, 0), n - 1)
        VStack(spacing: 3) {
            HStack(spacing: 4) {
                ForEach(Array(positions.enumerated()), id: \.offset) { k, s in
                    Text(s.lowercased()).font(PanelTheme.legendFont(9.5, weight: k == i ? .semibold : .regular))
                        .foregroundStyle(k == i ? PanelTheme.legendNew : PanelTheme.legend2)
                        .onTapGesture { model.value = spec.minValue + Float(k) }
                }
            }
            .lineLimit(1).minimumScaleFactor(0.7)
            ZStack {
                RoundedRectangle(cornerRadius: 4).fill(RadialGradient(colors: [Color(white: 0.6), Color(white: 0.2), Color(white: 0.07)], center: .center, startRadius: 2, endRadius: 14))
                    .frame(width: 22, height: 34)
                Capsule().fill(LinearGradient(colors: [Color(white: 0.55), Color(white: 0.96), Color(white: 0.55)], startPoint: .leading, endPoint: .trailing))
                    .frame(width: 6, height: 17).offset(y: -8.5)
                    .rotationEffect(.degrees(n == 1 ? 0 : -40 + 80 * Double(i) / Double(n - 1)), anchor: .center)
                    .shadow(color: .black.opacity(0.6), radius: 1, y: 2)
            }
            .contentShape(Rectangle())
            .onTapGesture { model.value = spec.minValue + Float((i + 1) % n) }
            LegendText(text: legend ?? PanelLegend.text(for: spec))
        }
        .frame(minWidth: 44)
        .sensoryFeedback(.selection, trigger: i)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(spec.displayName)
        .accessibilityValue(positions[i])
        .accessibilityAdjustableAction { dir in
            model.value = spec.minValue + Float(dir == .increment ? min(i + 1, n - 1) : max(i - 1, 0))
        }
    }
}

public struct PanelRotary: View {
    @Bindable var model: ParameterModel
    let spec: ParameterSpec
    var legend: String?

    public init(model: ParameterModel, spec: ParameterSpec, legend: String? = nil) { self.model = model; self.spec = spec; self.legend = legend }

    public var body: some View {
        let strings = spec.valueStrings ?? []
        let i = min(max(Int((model.value - spec.minValue).rounded()), 0), max(strings.count - 1, 0))
        VStack(spacing: 3) {
            Menu {
                Picker(spec.displayName, selection: Binding(get: { i }, set: { model.value = spec.minValue + Float($0) })) {
                    ForEach(strings.indices, id: \.self) { Text(strings[$0]).tag($0) }
                }
            } label: {
                KnobFace(normalised: strings.count > 1 ? Double(i) / Double(strings.count - 1) : 0, size: 36)
            }
            .menuStyle(.button).buttonStyle(.plain)
            Text(strings.indices.contains(i) ? strings[i] : "–").font(PanelTheme.legendFont(10.5)).foregroundStyle(PanelTheme.amber)
                .lineLimit(1).frame(maxWidth: 80).accessibilityHidden(true)
            LegendText(text: legend ?? PanelLegend.text(for: spec))
        }
        .sensoryFeedback(.selection, trigger: i)
        .accessibilityElement(children: .ignore)
        .accessibilityLabel(spec.displayName)
        .accessibilityValue(strings.indices.contains(i) ? strings[i] : "")
        .accessibilityAdjustableAction { dir in
            model.value = spec.minValue + Float(dir == .increment ? min(i + 1, strings.count - 1) : max(i - 1, 0))
        }
    }
}

/// Picks the right control for a parameter on a panel.
public struct PanelControl: View {
    let model: ParameterModel
    let spec: ParameterSpec
    var inSliderBank = false
    var legend: String?

    public init(model: ParameterModel, spec: ParameterSpec, inSliderBank: Bool = false, legend: String? = nil) {
        self.model = model; self.spec = spec; self.inSliderBank = inSliderBank; self.legend = legend
    }

    public var body: some View {
        switch ControlStyle.for(spec, inSliderBank: inSliderBank) {
        case .fader: PanelFader(model: model, spec: spec, legend: legend)
        case .knob: PanelKnob(model: model, spec: spec, legend: legend)
        case .lever: PanelLever(model: model, spec: spec, legend: legend)
        case .rotary: PanelRotary(model: model, spec: spec, legend: legend)
        }
    }
}

// MARK: - Lamps, jacks, meters

public struct PanelLED: View {
    var on: Bool
    var colour: Color = PanelTheme.lampRed
    public init(on: Bool, colour: Color = PanelTheme.lampRed) { self.on = on; self.colour = colour }
    public var body: some View {
        Circle().fill(on ? colour : colour.opacity(0.18))
            .overlay(Circle().fill(.white.opacity(on ? 0.5 : 0)).frame(width: 3, height: 3).offset(x: -1.5, y: -1.5))
            .frame(width: 9, height: 9)
            .shadow(color: on ? colour : .clear, radius: on ? 5 : 0)
            .accessibilityHidden(true)
    }
}

public struct PanelJack: View {
    let port: RackPort
    var armed = false
    var patched = false
    @Environment(\.panelWear) private var wear
    public init(port: RackPort, armed: Bool = false, patched: Bool = false) { self.port = port; self.armed = armed; self.patched = patched }

    public var body: some View {
        VStack(spacing: 2) {
            ZStack {
                Circle().fill(AngularGradient(colors: [Color(white: 0.95), Color(white: 0.54), Color(white: 0.9), Color(white: 0.44), Color(white: 0.95)], center: .center))
                Circle().fill(Color(white: 0.16)).padding(5.5)
                Circle().fill(.black).padding(6.5)
            }
            .frame(width: 22, height: 22)
            .shadow(color: armed ? PanelTheme.amber : .black, radius: armed ? 6 : 1, y: armed ? 0 : 1)
            .overlay(Circle().stroke(PanelTheme.amber, lineWidth: armed ? 2.5 : 0).padding(-3))
            Text(port.key.replacingOccurrences(of: "([A-Z])", with: " $1", options: .regularExpression).lowercased())
                .font(PanelTheme.legendFont(9.5)).lineLimit(1)
                .foregroundStyle(port.isInput ? PanelTheme.legend2 : Color.black)
                .padding(.horizontal, port.isInput ? 0 : 3)
                .background(port.isInput ? Color.clear : PanelTheme.legend(wear: wear), in: RoundedRectangle(cornerRadius: 2))
        }
        .frame(minWidth: 30, minHeight: 44)
        .contentShape(Rectangle())
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(port.name) \(port.isInput ? "input" : "output") jack")
        .accessibilityValue(patched ? "patched" : (armed ? "selected" : "empty"))
        .accessibilityAddTraits(.isButton)
    }
}

/// Needle VU meter: cream face, black scale, red zone above 0 VU. `level`
/// is a linear peak; the face reads dBFS + 6 as VU, with VU ballistics
/// (≈ 300 ms) applied by the caller's refresh rate.
public struct VUMeter: View {
    var level: Float
    var label: String
    public init(level: Float, label: String) { self.level = level; self.label = label }

    static func angle(_ db: Double) -> Double { min(max(-48 + (db + 20) / 23 * 96, -50), 50) }

    public var body: some View {
        let db = 20 * log10(Double(max(level, 1e-5))) + 6
        Canvas { gc, size in
            let w = size.width, h = size.height, c = CGPoint(x: w / 2, y: h * 0.97), r = h * 0.8
            gc.fill(Path(roundedRect: CGRect(origin: .zero, size: size), cornerRadius: 4),
                    with: .linearGradient(Gradient(colors: [Color(red: 0.965, green: 0.933, blue: 0.847), Color(red: 0.898, green: 0.851, blue: 0.725)]), startPoint: .zero, endPoint: CGPoint(x: 0, y: h)))
            func pt(_ deg: Double, _ rad: CGFloat) -> CGPoint { let a = (deg - 90) * .pi / 180; return CGPoint(x: c.x + cos(a) * rad, y: c.y + sin(a) * rad) }
            var red = Path(); red.addArc(center: c, radius: r - 2, startAngle: .degrees(Self.angle(0) - 90), endAngle: .degrees(Self.angle(3) - 90), clockwise: false)
            gc.stroke(red, with: .color(Color(red: 0.76, green: 0.16, blue: 0.12)), lineWidth: 4)
            for d in [-20.0, -10, -7, -5, -3, -1, 0, 1, 2, 3] {
                var t = Path(); t.move(to: pt(Self.angle(d), r - 6)); t.addLine(to: pt(Self.angle(d), r + 2))
                gc.stroke(t, with: .color(d > 0 ? Color(red: 0.76, green: 0.16, blue: 0.12) : Color(white: 0.13)), lineWidth: 1)
            }
            gc.draw(Text("VU").font(.system(size: 10, weight: .semibold)).foregroundColor(Color(white: 0.13)), at: CGPoint(x: c.x, y: h * 0.55))
            gc.draw(Text(label).font(.system(size: 7)).foregroundColor(Color(white: 0.33)), at: CGPoint(x: c.x, y: h * 0.72))
            var needle = Path(); needle.move(to: c); needle.addLine(to: pt(Self.angle(max(-21, db)), r + 4))
            gc.stroke(needle, with: .color(Color(white: 0.07)), lineWidth: 1.2)
            gc.fill(Path(ellipseIn: CGRect(x: c.x - 5, y: c.y - 5, width: 10, height: 10)), with: .color(Color(white: 0.07)))
            gc.stroke(Path(roundedRect: CGRect(origin: .zero, size: size).insetBy(dx: 1.5, dy: 1.5), cornerRadius: 4), with: .color(.black), lineWidth: 3)
        }
        .frame(width: 120, height: 72)
        .accessibilityElement()
        .accessibilityLabel("\(label) level")
        .accessibilityValue(db < -19 ? "below −20 VU" : String(format: "%+.0f VU", db))
    }
}

/// Red manual-gate push button (momentary).
public struct GateButton: View {
    var label: String
    var action: (Bool) -> Void
    @State private var down = false
    public init(label: String, action: @escaping (Bool) -> Void) { self.label = label; self.action = action }
    public var body: some View {
        VStack(spacing: 3) {
            Circle().fill(RadialGradient(colors: [Color(red: 1, green: 0.45, blue: 0.4), Color(red: 0.75, green: 0.1, blue: 0.08)], center: .init(x: 0.4, y: 0.35), startRadius: 0, endRadius: 14))
                .frame(width: 26, height: 26).scaleEffect(down ? 0.92 : 1).shadow(color: .black, radius: down ? 0.5 : 2, y: down ? 0.5 : 2)
                .gesture(DragGesture(minimumDistance: 0).onChanged { _ in if !down { down = true; action(true) } }.onEnded { _ in down = false; action(false) })
            LegendText(text: label)
        }
        .accessibilityElement(children: .ignore).accessibilityLabel(label).accessibilityAddTraits(.isButton)
        .accessibilityAction { action(true); action(false) }
    }
}
