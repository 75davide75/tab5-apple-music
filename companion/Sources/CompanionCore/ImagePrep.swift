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
    /// White text is drawn over it, so it is blurred until featureless and toned down.
    public static func blurredBackground(_ image: CGImage) -> Data {
        let w = 160, h = 90
        let src = rgba(image, width: w, height: h, quality: .medium)
        var px = [Float](repeating: 0, count: w * h * 3)
        for i in 0..<(w * h) { for c in 0..<3 { px[i * 3 + c] = Float(src[i * 4 + c]) } }

        // Three box-blur passes approximate a wide Gaussian.
        var tmp = px
        for _ in 0..<3 {
            boxBlur(&px, &tmp, w: w, h: h, radius: 28, horizontal: true)
            boxBlur(&tmp, &px, w: w, h: h, radius: 18, horizontal: false)
        }

        let peak = px.max() ?? 0
        let gain: Float = peak > 105 ? 105 / peak : 1
        var out = [UInt8](repeating: 255, count: w * h * 4)
        for i in 0..<(w * h) { for c in 0..<3 { out[i * 4 + c] = UInt8(max(0, min(255, px[i * 3 + c] * gain))) } }
        return pack565(out)
    }

    private static func boxBlur(_ src: inout [Float], _ dst: inout [Float], w: Int, h: Int, radius: Int,
                                horizontal: Bool) {
        let lines = horizontal ? h : w, length = horizontal ? w : h
        for line in 0..<lines {
            for c in 0..<3 {
                func at(_ i: Int) -> Int {
                    let k = min(max(i, 0), length - 1)   // clamp at the edges
                    return ((horizontal ? line * w + k : k * w + line) * 3) + c
                }
                var sum: Float = 0
                for i in -radius...radius { sum += src[at(i)] }
                for i in 0..<length {
                    dst[at(i)] = sum / Float(2 * radius + 1)
                    sum += src[at(i + radius + 1)] - src[at(i - radius)]
                }
            }
        }
    }
}
