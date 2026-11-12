#import "ObjCGuard.h"

BOOL tab5_try(NS_NOESCAPE void (^body)(void), NSString **reason) {
    @try {
        body();
        return YES;
    } @catch (NSException *e) {
        if (reason) *reason = [NSString stringWithFormat:@"%@: %@", e.name, e.reason];
        return NO;
    }
}
