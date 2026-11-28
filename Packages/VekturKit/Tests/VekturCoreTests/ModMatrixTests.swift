// ModMatrixTests.swift — Phase 4A matrix encodings shared with the engine.
import XCTest
@testable import VekturCore

final class ModMatrixTests: XCTestCase {
    func testSourceListMatchesParameterStrings() {
        let strings = ParameterRegistry.spec(identifier: "matrix.slot1Source")?.valueStrings
        XCTAssertEqual(strings?.count, ModSource.allCases.count)
        XCTAssertEqual(ModSource.modWheel.displayName, "Mod Wheel")
        XCTAssertEqual(ModSource.constant.rawValue, 20)   // permanent ids
        XCTAssertTrue(ModSource.lfo1.isBipolar)
        XCTAssertFalse(ModSource.eg1.isBipolar)
    }

    func testSlotAddressesMatchRegistry() {
        for i in 0..<ModSlotAddresses.slotCount {
            let a = ModSlotAddresses(i)
            XCTAssertEqual(ParameterRegistry.spec(identifier: "matrix.slot\(i + 1)Source")?.address, a.source)
            XCTAssertEqual(ParameterRegistry.spec(identifier: "matrix.slot\(i + 1)Dest")?.address, a.destination)
            XCTAssertEqual(ParameterRegistry.spec(identifier: "matrix.slot\(i + 1)Slew")?.address, a.slew)
        }
    }

    func testDestinationCodesRoundTrip() {
        let cutoff = ParameterRegistry.spec(identifier: "vcfB.cutoff")!.address
        for d in [ModDestination.none, .parameter(cutoff), .pitch, .amplitude, .pan, .allCutoffs] {
            XCTAssertEqual(ModDestination(code: d.code), d)
        }
        XCTAssertEqual(ModDestination(code: 99_999), .none)
        XCTAssertEqual(ModDestination.parameter(cutoff).displayName, "Filter B Cutoff")
    }

    func testCatalogueOffersOnlyModulatableParameters() {
        let all = ModDestination.catalogue.flatMap(\.destinations)
        let cutoff = ParameterRegistry.spec(identifier: "vcfB.cutoff")!.address
        let volume = ParameterRegistry.spec(identifier: "global.masterVolume")!.address
        let wave = ParameterRegistry.spec(identifier: "vco1.wave")!.address
        XCTAssertTrue(all.contains(.parameter(cutoff)))
        XCTAssertTrue(all.contains(.pitch))
        XCTAssertFalse(all.contains(.parameter(volume)))
        XCTAssertFalse(all.contains(.parameter(wave)))
        XCTAssertFalse(ParameterRegistry.all.contains { $0.group == "matrix" && $0.isModulatable })
    }

    func testPerformanceDefaultsKeepPhase3Behaviour() {
        XCTAssertEqual(ParameterRegistry.spec(identifier: "global.voiceMode")?.defaultValue, 0)      // poly
        XCTAssertEqual(ParameterRegistry.spec(identifier: "global.bendDown")?.defaultValue, -1)      // follow up range
        XCTAssertEqual(ParameterRegistry.spec(identifier: "global.glideMode")?.defaultValue, 0)      // always
        XCTAssertEqual(ParameterRegistry.spec(identifier: "matrix.slot1Source")?.defaultValue, 0)  // empty matrix
    }
}
