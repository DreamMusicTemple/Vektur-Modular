// Recording.swift — Phase 9. Records the instrument's output to WAV, AIFF or
// CAF at 16-bit, 24-bit or 32-bit float, at the session rate or converted to
// 44.1 / 48 / 88.2 / 96 kHz.
//
// The audio thread only copies each block into the kernel's wait-free ring
// (Recorder.hpp, 2^19 frames ≈ 11 s at 48 kHz). A utility-QoS queue drains it
// every 40 ms, converts and writes the file, so disk and converter work never
// touch the render thread. If the writer ever falls 11 s behind, whole frames
// are dropped and counted (shown in the UI) instead of blocking audio.
import AVFoundation
import Foundation
import Observation
import VekturCore
import VekturDSP

public enum RecordingContainer: String, CaseIterable, Identifiable, Codable, Sendable {
    case wav, aiff, caf
    public var id: String { rawValue }
    public var fileExtension: String { rawValue == "aiff" ? "aif" : rawValue }
    public var displayName: String { rawValue.uppercased() }
}

public enum RecordingDepth: String, CaseIterable, Identifiable, Codable, Sendable {
    case int16, int24, float32
    public var id: String { rawValue }
    public var bits: Int { switch self { case .int16: return 16; case .int24: return 24; case .float32: return 32 } }
    public var displayName: String { switch self { case .int16: return "16-bit"; case .int24: return "24-bit"; case .float32: return "32-bit float" } }
}

public struct RecordingSettings: Codable, Sendable, Equatable {
    public var container: RecordingContainer = .wav
    public var depth: RecordingDepth = .int24
    /// 0 = the session's own rate (no conversion).
    public var sampleRate: Double = 0
    public static let rates: [Double] = [0, 44_100, 48_000, 88_200, 96_000]
    public init() {}

    public func fileSettings(rate: Double) -> [String: Any] {
        [AVFormatIDKey: kAudioFormatLinearPCM, AVSampleRateKey: rate, AVNumberOfChannelsKey: 2,
         AVLinearPCMBitDepthKey: depth.bits, AVLinearPCMIsFloatKey: depth == .float32,
         AVLinearPCMIsBigEndianKey: container == .aiff, AVLinearPCMIsNonInterleaved: false]
    }
}

/// Owns the file and converter; used only on its serial queue.
private final class RecordingWriter: @unchecked Sendable {
    let kernel: KernelHandle
    let file: AVAudioFile
    let source: AVAudioFormat
    let converter: AVAudioConverter?
    let target: AVAudioFormat
    private var interleaved = [Float](repeating: 0, count: 2 * 4096)
    private(set) var framesWritten: Int64 = 0

    init(kernel: KernelHandle, url: URL, settings: RecordingSettings, sessionRate: Double) throws {
        self.kernel = kernel
        let rate = settings.sampleRate > 0 ? settings.sampleRate : sessionRate
        source = AVAudioFormat(standardFormatWithSampleRate: sessionRate, channels: 2)!
        target = AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2)!
        file = try AVAudioFile(forWriting: url, settings: settings.fileSettings(rate: rate), commonFormat: .pcmFormatFloat32, interleaved: false)
        converter = rate == sessionRate ? nil : AVAudioConverter(from: source, to: target)
        converter?.sampleRateConverterQuality = AVAudioQuality.max.rawValue
        converter?.sampleRateConverterAlgorithm = AVSampleRateConverterAlgorithm_Mastering
    }

    /// Drains everything currently in the ring. Returns false on a write error.
    func drain() -> Bool {
        while true {
            let n = interleaved.withUnsafeMutableBufferPointer { vk_kernel_record_read(kernel.raw, $0.baseAddress, 4096) }
            if n == 0 { return true }
            guard let buf = AVAudioPCMBuffer(pcmFormat: source, frameCapacity: n) else { return false }
            buf.frameLength = n
            let l = buf.floatChannelData![0], r = buf.floatChannelData![1]
            for i in 0..<Int(n) { l[i] = interleaved[2 * i]; r[i] = interleaved[2 * i + 1] }
            do { try write(buf) } catch { return false }
        }
    }

    private func write(_ buf: AVAudioPCMBuffer) throws {
        guard let converter else { try file.write(from: buf); framesWritten += Int64(buf.frameLength); return }
        let cap = AVAudioFrameCount(Double(buf.frameLength) * target.sampleRate / source.sampleRate) + 64
        guard let out = AVAudioPCMBuffer(pcmFormat: target, frameCapacity: cap) else { return }
        var supplied = false
        var err: NSError?
        let status = converter.convert(to: out, error: &err) { _, inputStatus in
            if supplied { inputStatus.pointee = .noDataNow; return nil }
            supplied = true; inputStatus.pointee = .haveData; return buf
        }
        if status == .error, let err { throw err }
        if out.frameLength > 0 { try file.write(from: out); framesWritten += Int64(out.frameLength) }
    }

    /// Flushes the converter's tail at the end of a take.
    func finish() {
        guard let converter, let out = AVAudioPCMBuffer(pcmFormat: target, frameCapacity: 4096) else { return }
        var err: NSError?
        _ = converter.convert(to: out, error: &err) { _, s in s.pointee = .endOfStream; return nil }
        if out.frameLength > 0 { try? file.write(from: out); framesWritten += Int64(out.frameLength) }
    }
}

@MainActor
@Observable
public final class RecordingService {
    public enum State: Equatable { case idle, recording(started: Date), finished(URL), failed(String) }
    public private(set) var state: State = .idle
    public private(set) var seconds: Double = 0
    public private(set) var droppedFrames: UInt64 = 0
    public var settings = RecordingSettings() { didSet { save() } }

    private let audioUnit: VekturModularAudioUnit
    private let queue = DispatchQueue(label: "vektur.recording", qos: .utility)
    private var writer: RecordingWriter?
    private var timer: DispatchSourceTimer?
    private var ticker: Task<Void, Never>?
    private static let key = "recording.settings.v1"

    /// One recorder per audio unit (the kernel tap has a single reader); the
    /// top bar and the Mixer screen share it.
    private static var shared: [ObjectIdentifier: RecordingService] = [:]
    public static func shared(for audioUnit: VekturModularAudioUnit) -> RecordingService {
        if let s = shared[ObjectIdentifier(audioUnit)] { return s }
        let s = RecordingService(audioUnit: audioUnit)
        shared[ObjectIdentifier(audioUnit)] = s
        return s
    }

    public init(audioUnit: VekturModularAudioUnit) {
        self.audioUnit = audioUnit
        if let d = UserDefaults.standard.data(forKey: Self.key), let s = try? JSONDecoder().decode(RecordingSettings.self, from: d) { settings = s }
    }

    public var isRecording: Bool { if case .recording = state { return true }; return false }

    /// Recordings folder (Documents/Recordings; shown in Files on iOS).
    public static var folder: URL {
        let d = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0].appendingPathComponent("Recordings", isDirectory: true)
        try? FileManager.default.createDirectory(at: d, withIntermediateDirectories: true)
        return d
    }

    public func start() {
        guard !isRecording else { return }
        let rate = audioUnit.outputBusses[0].format.sampleRate
        guard rate > 0 else { state = .failed("Audio is not running"); return }
        let stamp = ISO8601DateFormatter.string(from: Date(), timeZone: .current, formatOptions: [.withFullDate, .withTime])
            .replacingOccurrences(of: ":", with: "")
        let url = Self.folder.appendingPathComponent("Vektur \(stamp).\(settings.container.fileExtension)")
        do {
            let w = try RecordingWriter(kernel: audioUnit.kernel, url: url, settings: settings, sessionRate: rate)
            guard vk_kernel_record_start(audioUnit.kernel.raw) else { state = .failed("Could not allocate the recording buffer"); return }
            writer = w
            let base = vk_kernel_record_dropped(audioUnit.kernel.raw)
            let t = DispatchSource.makeTimerSource(queue: queue)
            t.schedule(deadline: .now() + .milliseconds(40), repeating: .milliseconds(40), leeway: .milliseconds(10))
            t.setEventHandler { _ = w.drain() }
            t.resume(); timer = t
            state = .recording(started: Date()); seconds = 0; droppedFrames = 0
            ticker = Task { [weak self] in
                while !Task.isCancelled {
                    try? await Task.sleep(for: .milliseconds(250))
                    guard let self, case .recording(let s) = self.state else { return }
                    self.seconds = Date().timeIntervalSince(s)
                    self.droppedFrames = vk_kernel_record_dropped(self.audioUnit.kernel.raw) &- base
                }
            }
        } catch {
            state = .failed(error.localizedDescription)
        }
    }

    public func stop() {
        guard isRecording, let w = writer else { return }
        vk_kernel_record_stop(audioUnit.kernel.raw)
        timer?.cancel(); timer = nil; ticker?.cancel(); ticker = nil
        let url = w.file.url
        queue.async {
            _ = w.drain()       // whatever the audio thread wrote before stop
            w.finish()
            Task { @MainActor [weak self] in self?.writer = nil; self?.state = .finished(url) }
        }
    }

    public static func recordings() -> [URL] {
        let keys: [URLResourceKey] = [.creationDateKey]
        let urls = (try? FileManager.default.contentsOfDirectory(at: folder, includingPropertiesForKeys: keys)) ?? []
        return urls.filter { ["wav", "aif", "caf"].contains($0.pathExtension.lowercased()) }
            .sorted { ((try? $0.resourceValues(forKeys: Set(keys)).creationDate) ?? .distantPast) > ((try? $1.resourceValues(forKeys: Set(keys)).creationDate) ?? .distantPast) }
    }

    private func save() { if let d = try? JSONEncoder().encode(settings) { UserDefaults.standard.set(d, forKey: Self.key) } }
}
