import XCTest
@testable import CompanionCore

final class PlaylistMatcherTests: XCTestCase {
    private func track(_ id: UInt32, _ title: String, _ artist: String = "Artista") -> Track {
        Track(id: id, title: title, artist: artist, album: "Album", durationS: 200)
    }

    private func playlist(_ id: String, count: Int, lastPlayed: Date? = nil, firstID: UInt32 = 1) -> Playlist {
        let tracks = (0..<count).map { track(firstID + UInt32($0), "Brano \($0)") }
        return Playlist(id: id, name: "Lista \(id)", tracks: tracks, lastPlayed: lastPlayed)
    }

    private func query(_ title: String, artist: String = "Artista", index: Int? = nil, count: Int? = nil,
                       shuffle: Bool = true) -> Query {
        Query(title: title, artist: artist, album: "Album", index: index, count: count, shuffle: shuffle)
    }

    func testSinglePlaylistIsFoundWithPosition() {
        let m = PlaylistMatcher().match(query("Brano 13", index: 13, count: 42, shuffle: false),
                                        in: [playlist("a", count: 42)])
        XCTAssertEqual(m?.playlist.id, "a")
        XCTAssertEqual(m?.currentPosition, 13)
    }

    func testCountMismatchIsRejected() {
        XCTAssertNil(PlaylistMatcher().match(query("Brano 3", count: 41), in: [playlist("a", count: 42)]))
    }

    func testWithoutShufflePositionMustMatch() {
        XCTAssertNil(PlaylistMatcher().match(query("Brano 13", index: 5, count: 42, shuffle: false),
                                             in: [playlist("a", count: 42)]))
    }

    func testWithShufflePositionIsIgnored() {
        let m = PlaylistMatcher().match(query("Brano 13", index: 5, count: 42, shuffle: true),
                                        in: [playlist("a", count: 42)])
        XCTAssertEqual(m?.currentPosition, 13)
    }

    func testTieIsBrokenByLastPlayedAndThenSticks() {
        let old = Date(timeIntervalSince1970: 1000), new = Date(timeIntervalSince1970: 2000)
        let matcher = PlaylistMatcher()
        let first = matcher.match(query("Brano 2", count: 10),
                                  in: [playlist("a", count: 10, lastPlayed: old), playlist("b", count: 10, lastPlayed: new)])
        XCTAssertEqual(first?.playlist.id, "b")
        // the other list becomes "more recent": the choice must not flip between tracks
        let second = matcher.match(query("Brano 7", count: 10),
                                   in: [playlist("a", count: 10, lastPlayed: new), playlist("b", count: 10, lastPlayed: old)])
        XCTAssertEqual(second?.playlist.id, "b")
    }

    func testTieWithoutDatesFallsBackToID() {
        let m = PlaylistMatcher().match(query("Brano 2", count: 10),
                                        in: [playlist("z", count: 10), playlist("c", count: 10)])
        XCTAssertEqual(m?.playlist.id, "c")
    }

    func testStickyChoiceIsDroppedWhenItNoLongerMatches() {
        let matcher = PlaylistMatcher()
        _ = matcher.match(query("Brano 2", count: 10), in: [playlist("a", count: 10)])
        let m = matcher.match(query("Brano 2", count: 12), in: [playlist("a", count: 10), playlist("b", count: 12)])
        XCTAssertEqual(m?.playlist.id, "b")
    }

    func testMatchingIgnoresCaseAccentsAndOuterSpaces() {
        let p = Playlist(id: "a", name: "x", tracks: [track(1, "Città Vuota", "Mina")], lastPlayed: nil)
        let m = PlaylistMatcher().match(query("CITTÀ vuota ", artist: " mina", count: 1), in: [p])
        XCTAssertEqual(m?.currentPosition, 0)
        XCTAssertNotNil(PlaylistMatcher().match(query("citta vuota", artist: "Mina", count: 1), in: [p]))
    }

    func testArtistMustMatch() {
        let p = Playlist(id: "a", name: "x", tracks: [track(1, "Intro", "Uno")], lastPlayed: nil)
        XCTAssertNil(PlaylistMatcher().match(query("Intro", artist: "Due", count: 1), in: [p]))
    }

    func testTrackInNoPlaylist() {
        XCTAssertNil(PlaylistMatcher().match(query("Sconosciuto", count: 42), in: [playlist("a", count: 42)]))
        XCTAssertNil(PlaylistMatcher().match(query("Brano 1"), in: []))
    }

    func testUnknownCountAcceptsAnySize() {
        let m = PlaylistMatcher().match(query("Brano 3", count: nil), in: [playlist("a", count: 99)])
        XCTAssertEqual(m?.playlist.id, "a")
    }

    func testDuplicateTrackWithoutShuffleUsesReportedIndex() {
        let tracks = [track(1, "Intro"), track(2, "Altro"), track(3, "Intro")]
        let p = Playlist(id: "a", name: "x", tracks: tracks, lastPlayed: nil)
        let m = PlaylistMatcher().match(query("Intro", index: 2, count: 3, shuffle: false), in: [p])
        XCTAssertEqual(m?.currentPosition, 2)
    }
}
