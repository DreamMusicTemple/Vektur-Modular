// MIDIInputService.swift — standalone CoreMIDI input and output (Phase 7).
//
// Input: every source is connected with a MIDI 2.0 input port, so CoreMIDI
// delivers Universal MIDI Packets (upconverting MIDI 1.0 devices) and the
// event lists go unchanged to the audio unit's scheduleMIDIEventListBlock —
// the same path a MIDI 2.0 host uses for the AUv3. Systems without the
// event-list block fall back to the MIDI 1.0 byte block.
//
// Output: a virtual source, "Vektur Modular", carries the audio unit's MIDI
// output (generated notes, clock) to other apps and devices.
import CoreMIDI
@preconcurrency import AudioToolbox
import os

/// Holds the schedule blocks for the CoreMIDI receive thread.
private final class MIDIForwarder: @unchecked Sendable {
    private static let umpWordCounts: [Int] = [1, 1, 1, 2, 2, 4, 1, 1, 2, 2, 2, 3, 3, 4, 4, 4]
    struct Blocks { var list: AUMIDIEventListBlock?; var bytes: AUScheduleMIDIEventBlock? }
    // Written on the main thread, read on CoreMIDI's thread. Reference swap
    // guarded by a lock that is never held during sends.
    private let lock = OSAllocatedUnfairLock<Blocks>(uncheckedState: Blocks())

    func set(list: AUMIDIEventListBlock?, bytes: AUScheduleMIDIEventBlock?) { lock.withLockUnchecked { $0 = Blocks(list: list, bytes: bytes) } }

    func forward(_ list: UnsafePointer<MIDIEventList>) {
        let b = lock.withLockUnchecked { $0 }
        if let schedule = b.list {
            _ = schedule(AUEventSampleTimeImmediate, 0, list)
            return
        }
        guard let schedule = b.bytes else { return }
        // MIDI 1.0 fallback: system messages and MIDI 1.0 channel voice only.
        list.unsafeSequence().forEach { packetPtr in
            let packet = packetPtr.pointee
            withUnsafeBytes(of: packet.words) { raw in
                let words = raw.bindMemory(to: UInt32.self)
                var i = 0
                let count = Int(packet.wordCount)
                while i < count {
                    let w = words[i]
                    let type = w >> 28
                    if type == 0x2 || type == 0x1 {
                        var bytes: (UInt8, UInt8, UInt8) = (UInt8((w >> 16) & 0xFF), UInt8((w >> 8) & 0x7F), UInt8(w & 0x7F))
                        let status = bytes.0
                        let len: Int
                        if type == 0x1 { len = status == 0xF2 ? 3 : (status == 0xF1 || status == 0xF3) ? 2 : 1 }
                        else { len = (status & 0xF0 == 0xC0 || status & 0xF0 == 0xD0) ? 2 : 3 }
                        withUnsafeBytes(of: &bytes) { p in
                            schedule(AUEventSampleTimeImmediate, 0, len, p.bindMemory(to: UInt8.self).baseAddress!)
                        }
                    }
                    i += Self.umpWordCounts[Int(type)]
                }
            }
        }
    }
}

@MainActor
public final class MIDIInputService {
    private var client = MIDIClientRef()
    private var port = MIDIPortRef()
    private var virtualSource = MIDIEndpointRef()
    private let forwarder = MIDIForwarder()
    private static let log = Logger(subsystem: "com.dmtcymatics.vekturmodular", category: "MIDI")
    public private(set) var connectedSourceNames: [String] = []
    /// The virtual source carrying the audio unit's MIDI output (0 = none).
    public var outputEndpoint: MIDIEndpointRef { virtualSource }

    public init() {}

    /// Starts (or refreshes) input. Pass the audio unit's blocks; the event-list
    /// block is preferred (MIDI 2.0).
    public func start(eventList: AUMIDIEventListBlock?, schedule: AUScheduleMIDIEventBlock?) {
        forwarder.set(list: eventList, bytes: schedule)
        guard client == 0 else { connectAllSources(); return }

        var status = MIDIClientCreateWithBlock("Vektur Modular" as CFString, &client) { [weak self] notePtr in
            if notePtr.pointee.messageID == .msgSetupChanged || notePtr.pointee.messageID == .msgObjectAdded {
                guard let service = self else { return }
                Task { @MainActor in service.connectAllSources() }
            }
        }
        guard status == noErr else { Self.log.error("MIDIClientCreate failed \(status)"); return }

        let fwd = forwarder
        status = MIDIInputPortCreateWithProtocol(client, "Vektur In" as CFString, ._2_0, &port) { list, _ in
            fwd.forward(list)
        }
        guard status == noErr else { Self.log.error("MIDIInputPortCreate failed \(status)"); return }
        // Output: MIDI 1.0 byte messages wrapped as UMP type 1/2 by CoreMIDI.
        status = MIDISourceCreateWithProtocol(client, "Vektur Modular" as CFString, ._1_0, &virtualSource)
        if status != noErr { Self.log.error("MIDISourceCreate failed \(status)"); virtualSource = 0 }
        connectAllSources()
    }

    /// Back-compat entry point (MIDI 1.0 only).
    public func start(schedule: AUScheduleMIDIEventBlock?) { start(eventList: nil, schedule: schedule) }

    public func stop() {
        forwarder.set(list: nil, bytes: nil)
        if virtualSource != 0 { MIDIEndpointDispose(virtualSource); virtualSource = 0 }
        if port != 0 { MIDIPortDispose(port); port = 0 }
        if client != 0 { MIDIClientDispose(client); client = 0 }
        connectedSourceNames = []
    }

    /// A block for `AUAudioUnit.midiOutputEventBlock` that sends to the virtual
    /// source. Called on the render thread: it builds the event list in a
    /// preallocated buffer and calls MIDIReceivedEventList, which is safe to
    /// call from a real-time thread (it does not block on the server).
    public func makeOutputBlock() -> AUMIDIOutputEventBlock? {
        guard virtualSource != 0 else { return nil }
        let sender = MIDIOutputSender(endpoint: virtualSource)
        return { _, _, length, bytes in
            sender.send(bytes: bytes, length: length)
            return noErr
        }
    }

    private func connectAllSources() {
        guard port != 0 else { return }
        var names: [String] = []
        for i in 0..<MIDIGetNumberOfSources() {
            let src = MIDIGetSource(i)
            if src == virtualSource { continue }   // never loop our own output back in
            MIDIPortConnectSource(port, src, nil)   // reconnecting an existing source is harmless
            var name: Unmanaged<CFString>?
            if MIDIObjectGetStringProperty(src, kMIDIPropertyDisplayName, &name) == noErr, let n = name?.takeRetainedValue() {
                names.append(n as String)
            }
        }
        connectedSourceNames = names
    }
}

/// Preallocated MIDIEventList for the render-thread output block.
private final class MIDIOutputSender: @unchecked Sendable {
    private let endpoint: MIDIEndpointRef
    private let list = UnsafeMutablePointer<MIDIEventList>.allocate(capacity: 1)

    init(endpoint: MIDIEndpointRef) { self.endpoint = endpoint }
    deinit { list.deallocate() }

    func send(bytes: UnsafePointer<UInt8>, length: Int) {
        guard length >= 1 else { return }
        let s = bytes[0]
        let w: UInt32
        if s >= 0xF0 {   // system: UMP type 1
            w = 0x1000_0000 | UInt32(s) << 16 | (length > 1 ? UInt32(bytes[1] & 0x7F) << 8 : 0) | (length > 2 ? UInt32(bytes[2] & 0x7F) : 0)
        } else {         // MIDI 1.0 channel voice: UMP type 2
            w = 0x2000_0000 | UInt32(s) << 16 | (length > 1 ? UInt32(bytes[1] & 0x7F) << 8 : 0) | (length > 2 ? UInt32(bytes[2] & 0x7F) : 0)
        }
        var word = w
        let packet = MIDIEventListInit(list, ._1_0)
        _ = MIDIEventListAdd(list, MemoryLayout<MIDIEventList>.size, packet, 0, 1, &word)
        MIDIReceivedEventList(endpoint, list)
    }
}
