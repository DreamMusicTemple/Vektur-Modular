// VekturModularAudioUnit.swift — the AUv3 instrument. The standalone app hosts
// this same class in-process, so there is exactly one engine implementation.
@preconcurrency import AVFoundation
@preconcurrency import AudioToolbox
import CoreMIDI
import os
import VekturCore
import VekturDSP

public final class VekturModularAudioUnit: AUAudioUnit, @unchecked Sendable {

    public let kernel = KernelHandle()
    /// Host musical-context blocks, captured once per allocation (Phase 4B).
    private let transport = HostTransport()
    /// MIDI Learn and drain state (Phase 7, MIDI.swift).
    let midiRuntime = MIDIRuntime()

    private let outputBus: AUAudioUnitBus
    private var _outputBusArray: AUAudioUnitBusArray!
    private var _parameterTree: AUParameterTree!
    private var _currentPreset: AUAudioUnitPreset?

    /// State sections owned by later-phase modules round-trip through here.
    private let stateLock = OSAllocatedUnfairLock(initialState: InstrumentState.default)
    private static let log = Logger(subsystem: "com.dmtcymatics.vekturmodular", category: "AudioUnit")
    /// vk_kernel_set_patch has a single-writer contract; hosts (fullState) and
    /// the UI may call from different threads, so patch publication is serialised.
    /// Never taken on the render thread.
    private let patchLock = OSAllocatedUnfairLock()
    /// Serialises the effects [ui] calls (rebuilds, IR loading): hosts and the UI
    /// may call from different threads. Never taken on the render thread.
    let effectsLock = OSAllocatedUnfairLock()
    /// Last latency announced to the host via KVO (main thread only).
    private var announcedLatency: TimeInterval = 0
    private let effectsUpdatePending = OSAllocatedUnfairLock(initialState: false)

    // MARK: - Registration (standalone in-process hosting)

    private static let registerOnce: Void = {
        AUAudioUnit.registerSubclass(VekturModularAudioUnit.self,
                                     as: AudioComponentInfo.description,
                                     name: AudioComponentInfo.componentName,
                                     version: AudioComponentInfo.componentVersion)
    }()

    /// Call before instantiating in-process via AVAudioUnit.instantiate.
    public static func registerInProcess() { _ = registerOnce }

    // MARK: - Init

    public override init(componentDescription: AudioComponentDescription,
                         options: AudioComponentInstantiationOptions = []) throws {
        guard let format = AVAudioFormat(standardFormatWithSampleRate: 48_000, channels: 2) else {
            throw NSError(domain: NSOSStatusErrorDomain, code: Int(kAudioUnitErr_FormatNotSupported))
        }
        outputBus = try AUAudioUnitBus(format: format)
        outputBus.maximumChannelCount = VK_MAX_CHANNELS
        try super.init(componentDescription: componentDescription, options: options)

        _outputBusArray = AUAudioUnitBusArray(audioUnit: self, busType: .output, busses: [outputBus])
        _parameterTree = ParameterTreeBuilder.makeTree()
        installParameterCallbacks()
        maximumFramesToRender = 4096
    }

    private func installParameterCallbacks() {
        let k = kernel
        // Writes from UI/host land in the kernel's lock-free store. A change to an
        // effect slot's type or structural setting also schedules a rebuild.
        _parameterTree.implementorValueObserver = { [weak self] param, value in
            k.set(param.address, value)
            if Self.isStructuralEffectAddress(param.address, kernel: k) { self?.scheduleEffectsUpdate() }
        }
        _parameterTree.implementorValueProvider = { param in k.get(param.address) }
        _parameterTree.implementorStringFromValueCallback = { param, valuePtr in
            let v = valuePtr?.pointee ?? param.value
            if let fx = FxSlotAddresses.decode(param.address), let index = fx.index {
                // Generic slot parameter: named and formatted by the slot's effect type.
                let type = UInt32(k.get(FxSlotAddresses(fx.slot).type).rounded())
                var buf = [CChar](repeating: 0, count: 32)
                let n = buf.withUnsafeMutableBufferPointer { vk_fx_format_value(type, UInt32(index), v, $0.baseAddress, 32) }
                if n > 0 { return buf.withUnsafeBufferPointer { String(cString: $0.baseAddress!) } }
                return "–"
            }
            if let strings = param.valueStrings {
                let i = Int(v.rounded())
                return strings.indices.contains(i) ? strings[i] : "\(i)"
            }
            switch param.unit {
            case .decibels: return v <= -60 ? "-∞ dB" : String(format: "%.1f dB", v)
            case .cents: return String(format: "%+.0f ct", v)
            case .relativeSemiTones: return String(format: "%+.0f st", v)
            case .hertz: return v >= 1000 ? String(format: "%.2f kHz", v / 1000) : String(format: "%.1f Hz", v)
            case .milliseconds: return v >= 1000 ? String(format: "%.2f s", v / 1000) : String(format: "%.1f ms", v)
            case .percent: return String(format: "%.0f %%", v)
            case .octaves: return String(format: "%+.2f oct", v)
            case .degrees: return String(format: "%.0f°", v)
            case .boolean: return v >= 0.5 ? "On" : "Off"
            default: return String(format: "%.2f", v)
            }
        }
        // Seed the kernel with the tree's defaults.
        for p in _parameterTree.allParameters { k.set(p.address, p.value) }
    }

    // MARK: - Busses, parameters, capabilities

    public override var outputBusses: AUAudioUnitBusArray { _outputBusArray }

    public override var parameterTree: AUParameterTree? {
        get { _parameterTree }
        set { /* fixed tree; hosts must not replace it */ }
    }

    /// 0 inputs; stereo or mono out.
    public override var channelCapabilities: [NSNumber]? { [0, 2, 0, 1] }

    /// Ask hosts for MIDI 2.0 (high-resolution velocity and controllers,
    /// per-note bend and controllers). Hosts that only speak MIDI 1.0 still
    /// send byte events; the render adapter accepts both.
    public override var audioUnitMIDIProtocol: MIDIProtocolID { ._2_0 }

    /// One MIDI output: generated notes (arpeggiator, sequencer, generative),
    /// optionally the played notes too, and MIDI clock (Phase 7).
    public override var midiOutputNames: [String] { ["Vektur Out"] }

    public override var supportsUserPresets: Bool { true }

    public override func shouldChange(to format: AVAudioFormat, for bus: AUAudioUnitBus) -> Bool {
        format.channelCount >= 1 && format.channelCount <= VK_MAX_CHANNELS &&
            format.commonFormat == .pcmFormatFloat32 && !format.isInterleaved
    }

    // MARK: - Render resources

    public override func allocateRenderResources() throws {
        try super.allocateRenderResources()
        let fmt = outputBus.format
        guard fmt.channelCount >= 1, fmt.channelCount <= VK_MAX_CHANNELS else {
            throw NSError(domain: NSOSStatusErrorDomain, code: Int(kAudioUnitErr_FormatNotSupported))
        }
        let status = kernel.allocate(sampleRate: fmt.sampleRate, channels: fmt.channelCount,
                                     maxFrames: maximumFramesToRender)
        if status != 0 {
            Self.log.error("Kernel allocate failed (\(status)) sr=\(fmt.sampleRate) ch=\(fmt.channelCount) max=\(self.maximumFramesToRender)")
            super.deallocateRenderResources()
            throw NSError(domain: NSOSStatusErrorDomain, code: Int(kAudioUnitErr_FailedInitialization))
        }
        transport.musicalContext = musicalContextBlock
        transport.transportState = transportStateBlock
        // MIDI out: the adapter retains the host's block and calls it on the
        // render thread after each buffer with sample-offset timestamps.
        if let out = midiOutputEventBlock {
            vk_au_set_midi_output_block(kernel.raw, Unmanaged.passUnretained(out as AnyObject).toOpaque())
        } else {
            vk_au_set_midi_output_block(kernel.raw, nil)
        }
        midiRuntime.global.reload()   // the app or another instance may have changed them
        publishMIDIMappings()
        startMIDIDrain()
        scheduleLatencyCheck()   // allocate rebuilt the effects at the new rate
    }

    public override func deallocateRenderResources() {
        stopMIDIDrain()
        kernel.deallocate()
        vk_au_set_midi_output_block(kernel.raw, nil)
        super.deallocateRenderResources()
    }

    public override var internalRenderBlock: AUInternalRenderBlock {
        // Capture only the raw kernel pointer: no Swift objects, no ARC, no
        // allocation on the render thread. All event handling happens in C++.
        nonisolated(unsafe) let k = kernel.raw
        let transport = self.transport
        return { actionFlags, timestamp, frameCount, _, outputData, realtimeEventListHead, _ in
            transport.publish(to: k)
            return vk_au_render(k,
                         UnsafeMutableRawPointer(actionFlags).assumingMemoryBound(to: UInt32.self),
                         timestamp.pointee.mSampleTime,
                         frameCount,
                         UnsafeMutableRawPointer(outputData),
                         realtimeEventListHead.map { UnsafeRawPointer($0) })
        }
    }

    // MARK: - State

    /// Snapshot of the current complete state (parameters read from the tree).
    public func captureState() -> InstrumentState {
        stateLock.withLock { stored in
            var s = stored
            s.schemaVersion = InstrumentState.currentSchemaVersion
            s.writtenBy = ProductInfo.marketingVersion
            for spec in ParameterRegistry.all {
                if let p = _parameterTree.parameter(withAddress: spec.address) {
                    s.parameters[spec.identifier] = Double(p.value)
                }
            }
            if let cp = _currentPreset {
                s.preset = PresetReference(kind: cp.number >= 0 ? .factory : .user, name: cp.name, number: cp.number)
            }
            return s
        }
    }

    /// Applies a complete state. Values are already sanitised by StateCodec.
    public func apply(_ state: InstrumentState) {
        let clean = state.sanitized()
        stateLock.withLock { $0 = clean }
        let result = publishPatch(clean.patch)
        if !result.isClean {
            Self.log.notice("Restored patch had \(result.problems.count) cable(s) the engine could not use")
        }
        for spec in ParameterRegistry.all {
            // setValue(_:originator:) updates the kernel via the observer and notifies host/UI.
            _parameterTree.parameter(withAddress: spec.address)?.setValue(clean.value(for: spec), originator: nil)
        }
        restoreImpulseResponses(clean.effects)
        updateEffects()
        publishMIDIMappings()   // the preset's own mappings, merged with the global ones
        NotificationCenter.default.post(name: Self.appearanceDidChange, object: self)
    }

    // MARK: - Effects latency & tail (Phase 6)

    /// Constant processing latency of the effects (all paths are compensated to it).
    public override var latency: TimeInterval {
        let sr = outputBus.format.sampleRate
        return sr > 0 ? Double(vk_kernel_effects_latency(kernel.raw)) / sr : 0
    }

    /// Reverbs, delays and other effects with tails ring on after the last note.
    public override var tailTime: TimeInterval {
        let tailTypes: Set<Int> = [1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13]
        for s in 0..<FxSlotAddresses.slotCount where tailTypes.contains(activeEffectType(slot: s)) { return 12 }
        return 0
    }

    static func isStructuralEffectAddress(_ address: UInt64, kernel k: KernelHandle) -> Bool {
        guard let fx = FxSlotAddresses.decode(address) else { return false }
        guard let index = fx.index else { return address % 256 == 0 }   // the slot's type
        let type = Int(k.get(FxSlotAddresses(fx.slot).type).rounded())
        return FxCatalogue.type(type).parameters.first { $0.index == index }?.structural ?? false
    }

    /// Coalesces rebuild requests onto the main queue.
    func scheduleEffectsUpdate() {
        let first = effectsUpdatePending.withLock { pending -> Bool in let was = pending; pending = true; return !was }
        guard first else { return }
        DispatchQueue.main.async { [weak self] in
            guard let self else { return }
            self.effectsUpdatePending.withLock { $0 = false }
            self.updateEffects()
        }
    }

    /// The new latency takes effect once the audio thread adopts the rebuilt
    /// effects (a few milliseconds); hosts are told through KVO on `latency`.
    func scheduleLatencyCheck() {
        for delay in [0.05, 0.3] {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
                guard let self else { return }
                let now = self.latency
                if now != self.announcedLatency {
                    self.willChangeValue(forKey: "latency")
                    self.announcedLatency = now
                    self.didChangeValue(forKey: "latency")
                }
            }
        }
    }

    public override var fullState: [String: Any]? {
        get {
            var dict = super.fullState ?? [:]
            do { dict[StateCodec.fullStateKey] = try StateCodec.encode(captureState()) }
            catch { Self.log.error("State encode failed: \(String(describing: error))") }
            return dict
        }
        set {
            super.fullState = newValue
            guard let data = newValue?[StateCodec.fullStateKey] as? Data else {
                // A host restoring a project saved before this key existed (or by
                // another tool): keep whatever super applied plus our defaults.
                return
            }
            do {
                let decoded = try StateCodec.decode(data)
                if decoded.isFromNewerVersion {
                    Self.log.notice("Loading state written by a newer version (schema \(decoded.originalSchemaVersion)); unknown sections preserved")
                }
                apply(decoded.state)
            } catch {
                // Never crash or half-apply on a bad blob: keep the current state.
                Self.log.error("Rejected saved state: \(String(describing: error))")
            }
        }
    }

    // MARK: - Patching

    /// The patch currently in effect.
    public var currentPatch: PatchState { stateLock.withLock { $0.patch } }

    /// Replaces the patch. Returns which cables the engine rejected and why; the
    /// rest take effect at the next audio block (with a 1.3 ms crossfade).
    @discardableResult
    public func applyPatch(_ patch: PatchState) -> PatchApplyResult {
        let clean = patch.sanitized()
        stateLock.withLock { $0.patch = clean }
        return publishPatch(clean)
    }

    /// Stores a new canvas layout. Visual only: the audio graph is untouched.
    public func updateLayout(_ layout: PatchLayout) {
        let clean = layout.sanitized()
        stateLock.withLock { $0.patch.layout = clean }
    }

    // MARK: - State sections owned by other files (Phase 5)

    /// Copy of the stored (non-parameter) state.
    var stateSnapshot: InstrumentState { stateLock.withLockUnchecked { $0 } }

    /// Edits the stored state. A generated preset also clears the host preset selection.
    func mutateState(_ body: (inout InstrumentState) -> Void) {
        stateLock.withLockUnchecked { body(&$0) }
        if stateSnapshot.preset?.kind == .generated { _currentPreset = nil }
    }

    // MARK: - Performance telemetry (Phase 4B)

    /// Live value of a matrix slot (0…15) on the most recently played voice.
    public func modSlotValue(_ slot: Int) -> Float {
        guard (0..<ModSlotAddresses.slotCount).contains(slot) else { return 0 }
        return vk_kernel_mod_slot_value(kernel.raw, UInt32(slot))
    }

    /// Keys currently held (MIDI note numbers, ascending).
    public func heldNotes() -> [UInt8] {
        var mask: (UInt32, UInt32, UInt32, UInt32) = (0, 0, 0, 0)
        withUnsafeMutableBytes(of: &mask) { vk_kernel_held_notes(kernel.raw, $0.baseAddress!.assumingMemoryBound(to: UInt32.self)) }
        let words = [mask.0, mask.1, mask.2, mask.3]
        return (0..<128).compactMap { n in (words[n >> 5] >> UInt32(n & 31)) & 1 == 1 ? UInt8(n) : nil }
    }

    /// Recent peak level carried by a cable (0 = silent), for cable animation.
    public func level(ofCable id: Int) -> Float {
        guard (0...PatchState.maxCableID).contains(id) else { return 0 }
        return vk_kernel_connection_level(kernel.raw, UInt32(id))
    }

    /// Recent peak level of a normalled (default) connection.
    public func level(ofNormal normal: RackNormal) -> Float {
        vk_kernel_connection_level(kernel.raw, normal.telemetrySlot)
    }

    private func publishPatch(_ patch: PatchState) -> PatchApplyResult {
        var result = PatchApplyResult()
        var cables: [VKCable] = []
        cables.reserveCapacity(patch.cables.count)
        for c in patch.cables {
            guard let src = RackCatalog.resolve(c.from, input: false), let dst = RackCatalog.resolve(c.to, input: true) else {
                result.problems.append(.init(cableID: c.id, reason: .unknownPort)); continue
            }
            cables.append(VKCable(id: UInt16(c.id), srcModule: UInt8(src.module), srcPort: UInt8(src.port),
                                  dstModule: UInt8(dst.module), dstPort: UInt8(dst.port), gain: Float(c.gain)))
        }
        var r = VKPatchResult()
        patchLock.withLockUnchecked {
            _ = cables.withUnsafeBufferPointer { vk_kernel_set_patch(kernel.raw, $0.baseAddress, UInt32($0.count), &r) }
        }
        result.feedbackConnections = Int(r.feedbackEdges)
        let reported = withUnsafeBytes(of: r.rejectedIds) { Array($0.bindMemory(to: UInt16.self)) }
        let reasons = withUnsafeBytes(of: r.rejectedReasons) { Array($0.bindMemory(to: UInt8.self)) }
        for i in 0..<min(Int(r.rejected), 16) {
            result.problems.append(.init(cableID: Int(reported[i]), reason: .init(engineCode: reasons[i])))
        }
        return result
    }

    // MARK: - Presets

    /// The 64 factory presets (Phase 9): number = bank * 32 + index.
    public override var factoryPresets: [AUAudioUnitPreset]? { Self.factoryPresetList }

    private static let factoryPresetList: [AUAudioUnitPreset] = FactoryPresetLibrary.all.map {
        let p = AUAudioUnitPreset(); p.number = $0.presetNumber; p.name = $0.name; return p
    }

    /// Loads a factory preset by its number, keeping the player's setup.
    public func loadFactoryPreset(number: Int) {
        guard let fp = FactoryPresetLibrary.preset(number: number) else {
            Self.log.notice("Factory preset \(number) does not exist"); return
        }
        apply(FactoryPresetLibrary.state(for: fp, keeping: captureState()))
        _currentPreset = Self.factoryPresetList.first { $0.number == number }
    }

    public override var currentPreset: AUAudioUnitPreset? {
        get { _currentPreset }
        set {
            guard let preset = newValue else { _currentPreset = nil; return }
            if preset.number < 0 {
                do {
                    let state = try presetState(for: preset)
                    fullState = state
                    _currentPreset = preset
                } catch {
                    Self.log.error("User preset '\(preset.name)' failed to load: \(String(describing: error))")
                }
            } else {
                loadFactoryPreset(number: preset.number)
            }
        }
    }
}

/// Chord names for held notes (engine-side analyser, shared with tests).
public enum ChordAnalyzer {
    public static func name(of notes: [UInt8]) -> String {
        var buffer = [CChar](repeating: 0, count: 24)
        _ = notes.withUnsafeBufferPointer { n in
            buffer.withUnsafeMutableBufferPointer { vk_chord_name(n.baseAddress, UInt32(n.count), $0.baseAddress, UInt32($0.count)) }
        }
        return String(cString: buffer)
    }
}
