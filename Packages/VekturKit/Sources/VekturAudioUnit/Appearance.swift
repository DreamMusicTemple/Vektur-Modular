// Appearance.swift — Phase 8: panel appearance (Wear) kept in the instrument
// state, so a project reopens with the same look.
import Foundation
import VekturCore

extension VekturModularAudioUnit {
    public static let appearanceDidChange = Notification.Name("VekturModularAppearanceDidChange")

    public var appearance: AppearanceState { stateSnapshot.appearance }

    public func setAppearance(_ a: AppearanceState) {
        let clean = a.sanitized()
        guard clean != appearance else { return }
        mutateState { $0.appearance = clean }
        NotificationCenter.default.post(name: Self.appearanceDidChange, object: self)
    }
}
