// MIDIMappingTests.swift — Phase 7: mapping validation, scope merging,
// learn replacement, hardware profile JSON and the state's `midi` section.
import XCTest
@testable import VekturCore

final class MIDIMappingTests: XCTestCase {

    func testSanitizeDropsInvalidAndClamps() {
        let set = MIDIMappingSet(mappings: [
            MIDIMapping(source: .cc, number: 74, parameter: "vcfB.cutoff", minimum: -2, maximum: .nan),
            MIDIMapping(source: .cc, number: 200, parameter: "vcfB.cutoff"),            // bad number
            MIDIMapping(source: .cc, number: 1, parameter: "no.suchParameter"),         // unknown target
            MIDIMapping(source: .nrpn, channel: 16, number: 5, parameter: "vcfB.cutoff"), // bad channel
            MIDIMapping(source: .pitchBend, number: 9, parameter: "vcfB.resonance", sensitivity: -1),
        ]).sanitized()
        XCTAssertEqual(set.mappings.count, 2)
        XCTAssertEqual(set.mappings[0].minimum, 0); XCTAssertEqual(set.mappings[0].maximum, 1)
        XCTAssertEqual(set.mappings[1].number, 0, "bend has no number")
        XCTAssertEqual(set.mappings[1].sensitivity, MIDIMapping.defaultSensitivity)
    }

    func testCapAndDuplicates() {
        let many = (0..<300).map { MIDIMapping(source: .nrpn, number: $0, parameter: "vcfB.cutoff") }
        XCTAssertEqual(MIDIMappingSet(mappings: many).sanitized().mappings.count, MIDIMapping.maxCount)
        let dup = MIDIMapping(source: .cc, number: 3, parameter: "vcfB.cutoff")
        XCTAssertEqual(MIDIMappingSet(mappings: [dup, MIDIMapping(source: .cc, number: 3, parameter: "vcfB.cutoff")]).sanitized().mappings.count, 1)
    }

    func testLearnReplacesTheParametersPreviousControl() {
        var s = MIDIMappingSet()
        s.assign(MIDIMapping(source: .cc, number: 20, parameter: "vcfB.cutoff"))
        s.assign(MIDIMapping(source: .cc, number: 21, parameter: "vcfB.resonance"))
        s.assign(MIDIMapping(source: .cc, number: 22, parameter: "vcfB.cutoff"))
        XCTAssertEqual(s.mappings.map(\.number), [21, 22])
    }

    func testPresetOverridesGlobal() {
        let global = MIDIMappingSet(mappings: [
            MIDIMapping(source: .cc, number: 74, parameter: "vcfB.cutoff"),
            MIDIMapping(source: .cc, number: 71, parameter: "vcfB.resonance"),
            MIDIMapping(source: .cc, number: 73, parameter: "eg2.attack"),
        ])
        let preset = MIDIMappingSet(mappings: [
            MIDIMapping(source: .cc, number: 74, parameter: "vcfA.cutoff"),     // same control, other parameter
            MIDIMapping(source: .cc, number: 10, parameter: "vcfB.resonance"),  // same parameter, other control
        ])
        let m = MIDIMappingSet.merged(preset: preset, global: global)
        XCTAssertEqual(m.map(\.number), [74, 10, 73])
        XCTAssertEqual(m.first?.parameter, "vcfA.cutoff")
    }

    func testProfileRoundTripAndValidation() throws {
        let p = MIDIHardwareProfile(name: "Desk", device: "Knobs 16", mappings: [
            MIDIMapping(source: .cc, channel: 2, number: 30, parameter: "vcfB.cutoff", minimum: 0.9, maximum: 0.1, mode: .relative,
                        relative: .signedBit, pickup: true, sensitivity: 0.01),
        ])
        let data = try p.encoded()
        let json = try XCTUnwrap(String(data: data, encoding: .utf8))
        XCTAssertTrue(json.contains("\"format\" : \"com.dreammusictemple.vekturmodular.midi-profile\""))
        let back = try MIDIHardwareProfile.decode(data)
        XCTAssertEqual(back.name, "Desk"); XCTAssertEqual(back.device, "Knobs 16")
        XCTAssertEqual(back.mappings.first?.relative, .signedBit)
        XCTAssertEqual(back.mappings.first?.channel, 2)
        XCTAssertEqual(back.mappings.first?.minimum, 0.9)

        XCTAssertThrowsError(try MIDIHardwareProfile.decode(Data(#"{"format":"other","mappings":[]}"#.utf8)))
        XCTAssertThrowsError(try MIDIHardwareProfile.decode(Data(#"{"format":"com.dreammusictemple.vekturmodular.midi-profile","version":9,"mappings":[]}"#.utf8))) {
            XCTAssertEqual($0 as? MIDIHardwareProfile.ProfileError, .newerVersion(9))
        }
        // Unknown keys and one broken entry are tolerated.
        let lenient = #"{"format":"com.dreammusictemple.vekturmodular.midi-profile","version":1,"name":"X","future":1,"#
            + #""mappings":[{"source":"cc","number":5,"parameter":"global.glide","extra":true},{"source":"warp","parameter":"x"}]}"#
        XCTAssertEqual(try MIDIHardwareProfile.decode(Data(lenient.utf8)).mappings.count, 1)
    }

    func testStandardProfileTargetsRealParameters() {
        let p = MIDIHardwareProfile.standard
        XCTAssertEqual(MIDIMappingSet(mappings: p.mappings).sanitized().mappings.count, p.mappings.count)
    }

    func testStateCarriesMappingsAndOldStatesLoad() throws {
        var s = InstrumentState()
        s.midi.assign(MIDIMapping(source: .note, number: 36, parameter: "arp.mode", mode: .toggle))
        let data = try StateCodec.encode(s)
        let back = try StateCodec.decode(data).state
        XCTAssertEqual(back.midi.mappings.first?.mode, .toggle)
        XCTAssertNil(back.preservedSections["midi"])
        XCTAssertTrue(InstrumentState().midi.isDefault)
        let plain = try StateCodec.encode(InstrumentState())
        XCTAssertFalse(String(decoding: plain, as: UTF8.self).contains("\"midi\""), "no section when there are no mappings")
        // A malformed section is dropped, not fatal.
        var obj = try XCTUnwrap(JSONSerialization.jsonObject(with: plain) as? [String: Any])
        obj["midi"] = ["mappings": 5]
        let bad = try JSONSerialization.data(withJSONObject: obj)
        XCTAssertTrue(try StateCodec.decode(bad).state.midi.isDefault)
    }

    func testSourceDescriptions() {
        XCTAssertEqual(MIDIMapping(source: .cc, channel: 0, number: 74, parameter: "vcfB.cutoff").sourceDescription, "CC 74 · Ch 1")
        XCTAssertEqual(MIDIMapping(source: .nrpn, number: 3 << 7 | 9, parameter: "vcfB.cutoff").sourceDescription, "NRPN 3:9 · Any")
        XCTAssertEqual(MIDIMapping(source: .note, channel: 9, number: 36, parameter: "vcfB.cutoff").sourceDescription, "Note C2 · Ch 10")
    }
}
