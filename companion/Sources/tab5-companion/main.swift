import CompanionCore
import CoreGraphics
import Foundation

let bridge = MusicBridge()
let artwork = ArtworkStore(bridge: bridge)
let lyricsStore = LyricsStore()

func image(for q: [String: String]) -> CGImage? {
    artwork.image(trackID: nil, title: q["title"] ?? "", artist: q["artist"] ?? "", album: q["album"] ?? "")
}

func handle(_ request: HTTPRequest) -> HTTPResponse {
    let q = request.query
    switch (request.method, request.path) {
    case ("GET", "/v1/track"):
        // Lyrics and background colours in one small answer: every request is slow on the tablet's link.
        let lyrics = lyricsStore.lyrics(title: q["title"] ?? "", artist: q["artist"] ?? "", album: q["album"] ?? "",
                                        duration: Double(q["duration"] ?? "") ?? 0)
        let background = image(for: q).map { ImagePrep.smallBackground($0) }
        return HTTPResponse(body: TrackInfo.encode(lyrics: lyrics, background: background))

    case ("GET", "/v1/cover.jpg"):
        guard let cg = image(for: q), let jpeg = ImagePrep.jpeg(cg, size: 400) else {
            log("cover '\(q["title"] ?? "?")' -> not found")
            return .notFound()
        }
        return HTTPResponse(contentType: "image/jpeg", body: jpeg)

    case ("GET", "/v1/blob"):   // link speed test (firmware built with LINK_TEST)
        let n = min(q["n"].flatMap { Int($0) } ?? 0, 1_000_000)
        return HTTPResponse(contentType: "application/octet-stream", body: Data(repeating: 0x5A, count: n))

    default:
        return .notFound()
    }
}

// Kept at file scope: the listener stops serving if the server object is released.
let server: HTTPServer
do {
    server = try HTTPServer(port: 8787, service: "_tab5music._tcp", handler: handle)
} catch {
    log("cannot start server: \(error)")
    exit(1)
}
server.start()
log("listening on port 8787")
DispatchQueue.global().async { _ = bridge.playlists() }
RunLoop.main.run()
