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

/// What the Tab5 knows about the track playing on the iPhone.
public struct Query: Sendable {
    public let title: String
    public let artist: String
    public let album: String
    public let index: Int?      // 0-based position in the queue, when known
    public let count: Int?      // queue length, when known
    public let shuffle: Bool

    public init(title: String, artist: String, album: String, index: Int?, count: Int?, shuffle: Bool) {
        self.title = title; self.artist = artist; self.album = album
        self.index = index; self.count = count; self.shuffle = shuffle
    }
}

public struct Match: Sendable {
    public let playlist: Playlist
    public let currentPosition: Int
    /// True only when the phone's queue position is this track's position in the playlist,
    /// i.e. the playlist order can be shown as "what plays next".
    public let inOrder: Bool

    public init(playlist: Playlist, currentPosition: Int, inOrder: Bool) {
        self.playlist = playlist; self.currentPosition = currentPosition; self.inOrder = inOrder
    }
}
