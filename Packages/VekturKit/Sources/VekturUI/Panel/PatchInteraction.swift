// PatchInteraction.swift — patching shared by the rack and the patch canvas:
// connect in either direction (output → input or input → output), and the
// long-press / right-click jack menu (remove, level, invert, colour, lock,
// patch to / from any jack).
import SwiftUI
import VekturAudioUnit
import VekturCore

public enum PatchEditor {
    /// Connects two jacks given in any order. Nil if both are the same
    /// direction or the patch is full; a locked cable on the input wins.
    public static func connect(_ patch: PatchState, _ a: PortRef, aIsInput: Bool, _ b: PortRef, bIsInput: Bool) -> (PatchState, String)? {
        guard aIsInput != bIsInput else { return nil }
        let from = aIsInput ? b : a, to = aIsInput ? a : b
        var next = patch
        if let existing = next.cables.first(where: { $0.to == to }) {
            if existing.locked { return nil }
            next.cables.removeAll { $0.to == to }
        }
        guard let id = next.nextCableID else { return nil }
        next.cables.append(CableState(id: id, from: from, to: to))
        return (next, "\(RackCatalog.displayName(from, input: false)) → \(RackCatalog.displayName(to, input: true))")
    }

    public static func cables(at ref: PortRef, isInput: Bool, in patch: PatchState) -> [CableState] {
        patch.cables.filter { isInput ? $0.to == ref : $0.from == ref }
    }

    public static func edit(_ patch: PatchState, id: Int, _ change: (inout CableState) -> Void) -> PatchState {
        var next = patch
        if let i = next.cables.firstIndex(where: { $0.id == id }) { change(&next.cables[i]) }
        return next
    }
}

/// Context menu for one jack. `commit` applies a new patch (and reports).
struct JackMenuContent: View {
    let ref: PortRef
    let isInput: Bool
    let patch: PatchState
    let commit: (PatchState, String) -> Void

    static let colours: [(key: String, name: String)] = [("red", "Red"), ("yellow", "Yellow"), ("blue", "Blue"), ("green", "Green"),
                                                          ("orange", "Orange"), ("white", "White"), ("purple", "Violet"), ("black", "Black")]

    var body: some View {
        let mine = PatchEditor.cables(at: ref, isInput: isInput, in: patch)
        Text(RackCatalog.displayName(ref, input: isInput) + (isInput ? " input" : " output"))
        ForEach(mine) { c in
            let other = isInput ? RackCatalog.displayName(c.from, input: false) : RackCatalog.displayName(c.to, input: true)
            Menu(isInput ? "From \(other)" : "To \(other)") {
                Button(role: .destructive) {
                    commit(patch.removing(c.id), "Removed cable")
                } label: { Label(isInput ? "Remove (restore default connection)" : "Remove", systemImage: "scissors") }
                .disabled(c.locked)
                Picker("Level", selection: Binding(get: { c.gain }, set: { g in commit(PatchEditor.edit(patch, id: c.id) { $0.gain = g }, "Cable level") })) {
                    Text("100 %").tag(1.0); Text("50 %").tag(0.5); Text("25 %").tag(0.25); Text("Inverted").tag(-1.0)
                }
                Picker("Colour", selection: Binding(get: { c.colour ?? "" }, set: { k in commit(PatchEditor.edit(patch, id: c.id) { $0.colour = k.isEmpty ? nil : k }, "Cable colour") })) {
                    Text("Automatic").tag("")
                    ForEach(Self.colours, id: \.key) { Text($0.name).tag($0.key) }
                }
                Toggle("Locked", isOn: Binding(get: { c.locked }, set: { l in commit(PatchEditor.edit(patch, id: c.id) { $0.locked = l }, l ? "Cable locked" : "Cable unlocked") }))
            }
        }
        if mine.filter({ !$0.locked }).count > 1 {
            Button("Remove all \(mine.count) cables", role: .destructive) {
                var p = patch; p.cables.removeAll { c in mine.contains { $0.id == c.id } && !c.locked }; commit(p, "Removed cables")
            }
        }
        Menu(isInput ? "Patch from…" : "Patch to…") {
            ForEach(RackCatalog.patchableModules) { m in
                let ports = isInput ? m.outputs : m.inputs
                if !ports.isEmpty {
                    Menu(m.name) {
                        ForEach(ports) { p in
                            Button(p.name) {
                                let other = PortRef(module: m.key, port: p.key)
                                if let r = PatchEditor.connect(patch, ref, aIsInput: isInput, other, bIsInput: !isInput) { commit(r.0, r.1) }
                            }
                        }
                    }
                }
            }
        }
    }
}

extension PatchState {
    func removing(_ id: Int) -> PatchState { var p = self; p.cables.removeAll { $0.id == id && !$0.locked }; return p }
}

/// The DMT Cymatics logo (bundled image).
public struct BrandLogo: View {
    var size: CGFloat
    public init(size: CGFloat = 32) { self.size = size }
    public var body: some View {
        Image("BrandLogo", bundle: .module)
            .resizable().interpolation(.high).scaledToFit()
            .frame(width: size, height: size)
            .accessibilityLabel("DMT Cymatics")
    }
}
