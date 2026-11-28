// TestKeyboardView.swift — minimal two-octave multi-touch test keyboard.
// Sends MIDI through scheduleMIDIEventBlock, the same path hosts use.
// Key zones, MPE and velocity-by-position arrive in Phase 4B.
import SwiftUI
@preconcurrency import AudioToolbox
import VekturAudioUnit

struct TestKeyboardView: View {
    let audioUnit: VekturModularAudioUnit
    var lowestNote = 48
    var octaves = 2

    @State private var held: [Int: Int] = [:]   // touch id → note (via drag gesture identity)

    init(audioUnit: VekturModularAudioUnit, lowestNote: Int = 48, octaves: Int = 2) {
        self.audioUnit = audioUnit; self.lowestNote = lowestNote; self.octaves = octaves
    }

    var body: some View {
        GeometryReader { g in
            let whites = whiteNotes
            let w = g.size.width / CGFloat(whites.count)
            ZStack(alignment: .topLeading) {
                ForEach(Array(whites.enumerated()), id: \.offset) { i, note in
                    key(note: note, isBlack: false)
                        .frame(width: w - 1, height: g.size.height)
                        .offset(x: CGFloat(i) * w)
                }
                ForEach(blackNotes, id: \.self) { note in
                    let leftWhite = whites.firstIndex(of: note - 1) ?? 0
                    key(note: note, isBlack: true)
                        .frame(width: w * 0.6, height: g.size.height * 0.6)
                        .offset(x: CGFloat(leftWhite + 1) * w - w * 0.3)
                }
            }
        }
    }

    private func key(note: Int, isBlack: Bool) -> some View {
        let down = held.values.contains(note)
        return RoundedRectangle(cornerRadius: 3)
            .fill(isBlack ? (down ? Color.gray : Color.black) : (down ? Color.gray.opacity(0.5) : Color.white))
            .overlay(RoundedRectangle(cornerRadius: 3).stroke(Color.black.opacity(0.4)))
            .gesture(DragGesture(minimumDistance: 0)
                .onChanged { _ in if held[note] == nil { held[note] = note; send(0x90, note, 100) } }
                .onEnded { _ in held[note] = nil; send(0x80, note, 0) })
            .accessibilityElement()
            .accessibilityLabel("Key \(note)")
            .accessibilityAddTraits(.isButton)
    }

    private func send(_ status: UInt8, _ note: Int, _ velocity: UInt8) {
        guard let schedule = audioUnit.scheduleMIDIEventBlock else { return }
        let bytes: [UInt8] = [status, UInt8(note & 0x7F), velocity]
        bytes.withUnsafeBufferPointer { schedule(AUEventSampleTimeImmediate, 0, 3, $0.baseAddress!) }
    }

    private var range: ClosedRange<Int> { lowestNote...(lowestNote + 12 * octaves) }
    private var whiteNotes: [Int] { range.filter { [0, 2, 4, 5, 7, 9, 11].contains($0 % 12) } }
    private var blackNotes: [Int] { range.filter { [1, 3, 6, 8, 10].contains($0 % 12) } }
}
