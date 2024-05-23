// AppDelegate.h boilerplate

#import <UIKit/UIKit.h>

@protocol PPSSPPViewController;

@interface AppDelegate : UIResponder <UIApplicationDelegate>

@property (strong, nonatomic) UIWindow *window;
@property (strong, nonatomic) UIScreen *screen;

@property (strong, nonatomic) id<PPSSPPViewController> viewController;

- (void)restart:(const char *)args;
- (BOOL)launchPPSSPP:(int)argc argv:(char**)argv;

@end
