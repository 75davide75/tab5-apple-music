import XCTest
@testable import CompanionCore

final class MacStateTests: XCTestCase {
    private func decode(_ d: Data) -> [String: Any] { (try? JSONSerialization.jsonObject(with: d)) as? [String: Any] ?? [:] }

    func testPlayingStateHasEveryKey() {
        let s = MacState(state: .playing, title: "Titolo", artist: "Artista", album: "Album", durationS: 200, elapsedS: 12.5)
        let j = decode(s.encode())
        XCTAssertEqual(j["state"] as? String, "playing")
        XCTAssertEqual(j["title"] as? String, "Titolo")
        XCTAssertEqual(j["artist"] as? String, "Artista")
        XCTAssertEqual(j["album"] as? String, "Album")
        XCTAssertEqual(j["duration_s"] as? Double, 200)
        XCTAssertEqual(j["elapsed_s"] as? Double, 12.5)
    }

    func testStoppedState() {
        let j = decode(MacState.stopped.encode())
        XCTAssertEqual(j["state"] as? String, "stopped")
        XCTAssertEqual(j["title"] as? String, "")
        XCTAssertEqual(j["duration_s"] as? Double, 0)
    }

    func testCommandParsing() {
        XCTAssertEqual(MacCommand.parse(Data(#"{"cmd":"toggle"}"#.utf8), durationS: 100), .toggle)
        XCTAssertEqual(MacCommand.parse(Data(#"{"cmd":"next"}"#.utf8), durationS: 100), .next)
        XCTAssertEqual(MacCommand.parse(Data(#"{"cmd":"prev"}"#.utf8), durationS: 100), .previous)
        XCTAssertEqual(MacCommand.parse(Data(#"{"cmd":"seek","position_s":42.5}"#.utf8), durationS: 100), .seek(42.5))
    }

    func testSeekIsClampedToTheTrack() {
        XCTAssertEqual(MacCommand.parse(Data(#"{"cmd":"seek","position_s":-5}"#.utf8), durationS: 100), .seek(0))
        XCTAssertEqual(MacCommand.parse(Data(#"{"cmd":"seek","position_s":500}"#.utf8), durationS: 100), .seek(100))
    }

    func testBadCommandsAreRejected() {
        XCTAssertNil(MacCommand.parse(Data(#"{"cmd":"explode"}"#.utf8), durationS: 100))
        XCTAssertNil(MacCommand.parse(Data(#"{"cmd":"seek"}"#.utf8), durationS: 100))
        XCTAssertNil(MacCommand.parse(Data("not json".utf8), durationS: 100))
        XCTAssertNil(MacCommand.parse(Data(), durationS: 100))
    }
}
