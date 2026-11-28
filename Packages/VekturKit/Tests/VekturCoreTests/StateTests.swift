import XCTest
@testable import VekturCore

final class StateCodecTests: XCTestCase {

    func testRoundTripPreservesEverything() throws {
        var s = InstrumentState.default
        s.parameters[ParameterRegistry.ID.masterVolume] = -12
        s.preset = PresetReference(kind: .user, name: "Night Drive", number: -3)
        let decoded = try StateCodec.decode(StateCodec.encode(s))
        XCTAssertEqual(decoded.state.parameters[ParameterRegistry.ID.masterVolume], -12)
        XCTAssertEqual(decoded.state.preset, s.preset)
        XCTAssertEqual(decoded.originalSchemaVersion, InstrumentState.currentSchemaVersion)
        XCTAssertFalse(decoded.isFromNewerVersion)
    }

    func testMissingParametersFallBackToDefaults() throws {
        // A state saved before a parameter existed must still load.
        let json = #"{"schemaVersion":1,"writtenBy":"0.0.1","parameters":{}}"#
        let decoded = try StateCodec.decode(Data(json.utf8))
        for spec in ParameterRegistry.all {
            XCTAssertEqual(decoded.state.parameters[spec.identifier], Double(spec.defaultValue), spec.identifier)
        }
    }

    func testOutOfRangeAndIndexedValuesAreSanitised() throws {
        let json = #"{"schemaVersion":1,"parameters":{"global.masterVolume":9999,"global.polyphony":3.7,"global.masterTune":-1e9}}"#
        let s = try StateCodec.decode(Data(json.utf8)).state
        XCTAssertEqual(s.parameters["global.masterVolume"], 6)
        XCTAssertEqual(s.parameters["global.polyphony"], 4)
        XCTAssertEqual(s.parameters["global.masterTune"], -100)
    }

    func testUnknownSectionsAndParametersSurviveRoundTrip() throws {
        // Written by a hypothetical future build.
        let json = #"""
        {"schemaVersion":1,"parameters":{"global.masterVolume":-3,"osc7.drift":0.4},
         "patchGraph":{"cables":[{"from":"osc1.out","to":"vcfA.in"}]}}
        """#
        let s = try StateCodec.decode(Data(json.utf8)).state
        XCTAssertEqual(s.parameters["osc7.drift"], 0.4, "unknown parameter kept")
        XCTAssertNotNil(s.preservedSections["patchGraph"])
        let again = try StateCodec.decode(StateCodec.encode(s)).state
        XCTAssertEqual(again.preservedSections, s.preservedSections)
    }

    func testNewerSchemaLoadsBestEffort() throws {
        let json = #"{"schemaVersion":99,"parameters":{"global.masterVolume":-20},"futureThing":true}"#
        let d = try StateCodec.decode(Data(json.utf8))
        XCTAssertTrue(d.isFromNewerVersion)
        XCTAssertEqual(d.state.parameters["global.masterVolume"], -20)
        XCTAssertEqual(d.state.preservedSections["futureThing"], .bool(true))
    }

    func testLegacyV0Migrates() throws {
        let json = #"{"params":{"global.masterVolume":-9}}"#
        let d = try StateCodec.decode(Data(json.utf8))
        XCTAssertEqual(d.originalSchemaVersion, 0)
        XCTAssertEqual(d.state.schemaVersion, 1)
        XCTAssertEqual(d.state.parameters["global.masterVolume"], -9)
        XCTAssertNil(d.state.preservedSections["params"])
    }

    func testCorruptInputIsRejectedNotCrashing() {
        for bad in ["", "not json", "[1,2,3]", #"{"parameters":"nope"}"#, "{\"schemaVersion\":1,"] {
            XCTAssertThrowsError(try StateCodec.decode(Data(bad.utf8)), "should reject: \(bad)")
        }
    }

    func testOversizeInputRejected() {
        let big = Data(repeating: 0x20, count: StateCodec.maxBytes + 1)
        XCTAssertThrowsError(try StateCodec.decode(big)) { err in
            XCTAssertEqual(err as? StateCodecError, .tooLarge(bytes: StateCodec.maxBytes + 1))
        }
    }

    func testPatchRoundTripAndSanitising() throws {
        var s = InstrumentState.default
        s.patch = PatchState(cables: [
            CableState(id: 3, from: PortRef(module: "vco4", port: "out"), to: PortRef(module: "mixer", port: "in1"), gain: 0.5, colour: "red"),
            CableState(id: 3, from: PortRef(module: "vco5", port: "out"), to: PortRef(module: "mult1", port: "in")),   // duplicate id
            CableState(id: 999, from: PortRef(module: "vco6", port: "out"), to: PortRef(module: "comb", port: "in")),  // id out of range
            CableState(id: 7, from: PortRef(module: "noise", port: "out"), to: PortRef(module: "comb", port: "in"), gain: 50),
        ])
        let d = try StateCodec.decode(StateCodec.encode(s)).state
        XCTAssertEqual(d.patch.cables.map(\.id), [3, 7])
        XCTAssertEqual(d.patch.cables[0].colour, "red")
        XCTAssertEqual(d.patch.cables[1].gain, 2, "gain clamped")
        XCTAssertEqual(d.patch.nextCableID, 0)
    }

    func testLayoutRoundTripsAndIsSanitised() throws {
        var s = InstrumentState.default
        s.patch.layout = PatchLayout(positions: ["vco1": .init(x: 40, y: 80), "bad": .init(x: .nan, y: 0)],
                                     zoom: 9, panX: -120, panY: 30, cableOpacity: 0.05, cableThickness: 4, showNormals: false)
        let d = try StateCodec.decode(StateCodec.encode(s)).state.patch.layout
        XCTAssertEqual(d.positions["vco1"], .init(x: 40, y: 80))
        XCTAssertNil(d.positions["bad"])
        XCTAssertEqual(d.zoom, PatchLayout.maxZoom, "zoom clamped to 500 %")
        XCTAssertEqual(d.cableOpacity, 0.1, "opacity floor is 10 %")
        XCTAssertFalse(d.showNormals)
    }

    func testMalformedPatchDoesNotBreakState() throws {
        let json = #"{"schemaVersion":1,"parameters":{"global.masterVolume":-9},"patch":{"cables":"oops"}}"#
        let s = try StateCodec.decode(Data(json.utf8)).state
        XCTAssertEqual(s.parameters["global.masterVolume"], -9)
        XCTAssertTrue(s.patch.cables.isEmpty)
    }

    func testOlderStateHasEmptyPatch() throws {
        let json = #"{"schemaVersion":1,"parameters":{}}"#
        XCTAssertTrue(try StateCodec.decode(Data(json.utf8)).state.patch.cables.isEmpty)
    }

    func testMissingMigrationStepIsAnError() {
        let m = StateMigrator(current: 3, steps: [0: { $0 }])   // no 1→2 step
        XCTAssertThrowsError(try m.migrate(["schemaVersion": .number(1)]))
    }
}

final class ParameterRegistryStabilityTests: XCTestCase {

    /// Every parameter that has ever shipped. Changing an existing line breaks
    /// saved projects and host automation. Only ever APPEND.
    private let shipped: [(String, UInt64)] = [
        ("global.masterVolume", 0),
        ("global.masterTune", 1),
        ("global.polyphony", 2),
        ("global.pitchBendRange", 3),
    ]

    func testShippedIdentifiersAndAddressesNeverChange() {
        for (id, address) in shipped {
            XCTAssertEqual(ParameterRegistry.spec(identifier: id)?.address, address, "\(id) moved or was removed")
        }
    }

    func testIdentifiersAndAddressesAreUnique() {
        XCTAssertEqual(Set(ParameterRegistry.all.map(\.identifier)).count, ParameterRegistry.all.count)
        XCTAssertEqual(Set(ParameterRegistry.all.map(\.address)).count, ParameterRegistry.all.count)
    }

    func testEveryParameterBelongsToAKnownGroup() {
        let groups = Set(ParameterRegistry.groups.map { $0.identifier })
        for s in ParameterRegistry.all {
            XCTAssertTrue(groups.contains(s.group), s.identifier)
            XCTAssertTrue(s.identifier.hasPrefix(s.group + "."), s.identifier)
            if s.taper == .log { XCTAssertGreaterThan(s.minValue, 0, s.identifier) }
        }
    }

    func testPhase1StateLoadsWithPhase2Defaults() throws {
        // A Phase 1 save (only the four global parameters) must load, with every
        // new synthesis parameter at its default.
        let json = #"{"schemaVersion":1,"parameters":{"global.masterVolume":-12,"global.polyphony":2}}"#
        let s = try StateCodec.decode(Data(json.utf8)).state
        XCTAssertEqual(s.parameters["global.masterVolume"], -12)
        XCTAssertEqual(s.parameters["vcfB.cutoff"], 1200)
        XCTAssertEqual(s.parameters["vco1.wave"], 2)
        XCTAssertEqual(s.parameters.count, ParameterRegistry.all.count)
    }

    func testSpecsAreInternallyConsistent() {
        for s in ParameterRegistry.all {
            XCTAssertLessThan(s.minValue, s.maxValue, s.identifier)
            XCTAssertTrue((s.minValue...s.maxValue).contains(s.defaultValue), s.identifier)
            if let vs = s.valueStrings { XCTAssertEqual(vs.count, Int(s.maxValue - s.minValue) + 1, s.identifier) }
        }
        XCTAssertEqual(ParameterRegistry.polyphonyChoices.count,
                       ParameterRegistry.spec(identifier: ParameterRegistry.ID.polyphony)!.valueStrings!.count)
    }
}
