import CompanionCore
import CoreGraphics
import Foundation

let bridge = MusicBridge()
let artwork = ArtworkStore(bridge: bridge)
let matcher = PlaylistMatcher()
let matcherLock = NSLock()

func image(for q: [String: String]) -> CGImage? {
    artwork.image(trackID: q["id"].flatMap { UInt32($0) }, title: q["title"] ?? "", artist: q["artist"] ?? "",
                  album: q["album"] ?? "")
}

func handle(_ request: HTTPRequest) -> HTTPResponse {
    let q = request.query
    switch (request.method, request.path) {
    case ("GET", "/v1/resolve"):
        let query = Query(title: q["title"] ?? "", artist: q["artist"] ?? "", album: q["album"] ?? "",
                          index: q["index"].flatMap { Int($0) }, count: q["count"].flatMap { Int($0) },
                          shuffle: q["shuffle"] == "1")
        let lists = bridge.playlists()
        matcherLock.lock()
        let match = matcher.match(query, in: lists)
        matcherLock.unlock()
        log("resolve '\(query.title)' -> \(match?.playlist.name ?? "no playlist")")
        return HTTPResponse(body: ResolveResponse.encode(match: match))

    case ("GET", "/v1/art"):
        let size = q["size"] == "52" ? 52 : 400
        guard let cg = image(for: q) else {
            log("art \(q["id"] ?? q["title"] ?? "?") size \(size) -> not found")
            return .notFound()
        }
        return HTTPResponse(contentType: "application/octet-stream", body: ImagePrep.rgb565(cg, width: size, height: size))

    case ("GET", "/v1/bg"):
        guard let cg = image(for: q) else { return .notFound() }
        return HTTPResponse(contentType: "application/octet-stream", body: ImagePrep.blurredBackground(cg))

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
