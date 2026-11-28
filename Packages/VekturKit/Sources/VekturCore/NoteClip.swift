// NoteClip.swift — a captured run of notes (Phase 5): a generative phrase or a
// recorded performance of the arpeggiator, sequencer, generative engine and
// keys. Stored in InstrumentState.clips and exported as a Standard MIDI File.
import Foundation

public struct ClipNote: Codable, Sendable, Equatable {
    /// Start, in beats from the clip start.
    public var beat: Double
    /// Duration in beats.
    public var length: Double
    public var note: UInt8
    public var velocity: UInt8
    /// 0 keys, 1 arpeggiator, 2 sequencer, 3 generative (VKNoteSource).
    public var source: UInt8

    public init(beat: Double, length: Double, note: UInt8, velocity: UInt8, source: UInt8 = 0) {
        self.beat = beat; self.length = length; self.note = note; self.velocity = velocity; self.source = source
    }
}

public struct NoteClip: Codable, Sendable, Equatable, Identifiable {
    public static let maxClips = 16
    public static let maxNotes = 8192

    public var id: UUID
    public var name: String
    public var tempo: Double
    /// Clip length in beats (a whole number of bars for phrases).
    public var lengthBeats: Double
    public var notes: [ClipNote]

    public init(id: UUID = UUID(), name: String, tempo: Double, lengthBeats: Double, notes: [ClipNote]) {
        self.id = id; self.name = name; self.tempo = tempo; self.lengthBeats = lengthBeats; self.notes = notes
    }

    /// Pairs note-on/off events (velocity 0 = off) into notes. Times are
    /// rebased so the clip starts at the bar containing the first note.
    /// Notes still open at the end close at the last event.
    public init(name: String, tempo: Double, events: [(beat: Double, note: UInt8, velocity: UInt8, source: UInt8)],
                lengthBeats: Double? = nil, rebaseToBar: Bool = true) {
        let sorted = events.enumerated().sorted { a, b in
            a.element.beat != b.element.beat ? a.element.beat < b.element.beat : a.offset < b.offset
        }.map(\.element)
        let origin: Double = rebaseToBar ? ((sorted.first { $0.velocity > 0 }?.beat).map { (($0 / 4).rounded(.down)) * 4 } ?? 0) : 0
        var open: [UInt8: [(Double, UInt8, UInt8)]] = [:]
        var out: [ClipNote] = []
        var last = 0.0
        for e in sorted {
            let t = e.beat - origin
            last = max(last, t)
            if e.velocity > 0 { open[e.note, default: []].append((t, e.velocity, e.source)) }
            else if var stack = open[e.note], !stack.isEmpty {
                let (start, vel, src) = stack.removeFirst()
                open[e.note] = stack
                out.append(ClipNote(beat: start, length: max(t - start, 1.0 / 480), note: e.note, velocity: vel, source: src))
            }
        }
        for (note, stack) in open { for (start, vel, src) in stack { out.append(ClipNote(beat: start, length: max(last - start, 1.0 / 480), note: note, velocity: vel, source: src)) } }
        out.sort { $0.beat != $1.beat ? $0.beat < $1.beat : $0.note < $1.note }
        let end = out.map { $0.beat + $0.length }.max() ?? 0
        self.init(name: name, tempo: tempo, lengthBeats: lengthBeats ?? max(4, (end / 4).rounded(.up) * 4), notes: out)
    }

    /// Finite, in range, bounded.
    public func sanitized() -> NoteClip {
        var c = self
        c.name = String(name.prefix(64))
        c.tempo = tempo.isFinite ? min(max(tempo, 20), 300) : 120
        c.notes = notes.prefix(Self.maxNotes).compactMap { n in
            guard n.beat.isFinite, n.length.isFinite, n.note < 128 else { return nil }
            return ClipNote(beat: max(0, n.beat), length: min(max(n.length, 1.0 / 480), 1024), note: n.note,
                            velocity: min(max(n.velocity, 1), 127), source: min(n.source, 3))
        }
        let end = c.notes.map { $0.beat + $0.length }.max() ?? 0
        c.lengthBeats = lengthBeats.isFinite ? min(max(lengthBeats, end, 1), 4096) : max(end, 4)
        return c
    }
}
