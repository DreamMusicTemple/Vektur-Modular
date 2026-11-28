// LinkTransport.swift — Ableton Link for the standalone app (Phase 7).
//
// The standalone app is the audio unit's host, so it supplies the host
// musical-context and transport blocks — exactly what a DAW does for the
// AUv3. With Link enabled those blocks report the Link session's tempo, beat
// (on a 4-beat quantum) and start/stop state; with Link off (or LinkKit
// absent) they report nothing and the engine runs on its own tempo.
//
// LinkKit is Ableton's iOS SDK, licensed separately (see docs/MIDI.md). It is
// not vendored: add LinkKit.xcframework to the app target and this file
// compiles the Link path in (`canImport(LinkKit)`). Link also needs the
// local-network usage string (in project.yml) and, on iOS 14+, the multicast
// networking entitlement once Apple grants it to the team.
//
// The blocks run on the render thread. LinkKit's audio-thread functions
// (capture/commit audio session state, beat/time conversion) are real-time
// safe by design; nothing here allocates or locks.
@preconcurrency import AudioToolbox
import Foundation
import Observation
#if canImport(LinkKit) && os(iOS)
import LinkKit
import UIKit
#endif

@MainActor
@Observable
public final class LinkTransport {
    /// Beats per Link phase (bar length shared with other Link apps).
    public static let quantum = 4.0
    public static let isAvailable: Bool = {
        #if canImport(LinkKit) && os(iOS)
        return true
        #else
        return false
        #endif
    }()

    public private(set) var isEnabled = false
    public private(set) var peers = 0
    public var tempo: Double = 120

    private let core = LinkCore()

    public init() {}

    /// Output latency of the audio path in seconds, so beats line up with what
    /// is heard (set when the engine starts or the route changes).
    public func setOutputLatency(_ seconds: Double) { core.setOutputLatency(seconds) }

    public func setEnabled(_ on: Bool) {
        core.setEnabled(on)
        isEnabled = core.enabled
    }

    public func setTempo(_ bpm: Double) {
        tempo = min(max(bpm, 20), 300)
        core.setTempo(tempo)
    }

    public func setPlaying(_ playing: Bool) { core.setPlaying(playing) }

    /// Call periodically (UI timer) to refresh peers / tempo.
    public func refresh() {
        peers = core.peerCount
        if let t = core.sessionTempo { tempo = t }
        isEnabled = core.enabled
    }

    /// Host blocks for the audio unit. Install before allocateRenderResources.
    nonisolated public var musicalContextBlock: AUHostMusicalContextBlock { core.musicalContextBlock }
    nonisolated public var transportStateBlock: AUHostTransportStateBlock { core.transportStateBlock }

    #if canImport(LinkKit) && os(iOS)
    /// Ableton's standard Link settings view (UIKit); present it from the UI.
    public func makeSettingsViewController() -> UIViewController? { core.settingsViewController() }
    #endif
}

/// Thread-agnostic part (render thread reads, main thread writes the Link session).
final class LinkCore: @unchecked Sendable {
    // Written on the main thread when the route changes; read on the render
    // thread. An aligned 64-bit load cannot tear on arm64 / x86-64, and a
    // momentarily stale latency only shifts the beat by a few milliseconds.
    private var latencyTicks: UInt64 = 0

    #if canImport(LinkKit) && os(iOS)
    private let link: ABLLinkRef

    init() {
        link = ABLLinkNew(120)
        ABLLinkSetStartStopSyncEnabled(link, true)
    }
    deinit { ABLLinkDelete(link) }

    var enabled: Bool { ABLLinkIsEnabled(link) }
    var peerCount: Int { ABLLinkIsConnected(link) ? 1 : 0 }   // LinkKit reports connection, not a count
    var sessionTempo: Double? {
        let s = ABLLinkCaptureAppSessionState(link)
        return ABLLinkGetTempo(s)
    }
    func setEnabled(_ on: Bool) { ABLLinkSetActive(link, on) }
    func setTempo(_ bpm: Double) {
        let s = ABLLinkCaptureAppSessionState(link)
        ABLLinkSetTempo(s, bpm, mach_absolute_time())
        ABLLinkCommitAppSessionState(link, s)
    }
    func setPlaying(_ playing: Bool) {
        let s = ABLLinkCaptureAppSessionState(link)
        // Start on the next bar line of the shared phase.
        ABLLinkSetIsPlayingAndRequestBeatAtTime(s, playing, mach_absolute_time(), 0, LinkTransport.quantum)
        ABLLinkCommitAppSessionState(link, s)
    }
    func settingsViewController() -> UIViewController? { ABLLinkSettingsViewController.instance(link) as? UIViewController }

    var musicalContextBlock: AUHostMusicalContextBlock {
        let link = self.link
        return { [unowned self] tempo, numerator, denominator, beat, sampleOffsetToNextBeat, downbeat in
            guard ABLLinkIsEnabled(link) else { return false }
            let s = ABLLinkCaptureAudioSessionState(link)
            let t = mach_absolute_time() &+ self.latencyTicks
            let b = ABLLinkBeatAtTime(s, t, LinkTransport.quantum)
            tempo?.pointee = ABLLinkGetTempo(s)
            numerator?.pointee = LinkTransport.quantum
            denominator?.pointee = 4
            beat?.pointee = b
            sampleOffsetToNextBeat?.pointee = 0
            downbeat?.pointee = (b / LinkTransport.quantum).rounded(.down) * LinkTransport.quantum
            return true
        }
    }
    var transportStateBlock: AUHostTransportStateBlock {
        let link = self.link
        return { flags, _, _, _ in
            guard ABLLinkIsEnabled(link) else { return false }
            let s = ABLLinkCaptureAudioSessionState(link)
            flags?.pointee = ABLLinkIsPlaying(s) ? [.changed, .moving] : [.changed]
            return true
        }
    }
    #else
    init() {}
    var enabled: Bool { false }
    var peerCount: Int { 0 }
    var sessionTempo: Double? { nil }
    func setEnabled(_ on: Bool) {}
    func setTempo(_ bpm: Double) {}
    func setPlaying(_ playing: Bool) {}
    /// No Link: report no host context, so the engine follows its own tempo.
    var musicalContextBlock: AUHostMusicalContextBlock { { _, _, _, _, _, _ in false } }
    var transportStateBlock: AUHostTransportStateBlock { { _, _, _, _ in false } }
    #endif

    func setOutputLatency(_ seconds: Double) {
        var info = mach_timebase_info_data_t()
        mach_timebase_info(&info)
        let ns = max(0, seconds) * 1e9
        latencyTicks = UInt64(ns * Double(info.denom) / Double(info.numer))
    }
}
