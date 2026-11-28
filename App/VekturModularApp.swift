// VekturModularApp.swift — standalone application shell. Hosts the same
// VekturModularAudioUnit the AUv3 extension exposes, in-process.
import SwiftUI
import VekturEngine
import VekturUI
#if os(iOS)
import UIKit
#endif

@main
struct VekturModularApp: App {
    @State private var engine = StandaloneAudioEngine()
    @State private var midi = MIDIInputService()
    @Environment(\.scenePhase) private var scenePhase
    // Phase 10 app-only settings (stored by VekturUI's Settings screen).
    @AppStorage(AppSettingsKey.keepScreenAwake) private var keepScreenAwake = false
    @State private var awake = ScreenAwake()

    var body: some Scene {
        WindowGroup {
            RootView(engine: engine)
                .task {
                    // MIDI first: its virtual source receives the unit's MIDI out.
                    midi.start(eventList: nil, schedule: nil)
                    engine.midiOutputBlock = midi.makeOutputBlock()
                    await engine.start()
                    connectMIDI()
                    awake.set(keepScreenAwake)
                }
                .onChange(of: keepScreenAwake) { _, on in awake.set(on) }
        }
        // Phase 8: any screen in its own window (Stage Manager, external display, Mac).
        WindowGroup("Vektur Modular", id: "screen", for: Screen.self) { $screen in
            if let au = engine.audioUnit {
                ScreenContent(screen: screen ?? .rack, audioUnit: au)
                    .environment(\.panelWear, au.appearance.wear)
                    .preferredColorScheme(.dark)
            } else {
                ProgressView()
            }
        }
        .onChange(of: scenePhase) { _, phase in
            switch phase {
            case .background: engine.appDidEnterBackground()
            case .active:
                engine.appWillEnterForeground()
                connectMIDI()
                awake.set(keepScreenAwake)
            default: break
            }
        }
    }

    /// MIDI 2.0 event lists when the unit offers them, else MIDI 1.0 bytes.
    private func connectMIDI() {
        midi.start(eventList: engine.audioUnit?.scheduleMIDIEventListBlock, schedule: engine.audioUnit?.scheduleMIDIEventBlock)
    }
}

struct RootView: View {
    let engine: StandaloneAudioEngine
    @State private var transport: AppTransport

    init(engine: StandaloneAudioEngine) {
        self.engine = engine
        _transport = State(initialValue: AppTransport(link: engine.link))
    }

    var body: some View {
        Group {
            if let au = engine.audioUnit {
                MainView(audioUnit: au, engineStatus: statusText, standalone: true, transport: transport)
            } else {
                VStack(spacing: 12) {
                    ProgressView()
                    Text(statusText).font(.caption.monospaced())
                }
            }
        }
    }

    private var statusText: String {
        switch engine.status {
        case .stopped: return "stopped"
        case .starting: return "starting…"
        case .running:
            let ms = engine.sampleRate > 0 ? Double(engine.ioBufferFrames) / engine.sampleRate * 1000 : 0
            return String(format: "%.0f Hz · %d fr (%.1f ms)", engine.sampleRate, engine.ioBufferFrames, ms)
        case .interrupted: return "interrupted"
        case .failed(let msg): return "audio failed: \(msg)"
        }
    }
}

/// Keep Screen Awake (Phase 10): the idle timer on iOS, a display-sleep
/// assertion on macOS. Only the app may do this; the AUv3 never does.
@MainActor
final class ScreenAwake {
    #if os(macOS)
    private var activity: NSObjectProtocol?
    #endif
    func set(_ on: Bool) {
        #if os(iOS)
        UIApplication.shared.isIdleTimerDisabled = on
        #else
        if on, activity == nil {
            activity = ProcessInfo.processInfo.beginActivity(options: [.idleDisplaySleepDisabled, .userInitiated], reason: "Keep Screen Awake")
        } else if !on, let a = activity { ProcessInfo.processInfo.endActivity(a); activity = nil }
        #endif
    }
}

/// The top bar's play / tempo in the standalone app: the engine's internal
/// clock, shared with Ableton Link when Link is enabled.
@MainActor
final class AppTransport: TransportControlling {
    let link: LinkTransport
    private(set) var isPlaying = false
    init(link: LinkTransport) { self.link = link }
    var tempo: Double { link.tempo }
    func setTempo(_ bpm: Double) { link.setTempo(bpm) }
    func setPlaying(_ playing: Bool) { isPlaying = playing; link.setPlaying(playing) }
}
