// RecordingPanel.swift — Phase 9 recording controls (Output screen, both the
// app and the AUv3). Files go to Documents/Recordings and can be shared.
import SwiftUI
import VekturAudioUnit

struct RecordingPanel: View {
    @State private var service: RecordingService
    @State private var files: [URL] = []

    init(audioUnit: VekturModularAudioUnit) { _service = State(initialValue: RecordingService.shared(for: audioUnit)) }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            Text("recorder").font(PanelTheme.legendFont(15, weight: .bold)).foregroundStyle(PanelTheme.legendNew)
            HStack(spacing: 14) {
                Button { service.isRecording ? service.stop() : service.start() } label: {
                    Label(service.isRecording ? "Stop" : "Record", systemImage: service.isRecording ? "stop.circle.fill" : "record.circle")
                        .font(.title3).foregroundStyle(service.isRecording ? .red : PanelTheme.legendNew)
                }
                .buttonStyle(.bordered)
                .keyboardShortcut("r", modifiers: [.command, .shift])
                .accessibilityHint("Records the instrument's output to a file")
                Text(timeString).font(.title3.monospacedDigit()).foregroundStyle(PanelTheme.legend2)
                    .accessibilityLabel("Recorded \(Int(service.seconds)) seconds")
                if service.droppedFrames > 0 {
                    Label("\(service.droppedFrames) frames dropped", systemImage: "exclamationmark.triangle").foregroundStyle(.orange).font(.caption)
                }
            }
            HStack {
                Picker("Format", selection: $service.settings.container) { ForEach(RecordingContainer.allCases) { Text($0.displayName).tag($0) } }
                Picker("Depth", selection: $service.settings.depth) { ForEach(RecordingDepth.allCases) { Text($0.displayName).tag($0) } }
                Picker("Rate", selection: $service.settings.sampleRate) {
                    ForEach(RecordingSettings.rates, id: \.self) { r in Text(r == 0 ? "session rate" : String(format: "%.1f kHz", r / 1000)).tag(r) }
                }
            }
            .pickerStyle(.menu).disabled(service.isRecording)
            switch service.state {
            case .failed(let m): Text(m).foregroundStyle(.red).font(.caption)
            case .finished(let url):
                HStack { Text(url.lastPathComponent).font(.caption); ShareLink(item: url) { Image(systemName: "square.and.arrow.up") }.accessibilityLabel("Share recording") }
            default: EmptyView()
            }
            if !files.isEmpty {
                DisclosureGroup("Recordings (\(files.count))") {
                    ForEach(files.prefix(12), id: \.self) { u in
                        HStack { Text(u.lastPathComponent).font(.caption); Spacer(); ShareLink(item: u) { Image(systemName: "square.and.arrow.up") } }
                    }
                }
                .font(.caption)
            }
        }
        .padding(14).frame(maxWidth: .infinity, alignment: .leading).background(PanelPaint(key: "out-rec"))
        .onAppear { files = RecordingService.recordings() }
        .onChange(of: service.state) { _, _ in files = RecordingService.recordings() }
    }

    private var timeString: String {
        let s = Int(service.seconds)
        return String(format: "%02d:%02d", s / 60, s % 60)
    }
}
