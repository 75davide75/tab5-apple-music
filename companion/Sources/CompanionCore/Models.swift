import Foundation

public struct Track: Codable, Equatable, Sendable {
    public let id: UInt32
    public let title: String
    public let artist: String
    public let album: String
    public let durationS: Double

    public init(id: UInt32, title: String, artist: String, album: String, durationS: Double) {
        self.id = id; self.title = title; self.artist = artist; self.album = album; self.durationS = durationS
    }
}

public struct Playlist: Sendable {
    public let id: String
    public let name: String
    public let tracks: [Track]
    public let lastPlayed: Date?

    public init(id: String, name: String, tracks: [Track], lastPlayed: Date?) {
        self.id = id; self.name = name; self.tracks = tracks; self.lastPlayed = lastPlayed
    }
}

/// Case-, accent- and whitespace-insensitive form used to compare titles and artists.
public func normalizedForMatching(_ s: String) -> String {
    s.trimmingCharacters(in: .whitespacesAndNewlines)
        .folding(options: [.caseInsensitive, .diacriticInsensitive], locale: nil)
}
