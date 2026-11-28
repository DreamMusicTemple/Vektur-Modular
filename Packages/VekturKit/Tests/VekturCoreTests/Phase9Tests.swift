// Phase9Tests.swift — factory preset library, exchange formats, MIDI reference, manual.
import XCTest
@testable import VekturCore

final class Phase9Tests: XCTestCase {
    func testFactoryLibraryHasTwoBanksOf32() {
        XCTAssertEqual(FactoryPresetLibrary.banks.count, 2)
        XCTAssertEqual(FactoryPresetLibrary.all.count, 64)
        XCTAssertEqual(Set(FactoryPresetLibrary.all.map(\.presetNumber)), Set(0..<64))
        XCTAssertEqual(Set(FactoryPresetLibrary.all.map(\.name)).count, 64, "names are unique")
    }

    func testFactoryPresetsReferenceOnlyRealParametersAndPorts() {
        for p in FactoryPresetLibrary.all {
            for k in p.parameters.keys { XCTAssertNotNil(ParameterRegistry.spec(identifier: k), "\(p.name): \(k)") }
            for c in p.cables { XCTAssertEqual(c.from.split(separator: ".").count, 2); XCTAssertEqual(c.to.split(separator: ".").count, 2) }
        }
    }

    func testLoadingAPresetKeepsSetupAndAppearance() {
        var current = InstrumentState()
        current.parameters["global.receiveChannel"] = 5
        current.parameters["vcfA.cutoff"] = 123
        current.appearance.wear = 0.9
        let s = FactoryPresetLibrary.state(for: FactoryPresetLibrary.all[0], keeping: current)
        XCTAssertEqual(s.parameters["global.receiveChannel"], 5)
        XCTAssertEqual(s.appearance.wear, 0.9)
        XCTAssertEqual(s.preset?.kind, .factory)
        XCTAssertNotEqual(s.parameters["vcfA.cutoff"], 123, "the old sound does not leak into the preset")
    }

    func testExchangeRoundTripAndKindChecks() throws {
        var st = InstrumentState()
        st.parameters["vco1.level"] = 42; st.parameters["seq.length"] = 7; st.parameters["global.receiveChannel"] = 3
        for kind in ExchangeKind.allCases {
            let data = try ExchangeFile.make(kind, name: "T", from: st).encoded()
            let f = try ExchangeFile.decode(data, expecting: kind)
            XCTAssertEqual(f.format, kind.rawValue)
            for other in ExchangeKind.allCases where other != kind {
                XCTAssertThrowsError(try ExchangeFile.decode(data, expecting: other))
            }
        }
        let pattern = try ExchangeFile.decode(try ExchangeFile.make(.pattern, name: "P", from: st).encoded(), expecting: .pattern)
        XCTAssertEqual(Set(pattern.state.parameters.keys), ["seq.length"])
        var other = InstrumentState(); other.parameters["vco1.level"] = 10
        let merged = pattern.merged(into: other)
        XCTAssertEqual(merged.parameters["vco1.level"], 10, "a pattern changes only the sequencer")
        XCTAssertEqual(merged.parameters["seq.length"], 7)
        let preset = try ExchangeFile.decode(try ExchangeFile.make(.preset, name: "S", from: st).encoded(), expecting: .preset)
        XCTAssertNil(preset.state.parameters["global.receiveChannel"], "setup is not part of a preset")
    }

    func testNewerSchemaIsRefused() {
        let json = #"{"format":"vektur.preset","schemaVersion":99,"writtenBy":"x","name":"n","state":{}}"#
        XCTAssertThrowsError(try ExchangeFile.decode(Data(json.utf8), expecting: .preset)) { XCTAssertEqual($0 as? ExchangeError, .newerSchema(99)) }
        XCTAssertThrowsError(try ExchangeFile.decode(Data("{}".utf8), expecting: .preset)) { XCTAssertEqual($0 as? ExchangeError, .notVektur) }
    }

    func testMIDIReferenceListsAutomatableParametersAndMappings() {
        let m = MIDIMapping(source: .cc, channel: nil, number: 74, parameter: "vcfA.cutoff")
        let rows = MIDIReference.rows(mappings: [m])
        XCTAssertEqual(rows.count, ParameterRegistry.all.filter(\.isAutomatable).count)
        XCTAssertFalse(rows.first { $0.identifier == "vcfA.cutoff" }!.assignments.isEmpty)
        XCTAssertTrue(MIDIReference.csv(rows).hasPrefix("identifier,name,group,address,range,midi\n"))
        XCTAssertTrue(MIDIReference.markdown(rows).contains("`vcfA.cutoff`"))
    }

    func testManualHas38ChaptersAndEveryScreenHasHelp() {
        XCTAssertEqual(ManualLibrary.chapters.count, 38)
        for c in ManualLibrary.chapters { XCTAssertTrue(c.markdown.hasPrefix("# \(c.number).")) }
        for s in ["rack", "patch", "performance", "sequencer", "arpeggiator", "generative", "effects", "modulation", "mixer", "midi", "presets", "settings", "diagnostics"] {
            XCTAssertNotNil(ManualLibrary.chapters.first { $0.screens.contains(s) }, s)
        }
    }
}
