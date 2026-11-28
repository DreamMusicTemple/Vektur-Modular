// RackView.swift — Phase 8 main rack: cabinets of module panels on
// brushed rails in a tolex case (layout from Panels.json), with patch
// cables drooping between jacks.
//
// Patching on the rack:
//   • drag from any jack to a jack of the other kind (output → input or
//     input → output); a live cable follows the finger and the target lights;
//   • or tap one jack, then the other (also the VoiceOver path);
//   • long-press (right-click on Mac) a jack for its options: remove,
//     level / invert, colour, lock, patch to / from any jack.
// New cables drop in and settle on a damped spring, sway gently, and show
// signal flow; all motion follows Reduce Motion. The cables are the same
// PatchState the patch canvas edits.
import SwiftUI
import VekturAudioUnit
import VekturCore

public struct RackView: View {
    let audioUnit: VekturModularAudioUnit
    var layout: PanelLayout
    @State private var models: PanelModels
    @State private var patch: PatchState
    @State private var jacks = JackState()
    @State private var wear: Double
    @State private var moduleWear: [String: Double]
    @State private var message: String?
    @State private var frames: [String: CGRect] = [:]
    @State private var drag: (from: PortRef, isInput: Bool, point: CGPoint)?
    @State private var springs = CableSprings()

    public init(audioUnit: VekturModularAudioUnit, layout: PanelLayout = .standard) {
        self.audioUnit = audioUnit
        self.layout = layout
        _models = State(initialValue: PanelModels(audioUnit: audioUnit, identifiers: layout.modules.flatMap(\.parameters)))
        _patch = State(initialValue: audioUnit.currentPatch)
        _wear = State(initialValue: audioUnit.appearance.wear)
        _moduleWear = State(initialValue: audioUnit.appearance.moduleWear)
    }

    @Environment(\.vkBatterySaver) private var batterySaver

    public var body: some View {
        ScrollView(.vertical) {
            VStack(alignment: .leading, spacing: 18) {
                ForEach(layout.rows) { row in cabinet(row) }
                if let message { Text(message).font(.footnote).foregroundStyle(PanelTheme.legend2).padding(.horizontal) }
            }
            .padding(.vertical, 12)
            .coordinateSpace(.named("rack"))
            .onPreferenceChange(JackFrameKey.self) { frames = $0 }
            .overlay {
                TimelineView(.animation(minimumInterval: 1.0 / PerformanceProfile.refreshHz(batterySaver: batterySaver))) { ctx in
                    CableLayer(patch: patch, frames: frames, drag: drag.map { (frames[JackFrameKey.id($0.from, input: $0.isInput)]?.center, $0.point, target(for: $0) != nil) },
                               opacity: patch.layout.cableOpacity, thickness: max(3, patch.layout.cableThickness),
                               level: { audioUnit.level(ofCable: $0) }, springs: springs, time: ctx.date.timeIntervalSinceReferenceDate)
                }
                .allowsHitTesting(false)
            }
        }
        .background(PanelTheme.room)
        .onReceive(NotificationCenter.default.publisher(for: VekturModularAudioUnit.appearanceDidChange, object: audioUnit)) { _ in
            wear = audioUnit.appearance.wear; moduleWear = audioUnit.appearance.moduleWear
        }
        .onAppear { refreshPatch() }
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Rack")
    }

    private func cabinet(_ row: PanelRow) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            Text(row.name).font(PanelTheme.legendFont(12, weight: .semibold)).tracking(1)
                .foregroundStyle(Color(white: 0.55)).padding(.leading, 22).padding(.bottom, 6)
                .accessibilityAddTraits(.isHeader)
            ScrollView(.horizontal) {
                VStack(spacing: 0) {
                    RackRail()
                    HStack(spacing: 0) {
                        ForEach(row.modules) { m in
                            ModulePanelView(module: m, models: models, jacks: jacks, lamp: { lamp(m.key) }, actions: actions)
                                .environment(\.panelWear, moduleWear[m.key] ?? wear)
                        }
                    }
                    RackRail()
                }
                .environment(\.panelWear, wear)
                .padding(.horizontal, 18).padding(.vertical, 10)
                .background(TolexBackground().clipShape(RoundedRectangle(cornerRadius: 10)))
                .shadow(color: .black.opacity(0.7), radius: 16, y: 12)
                .padding(.horizontal, 12)
            }
            .scrollIndicators(.visible)
            .scrollDisabled(drag != nil)
        }
    }

    private var actions: JackActions {
        var a = JackActions()
        a.tap = jackTapped
        a.drag = { ref, isInput, point in
            if let point { drag = (ref, isInput, point); jacks.armed = nil; return }
            if let d = drag, let t = target(for: d) { connect(d.from, d.isInput, t.ref, t.isInput) }
            drag = nil
        }
        a.menu = { ref, isInput in
            AnyView(JackMenuContent(ref: ref, isInput: isInput, patch: patch) { p, note in commit(p, note: note) })
        }
        return a
    }

    /// The jack of the other kind under the dragged cable's end, if any.
    private func target(for d: (from: PortRef, isInput: Bool, point: CGPoint)) -> (ref: PortRef, isInput: Bool)? {
        var best: (PortRef, Bool, CGFloat)?
        for (key, rect) in frames {
            let isInput = key.hasPrefix("in:")
            guard isInput != d.isInput else { continue }
            let dist = hypot(rect.midX - d.point.x, rect.midY - d.point.y)
            guard dist < 28, dist < (best?.2 ?? .greatestFiniteMagnitude) else { continue }
            let body = key.dropFirst(isInput ? 3 : 4)
            guard let dot = body.firstIndex(of: ".") else { continue }
            best = (PortRef(module: String(body[..<dot]), port: String(body[body.index(after: dot)...])), isInput, dist)
        }
        return best.map { ($0.0, $0.1) }
    }

    private func lamp(_ key: String) -> Bool {
        if key.hasPrefix("lfo"), let spec = ParameterRegistry.spec(identifier: "\(key).rate") {
            let hz = Double(audioUnit.kernel.get(spec.address))
            return sin(Date().timeIntervalSinceReferenceDate * 2 * .pi * min(hz, 25)) > 0
        }
        return !audioUnit.heldNotes().isEmpty
    }

    // MARK: Patching

    private func refreshPatch() {
        patch = audioUnit.currentPatch
        jacks.patchedInputs = Set(patch.cables.map(\.to))
        jacks.patchedOutputs = Set(patch.cables.map(\.from))
    }

    private func jackTapped(_ ref: PortRef, _ isInput: Bool) {
        guard let armed = jacks.armed else { jacks.armed = ref; jacks.armedIsInput = isInput; return }
        let armedIsInput = jacks.armedIsInput
        jacks.armed = nil
        guard armed != ref || armedIsInput != isInput else { return }
        connect(armed, armedIsInput, ref, isInput)
    }

    private func connect(_ a: PortRef, _ aIn: Bool, _ b: PortRef, _ bIn: Bool) {
        guard aIn != bIn else { message = "Connect an output to an input."; return }
        guard let result = PatchEditor.connect(patch, a, aIsInput: aIn, b, bIsInput: bIn) else {
            message = "That input has a locked cable, or the patch is full (\(PatchState.maxCables) cables)."; return
        }
        let (next, note) = result
        if let id = next.cables.last?.id { springs.drop(id) }
        commit(next, note: note)
    }

    private func commit(_ p: PatchState, note: String) {
        let result = audioUnit.applyPatch(p)
        refreshPatch()
        message = result.isClean ? note : "\(note) — \(result.problems.count) cable(s) not accepted by the engine"
    }
}

extension CGRect { var center: CGPoint { CGPoint(x: midX, y: midY) } }

/// Per-cable damped springs for the droop (a reference type so the canvas
/// can step it every frame without re-rendering the rack).
final class CableSprings {
    private var state: [Int: (sag: Double, vel: Double)] = [:]
    private var last: Double?
    /// A new cable starts taut and drops into place.
    func drop(_ id: Int) { state[id] = (0, 900) }
    func sag(_ id: Int, rest: Double, time: Double, reduceMotion: Bool) -> Double {
        if reduceMotion { return rest }
        var s = state[id] ?? (rest, 0)
        let dt = min(0.05, max(0, time - (last ?? time)))
        s.vel += ((rest - s.sag) * 90 - s.vel * 7) * dt
        s.sag += s.vel * dt
        state[id] = s
        return s.sag
    }
    func endFrame(_ time: Double) { last = time }
}

/// Cubic-droop cables, colour per cable (or chosen), a soft shadow, a glow
/// and moving signal dashes that follow the level, plus the live drag cable.
struct CableLayer: View {
    let patch: PatchState
    let frames: [String: CGRect]
    /// (start, current point, over a valid target)
    let drag: (CGPoint?, CGPoint, Bool)?
    var opacity: Double
    var thickness: Double
    var level: (Int) -> Float
    let springs: CableSprings
    var time: Double
    @Environment(\.accessibilityReduceMotion) private var reduceSystemMotion
    @Environment(\.vkBatterySaver) private var batterySaver
    private var reduceMotion: Bool { reduceSystemMotion || batterySaver }

    static let colours: [Color] = [Color(red: 0.85, green: 0.2, blue: 0.16), Color(red: 0.95, green: 0.76, blue: 0.19), Color(red: 0.18, green: 0.48, blue: 0.85),
                                   Color(red: 0.24, green: 0.65, blue: 0.35), Color(red: 0.94, green: 0.49, blue: 0.13), Color(red: 0.91, green: 0.9, blue: 0.87),
                                   Color(red: 0.61, green: 0.35, blue: 0.71), Color(white: 0.12)]

    static func path(_ a: CGPoint, _ b: CGPoint, sag: Double) -> Path {
        let dx = b.x - a.x
        var p = Path(); p.move(to: a)
        p.addCurve(to: b, control1: CGPoint(x: a.x + dx * 0.25, y: a.y + sag), control2: CGPoint(x: b.x - dx * 0.25, y: b.y + sag))
        return p
    }

    var body: some View {
        Canvas { gc, _ in
            for c in patch.cables {
                guard let a = frames[JackFrameKey.id(c.from, input: false)]?.center, let b = frames[JackFrameKey.id(c.to, input: true)]?.center else { continue }
                let rest = min(180, 30 + hypot(b.x - a.x, b.y - a.y) * 0.35)
                let sway = reduceMotion ? 0 : sin(time * 1.3 + Double(c.id)) * 2.5
                let p = Self.path(a, b, sag: springs.sag(c.id, rest: rest, time: time, reduceMotion: reduceMotion) + sway)
                let colour = PatchCanvasView.palette[c.colour ?? ""] ?? Self.colours[c.id % Self.colours.count]
                gc.stroke(p.offsetBy(dx: 2, dy: 4), with: .color(.black.opacity(0.35)), style: StrokeStyle(lineWidth: thickness + 2, lineCap: .round))
                let lv = Double(min(max(level(c.id) * 3, 0), 1))
                if lv > 0.02 && !reduceMotion {
                    gc.stroke(p, with: .color(colour.opacity(0.35 * lv)), style: StrokeStyle(lineWidth: thickness + 6, lineCap: .round))
                }
                gc.stroke(p, with: .color(colour.opacity(opacity)), style: StrokeStyle(lineWidth: thickness, lineCap: .round))
                if lv > 0.02 && !reduceMotion {   // signal flow: dashes running from output to input
                    gc.stroke(p, with: .color(.white.opacity(0.25 + 0.6 * lv)),
                              style: StrokeStyle(lineWidth: 2, lineCap: .round, dash: [2, 14], dashPhase: -CGFloat((time * 60).truncatingRemainder(dividingBy: 16))))
                }
                if c.gain < 0 { gc.fill(Path(ellipseIn: CGRect(x: b.x - 3, y: b.y - 16, width: 6, height: 6)), with: .color(.white)) }   // inverted mark
            }
            springs.endFrame(time)
            if let d = drag, let a = d.0 {
                let p = Self.path(a, d.1, sag: min(120, 20 + hypot(d.1.x - a.x, d.1.y - a.y) * 0.3))
                gc.stroke(p, with: .color(d.2 ? PanelTheme.lampGreen : PanelTheme.amber), style: StrokeStyle(lineWidth: thickness, lineCap: .round))
                gc.fill(Path(ellipseIn: CGRect(x: d.1.x - 7, y: d.1.y - 7, width: 14, height: 14)), with: .color(Color(white: 0.1)))
            }
        }
        .accessibilityHidden(true)
    }
}
