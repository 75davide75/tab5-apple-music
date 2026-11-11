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
}
