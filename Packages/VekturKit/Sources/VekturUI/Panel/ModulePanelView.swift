// ModulePanelView.swift — one hardware module panel, drawn from its
// PanelModule description: screws, title legend, printed block diagram,
// slider bank, knobs, switches, LEDs and the jack row (from the rack
// catalogue), on seeded painted metal with wear.
import SwiftUI
import VekturAudioUnit
import VekturCore

/// Printed block diagrams: thin white boxes and arrows (our own drawings).
struct BlockDiagram: View {
    let kind: String
    @Environment(\.panelWear) private var wear

    var body: some View {
        Canvas { gc, size in
            let sx = size.width / 100, sy = size.height / 34
            let ink = GraphicsContext.Shading.color(PanelTheme.legend(wear: wear))
            func P(_ x: CGFloat, _ y: CGFloat) -> CGPoint { CGPoint(x: x * sx, y: y * sy) }
            func box(_ x: CGFloat, _ t: String, _ w: CGFloat = 26) {
                gc.stroke(Path(roundedRect: CGRect(x: x * sx, y: 6 * sy, width: w * sx, height: 20 * sy), cornerRadius: 1.5), with: ink, lineWidth: 1)
                gc.draw(Text(t).font(.system(size: 8.5)).foregroundColor(PanelTheme.legend(wear: wear)), at: P(x + w / 2, 16))
            }
            func arrow(_ x1: CGFloat, _ x2: CGFloat, y: CGFloat = 16) {
                var p = Path(); p.move(to: P(x1, y)); p.addLine(to: P(x2 - 3, y)); gc.stroke(p, with: ink, lineWidth: 1)
                var h = Path(); h.move(to: P(x2 - 4, y - 3)); h.addLine(to: P(x2, y)); h.addLine(to: P(x2 - 4, y + 3)); h.closeSubpath(); gc.fill(h, with: ink)
            }
            func poly(_ pts: [(CGFloat, CGFloat)]) { var p = Path(); p.addLines(pts.map { P($0.0, $0.1) }); gc.stroke(p, with: ink, lineWidth: 1) }
            switch kind {
            case "osc": arrow(2, 18); box(18, "∿"); arrow(44, 58); box(58, "wave", 30); arrow(88, 100)
            case "lfo": box(10, "∿"); arrow(36, 52); box(52, "lvl"); arrow(78, 96)
            case "env": poly([(8, 28), (26, 6), (42, 16), (70, 16), (92, 28)])
            case "filter": arrow(2, 16); box(16, "vcf", 30); arrow(46, 60); poly([(62, 10), (78, 10), (90, 26)]); arrow(90, 100)
            case "ladder": arrow(2, 14); box(14, "4p", 22); box(38, "▸", 18); arrow(56, 70); poly([(70, 10), (80, 10), (94, 26)])
            case "amp": arrow(2, 26); poly([(28, 6), (28, 26), (56, 16), (28, 6)]); arrow(56, 96)
            case "noise": poly([(8, 16), (12, 10), (15, 20), (19, 8), (22, 22), (26, 14), (29, 20), (33, 11), (36, 22), (40, 15)]); arrow(50, 96)
            case "ring":
                arrow(2, 30); gc.stroke(Path(ellipseIn: CGRect(x: 32 * sx, y: 6 * sy, width: 20 * sx, height: 20 * sy)), with: ink, lineWidth: 1)
                poly([(35, 9), (49, 23)]); poly([(49, 9), (35, 23)]); arrow(52, 96)
            case "mixer":
                arrow(4, 39); gc.stroke(Path(ellipseIn: CGRect(x: 39 * sx, y: 5 * sy, width: 22 * sx, height: 22 * sy)), with: ink, lineWidth: 1)
                gc.draw(Text("Σ").font(.system(size: 11)).foregroundColor(PanelTheme.legend(wear: wear)), at: P(50, 16)); arrow(61, 96)
            case "sh": poly([(8, 26), (22, 26), (22, 12), (36, 12), (36, 20), (50, 20), (50, 8), (64, 8), (64, 24), (78, 24)]); arrow(80, 98)
            case "macro": box(8, "m1", 20); box(31, "m2", 20); box(54, "m3", 20); box(77, "m4", 20)
            case "keys":
                gc.stroke(Path(CGRect(x: 8 * sx, y: 8 * sy, width: 80 * sx, height: 18 * sy)), with: ink, lineWidth: 1)
                for k in 1..<8 { poly([(8 + CGFloat(k) * 10, 8), (8 + CGFloat(k) * 10, 26)]) }
            case "comb": arrow(2, 18); box(18, "z⁻ⁿ", 30); poly([(48, 16), (58, 16), (58, 28), (33, 28), (33, 26)]); arrow(58, 96)
            case "formant": arrow(2, 14); box(14, "a", 18); box(34, "e", 18); box(54, "o", 18); arrow(72, 96)
            case "bank": for i in 0..<8 { let h = CGFloat(18 - (i % 3) * 3); gc.stroke(Path(CGRect(x: (6 + CGFloat(i) * 11) * sx, y: (26 - h) * sy, width: 8 * sx, height: h * sy)), with: ink, lineWidth: 1) }
            case "switch": arrow(2, 22); poly([(28, 16), (52, 8)]); arrow(58, 96)
            case "mult": arrow(2, 40); poly([(42, 16), (72, 6)]); poly([(42, 16), (72, 16)]); poly([(42, 16), (72, 26)])
            case "out": arrow(2, 30); poly([(34, 11), (42, 11), (52, 5), (52, 27), (42, 21), (34, 21), (34, 11)])
            default: arrow(2, 30); box(30, "f(x)", 36); arrow(66, 96)
            }
        }
        .frame(height: 34)
        .accessibilityHidden(true)
    }
}

/// The parameter models a set of panels needs, keyed by address.
@MainActor
public final class PanelModels {
    public private(set) var models: [UInt64: ParameterModel] = [:]
    public init(audioUnit: VekturModularAudioUnit, identifiers: [String]) {
        let tree = audioUnit.parameterTree
        for id in identifiers {
            guard let spec = ParameterRegistry.spec(identifier: id), models[spec.address] == nil,
                  let p = tree?.parameter(withAddress: spec.address) else { continue }
            models[spec.address] = ParameterModel(p)
        }
    }
    public func model(_ id: String) -> (ParameterModel, ParameterSpec)? {
        guard let spec = ParameterRegistry.spec(identifier: id), let m = models[spec.address] else { return nil }
        return (m, spec)
    }
}

/// Jack interaction for the rack's patching.
public struct JackState: Sendable {
    public var armed: PortRef?
    public var armedIsInput = false
    public var patchedInputs: Set<PortRef> = []
    public var patchedOutputs: Set<PortRef> = []
}

/// Frame of each jack in the rack's coordinate space ("rack"), for drawing
/// cables and hit-testing drags.
struct JackFrameKey: PreferenceKey {
    static var defaultValue: [String: CGRect] { [:] }
    static func reduce(value: inout [String: CGRect], nextValue: () -> [String: CGRect]) {
        value.merge(nextValue(), uniquingKeysWith: { $1 })
    }
    static func id(_ ref: PortRef, input: Bool) -> String { "\(input ? "in" : "out"):\(ref.module).\(ref.port)" }
}

/// What the rack needs to know from a jack.
public struct JackActions {
    public var tap: (PortRef, Bool) -> Void = { _, _ in }
    /// Drag in progress (location in "rack" space) or ended (nil).
    public var drag: (PortRef, Bool, CGPoint?) -> Void = { _, _, _ in }
    public var menu: (PortRef, Bool) -> AnyView = { _, _ in AnyView(EmptyView()) }
    public init() {}
}

public struct ModulePanelView: View {
    let module: PanelModule
    let models: PanelModels
    var jacks: JackState
    var lamp: @MainActor () -> Bool
    var actions: JackActions
    @Environment(\.panelWear) private var wear
    @State private var showPopup = false
    @Environment(\.panelPopupHost) private var popupHost

    public init(module: PanelModule, models: PanelModels, jacks: JackState = JackState(), lamp: @escaping @MainActor () -> Bool = { false }, actions: JackActions = JackActions()) {
        self.module = module; self.models = models; self.jacks = jacks; self.lamp = lamp; self.actions = actions
    }

    private var rackModule: RackModule? { module.rack.isEmpty ? nil : RackCatalog.module(key: module.rack) }

    public var body: some View {
        let width = CGFloat(module.hp) * PanelTheme.hp
        VStack(spacing: 8) {
            Text(module.title).font(PanelTheme.legendFont(15, weight: .bold)).tracking(1.3)
                .foregroundStyle(PanelTheme.legend(wear: wear)).padding(.top, 4)
                .accessibilityAddTraits(.isHeader)
            BlockDiagram(kind: module.diagram)
            if module.key.hasPrefix("lfo") || module.key.hasPrefix("eg") || module.key == "keyboard" {
                TimelineView(.periodic(from: .now, by: 0.08)) { _ in
                    PanelLED(on: lamp(), colour: module.key.hasPrefix("lfo") ? PanelTheme.lampRed : PanelTheme.lampGreen)
                }
            }
            if !module.sliders.isEmpty {
                HStack(alignment: .top, spacing: 2) {
                    ForEach(module.sliders, id: \.self) { id in
                        if let c = models.model(id) { PanelControl(model: c.0, spec: c.1, inSliderBank: true).frame(maxWidth: .infinity) }
                    }
                }
            }
            if module.popup {
                LampButton(title: "settings ▸", on: false, colour: PanelTheme.amber, showsLamp: false) {
                    if let popupHost { popupHost.present(module, models) } else { showPopup = true }
                }
                .padding(.top, 6)
                .accessibilityLabel("\(module.title) settings")
                .accessibilityAddTraits(.isButton)
                // Fallback when no MainView hosts the overlay (e.g. a panel shown on its own).
                .sheet(isPresented: $showPopup) { PanelPopupContent(module: module, models: models, close: { showPopup = false }).presentationBackground(.clear) }
            } else {
                secondaryControls
            }
            Spacer(minLength: 0)
            if let rm = rackModule, rm.isPatchable { jackRow(rm) }
        }
        .padding(.horizontal, 8).padding(.vertical, 10)
        .frame(width: width)
        .frame(minHeight: PanelTheme.panelHeight)
        .background(PanelPaint(key: module.key))
        .overlay(alignment: .topLeading) { ScrewHead(angle: Double(WearPattern.seed(module.key) % 180)).padding(5) }
        .overlay(alignment: .topTrailing) { ScrewHead(angle: Double(WearPattern.seed(module.key + "b") % 180)).padding(5) }
        .overlay(alignment: .bottomLeading) { ScrewHead(angle: 30).padding(5) }
        .overlay(alignment: .bottomTrailing) { ScrewHead(angle: 70).padding(5) }
        .accessibilityElement(children: .contain)
        .accessibilityLabel(module.title)
    }

    @ViewBuilder private var secondaryControls: some View {
        if !module.knobs.isEmpty {
            FlowGrid(minWidth: 44) { ForEach(module.knobs, id: \.self) { id in if let c = models.model(id) { PanelControl(model: c.0, spec: c.1) } } }
        }
        if !module.switches.isEmpty {
            FlowGrid(minWidth: 48) { ForEach(module.switches, id: \.self) { id in if let c = models.model(id) { PanelControl(model: c.0, spec: c.1) } } }
        }
    }

    private func jackRow(_ rm: RackModule) -> some View {
        VStack(spacing: 0) {
            Rectangle().fill(Color(white: 0.17)).frame(height: 1).padding(.bottom, 6)
            FlowGrid(minWidth: 32) {
                ForEach(rm.inputs + rm.outputs) { port in
                    let ref = PortRef(module: rm.key, port: port.key)
                    let armed = jacks.armed == ref && jacks.armedIsInput == port.isInput
                    let patched = port.isInput ? jacks.patchedInputs.contains(ref) : jacks.patchedOutputs.contains(ref)
                    PanelJack(port: port, armed: armed, patched: patched)
                        .background(GeometryReader { g in
                            Color.clear.preference(key: JackFrameKey.self, value: [JackFrameKey.id(ref, input: port.isInput): g.frame(in: .named("rack"))])
                        })
                        .onTapGesture { actions.tap(ref, port.isInput) }
                        .gesture(DragGesture(minimumDistance: 6, coordinateSpace: .named("rack"))
                            .onChanged { v in actions.drag(ref, port.isInput, v.location) }
                            .onEnded { _ in actions.drag(ref, port.isInput, nil) })
                        .contextMenu { actions.menu(ref, port.isInput) }
                        .accessibilityAction { actions.tap(ref, port.isInput) }
                        .accessibilityHint("Double-tap to select, then select a jack of the other kind to connect. Long-press for options.")
                }
            }
        }
    }
}

/// Wrapping row layout for knobs, switches and jacks.
struct FlowGrid<Content: View>: View {
    var minWidth: CGFloat
    @ViewBuilder var content: Content
    var body: some View {
        LazyVGrid(columns: [GridItem(.adaptive(minimum: minWidth), spacing: 2, alignment: .top)], alignment: .center, spacing: 8) { content }
    }
}
