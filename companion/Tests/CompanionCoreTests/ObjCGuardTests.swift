import Foundation
import ObjCGuard
import XCTest

final class ObjCGuardTests: XCTestCase {
    func testReturnsTrueWhenNothingIsRaised() {
        var ran = false
        XCTAssertTrue(tab5_try({ ran = true }, nil))
        XCTAssertTrue(ran)
    }

    // The crash seen in the field: enumerating a list that shrank underneath us.
    func testCatchesRangeExceptionInsteadOfCrashing() {
        var reason: NSString?
        let ok = tab5_try({
            let a = NSArray(array: [1, 2, 3])
            _ = a.subarray(with: NSRange(location: 0, length: 13))
        }, &reason)
        XCTAssertFalse(ok)
        XCTAssertTrue((reason as String?)?.contains("NSRangeException") ?? false)
    }
}
