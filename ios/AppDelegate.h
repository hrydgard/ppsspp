// AppDelegate.h boilerplate

#import <UIKit/UIKit.h>

#include <string_view>

@interface AppDelegate : UIResponder <UIApplicationDelegate>

@property (strong, nonatomic) UIWindow *window;
@property (nonatomic, strong) NSDictionary *launchOptions;

- (void)processFilePath:(NSString *)path;

// Exports the game library to a caller app via a URL scheme callback.
// The caller requests it with "ppsspp://gameInfo?scheme=<callerScheme>", and
// PPSSPP responds by opening "<callerScheme>://ppsspp?games=<base64url-json>".
- (void)exportLibraryToScheme:(NSString *)callerScheme;

@end

void copyDeepLinkForPath(std::string_view filePath);
