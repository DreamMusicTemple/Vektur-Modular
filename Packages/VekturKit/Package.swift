// swift-tools-version: 6.0
// VekturKit — all shared code for the Vektur Modular app and its AUv3 extension.
// The app and extension targets in the Xcode project are thin shells over this.
import PackageDescription

let package = Package(
    name: "VekturKit",
    platforms: [.iOS(.v17), .macOS(.v14)],
    products: [
        .library(name: "VekturCore", targets: ["VekturCore"]),
        .library(name: "VekturAudioUnit", targets: ["VekturAudioUnit"]),
        .library(name: "VekturEngine", targets: ["VekturEngine"]),
        .library(name: "VekturUI", targets: ["VekturUI"]),
    ],
    targets: [
        // Real-time C++20 DSP core behind a plain-C header. No UI dependencies.
        .target(
            name: "VekturDSP",
            path: "Sources/VekturDSP",
            publicHeadersPath: "include",
            cxxSettings: [.define("NDEBUG", .when(configuration: .release))],
            linkerSettings: [.linkedFramework("AudioToolbox"), .linkedFramework("CoreMIDI"), .linkedFramework("Accelerate")]
        ),
        // Parameter registry, versioned state, migration, product info. No UI, no AU.
        .target(name: "VekturCore", dependencies: ["VekturDSP"], resources: [.copy("Resources/Panels.json"), .copy("Resources/FactoryPresets.json"), .copy("Resources/Manual")]),
        // AUAudioUnit subclass + parameter tree + state bridge.
        .target(name: "VekturAudioUnit", dependencies: ["VekturCore", "VekturDSP"]),
        // Standalone hosting: AVAudioEngine, audio session, CoreMIDI input.
        .target(name: "VekturEngine", dependencies: ["VekturAudioUnit", "VekturCore", "VekturDSP"]),
        // SwiftUI views shared by the app and the AUv3 view controller.
        .target(name: "VekturUI", dependencies: ["VekturAudioUnit", "VekturCore", "VekturDSP"], resources: [.process("Resources")]),

        .testTarget(name: "VekturCoreTests", dependencies: ["VekturCore"]),
        .testTarget(name: "VekturDSPTests", dependencies: ["VekturDSP", "VekturCore"]),
        .testTarget(name: "VekturAudioUnitTests", dependencies: ["VekturAudioUnit", "VekturCore", "VekturDSP"]),
    ],
    cxxLanguageStandard: .cxx20
)
