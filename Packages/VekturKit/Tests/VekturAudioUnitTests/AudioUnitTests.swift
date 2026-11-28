// In-process AUAudioUnit tests: lifecycle, parameter tree, rendering through
// the real internalRenderBlock, fullState round trip and corrupt-state safety.
// Host-level validation (auval / AU Lab / real DAWs) is in docs/PHASES.md.
@preconcurrency import AVFoundation
import XCTest
@testable import VekturAudioUnit
import VekturCore
import VekturDSP

final class AudioUnitTests: XCTestCase {

    private func makeUnit() throws -> VekturModularAudioUnit {
        VekturModularAudioUnit.registerInProcess()
        return try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
    }

    func testParameterTreeMatchesRegistry() throws {
        let au = try makeUnit()
        let params = au.parameterTree?.allParameters ?? []
        XCTAssertEqual(params.count, ParameterRegistry.all.count)
        for spec in ParameterRegistry.all {
            let p = try XCTUnwrap(au.parameterTree?.parameter(withAddress: spec.address))
            XCTAssertEqual(p.identifier, spec.identifier)
            XCTAssertEqual(p.value, spec.defaultValue)
        }
    }

    func testAllocateRenderAndDeallocateRepeatedly() throws {
        let au = try makeUnit()
        for rate in [44_100.0, 48_000, 88_200, 96_000] {
            let fmt = try XCTUnwrap(AVAudioFormat(standardFormatWithSampleRate: rate, channels: 2))
            try au.outputBusses[0].setFormat(fmt)
            au.maximumFramesToRender = 1024
            try au.allocateRenderResources()
            XCTAssertTrue(au.kernel.isAllocated)
            let peak = try render(au, frames: 512, blocks: 40, noteAt: 0)
            XCTAssertGreaterThan(peak, 0.05, "audible at \(rate) Hz")
            au.deallocateRenderResources()
            XCTAssertFalse(au.kernel.isAllocated)
        }
    }

    func testRejectsTooManyFrames() throws {
        let au = try makeUnit()
        au.maximumFramesToRender = 256
        try au.allocateRenderResources()
        defer { au.deallocateRenderResources() }
        XCTAssertNotEqual(try renderStatus(au, frames: 512), noErr)
    }

    func testFullStateRoundTrip() throws {
        let a = try makeUnit()
        a.parameterTree?.parameter(withAddress: ParameterRegistry.spec(identifier: ParameterRegistry.ID.masterVolume)!.address)?.value = -18
        a.parameterTree?.parameter(withAddress: ParameterRegistry.spec(identifier: ParameterRegistry.ID.polyphony)!.address)?.value = 8
        let saved = try XCTUnwrap(a.fullState)
        XCTAssertNotNil(saved[StateCodec.fullStateKey] as? Data)

        let b = try makeUnit()
        b.fullState = saved
        XCTAssertEqual(b.parameterTree?.parameter(withAddress: 0)?.value, -18)
        XCTAssertEqual(b.parameterTree?.parameter(withAddress: 2)?.value, 8)
        XCTAssertEqual(b.kernel.get(0), -18, "kernel received restored value")
    }

    func testCorruptFullStateKeepsCurrentState() throws {
        let au = try makeUnit()
        au.parameterTree?.parameter(withAddress: 0)?.value = -24
        var bad = au.fullState ?? [:]
        bad[StateCodec.fullStateKey] = Data("{ broken".utf8)
        au.fullState = bad
        XCTAssertEqual(au.parameterTree?.parameter(withAddress: 0)?.value, -24)
    }

    func testRackCatalogMatchesEngine() {
        XCTAssertEqual(RackCatalog.modules.count, 41)
        XCTAssertEqual(RackCatalog.patchableModules.count, 39)
        XCTAssertNotNil(RackCatalog.resolve(PortRef(module: "vco9", port: "sync"), input: true))
        XCTAssertNil(RackCatalog.resolve(PortRef(module: "vco9", port: "nope"), input: true))
        XCTAssertTrue(RackCatalog.normals.contains { $0.from == PortRef(module: "vca", port: "out") && $0.to == PortRef(module: "output", port: "in") })
    }

    func testPatchAppliesRoundTripsAndReportsProblems() throws {
        let au = try makeUnit()
        au.maximumFramesToRender = 512
        try au.allocateRenderResources()
        defer { au.deallocateRenderResources() }
        let patch = PatchState(cables: [
            CableState(id: 5, from: PortRef(module: "vco4", port: "out"), to: PortRef(module: "mixer", port: "in1")),
            CableState(id: 6, from: PortRef(module: "vco5", port: "out"), to: PortRef(module: "mixer", port: "in1")),   // input taken
            CableState(id: 7, from: PortRef(module: "nope", port: "out"), to: PortRef(module: "mixer", port: "in2")),   // unknown
        ])
        let r = au.applyPatch(patch)
        XCTAssertEqual(Set(r.problems.map(\.cableID)), [6, 7])
        XCTAssertTrue(r.problems.contains { $0.reason == .inputAlreadyPatched })
        XCTAssertTrue(r.problems.contains { $0.reason == .unknownPort })

        _ = try render(au, frames: 512, blocks: 60, noteAt: 0)
        XCTAssertGreaterThan(au.level(ofCable: 5), 0.05, "signal flows through the patched cable")

        let b = try makeUnit()
        b.fullState = au.fullState
        XCTAssertEqual(b.currentPatch, au.currentPatch, "patch restored from host state")
    }

    // MARK: helpers

    private func render(_ au: VekturModularAudioUnit, frames: Int, blocks: Int, noteAt: Int) throws -> Float {
        let fmt = au.outputBusses[0].format
        let buf = try XCTUnwrap(AVAudioPCMBuffer(pcmFormat: fmt, frameCapacity: AVAudioFrameCount(frames)))
        buf.frameLength = AVAudioFrameCount(frames)
        let block = au.internalRenderBlock
        var midi = AURenderEvent(MIDI: AUMIDIEvent(next: nil, eventSampleTime: AUEventSampleTimeImmediate,
                                                   eventType: .MIDI, reserved: 0, length: 3, cable: 0, data: (0x90, 64, 110)))
        var peak: Float = 0
        var flags = AudioUnitRenderActionFlags()
        for i in 0..<blocks {
            var ts = AudioTimeStamp(); ts.mSampleTime = Double(i * frames); ts.mFlags = .sampleTimeValid
            let status = withUnsafePointer(to: &midi) { ev in
                block(&flags, &ts, AUAudioFrameCount(frames), 0, buf.mutableAudioBufferList, i == noteAt ? ev : nil, nil)
            }
            XCTAssertEqual(status, noErr)
        }
        let ch = try XCTUnwrap(buf.floatChannelData)
        for f in 0..<frames { peak = max(peak, abs(ch[0][f])) }
        return peak
    }

    private func renderStatus(_ au: VekturModularAudioUnit, frames: Int) throws -> AUAudioUnitStatus {
        let buf = try XCTUnwrap(AVAudioPCMBuffer(pcmFormat: au.outputBusses[0].format, frameCapacity: AVAudioFrameCount(frames)))
        buf.frameLength = AVAudioFrameCount(frames)
        var flags = AudioUnitRenderActionFlags()
        var ts = AudioTimeStamp()
        return au.internalRenderBlock(&flags, &ts, AUAudioFrameCount(frames), 0, buf.mutableAudioBufferList, nil, nil)
    }

    // Phase 4B
    func testChordAnalyzerWrapper() {
        XCTAssertEqual(ChordAnalyzer.name(of: [60, 64, 67]), "C")
        XCTAssertEqual(ChordAnalyzer.name(of: [57, 60, 64, 67]), "Am7")
        XCTAssertEqual(ChordAnalyzer.name(of: []), "")
    }

    func testPerformanceTelemetryDefaults() throws {
        let au = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        XCTAssertEqual(au.heldNotes(), [])
        XCTAssertEqual(au.modSlotValue(0), 0)
        XCTAssertEqual(au.modSlotValue(99), 0)
    }

    // Phase 5
    func testSequencerStatusDefaultsAndCapture() throws {
        let au = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        let st = au.sequencerStatus()
        XCTAssertNil(st.sequencerStep)
        XCTAssertFalse(st.arpRunning || st.sequencerRunning || st.generativeRunning)
        XCTAssertTrue(au.drainCapturedEvents().isEmpty)
    }

    func testGenerativePhraseExportsAValidMIDIFile() throws {
        let au = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        let clip = au.generativePhrase()
        XCTAssertGreaterThan(clip.notes.count, 0)
        XCTAssertEqual(clip.lengthBeats, 16)                 // default: 4 bars
        XCTAssertEqual(au.generativeChords().count, 4)
        XCTAssertTrue(au.generativeChords().first?.hasPrefix("Am") ?? false)   // default A minor
        let data = MIDIFileWriter.data(for: clip)
        XCTAssertEqual(Array(data.prefix(4)), Array("MThd".utf8))
        XCTAssertEqual(Array(data[14..<18]), Array("MTrk".utf8))
        // Same seed → same phrase.
        XCTAssertEqual(au.generativePhrase().notes, clip.notes)
    }

    func testClipsAreKeptInState() throws {
        let au = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        au.storeClip(au.generativePhrase())
        XCTAssertEqual(au.clips.count, 1)
        let other = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        other.fullState = au.fullState
        XCTAssertEqual(other.clips.count, 1)
        other.removeClip(id: other.clips[0].id)
        XCTAssertTrue(other.clips.isEmpty)
    }

    func testGeneratedPresetIsAppliedReproducibleAndKeepsPerformanceSettings() throws {
        let au = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        let tree = try XCTUnwrap(au.parameterTree)
        tree.parameter(withAddress: UInt64(VK_P_ARP_MODE))?.setValue(1, originator: nil)
        tree.parameter(withAddress: UInt64(VK_P_GLOBAL_TEMPO))?.setValue(97, originator: nil)
        let r = au.generatePreset(category: .pad, seed: 42)
        XCTAssertFalse(r.usedFallback)
        XCTAssertGreaterThan(r.auditionLevel, -40)
        XCTAssertEqual(tree.parameter(withAddress: UInt64(VK_P_ARP_MODE))?.value, 1)
        XCTAssertEqual(tree.parameter(withAddress: UInt64(VK_P_GLOBAL_TEMPO))?.value, 97)
        let state = au.captureState()
        XCTAssertEqual(state.preset?.kind, .generated)
        XCTAssertEqual(state.preset?.seed, 42)
        XCTAssertTrue(state.patch.cables.isEmpty)
        let cutoff = tree.parameter(withAddress: UInt64(VK_P_VCFB_CUTOFF))?.value
        au.generatePreset(category: .bass, seed: 1)
        au.generatePreset(category: .pad, seed: 42)
        XCTAssertEqual(tree.parameter(withAddress: UInt64(VK_P_VCFB_CUTOFF))?.value, cutoff)
    }

    // Phase 6
    func testEffectCatalogueAndFormatting() throws {
        XCTAssertEqual(FxCatalogue.types.count, 28)
        XCTAssertEqual(FxCatalogue.type(6).name, "Hall Reverb")
        XCTAssertTrue(FxCatalogue.families.contains { $0.name == "Dynamics" && $0.types.count == 4 })
        let au = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        au.setEffectType(slot: 0, type: 1)   // stereo delay
        let tree = try XCTUnwrap(au.parameterTree)
        let p3 = try XCTUnwrap(tree.parameter(withAddress: FxSlotAddresses(0).parameter(2)))
        XCTAssertEqual(p3.string(fromValue: nil), "375 ms")   // Time L default
        XCTAssertEqual(tree.parameter(withAddress: FxSlotAddresses(0).mix)?.value, 30)
        XCTAssertEqual(au.formatEffectParameter(slot: 0, index: 0, normalised: FxCatalogue.type(1).parameters[0].defaultNormalised), "1/8 dotted")
    }

    func testEffectsLatencyIsReported() throws {
        let au = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        try au.allocateRenderResources()
        defer { au.deallocateRenderResources() }
        XCTAssertEqual(au.latency, 0)
        au.setEffectType(slot: 0, type: 25)   // limiter: 3 ms look-ahead
        for _ in 0..<20 { _ = try renderStatus(au, frames: 256) }
        XCTAssertEqual(au.effectsLatencySamples, 144)
        XCTAssertEqual(au.latency, 0.003, accuracy: 1e-6)
        XCTAssertEqual(au.activeEffectType(slot: 0), 25)
    }

    func testEffectTypesSurviveFullState() throws {
        let au = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        au.setEffectType(slot: 3, type: 6)
        let other = try VekturModularAudioUnit(componentDescription: AudioComponentInfo.description, options: [])
        other.fullState = au.fullState
        XCTAssertEqual(other.parameterTree?.parameter(withAddress: FxSlotAddresses(3).type)?.value, 6)
    }
    // MARK: - Phase 7: MIDI

    /// Renders one block, optionally with one MIDI 1.0 event at offset 0.
    private func renderMIDI(_ au: VekturModularAudioUnit, _ bytes: (UInt8, UInt8, UInt8)?, length: UInt16 = 3, frames: Int = 256) throws {
        let buf = try XCTUnwrap(AVAudioPCMBuffer(pcmFormat: au.outputBusses[0].format, frameCapacity: AVAudioFrameCount(frames)))
        buf.frameLength = AVAudioFrameCount(frames)
        var flags = AudioUnitRenderActionFlags()
        var ts = AudioTimeStamp(); ts.mFlags = .sampleTimeValid
        let block = au.internalRenderBlock
        var ev = AURenderEvent(MIDI: AUMIDIEvent(next: nil, eventSampleTime: AUEventSampleTimeImmediate, eventType: .MIDI, reserved: 0,
                                                 length: length, cable: 0, data: bytes ?? (0, 0, 0)))
        let status = withUnsafePointer(to: &ev) { p in
            block(&flags, &ts, AUAudioFrameCount(frames), 0, buf.mutableAudioBufferList, bytes == nil ? nil : p, nil)
        }
        XCTAssertEqual(status, noErr)
    }

    private func allocated() throws -> VekturModularAudioUnit {
        let au = try makeUnit()
        au.maximumFramesToRender = 512
        try au.allocateRenderResources()
        return au
    }

    func testAdvertisesMIDI2AndOneOutput() throws {
        let au = try makeUnit()
        XCTAssertEqual(au.audioUnitMIDIProtocol, ._2_0)
        XCTAssertEqual(au.midiOutputNames, ["Vektur Out"])
    }

    func testMIDILearnMapsAControlAndSyncsTheTree() throws {
        let au = try allocated()
        defer { au.deallocateRenderResources() }
        let res = try XCTUnwrap(ParameterRegistry.spec(identifier: "vcfB.resonance"))
        au.beginMIDILearn(MIDILearnRequest(parameter: res.identifier, scope: .preset))
        au.flushMIDIQueue()
        try renderMIDI(au, (0xB0, 20, 10))
        let events = au.drainMIDI()
        guard case .learned(let m, let scope)? = events.first else { return XCTFail("nothing learnt: \(events)") }
        XCTAssertEqual(m.source, .cc); XCTAssertEqual(m.number, 20); XCTAssertEqual(scope, .preset)
        XCTAssertEqual(au.midiMappings(.preset).mappings.map(\.parameter), [res.identifier])
        try renderMIDI(au, (0xB0, 20, 127))
        au.drainMIDI()
        XCTAssertEqual(au.parameterTree?.parameter(withAddress: res.address)?.value, res.maxValue, "the tree follows the learnt control")
        // Saved with the preset.
        let other = try makeUnit()
        other.fullState = au.fullState
        XCTAssertEqual(other.midiMappings(.preset).mappings.first?.number, 20)
    }

    func testRPNBendRangeReachesTheParameterTree() throws {
        let au = try allocated()
        defer { au.deallocateRenderResources() }
        for cc in [(UInt8(101), UInt8(0)), (100, 0), (6, 7)] { try renderMIDI(au, (0xB0, cc.0, cc.1)) }
        au.drainMIDI()
        XCTAssertEqual(au.parameterTree?.parameter(withAddress: UInt64(VK_P_GLOBAL_PITCH_BEND_RANGE))?.value, 7)
    }

    func testMIDIOutputReachesTheHostBlock() throws {
        let au = try makeUnit()
        let box = OutBox()
        au.midiOutputEventBlock = { time, _, length, bytes in
            box.add(Array(UnsafeBufferPointer(start: bytes, count: length)))
            return noErr
        }
        au.parameterTree?.parameter(withAddress: UInt64(VK_P_GLOBAL_MIDI_OUT))?.value = 2
        au.maximumFramesToRender = 512
        try au.allocateRenderResources()
        try renderMIDI(au, (0x90, 60, 100))
        try renderMIDI(au, (0x80, 60, 0))
        au.deallocateRenderResources()
        XCTAssertEqual(box.messages.first, [0x90, 60, 100])
        XCTAssertEqual(box.messages.last?.first, 0x80)
    }

    func testHostTimeSignatureReachesTheEngine() throws {
        let au = try makeUnit()
        au.musicalContextBlock = { tempo, num, den, beat, _, downbeat in
            tempo?.pointee = 120; num?.pointee = 3; den?.pointee = 4; beat?.pointee = 0; downbeat?.pointee = 0
            return true
        }
        au.maximumFramesToRender = 512
        try au.allocateRenderResources()
        defer { au.deallocateRenderResources() }
        try renderMIDI(au, nil)
        var beats = 0.0
        _ = vk_kernel_generative_phrase(au.kernel.raw, 0, nil, 0, &beats)
        let bars = Double(au.kernel.get(UInt64(VK_P_GEN_PHRASE_BARS)))
        XCTAssertGreaterThan(beats, 0)
        XCTAssertEqual(beats.truncatingRemainder(dividingBy: 3), 0, "phrases are whole 3/4 bars (\(beats) beats, bars index \(bars))")
    }

    func testProfilesApplyAndGlobalMappingsMerge() throws {
        let au = try makeUnit()
        let saved = au.midiMappings(.global)
        defer { au.setMIDIMappings(saved, scope: .global) }
        au.setMIDIMappings(MIDIMappingSet(), scope: .global)
        au.applyMIDIProfile(.standard, scope: .global, replace: true)
        XCTAssertEqual(au.midiMappings(.global).mappings.count, MIDIHardwareProfile.standard.mappings.count)
        var p = MIDIMappingSet(); p.assign(MIDIMapping(source: .cc, number: 74, parameter: "vcfA.cutoff"))
        au.setMIDIMappings(p, scope: .preset)
        let eff = au.effectiveMIDIMappings
        XCTAssertEqual(eff.filter { $0.number == 74 }.map(\.parameter), ["vcfA.cutoff"], "the preset's CC 74 wins")
        let exported = try au.exportMIDIProfile(scope: .global, name: "Test").encoded()
        XCTAssertEqual(try MIDIHardwareProfile.decode(exported).mappings.count, MIDIHardwareProfile.standard.mappings.count)
    }

    // MARK: - Phase 8: appearance

    func testAppearanceIsSavedWithTheProject() throws {
        let a = try makeUnit()
        a.setAppearance(AppearanceState(wear: 0.9, moduleWear: ["vcfB": 0.2]))
        let b = try makeUnit()
        b.fullState = a.fullState
        XCTAssertEqual(b.appearance.wear, 0.9)
        XCTAssertEqual(b.appearance.wear(for: "vcfB"), 0.2)
    }

    func testEveryPanelJackModuleExistsInTheRack() {
        for m in PanelLayout.standard.modules where !m.rack.isEmpty {
            XCTAssertNotNil(RackCatalog.module(key: m.rack), "panel \(m.key) points at unknown rack module \(m.rack)")
        }
    }
}

private final class OutBox: @unchecked Sendable {
    private let lock = NSLock()
    private var store: [[UInt8]] = []
    func add(_ m: [UInt8]) { lock.lock(); store.append(m); lock.unlock() }
    var messages: [[UInt8]] { lock.lock(); defer { lock.unlock() }; return store }
}
