import XCTest
@testable import CompanionCore

final class LyricsTests: XCTestCase {
    func testParsesTimedLines() {
        let lrc = "[00:12.34] Prima riga inventata\n[01:02.50]Seconda riga\n"
        let l = Lyrics.parseLRC(lrc)
        XCTAssertEqual(l.count, 2)
        XCTAssertEqual(l[0].time, 12.34, accuracy: 0.001)
        XCTAssertEqual(l[0].text, "Prima riga inventata")
        XCTAssertEqual(l[1].time, 62.5, accuracy: 0.001)
    }

    func testSkipsMetadataAndEmptyLines() {
        let lrc = "[ar:Qualcuno]\n[ti:Titolo]\n[length:03:20]\n[00:05.00]\n[00:07.10] Unica riga\n\nriga senza tempo\n"
        let l = Lyrics.parseLRC(lrc)
        XCTAssertEqual(l.map(\.text), ["Unica riga"])
    }

    func testRepeatedTimestampsAndSorting() {
        let lrc = "[00:30.00][00:10.00] Ritornello\n[00:20.00] Strofa\n"
        let l = Lyrics.parseLRC(lrc)
        XCTAssertEqual(l.map(\.text), ["Ritornello", "Strofa", "Ritornello"])
        XCTAssertEqual(l.map(\.time), [10, 20, 30])
    }

    func testThreeDigitFractionAndNoFraction() {
        let l = Lyrics.parseLRC("[00:01.250] a\n[00:02] b\n")
        XCTAssertEqual(l[0].time, 1.25, accuracy: 0.001)
        XCTAssertEqual(l[1].time, 2.0, accuracy: 0.001)
    }

    func testPlainLyricsKeepOrderAndDropBlankRuns() {
        let l = Lyrics.parsePlain("Uno\n\n\nDue\n  \nTre\n")
        XCTAssertEqual(l.map(\.text), ["Uno", "Due", "Tre"])
        XCTAssertTrue(l.allSatisfy { $0.time < 0 })
    }

    func testPicksSyncedOverPlain() {
        let l = Lyrics.from(synced: "[00:01.00] x", plain: "y")
        XCTAssertEqual(l?.synced, true)
        XCTAssertEqual(l?.lines.first?.text, "x")
    }

    func testFallsBackToPlainThenNil() {
        XCTAssertEqual(Lyrics.from(synced: nil, plain: "y")?.synced, false)
        XCTAssertEqual(Lyrics.from(synced: "[ar:x]", plain: "y")?.synced, false)   // synced text with no usable line
        XCTAssertNil(Lyrics.from(synced: nil, plain: nil))
        XCTAssertNil(Lyrics.from(synced: "", plain: "  \n"))
    }

    func testTrackInfoJSON() throws {
        let lyrics = Lyrics(synced: true, lines: [LyricLine(time: 1.5, text: "Ciao \"mondo\"")])
        let data = TrackInfo.encode(lyrics: lyrics, background: Data([0x00, 0xF8, 0xE0, 0x07]))
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        let l = try XCTUnwrap(json["lyrics"] as? [String: Any])
        XCTAssertEqual(l["synced"] as? Bool, true)
        let first = try XCTUnwrap((l["lines"] as? [[Any]])?.first)
        XCTAssertEqual(first[0] as? Double, 1.5)
        XCTAssertEqual(first[1] as? String, "Ciao \"mondo\"")
        XCTAssertEqual(json["bg"] as? String, "00f8e007")
    }

    func testTrackInfoJSONWithNothing() throws {
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: TrackInfo.encode(lyrics: nil, background: nil)) as? [String: Any])
        XCTAssertTrue(json["lyrics"] is NSNull)
        XCTAssertTrue(json["bg"] is NSNull)
    }

    func testLongLyricsAreCapped() {
        let lrc = (0..<500).map { String(format: "[%02d:%02d.00] riga %d", $0 / 60, $0 % 60, $0) }.joined(separator: "\n")
        XCTAssertEqual(Lyrics.parseLRC(lrc).count, Lyrics.maxLines)
    }
}
