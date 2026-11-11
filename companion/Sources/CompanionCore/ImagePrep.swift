import CoreGraphics
import Foundation

/// Converts artwork into the raw pixel formats the Tab5 draws directly.
public enum ImagePrep {
    private static func rgba(_ image: CGImage, width: Int, height: Int, quality: CGInterpolationQuality) -> [UInt8] {
        var buf = [UInt8](repeating: 0, count: width * height * 4)
        buf.withUnsafeMutableBytes { raw in
            guard let ctx = CGContext(data: raw.baseAddress, width: width, height: height, bitsPerComponent: 8,
                                      bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.noneSkipLast.rawValue) else { return }
            ctx.interpolationQuality = quality
            ctx.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
        }
        return buf
    }

    private static func pack565(_ rgba: [UInt8]) -> Data {
        var out = Data(capacity: rgba.count / 2)
        var i = 0
        while i < rgba.count {
            let v = (UInt16(rgba[i] >> 3) << 11) | (UInt16(rgba[i + 1] >> 2) << 5) | UInt16(rgba[i + 2] >> 3)
            out.append(UInt8(v & 0xFF))
            out.append(UInt8(v >> 8))
            i += 4
        }
        return out
    }

    /// RGB565, little endian, row by row from the top.
    public static func rgb565(_ image: CGImage, width: Int, height: Int) -> Data {
        let same = image.width == width && image.height == height
        return pack565(rgba(image, width: width, height: height, quality: same ? .none : .high))
    }

    /// A 160x90 soft wash of the artwork colours, used as the screen background.
    /// Shrinking to a handful of pixels and scaling back up gives the blur.
    public static func blurredBackground(_ image: CGImage) -> Data {
        let tiny = rgba(image, width: 8, height: 5, quality: .high)
        let provider = CGDataProvider(data: Data(tiny) as CFData)!
        let small = CGImage(width: 8, height: 5, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: 32,
                            space: CGColorSpaceCreateDeviceRGB(),
                            bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                            provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)!
        return pack565(rgba(small, width: 160, height: 90, quality: .high))
    }
}
