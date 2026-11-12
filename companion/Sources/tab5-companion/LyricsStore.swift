import CompanionCore
import Foundation

/// Looks lyrics up on lrclib.net (a public, community-maintained archive) and remembers the answers.
final class LyricsStore {
    private var cache: [String: Lyrics?] = [:]
    private let lock = NSLock()
    private var lookupFailed = false     // set when a request got no usable HTTP answer

    func lyrics(title: String, artist: String, album: String, duration: Double) -> Lyrics? {
        guard !title.isEmpty else { return nil }
        let key = "\(title)|\(artist)".lowercased()
        lock.lock()
        if let hit = cache[key] { lock.unlock(); return hit }
        lock.unlock()

        lookupFailed = false
        var result = exactMatch(title: title, artist: artist, album: album, duration: duration)
        if result?.synced != true, let better = search(title: title, artist: artist, duration: duration) {
            if better.synced || result == nil { result = better }
        }
        log("lyrics '\(title)' -> \(result.map { $0.synced ? "synced, \($0.lines.count) lines" : "plain" } ?? "none")")

        // "No lyrics" is remembered only when the archive really said so, not when it was unreachable.
        if result != nil || !lookupFailed {
            lock.lock()
            if cache.count > 300 { cache.removeAll() }
            cache[key] = result
            lock.unlock()
        }
        return result
    }

    private func getJSON(_ path: String, _ items: [URLQueryItem]) -> Any? {
        var parts = URLComponents(string: "https://lrclib.net/api/\(path)")!
        parts.queryItems = items
        guard let url = parts.url else { return nil }
        var request = URLRequest(url: url, timeoutInterval: 8)
        request.setValue("tab5-companion (personal now-playing display)", forHTTPHeaderField: "User-Agent")
        let done = DispatchSemaphore(value: 0)
        var body: Data?
        var status = 0
        URLSession.shared.dataTask(with: request) { data, response, _ in
            status = (response as? HTTPURLResponse)?.statusCode ?? 0
            if status == 200 { body = data }
            done.signal()
        }.resume()
        done.wait()
        if status != 200 && status != 404 { lookupFailed = true }
        return body.flatMap { try? JSONSerialization.jsonObject(with: $0) }
    }

    private func lyrics(from entry: [String: Any]) -> Lyrics? {
        Lyrics.from(synced: entry["syncedLyrics"] as? String, plain: entry["plainLyrics"] as? String)
    }

    private func exactMatch(title: String, artist: String, album: String, duration: Double) -> Lyrics? {
        var items = [URLQueryItem(name: "track_name", value: title), URLQueryItem(name: "artist_name", value: artist)]
        if !album.isEmpty { items.append(URLQueryItem(name: "album_name", value: album)) }
        if duration > 0 { items.append(URLQueryItem(name: "duration", value: String(Int(duration.rounded())))) }
        guard let entry = getJSON("get", items) as? [String: Any] else { return nil }
        return lyrics(from: entry)
    }

    /// Looser search: the entry by the same artist whose length is closest to ours, timed ones first.
    private func search(title: String, artist: String, duration: Double) -> Lyrics? {
        let items = [URLQueryItem(name: "track_name", value: title), URLQueryItem(name: "artist_name", value: artist)]
        guard let entries = getJSON("search", items) as? [[String: Any]] else { return nil }
        let wanted = normalizedForMatching(artist)
        let usable = entries.filter { normalizedForMatching(($0["artistName"] as? String) ?? "") == wanted }
        func distance(_ e: [String: Any]) -> Double {
            duration > 0 ? abs(((e["duration"] as? Double) ?? 0) - duration) : 0
        }
        // A different length usually means a different edit, whose timing would be wrong.
        let timed = usable.filter { ($0["syncedLyrics"] as? String)?.isEmpty == false && distance($0) <= 3 }
        if let best = timed.min(by: { distance($0) < distance($1) }), let l = lyrics(from: best) { return l }
        if let any = usable.first(where: { ($0["plainLyrics"] as? String)?.isEmpty == false }) {
            return Lyrics.from(synced: nil, plain: any["plainLyrics"] as? String)
        }
        return nil
    }
}
