import XCTest
@testable import CompanionCore

final class ResolveResponseTests: XCTestCase {
    private let playlist = Playlist(id: "p", name: "Sera in casa", tracks: (0..<5).map {
        Track(id: UInt32(100 + $0), title: "Brano \($0)", artist: "A", album: "B", durationS: 180.5)
    }, lastPlayed: nil)

    private func decode(_ data: Data) -> [String: Any] {
        (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] ?? [:]
    }

    func testShuffleReturnsWholePlaylist() {
        let json = decode(ResolveResponse.encode(match: Match(playlist: playlist, currentPosition: 2, inOrder: false)))
        XCTAssertEqual((json["playlist"] as? [String: Any])?["name"] as? String, "Sera in casa")
        XCTAssertEqual((json["playlist"] as? [String: Any])?["count"] as? Int, 5)
        XCTAssertEqual(json["current"] as? Int, 2)
        XCTAssertEqual(json["track_id"] as? Int, 102)
        XCTAssertEqual((json["tracks"] as? [[String: Any]])?.count, 5)
        XCTAssertEqual(json["ordered"] as? Bool, false)
    }

    func testInOrderReturnsOnlyUpcomingTracks() {
        let json = decode(ResolveResponse.encode(match: Match(playlist: playlist, currentPosition: 2, inOrder: true)))
        let tracks = json["tracks"] as? [[String: Any]] ?? []
        XCTAssertEqual(tracks.map { $0["id"] as? Int }, [103, 104])
        XCTAssertEqual(json["ordered"] as? Bool, true)
        XCTAssertEqual(tracks.first?["title"] as? String, "Brano 3")
        XCTAssertEqual(tracks.first?["duration_s"] as? Double, 180.5)
        XCTAssertEqual(json["track_id"] as? Int, 102)
    }

    func testLastTrackInOrderHasNoUpcoming() {
        let json = decode(ResolveResponse.encode(match: Match(playlist: playlist, currentPosition: 4, inOrder: true)))
        XCTAssertEqual((json["tracks"] as? [[String: Any]])?.count, 0)
    }

    func testNoMatch() {
        let json = decode(ResolveResponse.encode(match: nil))
        XCTAssertTrue(json["playlist"] is NSNull)
        XCTAssertTrue(json["track_id"] is NSNull)
        XCTAssertEqual((json["tracks"] as? [[String: Any]])?.count, 0)
    }
}
