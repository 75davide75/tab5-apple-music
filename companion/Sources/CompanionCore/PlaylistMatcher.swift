import Foundation

/// Works out which of the user's playlists the iPhone is playing from.
/// Keeps its previous answer while that playlist still fits, so the choice
/// does not flip between tracks when two playlists are equally plausible.
public final class PlaylistMatcher {
    private var lastChoiceID: String?

    public init() {}

    static func normalize(_ s: String) -> String {
        s.trimmingCharacters(in: .whitespacesAndNewlines)
            .folding(options: [.caseInsensitive, .diacriticInsensitive], locale: nil)
    }

    public func match(_ q: Query, in playlists: [Playlist]) -> Match? {
        let title = Self.normalize(q.title), artist = Self.normalize(q.artist)
        var candidates: [Match] = []

        for p in playlists {
            if let count = q.count, p.tracks.count != count { continue }
            let positions = p.tracks.indices.filter {
                Self.normalize(p.tracks[$0].title) == title && Self.normalize(p.tracks[$0].artist) == artist
            }
            guard let first = positions.first else { continue }
            if !q.shuffle, let index = q.index {
                // In order, the reported queue position must be this very track.
                guard positions.contains(index) else { continue }
                candidates.append(Match(playlist: p, currentPosition: index))
            } else {
                candidates.append(Match(playlist: p, currentPosition: first))
            }
        }

        let chosen = candidates.first { $0.playlist.id == lastChoiceID }
            ?? candidates.min { a, b in
                let da = a.playlist.lastPlayed ?? .distantPast, db = b.playlist.lastPlayed ?? .distantPast
                return da != db ? da > db : a.playlist.id < b.playlist.id
            }
        lastChoiceID = chosen?.playlist.id
        return chosen
    }
}
