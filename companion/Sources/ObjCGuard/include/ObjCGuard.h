#import <Foundation/Foundation.h>

/// Runs `body` and returns YES, or returns NO if it raised an Objective-C exception.
/// Swift cannot catch NSException, and ScriptingBridge raises them when the scripted
/// app quits or changes its lists while we are reading them.
BOOL tab5_try(NS_NOESCAPE void (^ _Nonnull body)(void), NSString * _Nullable * _Nullable reason);
