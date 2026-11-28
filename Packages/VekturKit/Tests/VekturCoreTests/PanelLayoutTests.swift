// PanelLayoutTests.swift — Phase 8: the bundled panel layout is complete and
// valid, wear is deterministic, appearance state round-trips and sanitises.
import XCTest
@testable import VekturCore

final class PanelLayoutTests: XCTestCase {

    func testBundledLayoutLoadsWithKnownParameters() {
        let l = PanelLayout.standard
        XCTAssertEqual(l.version, 1)
        XCTAssertGreaterThanOrEqual(l.modules.count, 30)
        XCTAssertEqual(l.unknownParameters, [], "every panel control must be a registered parameter")
        XCTAssertEqual(Set(l.modules.map(\.key)).count, l.modules.count, "panel keys are unique")
        for m in l.modules { XCTAssertGreaterThanOrEqual(m.hp, 4, m.key) }
    }

    func testCoreVoiceIsOnThePanels() {
        let ids = Set(PanelLayout.standard.modules.flatMap(\.parameters))
        for id in ["vco1.wave", "vco1.coarse", "vcfB.cutoff", "vcfB.resonance", "eg2.attack", "eg2.release", "lfo1.rate", "global.masterVolume"] {
            XCTAssertTrue(ids.contains(id), id)
        }
    }

    func testControlStyles() throws {
        let cutoff = try XCTUnwrap(ParameterRegistry.spec(identifier: "vcfB.cutoff"))
        XCTAssertEqual(ControlStyle.for(cutoff, inSliderBank: true), .fader)
        XCTAssertEqual(ControlStyle.for(cutoff, inSliderBank: false), .knob)
        XCTAssertEqual(ControlStyle.for(try XCTUnwrap(ParameterRegistry.spec(identifier: "vco1.octave")), inSliderBank: false), .rotary)
        XCTAssertEqual(ControlStyle.for(try XCTUnwrap(ParameterRegistry.spec(identifier: "global.programChange")), inSliderBank: false), .lever)
    }

    func testLegendsDropThePanelName() throws {
        XCTAssertEqual(PanelLegend.text(for: try XCTUnwrap(ParameterRegistry.spec(identifier: "vco1.pulseWidth"))), "pulse width")
        XCTAssertFalse(PanelLegend.text(for: try XCTUnwrap(ParameterRegistry.spec(identifier: "eg2.attack"))).contains("eg"))
    }

    func testWearIsDeterministicAndMatchesTheWebSeed() {
        let a = WearPattern.make(key: "vco1", width: 88, height: 430), b = WearPattern.make(key: "vco1", width: 88, height: 430)
        XCTAssertEqual(a, b)
        XCTAssertNotEqual(a, WearPattern.make(key: "vco2", width: 88, height: 430))
        XCTAssertEqual(a.scratches.count, 26); XCTAssertEqual(a.rubs.count, 10); XCTAssertEqual(a.smudges.count, 4)
        // FNV-1a("vco1") — the web build's hash() gives the same value.
        XCTAssertEqual(WearPattern.seed("vco1"), 1280795926)
        XCTAssertEqual(WearPattern.seed(""), 2166136261)
        for s in a.scratches { XCTAssertTrue((0...88).contains(s.x) && s.y >= 430 * 0.45 && s.y <= 430) }
    }

    func testAppearanceRoundTripAndSanitise() throws {
        var s = InstrumentState()
        s.appearance = AppearanceState(wear: 0.8, moduleWear: ["vco1": 0.1, "": 0.5, "eg1": .nan], tiltHighlights: false)
        let back = try StateCodec.decode(try StateCodec.encode(s)).state
        XCTAssertEqual(back.appearance.wear, 0.8)
        XCTAssertEqual(back.appearance.moduleWear, ["vco1": 0.1])
        XCTAssertFalse(back.appearance.tiltHighlights)
        XCTAssertEqual(back.appearance.wear(for: "vco1"), 0.1)
        XCTAssertEqual(back.appearance.wear(for: "vcfA"), 0.8)
        XCTAssertEqual(AppearanceState(wear: 7).sanitized().wear, 1)
        let plain = String(decoding: try StateCodec.encode(InstrumentState()), as: UTF8.self)
        XCTAssertFalse(plain.contains("\"appearance\""), "default appearance is not written")
    }

    func testKeyboardControlsLiveInAPopup() throws {
        let kb = try XCTUnwrap(PanelLayout.standard.rows.flatMap(\.modules).first { $0.key == "keyboard" })
        XCTAssertTrue(kb.popup)
        XCTAssertEqual(kb.sliders, ["global.glide"], "only the glide fader stays on the front")
        XCTAssertFalse(kb.knobs.isEmpty)
        XCTAssertEqual(PanelLayout.standard.rows.flatMap(\.modules).filter(\.popup).count, 1)
        // older layout files without the key still decode
        let old = #"{"key":"k","rack":"k","title":"t","diagram":"d","sliders":[],"knobs":[],"switches":[],"hp":4}"#
        XCTAssertFalse(try JSONDecoder().decode(PanelModule.self, from: Data(old.utf8)).popup)
    }
}
