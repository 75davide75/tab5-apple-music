import Foundation

public struct LyricLine: Equatable, Sendable {
    public let time: Double     // seconds from the start; negative when the lyrics are not timed
    public let text: String

    public init(time: Double, text: String) { self.time = time; self.text = text }
}

public struct Lyrics: Sendable {
    public let synced: Bool
    public let lines: [LyricLine]
    public static let maxLines = 300

    public init(synced: Bool, lines: [LyricLine]) { self.synced = synced; self.lines = lines }

    /// Timed lines of an LRC text ("[mm:ss.xx] words"), sorted by time.
    /// Tags such as [ar:...] and lines without words are dropped.
    public static func parseLRC(_ text: String) -> [LyricLine] {
        var out: [LyricLine] = []
        for raw in text.split(whereSeparator: \.isNewline) {
            var rest = Substring(raw)
            var times: [Double] = []
            while rest.first == "[", let close = rest.firstIndex(of: "]") {
                let tag = rest[rest.index(after: rest.startIndex)..<close]
                let parts = tag.split(separator: ":")
                guard parts.count == 2, let minutes = Double(parts[0]), let seconds = Double(parts[1]) else { break }
                times.append(minutes * 60 + seconds)
                rest = rest[rest.index(after: close)...]
            }
            let words = rest.trimmingCharacters(in: .whitespaces)
            if words.isEmpty { continue }
            for t in times { out.append(LyricLine(time: t, text: words)) }
        }
        out.sort { $0.time < $1.time }
        return Array(out.prefix(maxLines))
    }

    public static func parsePlain(_ text: String) -> [LyricLine] {
        let lines = text.split(whereSeparator: \.isNewline)
            .map { $0.trimmingCharacters(in: .whitespaces) }
            .filter { !$0.isEmpty }
            .map { LyricLine(time: -1, text: $0) }
        return Array(lines.prefix(maxLines))
    }

    /// Prefers timed lyrics, falls back to plain ones, nil when there is nothing to show.
    public static func from(synced: String?, plain: String?) -> Lyrics? {
        if let s = synced {
            let lines = parseLRC(s)
            if !lines.isEmpty { return Lyrics(synced: true, lines: lines) }
        }
        if let p = plain {
            let lines = parsePlain(p)
            if !lines.isEmpty { return Lyrics(synced: false, lines: lines) }
        }
        return nil
    }
}

/// JSON body of GET /v1/track: lyrics as [[time, text], ...] and the background
/// colours as a hex string of 16x9 little-endian RGB565 pixels.
public enum TrackInfo {
    public static func encode(lyrics: Lyrics?, background: Data?) -> Data {
        var obj: [String: Any] = ["lyrics": NSNull(), "bg": NSNull()]
        if let l = lyrics {
            obj["lyrics"] = ["synced": l.synced,
                             "lines": l.lines.map { [($0.time * 100).rounded() / 100, $0.text] as [Any] }] as [String: Any]
        }
        if let bg = background { obj["bg"] = bg.map { String(format: "%02x", $0) }.joined() }
        return (try? JSONSerialization.data(withJSONObject: obj)) ?? Data("{}".utf8)
    }
}
