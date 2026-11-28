import XCTest
@testable import VekturCore

final class NoteClipTests: XCTestCase {

    func testPairsEventsAndRebasesToTheBar() {
        let ev: [(beat: Double, note: UInt8, velocity: UInt8, source: UInt8)] = [
            (9.0, 60, 100, 1), (9.5, 60, 0, 1), (9.5, 64, 90, 1), (10.25, 64, 0, 1), (11, 67, 80, 2),   // 67 never released
        ]
        let c = NoteClip(name: "Take", tempo: 120, events: ev)
        XCTAssertEqual(c.notes.count, 3)
        XCTAssertEqual(c.notes[0].beat, 1.0)          // bar 3 starts at beat 8 → rebased
        XCTAssertEqual(c.notes[0].length, 0.5)
        XCTAssertEqual(c.notes[1].note, 64)
        XCTAssertEqual(c.notes[1].length, 0.75, accuracy: 1e-9)
        XCTAssertEqual(c.notes[2].source, 2)          // open note closed at the last event
        XCTAssertEqual(c.lengthBeats, 4)
    }

    func testClipsRoundTripAndAreSanitised() throws {
        var s = InstrumentState.default
        s.clips = [NoteClip(name: "Phrase", tempo: 98, lengthBeats: 16, notes: [ClipNote(beat: 0, length: 1, note: 57, velocity: 90, source: 3)])]
        let back = try StateCodec.decode(StateCodec.encode(s)).state
        XCTAssertEqual(back.clips, s.clips)

        let bad = NoteClip(name: String(repeating: "x", count: 200), tempo: .nan, lengthBeats: .infinity,
                           notes: [ClipNote(beat: -3, length: .nan, note: 200, velocity: 0), ClipNote(beat: 2, length: 0, note: 60, velocity: 0, source: 9)]).sanitized()
        XCTAssertEqual(bad.name.count, 64)
        XCTAssertEqual(bad.tempo, 120)
        XCTAssertEqual(bad.notes.count, 1)
        XCTAssertEqual(bad.notes[0].velocity, 1)
        XCTAssertEqual(bad.notes[0].source, 3)
        XCTAssertTrue(bad.lengthBeats.isFinite)
    }

    func testOlderStateWithoutClipsLoads() throws {
        let s = try StateCodec.decode(Data(#"{"schemaVersion":1,"parameters":{}}"#.utf8)).state
        XCTAssertTrue(s.clips.isEmpty)
        let malformed = try StateCodec.decode(Data(#"{"schemaVersion":1,"parameters":{},"clips":"oops"}"#.utf8)).state
        XCTAssertTrue(malformed.clips.isEmpty)
    }

    func testPhase5ParametersAndSources() {
        XCTAssertGreaterThanOrEqual(ParameterRegistry.all.count, 581)
        XCTAssertEqual(ParameterRegistry.spec(identifier: "arp.mode")?.address, 36 * 256)
        XCTAssertEqual(ParameterRegistry.spec(identifier: "seq.step16Ratchet")?.address, 37 * 256 + 32 + 15 * 8 + 5)
        XCTAssertEqual(ParameterRegistry.spec(identifier: "gen.gate")?.address, 38 * 256 + 15)
        XCTAssertFalse(ParameterRegistry.spec(identifier: "seq.step1A")!.isModulatable)
        XCTAssertEqual(ModSource.seqRowC.rawValue, 25)
        XCTAssertEqual(ModSource.seqRowA.displayName, "Seq Row A")
        XCTAssertFalse(ModSource.seqRowB.isBipolar)
        XCTAssertFalse(ParameterRegistry.spec(identifier: "arp.seed")!.isAutomatable)
    }
}

final class EffectsStateTests: XCTestCase {
    func testPhase6Parameters() {
        XCTAssertEqual(ParameterRegistry.all.count, 737)
        XCTAssertEqual(ParameterRegistry.spec(identifier: "fx1.type")?.address, 39 * 256)
        XCTAssertEqual(ParameterRegistry.spec(identifier: "fx8.p12")?.address, 46 * 256 + 16)
        XCTAssertEqual(ParameterRegistry.spec(identifier: "fxBus.output")?.address, 47 * 256 + 11)
        XCTAssertFalse(ParameterRegistry.spec(identifier: "fx1.type")!.isAutomatable)
        XCTAssertTrue(ParameterRegistry.spec(identifier: "fx3.p5")!.isAutomatable)
        XCTAssertEqual(ParameterRegistry.spec(identifier: "fx1.type")?.valueStrings?.count, 28)
        XCTAssertEqual(ParameterRegistry.spec(identifier: "fx1.type")?.valueStrings?[9], "Convolution Reverb")
    }

    func testIRReferencesRoundTripAndSanitise() throws {
        var s = InstrumentState.default
        let hash = String(repeating: "ab", count: 32)
        s.effects.impulseResponses = [
            ImpulseResponseReference(slot: 2, sha256: hash, name: "Hall.wav", frames: 96000, sampleRate: 48000, channels: 2),
            ImpulseResponseReference(slot: 2, sha256: hash, name: "dup", frames: 1, sampleRate: 48000, channels: 2),     // duplicate slot
            ImpulseResponseReference(slot: 9, sha256: hash, name: "bad slot", frames: 1, sampleRate: 48000, channels: 1),
            ImpulseResponseReference(slot: 1, sha256: "nothex", name: "bad hash", frames: 1, sampleRate: 48000, channels: 1),
        ]
        let back = try StateCodec.decode(StateCodec.encode(s)).state
        XCTAssertEqual(back.effects.impulseResponses.count, 1)
        XCTAssertEqual(back.effects.impulseResponses.first?.name, "Hall.wav")
        XCTAssertTrue(try StateCodec.decode(Data(#"{"schemaVersion":1,"parameters":{}}"#.utf8)).state.effects.isDefault)
    }
}
