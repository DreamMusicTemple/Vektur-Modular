// PatchCanvasView.swift — PHASE 3B modular workspace.
//
// • Modules are panels on a workspace; drag a header to move (snaps to 10 pt).
// • Drag from an output jack (right side) to an input jack (left side) to patch.
// • Tap a cable to inspect it: colour, gain, lock, remove.
// • Pinch to zoom (up to 500 %) around your fingers; drag empty space to pan.
// • Cable glow travels at a speed and brightness set by the REAL signal level
//   from the engine's connection telemetry. Silent cables stay still.
// Everything is drawn in workspace coordinates inside one transform, so cable
// ends stay attached at every zoom level. Rendering never touches the audio
// thread; telemetry is read at display rate (lower in Battery Saving, Phase 10).
//
// Visual styling here is functional; the brushed-metal hardware look is Phase 8.
import SwiftUI
import VekturAudioUnit
import VekturCore

public struct PatchCanvasView: View {
    let audioUnit: VekturModularAudioUnit
    /// Display refresh for cable animation (Battery Saving lowers it).
    var refreshHz: Double

    @State private var patch = PatchState.empty
    @State private var liveZoom: CGFloat = 1        // during a pinch
    @State private var livePan: CGSize = .zero      // during a pan
    @State private var pinchAnchor: CGPoint?
    @State private var dragWire: (from: PortRef, isInput: Bool, to: CGPoint)?
    @State private var movingModule: (key: String, start: CGPoint)?
    @State private var selectedCable: Int?
    @State private var message: String?
    @State private var viewSize: CGSize = .zero

    public init(audioUnit: VekturModularAudioUnit, refreshHz: Double = 30) {
        self.audioUnit = audioUnit
        self.refreshHz = refreshHz
    }

    private var layout: PatchLayout { patch.layout }
    private var zoom: CGFloat { CGFloat(layout.zoom) * liveZoom }
    private var pan: CGSize { CGSize(width: layout.panX + livePan.width, height: layout.panY + livePan.height) }

    public var body: some View {
        VStack(spacing: 6) {
            toolbar
            GeometryReader { geo in
                ZStack(alignment: .topLeading) {
                    Color.clear.contentShape(Rectangle())
                        .gesture(panGesture)
                        .simultaneousGesture(TapGesture().onEnded { selectedCable = nil })
                    workspace
                        .scaleEffect(zoom, anchor: .topLeading)
                        .offset(pan)
                }
                .clipped()
                .gesture(pinchGesture)
                .onAppear { viewSize = geo.size; patch = audioUnit.currentPatch }
                .onChange(of: geo.size) { _, s in viewSize = s }
            }
            .background(TolexBackground())   // Phase 8: the canvas is the inside of the cabinet
            .clipShape(RoundedRectangle(cornerRadius: 8))
            if let id = selectedCable, let i = patch.cables.firstIndex(where: { $0.id == id }) {
                inspector(patch.cables[i])
            }
            if let message { Text(message).font(.caption).foregroundStyle(.orange) }
        }
    }

    // MARK: - Workspace (workspace coordinates)

    private var workspace: some View {
        let bounds = PatchGeometry.workspaceBounds(layout: layout)
        return ZStack(alignment: .topLeading) {
            ForEach(RackCatalog.patchableModules) { m in
                modulePanel(m)
                    .position(x: PatchGeometry.frame(of: m, layout: layout).midX, y: PatchGeometry.frame(of: m, layout: layout).midY)
            }
            TimelineView(.periodic(from: .now, by: 1.0 / max(1, refreshHz))) { ctx in
                Canvas { gc, _ in drawCables(gc, time: ctx.date.timeIntervalSinceReferenceDate) }
                    .allowsHitTesting(false)
            }
            .frame(width: bounds.maxX, height: bounds.maxY)
            // Cable tap targets
            Color.clear.frame(width: bounds.maxX, height: bounds.maxY)
                .contentShape(CableHitShape(patch: patch))
                .onTapGesture(coordinateSpace: .named("workspace")) { pt in selectCable(at: pt) }
        }
        .frame(width: bounds.maxX, height: bounds.maxY, alignment: .topLeading)
        .coordinateSpace(.named("workspace"))
    }

    private func modulePanel(_ m: RackModule) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            // Phase 8 panel language: brushed-aluminium header, black paint, white silkscreen.
            Text(m.name.lowercased()).font(PanelTheme.legendFont(12, weight: .bold)).tracking(0.8).lineLimit(1)
                .foregroundStyle(Color(white: 0.08))
                .frame(maxWidth: .infinity, minHeight: PatchGeometry.headerHeight, alignment: .leading)
                .padding(.horizontal, 8)
                .background(LinearGradient(colors: [Color(white: 0.86), Color(white: 0.64), Color(white: 0.76)], startPoint: .top, endPoint: .bottom))
                .gesture(moveGesture(m))
                .accessibilityLabel("\(m.name) module")
                .accessibilityHint("Drag to move")
            HStack(alignment: .top, spacing: 0) {
                VStack(alignment: .leading, spacing: 0) { ForEach(m.inputs) { p in jackRow(m, p) } }
                Spacer(minLength: 0)
                VStack(alignment: .trailing, spacing: 0) { ForEach(m.outputs) { p in jackRow(m, p) } }
            }
            Spacer(minLength: 0)
        }
        .frame(width: PatchGeometry.panelWidth, height: PatchGeometry.panelHeight(m))
        .background(PanelPaint(key: m.key))
        .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.black, lineWidth: 1))
        .clipShape(RoundedRectangle(cornerRadius: 3))
        .shadow(color: .black.opacity(0.6), radius: 4, y: 3)
    }

    private func jackRow(_ m: RackModule, _ p: RackPort) -> some View {
        let ref = PortRef(module: m.key, port: p.key)
        let label = Text(p.name.lowercased()).font(PanelTheme.legendFont(9.5)).foregroundStyle(p.isInput ? PanelTheme.legend2 : PanelTheme.legendNew).lineLimit(1)
        let jack = Circle()
            .fill(AngularGradient(colors: [Color(white: 0.95), Color(white: 0.54), Color(white: 0.9), Color(white: 0.44), Color(white: 0.95)], center: .center))
            .overlay(Circle().fill(Color.black).padding(3.5))
            .overlay(Circle().stroke(kindColour(p.kind), lineWidth: 1.5).padding(-1.5))   // signal-kind ring kept for legibility
            .frame(width: PatchGeometry.jackRadius * 2, height: PatchGeometry.jackRadius * 2)
            .contentShape(Circle().inset(by: -8))   // generous touch target
            .accessibilityElement()
            .accessibilityLabel("\(m.name), \(p.name), \(p.isInput ? "input" : "output") jack")
        return HStack(spacing: 4) {
            if p.isInput { jack.gesture(wireGesture(from: ref, isInput: true)); label } else { label; jack.gesture(wireGesture(from: ref)) }
        }
        .contextMenu {
            JackMenuContent(ref: ref, isInput: p.isInput, patch: patch) { next, note in
                let r = audioUnit.applyPatch(next)
                patch = audioUnit.currentPatch
                message = r.isClean ? note : "\(note) — \(r.problems.count) cable(s) not accepted"
            }
        }
        .frame(height: PatchGeometry.rowHeight)
        .padding(.horizontal, 5)
    }

    // MARK: - Drawing

    private func drawCables(_ gc: GraphicsContext, time: Double) {
        let opacity = layout.cableOpacity
        let width = CGFloat(layout.cableThickness)
        let patchedInputs = Set(patch.cables.map { $0.to })

        if layout.showNormals {
            for n in RackCatalog.normals where !patchedInputs.contains(n.to) {
                guard let a = PatchGeometry.jack(n.from, input: false, layout: layout),
                      let b = PatchGeometry.jack(n.to, input: true, layout: layout) else { continue }   // hidden routing nodes are skipped
                let level = audioUnit.level(ofNormal: n)
                let path = Path(PatchGeometry.cablePath(from: a, to: b))
                gc.stroke(path, with: .color(.white.opacity(0.12 * opacity)), style: StrokeStyle(lineWidth: 1, dash: [3, 4]))
                if level > 0.002 { glow(gc, path, colour: .white, level: level, width: 1.5, opacity: 0.35 * opacity, time: time) }
            }
        }
        for c in patch.cables {
            guard let a = PatchGeometry.jack(c.from, input: false, layout: layout),
                  let b = PatchGeometry.jack(c.to, input: true, layout: layout) else { continue }
            let path = Path(PatchGeometry.cablePath(from: a, to: b))
            let colour = cableColour(c)
            let selected = c.id == selectedCable
            gc.stroke(path, with: .color(.black.opacity(0.5 * opacity)), style: StrokeStyle(lineWidth: width + 2, lineCap: .round))
            gc.stroke(path, with: .color(colour.opacity(opacity)), style: StrokeStyle(lineWidth: width, lineCap: .round))
            if selected { gc.stroke(path, with: .color(.white.opacity(0.9)), style: StrokeStyle(lineWidth: 1, dash: [6, 4])) }
            glow(gc, path, colour: colour, level: audioUnit.level(ofCable: c.id), width: width, opacity: opacity, time: time)
            if c.locked { gc.fill(Path(ellipseIn: CGRect(x: a.x - 3, y: a.y - 3, width: 6, height: 6)), with: .color(.yellow)) }
        }
        if let w = dragWire, let a = PatchGeometry.jack(w.from, input: w.isInput, layout: layout) {
            gc.stroke(Path(PatchGeometry.cablePath(from: a, to: w.to)), with: .color(.white.opacity(0.7)), style: StrokeStyle(lineWidth: width, lineCap: .round, dash: [8, 5]))
        }
    }

    /// Travelling light: speed and brightness follow the measured signal level.
    /// No signal → no movement. Gate/trigger cables pulse as their level jumps.
    private func glow(_ gc: GraphicsContext, _ path: Path, colour: Color, level: Float, width: CGFloat, opacity: Double, time: Double) {
        guard level > 0.002 else { return }
        let l = Double(min(level, 1))
        let speed = 30 + 170 * l                       // points per second
        let phase = CGFloat((time * speed).truncatingRemainder(dividingBy: 40))
        var g = gc
        g.blendMode = .plusLighter
        g.stroke(path, with: .color(colour.opacity(opacity * (0.25 + 0.75 * l))),
                 style: StrokeStyle(lineWidth: width * 0.8, lineCap: .round, dash: [10, 30], dashPhase: -phase))
    }

    private func cableColour(_ c: CableState) -> Color {
        if let name = c.colour, let col = Self.palette[name] { return col }
        let kind = RackCatalog.module(key: c.from.module)?.outputs.first { $0.key == c.from.port }?.kind ?? .audio
        return kindColour(kind)
    }

    private func kindColour(_ k: PortKind) -> Color {
        switch k {
        case .audio: return .orange
        case .cv: return Color(red: 0.35, green: 0.65, blue: 1.0)
        case .gate, .trigger: return Color(red: 0.4, green: 0.85, blue: 0.45)
        }
    }

    static let palette: [String: Color] = ["red": .red, "orange": .orange, "yellow": .yellow, "green": .green,
                                           "blue": .blue, "purple": .purple, "white": .white]

    // MARK: - Gestures

    private var panGesture: some Gesture {
        DragGesture(minimumDistance: 4)
            .onChanged { v in livePan = v.translation }
            .onEnded { v in
                var l = layout; l.panX += v.translation.width; l.panY += v.translation.height
                livePan = .zero; saveLayout(l)
            }
    }

    private var pinchGesture: some Gesture {
        MagnifyGesture()
            .onChanged { v in
                if pinchAnchor == nil { pinchAnchor = v.startLocation }
                liveZoom = clampZoom(CGFloat(layout.zoom) * v.magnification) / CGFloat(layout.zoom)
                // Keep the workspace point under the fingers fixed while zooming.
                if let a = pinchAnchor {
                    let z0 = CGFloat(layout.zoom), z1 = z0 * liveZoom
                    let wx = (a.x - layout.panX) / z0, wy = (a.y - layout.panY) / z0
                    livePan = CGSize(width: a.x - wx * z1 - layout.panX, height: a.y - wy * z1 - layout.panY)
                }
            }
            .onEnded { _ in
                var l = layout
                l.zoom = Double(clampZoom(CGFloat(l.zoom) * liveZoom))
                l.panX += livePan.width; l.panY += livePan.height
                liveZoom = 1; livePan = .zero; pinchAnchor = nil
                saveLayout(l)
            }
    }

    private func clampZoom(_ z: CGFloat) -> CGFloat { min(max(z, fitZoom * 0.9), CGFloat(PatchLayout.maxZoom)) }

    /// Zoom at which the whole rack fits the view (the comfortable minimum).
    private var fitZoom: CGFloat {
        let b = PatchGeometry.workspaceBounds(layout: layout)
        guard viewSize.width > 0, b.width > 0 else { return 0.3 }
        return max(CGFloat(PatchLayout.minZoom), min(viewSize.width / b.maxX, viewSize.height / b.maxY))
    }

    private func moveGesture(_ m: RackModule) -> some Gesture {
        DragGesture(coordinateSpace: .named("workspace"))
            .onChanged { v in
                if movingModule?.key != m.key { movingModule = (m.key, PatchGeometry.origin(of: m, layout: layout)) }
                guard let start = movingModule?.start else { return }
                var p = patch
                p.layout.positions[m.key] = .init(x: (start.x + v.translation.width).rounded(toMultiple: 10),
                                                  y: (start.y + v.translation.height).rounded(toMultiple: 10))
                patch = p
            }
            .onEnded { _ in movingModule = nil; saveLayout(patch.layout) }
    }

    /// Phase 8: wires drag from outputs to inputs or from inputs to outputs.
    private func wireGesture(from ref: PortRef, isInput: Bool = false) -> some Gesture {
        DragGesture(minimumDistance: 2, coordinateSpace: .named("workspace"))
            .onChanged { v in dragWire = (ref, isInput, v.location) }
            .onEnded { v in
                dragWire = nil
                guard let target = nearestJack(to: v.location, input: !isInput) else { return }
                if isInput { connect(from: target, to: ref) } else { connect(from: ref, to: target) }
            }
    }

    private func nearestInput(to pt: CGPoint) -> PortRef? { nearestJack(to: pt, input: true) }

    private func nearestJack(to pt: CGPoint, input: Bool) -> PortRef? {
        var best: (PortRef, CGFloat)?
        for m in RackCatalog.patchableModules {
            for p in (input ? m.inputs : m.outputs) {
                let ref = PortRef(module: m.key, port: p.key)
                guard let j = PatchGeometry.jack(ref, input: input, layout: layout) else { continue }
                let d = hypot(j.x - pt.x, j.y - pt.y)
                if d < 26, d < (best?.1 ?? .greatestFiniteMagnitude) { best = (ref, d) }
            }
        }
        return best?.0
    }

    private func selectCable(at pt: CGPoint) {
        var best: (Int, CGFloat)?
        for c in patch.cables {
            guard let a = PatchGeometry.jack(c.from, input: false, layout: layout),
                  let b = PatchGeometry.jack(c.to, input: true, layout: layout) else { continue }
            let d = PatchGeometry.distance(pt, toCableFrom: a, to: b)
            if d < 12, d < (best?.1 ?? .greatestFiniteMagnitude) { best = (c.id, d) }
        }
        selectedCable = best?.0
    }

    // MARK: - Editing

    private func connect(from: PortRef, to: PortRef) {
        var next = patch
        if let existing = next.cables.first(where: { $0.to == to }) {
            if existing.locked { message = "That input has a locked cable."; return }
            next.cables.removeAll { $0.to == to }          // re-patching an input replaces its cable
        }
        guard let id = next.nextCableID else { message = PatchApplyResult.Problem.Reason.tooManyCables.message; return }
        next.cables.append(CableState(id: id, from: from, to: to))
        let r = audioUnit.applyPatch(next)
        if let problem = r.problems.first(where: { $0.cableID == id }) {
            message = problem.reason.message
            audioUnit.applyPatch(patch)                    // restore what was there
        } else {
            patch = next
            message = r.feedbackConnections > 0 ? "Feedback loop: kept stable with a short delay and limiter." : nil
        }
    }

    private func update(_ id: Int, _ change: (inout CableState) -> Void) {
        var next = patch
        guard let i = next.cables.firstIndex(where: { $0.id == id }) else { return }
        change(&next.cables[i])
        audioUnit.applyPatch(next)
        patch = next
    }

    private func saveLayout(_ l: PatchLayout) {
        patch.layout = l.sanitized()
        audioUnit.updateLayout(patch.layout)
    }

    // MARK: - Chrome

    private var toolbar: some View {
        HStack(spacing: 10) {
            Button { fit() } label: { Label("Fit", systemImage: "arrow.down.right.and.arrow.up.left") }
            Button { var l = layout; l.zoom = 1; l.panX = 0; l.panY = 0; saveLayout(l) } label: { Label("100 %", systemImage: "1.magnifyingglass") }
            Text("\(Int((zoom * 100).rounded())) %").font(.caption.monospaced()).frame(width: 46)
            Divider().frame(height: 18)
            Image(systemName: "circle.lefthalf.filled").foregroundStyle(.secondary).accessibilityHidden(true)
            Slider(value: Binding(get: { layout.cableOpacity }, set: { var l = layout; l.cableOpacity = $0; saveLayout(l) }), in: 0.1...1)
                .frame(maxWidth: 110).accessibilityLabel("Cable opacity")
            Image(systemName: "lineweight").foregroundStyle(.secondary).accessibilityHidden(true)
            Slider(value: Binding(get: { layout.cableThickness }, set: { var l = layout; l.cableThickness = $0; saveLayout(l) }), in: 1...8)
                .frame(maxWidth: 90).accessibilityLabel("Cable thickness")
            Toggle("Defaults", isOn: Binding(get: { layout.showNormals }, set: { var l = layout; l.showNormals = $0; saveLayout(l) }))
                .toggleStyle(.button).font(.caption)
        }
        .buttonStyle(.bordered)
        .font(.caption)
    }

    private func fit() {
        let b = PatchGeometry.workspaceBounds(layout: layout)
        var l = layout
        l.zoom = Double(fitZoom)
        l.panX = Double((viewSize.width - b.maxX * fitZoom) / 2)
        l.panY = 0
        saveLayout(l)
    }

    private func inspector(_ c: CableState) -> some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 2) {
                Text(RackCatalog.displayName(c.from, input: false)).font(.caption)
                Text("→ " + RackCatalog.displayName(c.to, input: true)).font(.caption).foregroundStyle(.secondary)
            }
            Spacer()
            Menu {
                Button("Automatic (by signal type)") { update(c.id) { $0.colour = nil } }
                ForEach(Self.palette.keys.sorted(), id: \.self) { name in Button(name.capitalized) { update(c.id) { $0.colour = name } } }
            } label: { Label("Colour", systemImage: "paintpalette") }
            Slider(value: Binding(get: { c.gain }, set: { g in update(c.id) { $0.gain = g } }), in: -2...2).frame(width: 110)
                .accessibilityLabel("Cable gain")
            Text(String(format: "%+.2f", c.gain)).font(.caption.monospaced())
            Toggle(isOn: Binding(get: { c.locked }, set: { v in update(c.id) { $0.locked = v } })) { Image(systemName: c.locked ? "lock" : "lock.open") }
                .toggleStyle(.button).accessibilityLabel(c.locked ? "Unlock cable" : "Lock cable")
            Button(role: .destructive) {
                var next = patch; next.cables.removeAll { $0.id == c.id }
                audioUnit.applyPatch(next); patch = next; selectedCable = nil
            } label: { Image(systemName: "trash") }
                .disabled(c.locked).accessibilityLabel("Remove cable")
        }
        .buttonStyle(.bordered)
        .font(.caption)
        .padding(8)
        .background(.quaternary, in: RoundedRectangle(cornerRadius: 8))
    }
}

/// Hit shape covering only the cables, so taps elsewhere fall through to panels.
private struct CableHitShape: Shape {
    let patch: PatchState
    func path(in rect: CGRect) -> Path {
        var p = Path()
        for c in patch.cables {
            guard let a = PatchGeometry.jack(c.from, input: false, layout: patch.layout),
                  let b = PatchGeometry.jack(c.to, input: true, layout: patch.layout) else { continue }
            p.addPath(Path(PatchGeometry.cablePath(from: a, to: b).copy(strokingWithWidth: 22, lineCap: .round, lineJoin: .round, miterLimit: 4)))
        }
        return p
    }
}

private extension CGFloat {
    func rounded(toMultiple m: CGFloat) -> CGFloat { (self / m).rounded() * m }
}
