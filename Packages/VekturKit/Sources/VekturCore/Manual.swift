// Manual.swift — Phase 9. The 38-chapter manual (tools/manual.py) bundled as
// Markdown, with the screens each chapter documents for contextual help.
import Foundation

public struct ManualChapter: Codable, Sendable, Identifiable, Hashable {
    public var number: Int
    public var slug: String
    public var title: String
    public var screens: [String]
    public var file: String
    public var id: Int { number }

    public var markdown: String {
        guard let url = Bundle.module.url(forResource: (file as NSString).deletingPathExtension, withExtension: "md", subdirectory: "Manual")
                ?? Bundle.module.url(forResource: (file as NSString).deletingPathExtension, withExtension: "md"),
              let s = try? String(contentsOf: url, encoding: .utf8) else { return "# \(number). \(title)\n\nThis chapter is missing from the build." }
        return s
    }
}

public enum ManualLibrary {
    private struct Index: Codable { var chapters: [ManualChapter] }
    public static let chapters: [ManualChapter] = {
        guard let url = Bundle.module.url(forResource: "index", withExtension: "json", subdirectory: "Manual")
                ?? Bundle.module.url(forResource: "index", withExtension: "json"),
              let d = try? Data(contentsOf: url), let i = try? JSONDecoder().decode(Index.self, from: d) else { return [] }
        return i.chapters
    }()

    /// The chapter to open for a screen (its first-listed chapter that names it
    /// as the main subject, else any chapter mentioning it, else chapter 1).
    public static func chapter(forScreen key: String) -> ManualChapter? {
        chapters.first { $0.screens.first == key } ?? chapters.first { $0.screens.contains(key) } ?? chapters.first
    }

    /// Chapters whose text contains every word of the query (case-insensitive).
    public static func search(_ q: String) -> [ManualChapter] {
        let words = q.lowercased().split(separator: " ").map(String.init)
        guard !words.isEmpty else { return chapters }
        return chapters.filter { c in let t = c.markdown.lowercased(); return words.allSatisfy { t.contains($0) } }
    }
}
