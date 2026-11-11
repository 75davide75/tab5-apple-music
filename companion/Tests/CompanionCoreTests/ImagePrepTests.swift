import XCTest
import CoreGraphics
@testable import CompanionCore

final class ImagePrepTests: XCTestCase {
    private func image(_ pixels: [UInt8], width: Int, height: Int) -> CGImage {
        let data = CFDataCreate(nil, pixels, pixels.count)!
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                       space: CGColorSpaceCreateDeviceRGB(),
                       bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                       provider: CGDataProvider(data: data)!, decode: nil, shouldInterpolate: false,
                       intent: .defaultIntent)!
    }

    func testPureColoursBecomeLittleEndianRGB565() {
        let img = image([255, 0, 0, 255,  0, 255, 0, 255,
                         0, 0, 255, 255,  255, 255, 255, 255], width: 2, height: 2)
        let out = ImagePrep.rgb565(img, width: 2, height: 2)
        XCTAssertEqual([UInt8](out), [0x00, 0xF8,  0xE0, 0x07,  0x1F, 0x00,  0xFF, 0xFF])
    }

    func testOutputSizeFollowsRequestedSize() {
        let img = image([UInt8](repeating: 128, count: 8 * 8 * 4), width: 8, height: 8)
        XCTAssertEqual(ImagePrep.rgb565(img, width: 52, height: 52).count, 52 * 52 * 2)
        XCTAssertEqual(ImagePrep.rgb565(img, width: 400, height: 400).count, 320_000)
    }

    func testBlurredBackgroundIs160x90() {
        let img = image([UInt8](repeating: 200, count: 8 * 8 * 4), width: 8, height: 8)
        XCTAssertEqual(ImagePrep.blurredBackground(img).count, 160 * 90 * 2)
    }

    private func unpack(_ data: Data) -> [(r: Int, g: Int, b: Int)] {
        let bytes = [UInt8](data)
        return stride(from: 0, to: bytes.count, by: 2).map {
            let v = Int(bytes[$0]) | (Int(bytes[$0 + 1]) << 8)
            return (r: (v >> 11) << 3, g: ((v >> 5) & 63) << 2, b: (v & 31) << 3)
        }
    }

    // White text sits on top of this background, so a bright cover must be toned down.
    func testBrightArtworkGivesDarkBackground() {
        let img = image([UInt8](repeating: 255, count: 16 * 16 * 4), width: 16, height: 16)
        let px = unpack(ImagePrep.blurredBackground(img))
        let brightest = px.map { max($0.r, $0.g, $0.b) }.max() ?? 255
        XCTAssertLessThanOrEqual(brightest, 120)
    }

    func testDarkArtworkIsNotBrightened() {
        let img = image([UInt8](repeating: 30, count: 16 * 16 * 4), width: 16, height: 16)
        let px = unpack(ImagePrep.blurredBackground(img))
        XCTAssertLessThanOrEqual(px.map { $0.r }.max() ?? 255, 40)
    }

    // Left half black, right half white: the result must be a smooth ramp, not blocks.
    func testBackgroundIsSmooth() {
        var raw = [UInt8]()
        for _ in 0..<16 { for x in 0..<16 { raw += x < 8 ? [0, 0, 0, 255] : [255, 255, 255, 255] } }
        let px = unpack(ImagePrep.blurredBackground(image(raw, width: 16, height: 16)))
        var biggestStep = 0
        for y in 0..<90 { for x in 1..<160 { biggestStep = max(biggestStep, abs(px[y * 160 + x].g - px[y * 160 + x - 1].g)) } }
        XCTAssertLessThanOrEqual(biggestStep, 8)
        XCTAssertLessThan(px[45 * 160 + 5].g, px[45 * 160 + 154].g)
    }
}
