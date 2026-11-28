// PatchBayView.swift — PHASE 3A list-based patch bay for the development
// console: add, adjust and remove cables, with live signal level per cable.
// The animated patch canvas (Phase 3B) replaces this as the main patching UI;
// both edit the same PatchState through VekturModularAudioUnit.applyPatch.
import SwiftUI
import VekturAudioUnit
import VekturCore

struct PatchBayView: View {
    let audioUnit: VekturModularAudioUnit

    @State private var patch = PatchState.empty
    @State private var from: PortRef?
    @State private var to: PortRef?
    @State private var message: String?

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                portMenu(title: from.map { RackCatalog.displayName($0, input: false) } ?? "From output…", input: false) { from = $0 }
                Image(systemName: "arrow.right").foregroundStyle(.secondary)
                portMenu(title: to.map { RackCatalog.displayName($0, input: true) } ?? "To input…", input: true) { to = $0 }
                Button("Connect") { connect() }
                    .disabled(from == nil || to == nil)
            }
            if let message { Text(message).font(.caption).foregroundStyle(.red) }

            if patch.cables.isEmpty {
                Text("No cables: the normalled voice is playing. Patching an input replaces its default connection.")
                    .font(.caption).foregroundStyle(.secondary)
            }
            TimelineView(.periodic(from: .now, by: 1.0 / 15.0)) { _ in
                VStack(spacing: 6) {
                    ForEach(patch.cables) { cable in row(cable) }
                }
            }
        }
        .onAppear { patch = audioUnit.currentPatch }
    }

    private func row(_ cable: CableState) -> some View {
        HStack(spacing: 8) {
            Circle().fill(level(cable) > 0.001 ? Color.green : Color.gray.opacity(0.4)).frame(width: 8, height: 8)
                .accessibilityLabel(level(cable) > 0.001 ? "Signal" : "No signal")
            VStack(alignment: .leading, spacing: 2) {
                Text(RackCatalog.displayName(cable.from, input: false)).font(.caption)
                Text("→ " + RackCatalog.displayName(cable.to, input: true)).font(.caption).foregroundStyle(.secondary)
                levelBar(level(cable))
            }
            Spacer(minLength: 4)
            Slider(value: Binding(get: { cable.gain }, set: { setGain(cable.id, $0) }), in: -2...2)
                .frame(width: 110)
                .accessibilityLabel("Cable gain")
            Text(String(format: "%+.2f", cable.gain)).font(.caption.monospaced()).frame(width: 44)
            Button(role: .destructive) { remove(cable.id) } label: { Image(systemName: "trash") }
                .disabled(cable.locked)
                .accessibilityLabel("Remove cable")
        }
    }

    private func levelBar(_ l: Float) -> some View {
        GeometryReader { g in
            ZStack(alignment: .leading) {
                Capsule().fill(.quaternary)
                Capsule().fill(Color.green).frame(width: g.size.width * CGFloat(min(1, l)))
            }
        }.frame(height: 4)
    }

    private func level(_ c: CableState) -> Float { audioUnit.level(ofCable: c.id) }

    private func portMenu(title: String, input: Bool, pick: @escaping (PortRef) -> Void) -> some View {
        Menu(title) {
            ForEach(RackCatalog.patchableModules) { m in
                let ports = input ? m.inputs : m.outputs
                if !ports.isEmpty {
                    Menu(m.name) {
                        ForEach(ports) { p in
                            Button("\(p.name) (\(p.kind.label))") { pick(PortRef(module: m.key, port: p.key)) }
                        }
                    }
                }
            }
        }
        .font(.caption)
        .lineLimit(1)
    }

    private func connect() {
        guard let from, let to else { return }
        guard let id = patch.nextCableID else { message = PatchApplyResult.Problem.Reason.tooManyCables.message; return }
        var next = patch
        next.cables.append(CableState(id: id, from: from, to: to))
        commit(next, newCable: id)
    }

    private func setGain(_ id: Int, _ gain: Double) {
        var next = patch
        if let i = next.cables.firstIndex(where: { $0.id == id }) { next.cables[i].gain = gain }
        commit(next, newCable: nil)
    }

    private func remove(_ id: Int) {
        var next = patch
        next.cables.removeAll { $0.id == id }
        commit(next, newCable: nil)
    }

    private func commit(_ next: PatchState, newCable: Int?) {
        let result = audioUnit.applyPatch(next)
        if let id = newCable, let problem = result.problems.first(where: { $0.cableID == id }) {
            // Keep the saved patch identical to what the engine accepted.
            var accepted = next
            accepted.cables.removeAll { $0.id == id }
            audioUnit.applyPatch(accepted)
            patch = accepted
            message = problem.reason.message
        } else {
            patch = next
            message = result.feedbackConnections > 0 && newCable != nil
                ? "Feedback loop: a short delay and limiter keep it stable." : nil
        }
    }
}
