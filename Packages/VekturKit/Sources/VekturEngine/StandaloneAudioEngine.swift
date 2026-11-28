// StandaloneAudioEngine.swift — hosts the VekturModularAudioUnit in-process for
// the standalone app, so standalone and AUv3 share one engine and one MIDI path.
@preconcurrency import AVFoundation
import Observation
import os
import VekturAudioUnit
import VekturCore
import VekturDSP

@MainActor
@Observable
public final class StandaloneAudioEngine {

    public enum Status: Equatable { case stopped, starting, running, interrupted, failed(String) }

    public private(set) var status: Status = .stopped
    public private(set) var audioUnit: VekturModularAudioUnit?
    public private(set) var sampleRate: Double = 0
    public private(set) var ioBufferFrames: Int = 0
    /// Ableton Link (Phase 7): the standalone's host transport.
    public let link = LinkTransport()
    /// Where the audio unit's MIDI output goes (the MIDI service's virtual
    /// source). Set before `start()`; applied when the unit allocates.
    public var midiOutputBlock: AUMIDIOutputEventBlock?

    /// User setting (Settings screen arrives in Phase 8/10): keep rendering in background.
    public var backgroundAudioEnabled: Bool {
        get { UserDefaults.standard.bool(forKey: Self.backgroundKey) }
        set { UserDefaults.standard.set(newValue, forKey: Self.backgroundKey) }
    }

    private let engine = AVAudioEngine()
    private var avUnit: AVAudioUnit?
    private var observers: [NSObjectProtocol] = []
    private static let backgroundKey = "settings.backgroundAudio"
    private static let log = Logger(subsystem: "com.dreammusictemple.vekturmodular", category: "Engine")

    public init() {}

    // MARK: - Lifecycle

    public func start() async {
        guard status != .running, status != .starting else { return }
        status = .starting
        do {
            try configureSession()
            if avUnit == nil {
                VekturModularAudioUnit.registerInProcess()
                let unit = try await AVAudioUnit.instantiate(with: AudioComponentInfo.description,
                                                             options: .loadInProcess)
                guard let au = unit.auAudioUnit as? VekturModularAudioUnit else {
                    throw NSError(domain: "Vektur", code: 1,
                                  userInfo: [NSLocalizedDescriptionKey: "In-process instantiation returned an unexpected unit"])
                }
                // Act as the unit's host: musical context and transport come
                // from Link (or nothing, when Link is off), MIDI out goes to
                // the virtual source. All are read at allocateRenderResources.
                au.musicalContextBlock = link.musicalContextBlock
                au.transportStateBlock = link.transportStateBlock
                au.midiOutputEventBlock = midiOutputBlock
                engine.attach(unit)
                avUnit = unit
                audioUnit = au
                restoreStandaloneState(into: au)
                installObservers()
            }
            connectGraph()
            engine.prepare()
            try engine.start()
            refreshIOInfo()
            status = .running
        } catch {
            Self.log.error("Engine start failed: \(String(describing: error))")
            status = .failed(error.localizedDescription)
        }
    }

    public func stop() {
        engine.stop()
        if status == .running { status = .stopped }
    }

    /// Persist the standalone session (the AUv3 is persisted by its host instead).
    public func saveStandaloneState() {
        guard let au = audioUnit else { return }
        do {
            let data = try StateCodec.encode(au.captureState())
            try data.write(to: Self.stateURL, options: .atomic)
        } catch {
            Self.log.error("Saving standalone state failed: \(String(describing: error))")
        }
    }

    // MARK: - Graph

    private func connectGraph() {
        guard let unit = avUnit else { return }
        let hwRate = engine.outputNode.outputFormat(forBus: 0).sampleRate
        let rate = hwRate > 0 ? hwRate : 48_000
        let format = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2)
        engine.disconnectNodeOutput(unit)
        engine.connect(unit, to: engine.mainMixerNode, format: format)
    }

    private func refreshIOInfo() {
        sampleRate = engine.outputNode.outputFormat(forBus: 0).sampleRate
        #if os(iOS)
        let s = AVAudioSession.sharedInstance()
        ioBufferFrames = Int((s.ioBufferDuration * s.sampleRate).rounded())
        link.setOutputLatency(s.outputLatency + s.ioBufferDuration)
        #else
        link.setOutputLatency(engine.outputNode.presentationLatency)
        #endif
    }

    // MARK: - Session (iOS)

    private func configureSession() throws {
        #if os(iOS)
        let s = AVAudioSession.sharedInstance()
        try s.setCategory(.playback, mode: .default, options: [])
        try s.setPreferredIOBufferDuration(0.005)   // ~256 frames @ 48 kHz; the OS may choose otherwise
        try s.setActive(true)
        #endif
    }

    private func installObservers() {
        let nc = NotificationCenter.default
        observers.append(nc.addObserver(forName: .AVAudioEngineConfigurationChange, object: engine, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.handleConfigurationChange() }
        })
        #if os(iOS)
        observers.append(nc.addObserver(forName: AVAudioSession.interruptionNotification, object: nil, queue: .main) { [weak self] note in
            let typeRaw = note.userInfo?[AVAudioSessionInterruptionTypeKey] as? UInt
            let optsRaw = note.userInfo?[AVAudioSessionInterruptionOptionKey] as? UInt
            MainActor.assumeIsolated { self?.handleInterruption(typeRaw: typeRaw, optionsRaw: optsRaw) }
        })
        observers.append(nc.addObserver(forName: AVAudioSession.mediaServicesWereResetNotification, object: nil, queue: .main) { [weak self] _ in
            MainActor.assumeIsolated { self?.handleMediaServicesReset() }
        })
        #endif
    }

    private func handleConfigurationChange() {
        // Route or sample-rate change: the engine stopped itself. Reconnect at the
        // new hardware rate; the AU reallocates its kernel for the new format.
        Self.log.notice("Audio configuration changed; restarting engine")
        connectGraph()
        do { try engine.start(); refreshIOInfo(); status = .running }
        catch { status = .failed(error.localizedDescription) }
    }

    #if os(iOS)
    private func handleInterruption(typeRaw: UInt?, optionsRaw: UInt?) {
        guard let raw = typeRaw, let type = AVAudioSession.InterruptionType(rawValue: raw) else { return }
        switch type {
        case .began:
            status = .interrupted
            audioUnit?.kernel.post(VK_CMD_ALL_NOTES_OFF)
        case .ended:
            let opts = AVAudioSession.InterruptionOptions(rawValue: optionsRaw ?? 0)
            if opts.contains(.shouldResume) {
                do { try configureSession(); try engine.start(); status = .running }
                catch { status = .failed(error.localizedDescription) }
            }
        @unknown default: break
        }
    }

    private func handleMediaServicesReset() {
        // Every audio object is invalid after a media-services reset: rebuild.
        Self.log.notice("Media services reset; rebuilding engine")
        saveStandaloneState()
        if let unit = avUnit { engine.detach(unit) }
        avUnit = nil
        audioUnit = nil
        status = .stopped
        Task { await start() }
    }
    #endif

    // MARK: - App lifecycle

    public func appDidEnterBackground() {
        saveStandaloneState()
        if !backgroundAudioEnabled { stop() }
    }

    public func appWillEnterForeground() {
        if status != .running { Task { await start() } }
    }

    // MARK: - Standalone persistence

    private static var stateURL: URL {
        let dir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        return dir.appendingPathComponent("StandaloneSession.vekturmodular.json")
    }

    private func restoreStandaloneState(into au: VekturModularAudioUnit) {
        guard let data = try? Data(contentsOf: Self.stateURL) else { return }
        do { au.apply(try StateCodec.decode(data).state) }
        catch {
            // Keep the corrupt file for diagnosis instead of silently deleting user data.
            let bad = Self.stateURL.appendingPathExtension("corrupt-\(Int(Date().timeIntervalSince1970))")
            try? FileManager.default.moveItem(at: Self.stateURL, to: bad)
            Self.log.error("Standalone state unreadable, moved aside: \(String(describing: error))")
        }
    }
}
