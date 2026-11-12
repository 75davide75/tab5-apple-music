import CompanionCore
import CoreGraphics
import Foundation
import ImageIO

/// Finds cover art: first in the Music library, then in the public iTunes Search catalogue.
final class ArtworkStore {
    private let bridge: MusicBridge
    private var cache: [String: CGImage] = [:]
    private var order: [String] = []
    private let lock = NSLock()
    private let limit = 200

    init(bridge: MusicBridge) { self.bridge = bridge }

    func image(trackID: UInt32?, title: String, artist: String, album: String) -> CGImage? {
        let key = trackID.map { "id:\($0)" } ?? "q:\(title)|\(artist)|\(album)".lowercased()
        lock.lock()
        if let hit = cache[key] { lock.unlock(); return hit }
        lock.unlock()

        // The artwork in the user's own library is the exact one they see on the phone.
        var found: CGImage?
        if let id = trackID ?? bridge.findTrackID(title: title, artist: artist) { found = bridge.artwork(trackID: id) }
        if found == nil, !title.isEmpty { found = searchCatalogue(title: title, artist: artist) }
        guard let image = found else { return nil }

        lock.lock()
        cache[key] = image
        order.append(key)
        if order.count > limit { cache.removeValue(forKey: order.removeFirst()) }
        lock.unlock()
        return image
    }

    private func fetch(_ url: URL) -> Data? {
        var request = URLRequest(url: url, timeoutInterval: 6)
        request.setValue("tab5-companion", forHTTPHeaderField: "User-Agent")
        let done = DispatchSemaphore(value: 0)
        var body: Data?
        URLSession.shared.dataTask(with: request) { data, response, _ in
            if (response as? HTTPURLResponse)?.statusCode == 200 { body = data }
            done.signal()
        }.resume()
        done.wait()
        return body
    }

    private func searchCatalogue(title: String, artist: String) -> CGImage? {
        var parts = URLComponents(string: "https://itunes.apple.com/search")!
        parts.queryItems = [URLQueryItem(name: "entity", value: "song"), URLQueryItem(name: "limit", value: "5"),
                            URLQueryItem(name: "term", value: "\(title) \(artist)")]
        guard let url = parts.url, let data = fetch(url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let results = json["results"] as? [[String: Any]] else { return nil }
        let wanted = normalizedForMatching(artist)
        let sameArtist = results.first { r in
            normalizedForMatching((r["artistName"] as? String) ?? "") == wanted
        }
        let pick: [String: Any]? = sameArtist ?? results.first
        guard let small = pick?["artworkUrl100"] as? String else { return nil }
        let large = small.replacingOccurrences(of: "100x100", with: "600x600")
        guard let artURL = URL(string: large), let bytes = fetch(artURL),
              let source = CGImageSourceCreateWithData(bytes as CFData, nil) else { return nil }
        return CGImageSourceCreateImageAtIndex(source, 0, nil)
    }
}
