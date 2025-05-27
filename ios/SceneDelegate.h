// SceneDelegate.h
#import <UIKit/UIKit.h>
#import "ViewControllerCommon.h"

@interface SceneDelegate : UIResponder <UIWindowSceneDelegate>

- (void)restart:(const char *)args;

@property (strong, nonatomic) UIWindow * window;
@property (strong, nonatomic) UIWindowScene *windowScene;

@end
