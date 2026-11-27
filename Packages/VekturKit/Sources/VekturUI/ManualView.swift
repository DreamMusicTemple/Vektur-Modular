// ManualView.swift — Phase 9 manual reader and contextual help sheet.
import SwiftUI
import VekturCore

extension ManualLibrary {
    @MainActor static func chapter(for screen: Screen) -> ManualChapter? {
        // Per-screen preferences where the first match would be too general.
        let preferred: [Screen: String] = [.rack: "signal-flow", .mixer: "recording", .settings: "performance-settings",
                                           .diagnostics: "performance-settings", .performance: "voices", .midi: "midi-input"]
        if let slug = preferred[screen], let c = chapters.first(where: { $0.slug == slug }) { return c }
        return chapter(forScreen: screen.rawValue)
    }
}

public struct ManualView: View {
    @State private var selection: ManualChapter?
    @State private var query = ""
    @Environment(\.dismiss) private var dismiss

    public init(initial: ManualChapter? = nil) { _selection = State(initialValue: initial) }

    public var body: some View {
        Group {
            if let c = selection {
                ChapterView(chapter: c, select: { selection = $0 })
                    .toolbar { ToolbarItem(placement: .cancellationAction) { Button("Contents") { selection = nil } } }
            } else {
                List(ManualLibrary.search(query)) { c in
                    Button { selection = c } label: { Text("\(c.number). \(c.title)").frame(maxWidth: .infinity, alignment: .leading) }
                }
                .searchable(text: $query, prompt: "Search the manual")
                .navigationTitle("Manual")
            }
        }
        .toolbar { ToolbarItem(placement: .confirmationAction) { Button("Done") { dismiss() } } }
    }
}

private struct ChapterView: View {
    let chapter: ManualChapter
    let select: (ManualChapter) -> Void

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 10) {
                ForEach(Array(MarkdownBlocks.parse(chapter.markdown).enumerated()), id: \.offset) { _, b in block(b) }
                HStack {
                    if let p = ManualLibrary.chapters.first(where: { $0.number == chapter.number - 1 }) { Button("‹ \(p.title)") { select(p) } }
                    Spacer()
                    if let n = ManualLibrary.chapters.first(where: { $0.number == chapter.number + 1 }) { Button("\(n.title) ›") { select(n) } }
                }
                .padding(.top, 20)
            }
            .padding()
            .frame(maxWidth: 760, alignment: .leading)
        }
        .navigationTitle(chapter.title)
    }

    @ViewBuilder private func block(_ b: MarkdownBlocks.Block) -> some View {
        switch b {
        case .heading(let t): Text(t).font(.title2.bold()).accessibilityAddTraits(.isHeader).padding(.bottom, 4)
        case .paragraph(let t): Text(MarkdownBlocks.inline(t)).fixedSize(horizontal: false, vertical: true)
        case .bullet(let marker, let t):
            HStack(alignment: .firstTextBaseline, spacing: 8) { Text(marker).foregroundStyle(.secondary); Text(MarkdownBlocks.inline(t)) }.padding(.leading, 6)
        case .table(let rows):
            ScrollView(.horizontal) {
                Grid(alignment: .leading, horizontalSpacing: 14, verticalSpacing: 4) {
                    ForEach(Array(rows.enumerated()), id: \.offset) { i, r in
                        GridRow { ForEach(Array(r.enumerated()), id: \.offset) { _, c in Text(MarkdownBlocks.inline(c)).font(i == 0 ? .caption.bold() : .caption) } }
                        if i == 0 { Divider() }
                    }
                }
                .padding(8)
            }
            .background(.quaternary.opacity(0.4), in: RoundedRectangle(cornerRadius: 6))
        }
    }
}

/// A small Markdown block parser: headings, paragraphs, list items and tables
/// (what the manual uses). Inline styling uses AttributedString's parser.
enum MarkdownBlocks {
    enum Block { case heading(String), paragraph(String), bullet(String, String), table([[String]]) }

    static func parse(_ md: String) -> [Block] {
        var out: [Block] = [], para: [String] = [], table: [[String]] = []
        func flush() {
            if !para.isEmpty { out.append(.paragraph(para.joined(separator: " "))); para = [] }
            if !table.isEmpty { out.append(.table(table)); table = [] }
        }
        for raw in md.components(separatedBy: "\n") {
            let line = raw.trimmingCharacters(in: .whitespaces)
            if line.isEmpty { flush(); continue }
            if line.hasPrefix("|") {
                if !para.isEmpty { out.append(.paragraph(para.joined(separator: " "))); para = [] }
                let cells = line.trimmingCharacters(in: CharacterSet(charactersIn: "|")).components(separatedBy: "|").map { $0.trimmingCharacters(in: .whitespaces) }
                if !cells.allSatisfy({ $0.allSatisfy { "-:".contains($0) } }) { table.append(cells) }
                continue
            }
            if line.hasPrefix("#") { flush(); out.append(.heading(line.drop { $0 == "#" }.trimmingCharacters(in: .whitespaces))); continue }
            if line.hasPrefix("- ") { flush(); out.append(.bullet("•", String(line.dropFirst(2)))); continue }
            if let dot = line.firstIndex(of: "."), line[..<dot].allSatisfy(\.isNumber), !line[..<dot].isEmpty, line[line.index(after: dot)...].hasPrefix(" ") {
                flush(); out.append(.bullet(String(line[...dot]), String(line[line.index(dot, offsetBy: 2)...]))); continue
            }
            para.append(line)
        }
        flush()
        return out
    }

    static func inline(_ s: String) -> AttributedString {
        (try? AttributedString(markdown: s, options: .init(interpretedSyntax: .inlineOnlyPreservingWhitespace))) ?? AttributedString(s)
    }
}
