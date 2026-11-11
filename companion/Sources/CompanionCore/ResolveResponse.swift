import Foundation

/// JSON body of GET /v1/resolve.
public enum ResolveResponse {
    public static func encode(match: Match?) -> Data {
        var obj: [String: Any] = ["playlist": NSNull(), "current": 0, "track_id": NSNull(), "ordered": false,
                                  "tracks": [[String: Any]]()]
        if let m = match {
            let all = m.playlist.tracks
            // In order we list what comes next; otherwise we can only show the playlist itself.
            let shown = m.inOrder ? all[min(m.currentPosition + 1, all.count)...] : all[...]
            obj["ordered"] = m.inOrder
            obj["playlist"] = ["name": m.playlist.name, "count": all.count]
            obj["current"] = m.currentPosition
            obj["track_id"] = Int(all[m.currentPosition].id)
            obj["tracks"] = shown.map {
                ["id": Int($0.id), "title": $0.title, "artist": $0.artist, "duration_s": $0.durationS] as [String: Any]
            }
        }
        return (try? JSONSerialization.data(withJSONObject: obj)) ?? Data("{}".utf8)
    }
}
