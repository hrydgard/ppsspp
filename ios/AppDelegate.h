// AppDelegate.h boilerplate

#import <UIKit/UIKit.h>

#include <string_view>

@protocol PPSSPPViewController;

@interface AppDelegate : UIResponder <UIApplicationDelegate>

@property (strong, nonatomic) UIWindow *window;
@property (strong, nonatomic) UIScreen *screen;
@property (nonatomic, strong) NSDictionary *launchOptions;

- (BOOL)launchPPSSPP:(int)argc argv:(char**)argv;
- (void)processFilePath:(NSString *)path;

@end

void copyDeepLinkForPath(std::string_view filePath);
