import AppKit
import CompanionCore
import Foundation
import ScriptingBridge

// The parts of Music.app's scripting interface we use.
@objc private protocol MusicTrackSB {
    @objc optional var name: String { get }
    @objc optional var artist: String { get }
    @objc optional var album: String { get }
    @objc optional var duration: Double { get }
    @objc optional var databaseID: Int { get }
    @objc optional func artworks() -> SBElementArray
}

@objc private protocol MusicArtworkSB {
    @objc optional var rawData: Any { get }
    @objc optional var data: NSImage { get }
}

@objc private protocol MusicPlaylistSB {
    @objc optional var name: String { get }
    @objc optional var persistentID: String { get }
    @objc optional func tracks() -> SBElementArray
}

@objc private protocol MusicApplicationSB {
    @objc optional func userPlaylists() -> SBElementArray
    @objc optional func playlists() -> SBElementArray
}

extension SBObject: MusicTrackSB, MusicArtworkSB, MusicPlaylistSB {}
extension SBApplication: MusicApplicationSB {}

/// Reads playlists and artwork from Music.app. All calls are serialised on one queue.
final class MusicBridge {
    private let queue = DispatchQueue(label: "music-bridge")
    private let app: SBApplication?
    private var cached: [Playlist] = []
    private var cachedAt = Date.distantPast
    private let cacheLifetime: TimeInterval = 60

    init() {
        app = SBApplication(bundleIdentifier: "com.apple.Music")
        if app == nil { log("Music.app not found") }
    }

    private var refreshing = false

    /// The user's playlists. Answers from the cache straight away and refreshes it in the
    /// background once it is older than a minute; only the very first call waits.
    func playlists() -> [Playlist] {
        let (current, stale) = queue.sync { (cached, Date().timeIntervalSince(cachedAt) > cacheLifetime) }
        if !current.isEmpty {
            if stale { refreshInBackground() }
            return current
        }
        return queue.sync {
            if cached.isEmpty { cached = loadPlaylists(); cachedAt = Date() }
            return cached
        }
    }

    private func refreshInBackground() {
        queue.async {
            guard !self.refreshing else { return }
            self.refreshing = true
            let fresh = self.loadPlaylists()
            if !fresh.isEmpty { self.cached = fresh }
            self.cachedAt = Date()
            self.refreshing = false
        }
    }

    private func column<T>(_ tracks: SBElementArray, _ selector: Selector) -> [T] {
        (tracks.array(byApplying: selector) as? [T]) ?? []
    }

    private func loadPlaylists() -> [Playlist] {
        guard let app, let lists = (app as MusicApplicationSB).userPlaylists?() else { return [] }
        let started = Date()
        var result: [Playlist] = []
        for case let p as SBObject in lists {
            let pl = p as MusicPlaylistSB
            guard let name = pl.name, let pid = pl.persistentID, let tracks = pl.tracks?() else { continue }
            let names: [String] = column(tracks, #selector(getter: MusicTrackSB.name))
            if names.isEmpty { continue }
            let artists: [String] = column(tracks, #selector(getter: MusicTrackSB.artist))
            let albums: [String] = column(tracks, #selector(getter: MusicTrackSB.album))
            let durations: [NSNumber] = column(tracks, #selector(getter: MusicTrackSB.duration))
            let ids: [NSNumber] = column(tracks, #selector(getter: MusicTrackSB.databaseID))
            guard artists.count == names.count, ids.count == names.count else { continue }
            let items = names.indices.map { i in
                Track(id: ids[i].uint32Value, title: names[i], artist: artists[i],
                      album: i < albums.count ? albums[i] : "",
                      durationS: i < durations.count ? durations[i].doubleValue : 0)
            }
            result.append(Playlist(id: pid, name: name, tracks: items, lastPlayed: nil))
        }
        log("loaded \(result.count) playlists in \(String(format: "%.1f", Date().timeIntervalSince(started))) s")
        return result
    }

    /// Artwork stored in the Music library for a track, if any.
    func artwork(trackID: UInt32) -> CGImage? {
        queue.sync {
            guard let app, let all = (app as MusicApplicationSB).playlists?(),
                  let library = all.firstObject as? SBObject,
                  let tracks = (library as MusicPlaylistSB).tracks?() else { return nil }
            let found = tracks.filtered(using: NSPredicate(format: "databaseID == %d", Int(trackID)))
            guard let track = (found as? [SBObject])?.first,
                  let art = (track as MusicTrackSB).artworks?().firstObject as? SBObject else { return nil }
            let a = art as MusicArtworkSB
            if let image = a.data, let cg = image.cgImage(forProposedRect: nil, context: nil, hints: nil) { return cg }
            if let raw = a.rawData as? Data, let image = NSImage(data: raw) {
                return image.cgImage(forProposedRect: nil, context: nil, hints: nil)
            }
            return nil
        }
    }

    /// Library id of a track the user has in one of their playlists.
    func findTrackID(title: String, artist: String) -> UInt32? {
        let t = normalizedForMatching(title), a = normalizedForMatching(artist)
        for p in playlists() {
            if let hit = p.tracks.first(where: { normalizedForMatching($0.title) == t && normalizedForMatching($0.artist) == a }) {
                return hit.id
            }
        }
        return nil
    }
}

func log(_ message: String) {
    FileHandle.standardError.write(Data("[tab5-companion] \(message)\n".utf8))
}
