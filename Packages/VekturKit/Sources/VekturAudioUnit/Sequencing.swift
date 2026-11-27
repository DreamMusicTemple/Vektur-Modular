// Sequencing.swift — Phase 5 Swift surface of the arpeggiator, step
// sequencer, generative engine, note capture, MIDI export and the generative
// preset creator. The engines themselves live in the C++ kernel
// (VekturDSP/seq) and are driven entirely by ordinary parameters (modules
// 36–38), so they save, restore and automate like every other control.
import Foundation
import VekturCore
import VekturDSP

/// Live state of the note generators (for LEDs and readouts).
public struct SequencerStatus: Sendable, Equatable {
    public var sequencerStep: Int?      // 0…15
    public var arpNote: Int?
    public var generativeBar: Int?
    public var generativePhrase: Int?
    public var arpRunning: Bool
    public var sequencerRunning: Bool
    public var generativeRunning: Bool
    public var hostPlaying: Bool
    public var capturing: Bool
    public var rows: (a: Float, b: Float, c: Float)
    public var beat: Double

    public static func == (l: Self, r: Self) -> Bool {
        l.sequencerStep == r.sequencerStep && l.arpNote == r.arpNote && l.generativeBar == r.generativeBar &&
        l.generativePhrase == r.generativePhrase && l.arpRunning == r.arpRunning && l.sequencerRunning == r.sequencerRunning &&
        l.generativeRunning == r.generativeRunning && l.hostPlaying == r.hostPlaying && l.capturing == r.capturing &&
        l.rows == r.rows && l.beat == r.beat
    }
}

/// Sound categories of the generative preset creator (VKPresetCategory).
public enum GeneratedPresetCategory: Int, CaseIterable, Sendable, Identifiable {
    case bass = 0, lead, pad, pluck, keys, brass, fx, drone
    public var id: Int { rawValue }
    public var displayName: String {
        switch self {
        case .bass: return "Bass"
        case .lead: return "Lead"
        case .pad: return "Pad"
        case .pluck: return "Pluck"
        case .keys: return "Keys"
        case .brass: return "Brass"
        case .fx: return "FX"
        case .drone: return "Drone"
        }
    }
}

public struct GeneratedPresetReport: Sendable, Equatable {
    public var name: String
    public var seed: UInt32
    public var category: GeneratedPresetCategory
    /// Loudest 50 ms of the offline audition (held C3 + C4), dBFS.
    public var auditionLevel: Float
    public var attempts: Int
    public var usedFallback: Bool
}

/// Standard MIDI File export of note clips.
public enum MIDIFileWriter {
    public static func data(for clip: NoteClip) -> Data {
        var records: [VKNoteRecord] = []
        records.reserveCapacity(clip.notes.count * 2)
        for n in clip.notes {
            records.append(VKNoteRecord(beat: n.beat, note: n.note, velocity: max(n.velocity, 1), source: n.source, reserved: 0))
            records.append(VKNoteRecord(beat: n.beat + n.length, note: n.note, velocity: 0, source: n.source, reserved: 0))
        }
        return records.withUnsafeBufferPointer { buf -> Data in
            let size = vk_midi_file_write(buf.baseAddress, UInt32(buf.count), clip.tempo, nil, 0)
            var bytes = [UInt8](repeating: 0, count: Int(size))
            _ = bytes.withUnsafeMutableBufferPointer { vk_midi_file_write(buf.baseAddress, UInt32(buf.count), clip.tempo, $0.baseAddress, size) }
            return Data(bytes)
        }
    }
}

extension VekturModularAudioUnit {

    // MARK: Status

    public func sequencerStatus() -> SequencerStatus {
        var s = VKSequencerStatus()
        vk_kernel_sequencer_status(kernel.raw, &s)
        func opt(_ v: Int32) -> Int? { v >= 0 ? Int(v) : nil }
        let f = s.flags
        return SequencerStatus(sequencerStep: opt(s.seqStep), arpNote: opt(s.arpNote), generativeBar: opt(s.genBar),
                               generativePhrase: opt(s.genPhrase),
                               arpRunning: f & VK_SEQ_FLAG_ARP_RUNNING != 0, sequencerRunning: f & VK_SEQ_FLAG_SEQ_RUNNING != 0,
                               generativeRunning: f & VK_SEQ_FLAG_GEN_RUNNING != 0, hostPlaying: f & VK_SEQ_FLAG_HOST_PLAYING != 0,
                               capturing: f & VK_SEQ_FLAG_CAPTURING != 0,
                               rows: (s.rows.0, s.rows.1, s.rows.2), beat: s.beat)
    }

    // MARK: Capture (call from the main thread: single consumer)

    /// Starts or stops recording every note the instrument plays.
    public func setCapturing(_ on: Bool) { kernel.post(VK_CMD_CAPTURE, arg: on ? 1 : 0) }

    /// Notes recorded since the last call, as on/off events.
    public func drainCapturedEvents() -> [(beat: Double, note: UInt8, velocity: UInt8, source: UInt8)] {
        var out: [(beat: Double, note: UInt8, velocity: UInt8, source: UInt8)] = []
        var r = VKNoteRecord()
        while vk_kernel_pop_note_record(kernel.raw, &r) { out.append((r.beat, r.note, r.velocity, r.source)) }
        return out
    }

    /// Current tempo the generators use (host or internal).
    public var generatorTempo: Double { Double(kernel.get(UInt64(VK_P_GLOBAL_TEMPO))) }

    // MARK: Generative engine

    /// Phrase `repeat` of the generative engine from the current settings,
    /// exactly as it plays (including swing, gate and Evolve).
    public func generativePhrase(repeat index: Int = 0, tempo: Double? = nil) -> NoteClip {
        let capacity = 4096
        var recs = [VKNoteRecord](repeating: VKNoteRecord(), count: capacity)
        var beats = 0.0
        let n = recs.withUnsafeMutableBufferPointer {
            vk_kernel_generative_phrase(kernel.raw, UInt32(max(index, 0)), $0.baseAddress, UInt32(capacity), &beats)
        }
        let events = recs.prefix(Int(n)).map { (beat: $0.beat, note: $0.note, velocity: $0.velocity, source: $0.source) }
        let seed = Int(kernel.get(UInt64(VK_P_GEN_SEED)))
        return NoteClip(name: "Phrase \(seed)·\(index + 1)", tempo: tempo ?? generatorTempo, events: events,
                        lengthBeats: beats, rebaseToBar: false)
    }

    /// Chord symbol of each bar of phrase `repeat` ("Am", "F", "G7" …).
    public func generativeChords(repeat index: Int = 0) -> [String] {
        var flat = [CChar](repeating: 0, count: 96)   // 8 names × 12 bytes, as the C API lays them out
        let bars = flat.withUnsafeMutableBytes { raw -> UInt32 in
            let p = raw.baseAddress!.assumingMemoryBound(to: (CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar).self)
            return vk_kernel_generative_chords(kernel.raw, UInt32(max(index, 0)), p)
        }
        return (0..<Int(min(bars, 8))).map { b in
            let bytes = flat[(b * 12)..<(b * 12 + 12)].prefix { $0 != 0 }.map { UInt8(bitPattern: $0) }
            return String(decoding: bytes, as: UTF8.self)
        }
    }

    // MARK: Clips kept with the project

    public var clips: [NoteClip] { stateSnapshot.clips }

    /// Adds a clip to the saved state (oldest dropped beyond 16).
    public func storeClip(_ clip: NoteClip) {
        mutateState { s in
            s.clips.append(clip.sanitized())
            if s.clips.count > NoteClip.maxClips { s.clips.removeFirst(s.clips.count - NoteClip.maxClips) }
        }
    }

    public func removeClip(id: UUID) { mutateState { $0.clips.removeAll { $0.id == id } } }

    // MARK: Generative preset creator

    /// Builds a new sound for `category` from `seed` and applies it: every
    /// synthesis parameter is set and the patch returns to the normalled voice.
    /// Performance settings and the arp / sequencer / generative engines are
    /// left untouched. The same seed always gives the same sound. [main thread]
    @discardableResult
    public func generatePreset(category: GeneratedPresetCategory, seed: UInt32) -> GeneratedPresetReport {
        var values = [VKParamValue](repeating: VKParamValue(), count: 1024)
        var report = VKPresetReport()
        let n = values.withUnsafeMutableBufferPointer { vk_preset_generate(seed, UInt32(category.rawValue), $0.baseAddress, 1024, &report) }
        applyPatch(.empty)
        for v in values.prefix(Int(n)) {
            parameterTree?.parameter(withAddress: v.address)?.setValue(v.value, originator: nil)
        }
        let name = "\(category.displayName) \(seed)"
        mutateState { $0.preset = PresetReference(kind: .generated, name: name, seed: UInt64(seed)) }
        return GeneratedPresetReport(name: name, seed: seed, category: category, auditionLevel: report.rmsDb,
                                     attempts: Int(report.attempts), usedFallback: report.repaired)
    }
}
