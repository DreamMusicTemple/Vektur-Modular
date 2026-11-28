// TelemetryModel.swift — drains the kernel's audio→UI telemetry queue at
// display rate. The audio thread never waits on this; if the UI stalls, the
// kernel simply drops telemetry frames.
import Foundation
import Observation
import VekturAudioUnit
import VekturDSP

@MainActor
@Observable
public final class TelemetryModel {
    public private(set) var peakL: Float = 0
    public private(set) var peakR: Float = 0
    public private(set) var rmsL: Float = 0
    public private(set) var activeVoices: Int = 0
    /// Worst render time in the last interval as a share of the buffer duration.
    public private(set) var dspLoad: Double = 0
    public private(set) var clipped = false
    public private(set) var recoveredNonFinite = false
    public private(set) var eventsDropped = false

    private let kernel: KernelHandle
    private let sampleRate: @MainActor () -> Double
    private var task: Task<Void, Never>?

    /// - Parameter refreshHz: lowered by Battery Saving Mode (Phase 10).
    public init(kernel: KernelHandle, sampleRate: @escaping @MainActor () -> Double) {
        self.kernel = kernel
        self.sampleRate = sampleRate
    }

    /// - Parameter refreshHz: nil = 30 Hz, or 15 Hz in Battery Saving Mode.
    /// One model per kernel: the telemetry queue has a single consumer, so the
    /// top bar and the screens share it (start/stop are reference-counted).
    private static var shared: [ObjectIdentifier: TelemetryModel] = [:]
    public static func shared(kernel: KernelHandle, sampleRate: @escaping @MainActor () -> Double) -> TelemetryModel {
        if let m = shared[ObjectIdentifier(kernel)] { return m }
        let m = TelemetryModel(kernel: kernel, sampleRate: sampleRate)
        shared[ObjectIdentifier(kernel)] = m
        return m
    }
    private var users = 0

    public func start(refreshHz: Double? = nil) {
        users += 1
        guard users == 1 || refreshHz != nil else { return }
        let refreshHz = refreshHz ?? (UserDefaults.standard.bool(forKey: "perf.batterySaver") ? 15 : 30)
        task?.cancel()
        let interval = Duration.milliseconds(Int(1000 / max(1, refreshHz)))
        task = Task { [weak self] in
            while !Task.isCancelled {
                self?.drain()
                try? await Task.sleep(for: interval)
            }
        }
    }

    public func stop() {
        users = max(0, users - 1)
        if users == 0 { task?.cancel(); task = nil }
    }

    private func drain() {
        var pl: Float = 0, pr: Float = 0, rl: Float = 0, worst: Double = 0
        var voices = activeVoices, flags: UInt32 = 0, any = false
        let sr = max(sampleRate(), 1)
        while let t = kernel.popTelemetry() {
            any = true
            pl = max(pl, t.peak.0); pr = max(pr, t.peak.1); rl = max(rl, t.rms.0)
            voices = Int(t.activeVoices)
            flags |= t.flags
            if t.frames > 0 {
                let budget = Double(t.frames) / sr * 1e9
                worst = max(worst, Double(t.renderNanos) / budget)
            }
        }
        // Meter ballistics: instant attack, ~300 ms fall at 30 Hz.
        peakL = any ? max(pl, peakL * 0.85) : peakL * 0.85
        peakR = any ? max(pr, peakR * 0.85) : peakR * 0.85
        rmsL = any ? rl : rmsL * 0.85
        activeVoices = voices
        dspLoad = any ? worst : dspLoad
        clipped = flags & VK_TELEMETRY_FLAG_CLIPPED != 0
        recoveredNonFinite = recoveredNonFinite || (flags & VK_TELEMETRY_FLAG_NONFINITE != 0)
        eventsDropped = eventsDropped || (flags & VK_TELEMETRY_FLAG_EVENTS_DROP != 0)
    }
}
