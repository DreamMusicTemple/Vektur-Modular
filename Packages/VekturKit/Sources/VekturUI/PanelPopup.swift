// PanelPopup.swift — the pop-up window for panels marked `popup` in
// tools/panels.py (the keyboard ctrl panel). It matches the web build's
// <dialog class="pop"> exactly: a centred painted panel over a dimmed, lightly
// blurred screen; title, the knobs, then the switches, each row wrapped and
// centred; a "done" button. Tap outside, "done" or Escape closes it. The same
// window on iPhone, iPad, Mac and in the AUv3.
import Observation
import SwiftUI
import VekturCore

/// Hosts the pop-up above everything (top bar and keyboard included), as the
/// web dialog does. MainView owns one and puts it in the environment.
@MainActor
@Observable
public final class PanelPopupHost {
    public private(set) var module: PanelModule?
    private(set) var models: PanelModels?
    public init() {}
    func present(_ m: PanelModule, _ models: PanelModels) { self.models = models; withAnimation(.easeOut(duration: 0.16)) { module = m } }
    public func dismiss() { withAnimation(.easeIn(duration: 0.12)) { module = nil } }
}

private struct PanelPopupHostKey: EnvironmentKey { static let defaultValue: PanelPopupHost? = nil }
public extension EnvironmentValues {
    var panelPopupHost: PanelPopupHost? { get { self[PanelPopupHostKey.self] } set { self[PanelPopupHostKey.self] = newValue } }
}

/// The full-window overlay: backdrop + centred window.
struct PanelPopupOverlay: View {
    let host: PanelPopupHost
    var body: some View {
        if let m = host.module, let models = host.models {
            ZStack {
                // ::backdrop { background: rgba(0,0,0,.55); backdrop-filter: blur(2px) }
                Rectangle().fill(.ultraThinMaterial).opacity(0.35)
                    .overlay(Color.black.opacity(0.55))
                    .ignoresSafeArea()
                    .onTapGesture { host.dismiss() }
                    .accessibilityLabel("Close \(m.title)")
                    .accessibilityAddTraits(.isButton)
                PanelPopupContent(module: m, models: models, close: host.dismiss)
                    .padding(16)
                    .transition(.scale(scale: 0.96).combined(with: .opacity))
            }
            .transition(.opacity)
            .zIndex(100)
        }
    }
}

/// The window itself (.poppanel).
struct PanelPopupContent: View {
    let module: PanelModule
    let models: PanelModels
    let close: () -> Void
    @Environment(\.panelWear) private var wear

    var body: some View {
        VStack(spacing: 0) {
            Text(module.title).font(PanelTheme.legendFont(17, weight: .bold)).tracking(1.3)
                .foregroundStyle(PanelTheme.legend(wear: wear))
                .accessibilityAddTraits(.isHeader)
            if !module.knobs.isEmpty {
                CentredWrap(hSpacing: 22, vSpacing: 14) {
                    ForEach(module.knobs, id: \.self) { id in if let c = models.model(id) { PanelControl(model: c.0, spec: c.1) } }
                }
                .padding(.vertical, 12)
            }
            if !module.switches.isEmpty {
                CentredWrap(hSpacing: 18, vSpacing: 12) {
                    ForEach(module.switches, id: \.self) { id in if let c = models.model(id) { PanelControl(model: c.0, spec: c.1) } }
                }
            }
            LampButton(title: "done", on: false, colour: PanelTheme.amber, showsLamp: false, action: close)
                .keyboardShortcut(.cancelAction)
                .padding(.top, 16)
        }
        .padding(.top, 26).padding(.horizontal, 18).padding(.bottom, 18)
        .frame(maxWidth: 560)
        .background {
            // background: #161616 + linear-gradient(rgba(255,255,255,.05), rgba(0,0,0,.2))
            RoundedRectangle(cornerRadius: 6)
                .fill(Color(red: 0.086, green: 0.086, blue: 0.086))
                .overlay(RoundedRectangle(cornerRadius: 6).fill(LinearGradient(colors: [.white.opacity(0.05), .black.opacity(0.2)], startPoint: .top, endPoint: .bottom)))
                .overlay(RoundedRectangle(cornerRadius: 6).strokeBorder(.white.opacity(0.06), lineWidth: 1))
                .shadow(color: .black.opacity(0.8), radius: 25, y: 18)
        }
        .environment(\.panelWear, wear)
        .accessibilityElement(children: .contain)
        .accessibilityAddTraits(.isModal)
        .accessibilityLabel(module.title)
        #if os(macOS)
        .onExitCommand(perform: close)
        #endif
    }
}

/// Wrapping row whose lines are centred (CSS flex-wrap + justify-content:center).
struct CentredWrap: Layout {
    var hSpacing: CGFloat
    var vSpacing: CGFloat

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let rows = lines(subviews, width: proposal.width ?? .infinity)
        let w = rows.map(\.width).max() ?? 0
        let h = rows.reduce(0) { $0 + $1.height } + vSpacing * CGFloat(max(0, rows.count - 1))
        // Shrink-wrap like the web dialog (fit-content up to its max width):
        // lines are already broken to fit the proposal, so the widest line is the width.
        return CGSize(width: w, height: h)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var y = bounds.minY
        for row in lines(subviews, width: bounds.width) {
            var x = bounds.minX + (bounds.width - row.width) / 2
            for i in row.indices {
                let s = subviews[i].sizeThatFits(.unspecified)
                subviews[i].place(at: CGPoint(x: x, y: y), proposal: ProposedViewSize(s))
                x += s.width + hSpacing
            }
            y += row.height + vSpacing
        }
    }

    private struct Line { var indices: [Int] = []; var width: CGFloat = 0; var height: CGFloat = 0 }
    private func lines(_ subviews: Subviews, width: CGFloat) -> [Line] {
        var out: [Line] = [], cur = Line()
        for (i, v) in subviews.enumerated() {
            let s = v.sizeThatFits(.unspecified)
            let w = cur.indices.isEmpty ? s.width : cur.width + hSpacing + s.width
            if !cur.indices.isEmpty && w > width { out.append(cur); cur = Line() }
            cur.width = cur.indices.isEmpty ? s.width : cur.width + hSpacing + s.width
            cur.height = max(cur.height, s.height)
            cur.indices.append(i)
        }
        if !cur.indices.isEmpty { out.append(cur) }
        return out
    }
}
