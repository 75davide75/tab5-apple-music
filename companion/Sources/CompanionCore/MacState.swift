import Foundation

/// What the Music app on the Mac is doing: the body of GET /v1/mac/state.
public struct MacState: Sendable {
    public enum Playback: String, Sendable { case playing, paused, stopped }

    public let state: Playback
    public let title: String
    public let artist: String
    public let album: String
    public let durationS: Double
    public let elapsedS: Double

    public static let stopped = MacState(state: .stopped, title: "", artist: "", album: "", durationS: 0, elapsedS: 0)

    public init(state: Playback, title: String, artist: String, album: String, durationS: Double, elapsedS: Double) {
        self.state = state; self.title = title; self.artist = artist; self.album = album
        self.durationS = durationS; self.elapsedS = elapsedS
    }

    public func encode() -> Data {
        let obj: [String: Any] = ["state": state.rawValue, "title": title, "artist": artist, "album": album,
                                  "duration_s": durationS, "elapsed_s": elapsedS]
        return (try? JSONSerialization.data(withJSONObject: obj)) ?? Data("{}".utf8)
    }
}

/// A transport command from the Tab5: the body of POST /v1/mac/command.
public enum MacCommand: Equatable, Sendable {
    case toggle, next, previous
    case seek(Double)

    public static func parse(_ body: Data, durationS: Double) -> MacCommand? {
        guard let obj = (try? JSONSerialization.jsonObject(with: body)) as? [String: Any],
              let cmd = obj["cmd"] as? String else { return nil }
        switch cmd {
        case "toggle": return .toggle
        case "next": return .next
        case "prev": return .previous
        case "seek":
            guard let position = obj["position_s"] as? Double else { return nil }
            return .seek(min(max(position, 0), max(durationS, 0)))
        default: return nil
        }
    }
}
