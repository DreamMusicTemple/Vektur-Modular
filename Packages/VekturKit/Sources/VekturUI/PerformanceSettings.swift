// PerformanceSettings.swift — Phase 10: Battery Saving Mode, Keep Screen
// Awake, Background Audio, oscillator / filter quality, and tilt-reactive
// panel highlights (CoreMotion).
//
// VekturUI is shared with the AUv3 extension, which may not touch
// UIApplication, so the app-only settings are stored here (UserDefaults keys)
// and *applied* by the app target (VekturModularApp.swift).
import Observation
import SwiftUI
import VekturAudioUnit
import VekturCore
#if canImport(CoreMotion) && os(iOS)
import CoreMotion
#endif

public enum AppSettingsKey {
    public static let batterySaver = "perf.batterySaver"
    public static let keepScreenAwake = "app.keepScreenAwake"
    /// Shared with StandaloneAudioEngine.backgroundAudioEnabled.
    public static let backgroundAudio = "settings.backgroundAudio"
    static let savedQuality = "perf.batterySaver.savedQuality"
    static let savedOversampling = "perf.batterySaver.savedOversampling"
}

// MARK: - Environment

private struct StandaloneKey: EnvironmentKey { static let defaultValue = false }
private struct BatterySaverKey: EnvironmentKey { static let defaultValue = false }
private struct TiltKeyOffset: EnvironmentKey { static let defaultValue = CGPoint.zero }
public extension EnvironmentValues {
    /// True in the standalone app (shows app-only settings).
    var vkStandalone: Bool { get { self[StandaloneKey.self] } set { self[StandaloneKey.self] = newValue } }
    /// Battery Saving Mode: lower refresh rates, no decorative motion.
    var vkBatterySaver: Bool { get { self[BatterySaverKey.self] } set { self[BatterySaverKey.self] = newValue } }
    /// Device tilt, −1…1 on each axis (zero when off or unavailable).
    var panelTilt: CGPoint { get { self[TiltKeyOffset.self] } set { self[TiltKeyOffset.self] = newValue } }
}

public enum PerformanceProfile {
    /// UI refresh for meters and cable animation.
    public static func refreshHz(batterySaver: Bool) -> Double { batterySaver ? 15 : 30 }

    /// Turns Battery Saving on or off. On: remembers the quality settings and
    /// drops to Standard oscillators and 1× filters. Off: restores them.
    @MainActor public static func setBatterySaver(_ on: Bool, audioUnit: VekturModularAudioUnit) {
        let d = UserDefaults.standard
        guard let tree = audioUnit.parameterTree,
              let q = ParameterRegistry.spec(identifier: "global.oscillatorQuality").flatMap({ tree.parameter(withAddress: $0.address) }),
              let o = ParameterRegistry.spec(identifier: "global.oversampling").flatMap({ tree.parameter(withAddress: $0.address) }) else {
            d.set(on, forKey: AppSettingsKey.batterySaver); return
        }
        if on {
            d.set(Double(q.value), forKey: AppSettingsKey.savedQuality)
            d.set(Double(o.value), forKey: AppSettingsKey.savedOversampling)
            q.setValue(0, originator: nil); o.setValue(0, originator: nil)
        } else if d.bool(forKey: AppSettingsKey.batterySaver) {
            q.setValue(Float(d.double(forKey: AppSettingsKey.savedQuality)), originator: nil)
            o.setValue(Float(d.double(forKey: AppSettingsKey.savedOversampling)), originator: nil)
        }
        d.set(on, forKey: AppSettingsKey.batterySaver)
    }
}

// MARK: - Tilt

/// Reads device attitude at 20 Hz while enabled (iOS only; zero elsewhere).
@MainActor
@Observable
public final class TiltMotion {
    public private(set) var offset: CGPoint = .zero
    #if canImport(CoreMotion) && os(iOS)
    private let manager = CMMotionManager()
    #endif
    public init() {}

    public func setEnabled(_ on: Bool) {
        #if canImport(CoreMotion) && os(iOS)
        if on, manager.isDeviceMotionAvailable, !manager.isDeviceMotionActive {
            manager.deviceMotionUpdateInterval = 1.0 / 20
            manager.startDeviceMotionUpdates(to: .main) { [weak self] m, _ in
                guard let m else { return }
                let x = max(-1, min(1, m.gravity.x * 1.6)), y = max(-1, min(1, (m.gravity.y + 0.6) * 1.6))
                MainActor.assumeIsolated {
                    guard let self else { return }
                    // light low-pass so the sheen glides
                    self.offset = CGPoint(x: self.offset.x * 0.7 + x * 0.3, y: self.offset.y * 0.7 + y * 0.3)
                }
            }
        } else if !on, manager.isDeviceMotionActive {
            manager.stopDeviceMotionUpdates(); offset = .zero
        }
        #endif
    }
}

// MARK: - Settings section

struct PerformanceSettingsSection: View {
    let audioUnit: VekturModularAudioUnit
    @Environment(\.vkStandalone) private var standalone
    @AppStorage(AppSettingsKey.batterySaver) private var batterySaver = false
    @AppStorage(AppSettingsKey.keepScreenAwake) private var keepAwake = false
    @AppStorage(AppSettingsKey.backgroundAudio) private var backgroundAudio = false
    @State private var models: PanelModels

    init(audioUnit: VekturModularAudioUnit) {
        self.audioUnit = audioUnit
        _models = State(initialValue: PanelModels(audioUnit: audioUnit, identifiers: ["global.oscillatorQuality", "global.oversampling"]))
    }

    var body: some View {
        Section {
            if let q = models.model("global.oscillatorQuality") {
                Picker("Oscillators", selection: Binding(get: { Int(q.0.value.rounded()) }, set: { q.0.value = Float($0) })) {
                    Text("Standard").tag(0); Text("HQ (2× oversampled)").tag(1)
                }
                .disabled(batterySaver)
            }
            if let o = models.model("global.oversampling") {
                Picker("Filter oversampling", selection: Binding(get: { Int(o.0.value.rounded()) }, set: { o.0.value = Float($0) })) {
                    Text("1×").tag(0); Text("2×").tag(1); Text("4×").tag(2)
                }
                .disabled(batterySaver)
            }
            Toggle("Battery Saving Mode", isOn: Binding(get: { batterySaver }, set: { PerformanceProfile.setBatterySaver($0, audioUnit: audioUnit); batterySaver = $0 }))
            if standalone {
                Toggle("Keep Screen Awake", isOn: $keepAwake)
                Toggle("Background Audio", isOn: $backgroundAudio)
            }
        } header: { Text("Performance") } footer: {
            Text(standalone
                 ? "HQ oscillators cut aliasing on high notes (roughly a quarter more engine CPU). Battery Saving lowers display refresh, stops decorative motion and uses Standard oscillators with 1× filters. Background Audio keeps playing when you switch apps so other apps can play it over MIDI."
                 : "HQ oscillators cut aliasing on high notes (roughly a quarter more engine CPU). Battery Saving lowers display refresh, stops decorative motion and uses Standard oscillators with 1× filters.")
        }
    }
}
