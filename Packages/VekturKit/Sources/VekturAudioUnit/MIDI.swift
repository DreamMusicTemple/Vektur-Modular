// MIDI.swift — Phase 7 Swift surface of the MIDI engine: MIDI Learn (preset
// and global scopes, merged and published to the kernel lock-free), hardware
// profiles, program change → presets, parameter-tree sync for values MIDI
// changed inside the kernel (RPN, MCM, learnt controls), the MIDI monitor and
// MIDI output to the host.
//
// The MIDI itself is handled sample-accurately in the C++ kernel
// (KernelMidi.cpp). Nothing here runs on the render thread: a 30 Hz timer on
// a utility queue drains the kernel's UI queue.
@preconcurrency import AVFoundation
import Foundation
import os
import VekturCore
import VekturDSP

// MARK: - Status

public struct MIDIEngineStatus: Sendable, Equatable {
    public enum Sync: Int, Sendable { case `internal` = 0, host = 1, midiClock = 2 }
    public var clockTempo: Double?
    public var clockRunning: Bool
    public var clockActive: Bool
    public var syncInEffect: Sync
    public var messagesIn: UInt32
    public var messagesOut: UInt32
    public var lastMessage: (UInt8, UInt8, UInt8)

    public static func == (l: Self, r: Self) -> Bool {
        l.clockTempo == r.clockTempo && l.clockRunning == r.clockRunning && l.clockActive == r.clockActive &&
        l.syncInEffect == r.syncInEffect && l.messagesIn == r.messagesIn && l.messagesOut == r.messagesOut &&
        l.lastMessage == r.lastMessage
    }

    /// "Note On ch 1 · 60 100", "CC ch 2 · 74 64", "MIDI 2.0 CC ch 1 · 74".
    public var lastMessageDescription: String {
        let (s, a, b) = lastMessage
        guard s != 0 else { return "—" }
        let ch = Int(s & 0x0F) + 1
        switch s & 0xF0 {
        case 0x80: return "Note Off ch \(ch) · \(a)"
        case 0x90: return "Note On ch \(ch) · \(a) \(b)"
        case 0xA0: return "Poly Pressure ch \(ch) · \(a) \(b)"
        case 0xB0: return "CC ch \(ch) · \(a) \(b)"
        case 0xC0: return "Program ch \(ch) · \(Int(a) + 1)"
        case 0xD0: return "Pressure ch \(ch) · \(a)"
        case 0xE0: return "Pitch Bend ch \(ch) · \(Int(a) | Int(b) << 7)"
        default:
            // MIDI 2.0 messages are logged as (opcode << 4 | channel).
            return "MIDI 2.0 op \(s >> 4) ch \(ch) · \(a)"
        }
    }
}

/// What the MIDI drain reports to the UI.
public enum MIDIEvent: Sendable, Equatable {
    case learned(MIDIMapping, scope: MIDIMappingScope)
    case programChange(program: Int, bankMSB: Int, bankLSB: Int, channel: Int, presetName: String?)
}

/// A parameter waiting for a control while MIDI Learn is armed.
public struct MIDILearnRequest: Sendable, Equatable {
    public var parameter: String
    public var scope: MIDIMappingScope
    public var mode: MIDIMapMode
    public var relative: MIDIRelativeEncoding
    public var pickup: Bool
    public var minimum: Double
    public var maximum: Double
    public var anyChannel: Bool

    public init(parameter: String, scope: MIDIMappingScope = .preset, mode: MIDIMapMode = .absolute,
                relative: MIDIRelativeEncoding = .twosComplement, pickup: Bool = false,
                minimum: Double = 0, maximum: Double = 1, anyChannel: Bool = true) {
        self.parameter = parameter; self.scope = scope; self.mode = mode; self.relative = relative
        self.pickup = pickup; self.minimum = minimum; self.maximum = maximum; self.anyChannel = anyChannel
    }
}

// MARK: - Global mappings (device-wide, shared by the app and the extension)

public final class GlobalMIDIMappings: @unchecked Sendable {
    public static let shared = GlobalMIDIMappings()
    private static let key = "midi.globalMappings.v1"
    private let lock = OSAllocatedUnfairLock(initialState: MIDIMappingSet())
    private let defaults: UserDefaults

    init(defaults: UserDefaults? = nil) {
        self.defaults = defaults ?? UserDefaults(suiteName: ImpulseResponseLibrary.appGroup) ?? .standard
        if let data = self.defaults.data(forKey: Self.key), let set = try? JSONDecoder().decode(MIDIMappingSet.self, from: data) {
            lock.withLock { $0 = set.sanitized() }
        }
    }

    public var mappings: MIDIMappingSet {
        get { lock.withLock { $0 } }
        set {
            let clean = newValue.sanitized()
            lock.withLock { $0 = clean }
            if let data = try? JSONEncoder().encode(clean) { defaults.set(data, forKey: Self.key) }
        }
    }

    /// The other process (app ⇄ extension) may have changed them.
    public func reload() {
        if let data = defaults.data(forKey: Self.key), let set = try? JSONDecoder().decode(MIDIMappingSet.self, from: data) {
            lock.withLock { $0 = set.sanitized() }
        }
    }
}

// MARK: - Runtime (stored by the audio unit)

final class MIDIRuntime: @unchecked Sendable {
    let state = OSAllocatedUnfairLock(uncheckedState: Pending())
    struct Pending { var learn: MIDILearnRequest?; var timer: DispatchSourceTimer? }
    let queue = DispatchQueue(label: "com.dmtcymatics.vekturmodular.midi", qos: .userInitiated)
    let global = GlobalMIDIMappings.shared
}

extension VekturModularAudioUnit {
    public static let midiEventNotification = Notification.Name("VekturModularMIDIEvent")
    static let midiLog = Logger(subsystem: "com.dmtcymatics.vekturmodular", category: "MIDI")

    // MARK: Mappings

    public func midiMappings(_ scope: MIDIMappingScope) -> MIDIMappingSet {
        switch scope {
        case .preset: return stateSnapshot.midi
        case .global: return midiRuntime.global.mappings
        }
    }

    public func setMIDIMappings(_ set: MIDIMappingSet, scope: MIDIMappingScope) {
        switch scope {
        case .preset: mutateState { $0.midi = set.sanitized() }
        case .global: midiRuntime.global.mappings = set
        }
        publishMIDIMappings()
    }

    /// Every mapping the engine is using, preset first.
    public var effectiveMIDIMappings: [MIDIMapping] {
        MIDIMappingSet.merged(preset: stateSnapshot.midi, global: midiRuntime.global.mappings)
    }

    /// Sends the merged table to the kernel (lock-free; pickup state resets).
    public func publishMIDIMappings() {
        let merged = effectiveMIDIMappings
        var table: [VKMidiMapping] = []
        table.reserveCapacity(merged.count)
        for m in merged {
            guard let spec = m.spec else { continue }
            table.append(VKMidiMapping(source: m.source.engineCode, channel: UInt8(m.channel ?? Int(VK_MIDI_ANY_CHANNEL)),
                                       number: UInt16(m.number), address: spec.address,
                                       minimum: Float(m.minimum), maximum: Float(m.maximum),
                                       mode: m.mode.engineCode, relative: m.relative.engineCode,
                                       pickup: m.pickup, sensitivity: Float(m.sensitivity)))
        }
        table.withUnsafeBufferPointer { vk_kernel_set_midi_mappings(kernel.raw, $0.baseAddress, UInt32($0.count)) }
    }

    // MARK: Learn

    /// Arms MIDI Learn: the next control moved is mapped to `request.parameter`.
    public func beginMIDILearn(_ request: MIDILearnRequest) {
        guard ParameterRegistry.spec(identifier: request.parameter) != nil else { return }
        startMIDIDrain()
        // The kernel's UI queue has one consumer: the MIDI queue. Events
        // already queued are handled first, so none is taken as the answer.
        midiRuntime.queue.async { [weak self] in
            guard let self else { return }
            self.midiRuntime.state.withLockUnchecked { $0.learn = nil }
            self.drainMIDIOnQueue()
            self.midiRuntime.state.withLockUnchecked { $0.learn = request }
            vk_kernel_midi_learn(self.kernel.raw, true)
        }
    }

    public func cancelMIDILearn() {
        midiRuntime.queue.async { [weak self] in
            guard let self else { return }
            self.midiRuntime.state.withLockUnchecked { $0.learn = nil }
            vk_kernel_midi_learn(self.kernel.raw, false)
        }
    }

    public var midiLearnRequest: MIDILearnRequest? { midiRuntime.state.withLockUnchecked { $0.learn } }

    /// Removes every mapping (both scopes) of one parameter.
    public func clearMIDIMappings(forParameter id: String) {
        var p = midiMappings(.preset); p.removeAll(forParameter: id)
        var g = midiMappings(.global); g.removeAll(forParameter: id)
        mutateState { $0.midi = p }
        midiRuntime.global.mappings = g
        publishMIDIMappings()
    }

    // MARK: Profiles

    /// Loads a profile into a scope (replacing it, or adding to it).
    public func applyMIDIProfile(_ profile: MIDIHardwareProfile, scope: MIDIMappingScope, replace: Bool) {
        var set = replace ? MIDIMappingSet() : midiMappings(scope)
        for m in profile.mappings { set.assign(MIDIMapping(source: m.source, channel: m.channel, number: m.number, parameter: m.parameter,
                                                           minimum: m.minimum, maximum: m.maximum, mode: m.mode, relative: m.relative,
                                                           pickup: m.pickup, sensitivity: m.sensitivity)) }
        setMIDIMappings(set, scope: scope)
    }

    /// The current mappings of a scope as a profile for export.
    public func exportMIDIProfile(scope: MIDIMappingScope, name: String, device: String? = nil) -> MIDIHardwareProfile {
        MIDIHardwareProfile(name: name, device: device, mappings: midiMappings(scope).mappings)
    }

    // MARK: Status

    public func midiStatus() -> MIDIEngineStatus {
        var s = VKMidiStatus()
        vk_kernel_midi_status(kernel.raw, &s)
        return MIDIEngineStatus(clockTempo: s.clockActive ? s.clockTempo : nil, clockRunning: s.clockRunning, clockActive: s.clockActive,
                                syncInEffect: MIDIEngineStatus.Sync(rawValue: Int(s.syncSource)) ?? .internal,
                                messagesIn: s.messagesIn, messagesOut: s.messagesOut, lastMessage: (s.lastStatus, s.lastData1, s.lastData2))
    }

    // MARK: Drain (utility queue, 30 Hz while allocated or learning)

    func startMIDIDrain() {
        midiRuntime.state.withLockUnchecked { pending in
            guard pending.timer == nil else { return }
            let t = DispatchSource.makeTimerSource(queue: midiRuntime.queue)
            t.schedule(deadline: .now(), repeating: .milliseconds(33), leeway: .milliseconds(10))
            t.setEventHandler { [weak self] in self?.drainMIDIOnQueue() }
            pending.timer = t
            t.resume()
        }
    }

    func stopMIDIDrain() {
        midiRuntime.state.withLockUnchecked { pending in
            pending.timer?.cancel()
            pending.timer = nil
        }
    }

    /// Drains the kernel's MIDI UI queue now (tests, or a UI that wants the
    /// answer synchronously). Never call from the MIDI queue itself.
    @discardableResult
    public func drainMIDI() -> [MIDIEvent] { midiRuntime.queue.sync { drainMIDIOnQueue() } }

    /// Waits for queued learn / cancel requests (tests).
    public func flushMIDIQueue() { midiRuntime.queue.sync { } }

    @discardableResult
    func drainMIDIOnQueue() -> [MIDIEvent] {
        var events: [MIDIEvent] = []
        var e = VKMidiUIEvent()
        var paramsChanged = false
        while vk_kernel_pop_midi_ui_event(kernel.raw, &e) {
            switch Int(e.kind) {
            case Int(VK_MIDI_UI_LEARNED.rawValue):
                if let ev = completeLearn(e) { events.append(ev) }
            case Int(VK_MIDI_UI_PROGRAM.rawValue):
                let name = selectPreset(program: Int(e.number), bankMSB: Int(e.bankMSB))
                events.append(.programChange(program: Int(e.number), bankMSB: Int(e.bankMSB), bankLSB: Int(e.bankLSB),
                                             channel: Int(e.channel), presetName: name))
            default:
                paramsChanged = true
            }
        }
        if paramsChanged { syncMIDIChangedParameters() }
        if !events.isEmpty {
            DispatchQueue.main.async {
                for ev in events { NotificationCenter.default.post(name: Self.midiEventNotification, object: self, userInfo: ["event": ev]) }
            }
        }
        return events
    }

    private func completeLearn(_ e: VKMidiUIEvent) -> MIDIEvent? {
        guard let req = midiRuntime.state.withLockUnchecked({ p -> MIDILearnRequest? in let r = p.learn; p.learn = nil; return r }),
              let source = MIDISourceKind(engineCode: e.source) else { return nil }
        // A note learnt in absolute mode is most useful as a toggle.
        let mode: MIDIMapMode = source == .note && req.mode == .absolute ? .toggle : req.mode
        let m = MIDIMapping(source: source, channel: req.anyChannel ? nil : Int(e.channel), number: Int(e.number), parameter: req.parameter,
                            minimum: req.minimum, maximum: req.maximum, mode: mode, relative: req.relative, pickup: req.pickup)
        var set = midiMappings(req.scope)
        set.assign(m)
        setMIDIMappings(set, scope: req.scope)
        Self.midiLog.info("Learnt \(m.sourceDescription, privacy: .public) → \(m.parameter, privacy: .public) (\(req.scope.rawValue, privacy: .public))")
        return .learned(m, scope: req.scope)
    }

    /// Values MIDI changed inside the kernel (learnt controls, RPN bend range,
    /// MPE configuration) are pushed into the parameter tree so the host and
    /// the UI see them. The value written back equals the kernel's.
    func syncMIDIChangedParameters() {
        guard let tree = parameterTree else { return }
        var addrs = [UInt64](repeating: 0, count: 512)
        let n = addrs.withUnsafeMutableBufferPointer { vk_kernel_take_midi_changes(kernel.raw, $0.baseAddress, 512) }
        for a in addrs.prefix(Int(n)) {
            guard let p = tree.parameter(withAddress: a) else { continue }
            let v = kernel.get(a)
            if p.value != v { p.setValue(v, originator: nil) }
        }
    }

    // MARK: Program change

    /// Bank MSB 0 and 1 = the two factory banks (32 each); bank MSB 2 = user
    /// presets ordered by name. Other banks are ignored.
    private func selectPreset(program: Int, bankMSB: Int) -> String? {
        let preset: AUAudioUnitPreset
        switch bankMSB {
        case 0, 1:
            guard let p = (factoryPresets ?? []).first(where: { $0.number == bankMSB * 32 + program }) else { return nil }
            preset = p
        case 2:
            let user = userPresets.sorted { $0.name.localizedStandardCompare($1.name) == .orderedAscending }
            guard user.indices.contains(program) else { return nil }
            preset = user[program]
        default: return nil
        }
        DispatchQueue.main.async { [weak self] in self?.currentPreset = preset }
        return preset.name
    }
}

// MARK: - Host transport (render thread)

/// Holds the host's musical-context and transport blocks for the render block.
/// Written in allocateRenderResources (render not running), read on the render
/// thread. Calling host blocks from the render thread is the documented use.
final class HostTransport: @unchecked Sendable {
    var musicalContext: AUHostMusicalContextBlock?
    var transportState: AUHostTransportStateBlock?

    func publish(to k: OpaquePointer) {
        guard let context = musicalContext else { return }
        var tempo = 0.0, beat = 0.0, numerator = 0.0, downbeat = 0.0
        var denominator = 0
        guard context(&tempo, &numerator, &denominator, &beat, nil, &downbeat) else { return }
        var playing = false
        if let state = transportState {
            var flags = AUHostTransportStateFlags()
            if state(&flags, nil, nil, nil) { playing = flags.contains(.moving) }
        }
        if numerator >= 1, denominator >= 1 {
            vk_kernel_set_transport_ex(k, tempo, beat, playing, downbeat, numerator, Double(denominator))
        } else {
            vk_kernel_set_transport(k, tempo, beat, playing)
        }
    }
}
