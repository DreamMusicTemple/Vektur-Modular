// MainView.swift — Phase 8 app and AUv3 root: the thirteen screens.
//
// Regular width (iPad full screen, Split View ≥ ½, Stage Manager windows,
// external displays, Mac): a sidebar beside the screen. Compact width
// (iPhone, narrow Split View / Slide Over): a tab bar (iOS adds "More" for
// the screens that do not fit). Every screen also opens in its own window on
// iPadOS and macOS (Stage Manager, external display) via `openWindow`.
import SwiftUI
import VekturAudioUnit
import VekturCore

public enum Screen: String, CaseIterable, Identifiable, Hashable, Codable, Sendable {
    case rack, patch, performance, sequencer, arpeggiator, generative, effects, modulation, mixer, midi, presets, settings, diagnostics
    public var id: String { rawValue }
    public var title: String {
        switch self {
        case .rack: return "Rack"
        case .patch: return "Patch"
        case .performance: return "Perform"
        case .sequencer: return "Sequencer"
        case .arpeggiator: return "Arpeggiator"
        case .generative: return "Generative"
        case .effects: return "Effects"
        case .modulation: return "Modulation"
        case .mixer: return "Mixer"
        case .midi: return "MIDI"
        case .presets: return "Presets"
        case .settings: return "Settings"
        case .diagnostics: return "Diagnostics"
        }
    }
    public var symbol: String {
        switch self {
        case .rack: return "square.grid.3x3.square"
        case .patch: return "point.3.connected.trianglepath.dotted"
        case .performance: return "pianokeys"
        case .sequencer: return "slider.vertical.3"
        case .arpeggiator: return "arrow.up.and.down.text.horizontal"
        case .generative: return "sparkles"
        case .effects: return "dial.medium"
        case .modulation: return "waveform.path"
        case .mixer: return "gauge.with.needle"
        case .midi: return "cable.connector"
        case .presets: return "square.stack.3d.up"
        case .settings: return "gearshape"
        case .diagnostics: return "stethoscope"
        }
    }
    /// Screens that show the docked test keyboard underneath.
    var wantsKeyboard: Bool { [.rack, .patch, .sequencer, .arpeggiator, .generative, .effects, .modulation, .midi].contains(self) }
}

public struct MainView: View {
    let audioUnit: VekturModularAudioUnit
    let engineStatus: String?
    @State private var screen: Screen
    @AppStorage("ui.showKeyboard") private var showKeyboard = true
    @AppStorage(AppSettingsKey.batterySaver) private var batterySaver = false
    @State private var wear: Double
    @State private var tiltOn: Bool
    @State private var tilt = TiltMotion()
    @State private var helpChapter: ManualChapter?
    @State private var panelPopup = PanelPopupHost()
    let standalone: Bool
    let transport: TransportControlling?
    #if os(iOS)
    @Environment(\.horizontalSizeClass) private var sizeClass
    private var isCompact: Bool { sizeClass == .compact }
    #else
    private var isCompact: Bool { false }
    #endif

    /// - Parameter standalone: true in the app (shows app-only settings); false in the AUv3.
    /// - Parameter transport: the standalone app's clock (play / tempo in the top bar); nil inside a host.
    public init(audioUnit: VekturModularAudioUnit, engineStatus: String? = nil, initial: Screen = .rack, standalone: Bool = false,
                transport: TransportControlling? = nil) {
        self.audioUnit = audioUnit
        self.transport = transport
        self.engineStatus = engineStatus
        self.standalone = standalone
        _tiltOn = State(initialValue: audioUnit.appearance.tiltHighlights)
        _screen = State(initialValue: initial)
        _wear = State(initialValue: audioUnit.appearance.wear)
    }

    public var body: some View {
        VStack(spacing: 0) {
            TopBar(audioUnit: audioUnit, transport: transport, compact: isCompact, showKeyboard: $showKeyboard,
                   showsKeyboardToggle: screen.wantsKeyboard, help: { helpChapter = ManualLibrary.chapter(for: screen) })
                .zIndex(1)
            Group {
                if isCompact { compact } else { regular }
            }
        }
        .background(PanelTheme.room)
        .background {   // ⌘? opens contextual help even with the bar scrolled
            Button("") { helpChapter = ManualLibrary.chapter(for: screen) }.keyboardShortcut("?", modifiers: .command).hidden()
        }
        .environment(\.panelWear, wear)
        .environment(\.vkStandalone, standalone)
        .environment(\.panelPopupHost, panelPopup)
        .overlay { PanelPopupOverlay(host: panelPopup).environment(\.panelWear, wear) }   // above the top bar and keyboard, like the web dialog
        .environment(\.vkBatterySaver, batterySaver)
        .environment(\.panelTilt, tilt.offset)
        .preferredColorScheme(.dark)
        .tint(PanelTheme.amber)
        .onReceive(NotificationCenter.default.publisher(for: VekturModularAudioUnit.appearanceDidChange, object: audioUnit)) { _ in
            wear = audioUnit.appearance.wear; tiltOn = audioUnit.appearance.tiltHighlights
        }
        .onAppear { tilt.setEnabled(tiltOn && !batterySaver) }
        .onDisappear { tilt.setEnabled(false) }
        .onChange(of: tiltOn) { _, on in tilt.setEnabled(on && !batterySaver) }
        .onChange(of: batterySaver) { _, b in tilt.setEnabled(tiltOn && !b) }
        .sheet(item: $helpChapter) { c in NavigationStack { ManualView(initial: c) } }
    }

    private var regular: some View {
        NavigationSplitView {
            List(Screen.allCases, selection: Binding(get: { screen }, set: { if let s = $0 { screen = s } })) { s in
                Label(s.title, systemImage: s.symbol).tag(s)
                    .contextMenu { OpenWindowButton(screen: s) }
            }
            .navigationTitle("Screens")
            .safeAreaInset(edge: .bottom) {
                if let engineStatus { Text(engineStatus).font(.caption2.monospaced()).foregroundStyle(.secondary).padding(8) }
            }
        } detail: {
            detail(screen)
                .navigationTitle(screen.title)
                .toolbar { ToolbarItem { OpenWindowButton(screen: screen) } }
        }
    }

    private var compact: some View {
        TabView(selection: $screen) {
            ForEach(Screen.allCases) { s in
                NavigationStack {
                    detail(s).navigationTitle(s.title)
                        .toolbarTitleDisplayMode(.inline)
                }
                    .tabItem { Label(s.title, systemImage: s.symbol) }
                    .tag(s)
            }
        }
    }

    @ViewBuilder
    private func detail(_ s: Screen) -> some View {
        VStack(spacing: 0) {
            ScreenContent(screen: s, audioUnit: audioUnit, engineStatus: engineStatus)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            if s.wantsKeyboard && showKeyboard {
                TestKeyboardView(audioUnit: audioUnit, lowestNote: 48, octaves: isCompact ? 1 : 2)
                    .frame(height: isCompact ? 70 : 90)
                    .padding(6)
                    .background(LinearGradient(colors: [Color(white: 0.16), Color(white: 0.06)], startPoint: .top, endPoint: .bottom))
                    .overlay(alignment: .topTrailing) {
                        Button { withAnimation(.snappy) { showKeyboard = false } } label: { Image(systemName: "chevron.down").padding(8) }
                            .buttonStyle(.plain).background(Color(white: 0.12), in: RoundedRectangle(cornerRadius: 6))
                            .offset(y: -30).padding(.trailing, 10)
                            .accessibilityLabel("Hide keyboard")
                    }
                    .transition(.move(edge: .bottom))
            } else if s.wantsKeyboard {
                HStack { Spacer(); Button { withAnimation(.snappy) { showKeyboard = true } } label: { Label("Show keyboard", systemImage: "chevron.up") }
                    .buttonStyle(.bordered).padding(6) }
            }
        }
        .background(PanelTheme.room)
    }
}

/// The content of one screen (also the root of a screen's own window).
public struct ScreenContent: View {
    let screen: Screen
    let audioUnit: VekturModularAudioUnit
    var engineStatus: String?

    @Environment(\.vkBatterySaver) private var batterySaver

    public init(screen: Screen, audioUnit: VekturModularAudioUnit, engineStatus: String? = nil) {
        self.screen = screen; self.audioUnit = audioUnit; self.engineStatus = engineStatus
    }

    public var body: some View {
        switch screen {
        case .rack: RackView(audioUnit: audioUnit)
        case .patch: PatchCanvasView(audioUnit: audioUnit, refreshHz: PerformanceProfile.refreshHz(batterySaver: batterySaver)).padding(8)
        case .performance: PerformanceScreen(audioUnit: audioUnit)
        case .sequencer: SequencerView(audioUnit: audioUnit).padding(.horizontal)
        case .arpeggiator: ArpeggiatorScreen(audioUnit: audioUnit)
        case .generative: GenerativeScreen(audioUnit: audioUnit)
        case .effects: EffectsView(audioUnit: audioUnit).padding(.horizontal)
        case .modulation: ModulationView(audioUnit: audioUnit).padding(.horizontal)
        case .mixer: OutputScreen(audioUnit: audioUnit)
        case .midi: MIDIView(audioUnit: audioUnit).padding(.horizontal)
        case .presets: PresetsScreen(audioUnit: audioUnit)
        case .settings: SettingsScreen(audioUnit: audioUnit)
        case .diagnostics: DiagnosticsConsoleView(audioUnit: audioUnit, engineStatus: engineStatus)
        }
    }
}

/// "Open in New Window" — iPadOS Stage Manager / external display, macOS.
/// The app declares the `screen` WindowGroup; inside an AUv3 (no scenes) the
/// button is hidden.
struct OpenWindowButton: View {
    let screen: Screen
    @Environment(\.openWindow) private var openWindow
    @Environment(\.supportsMultipleWindows) private var multi
    var body: some View {
        if multi && !MainView.isAppExtension {
            Button { openWindow(id: "screen", value: screen) } label: { Label("Open in New Window", systemImage: "macwindow.badge.plus") }
        }
    }
}

extension MainView {
    static let isAppExtension = Bundle.main.bundlePath.hasSuffix(".appex")
}
