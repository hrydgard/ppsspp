#pragma once

#import <UIKit/UIKit.h>

@protocol PPSSPPViewController<NSObject>
@optional

- (void)hideKeyboard;
- (void)showKeyboard;
- (void)shareText:(NSString *)text;
- (void)shutdown;
- (void)bindDefaultFBO;
- (UIView *)getView;
- (void)startLocation;
- (void)stopLocation;
- (void)startVideo:(int)width height:(int)height;
- (void)stopVideo;
- (void)appSwitchModeChanged;

// Forwarded from the AppDelegate
- (void)didBecomeActive;
- (void)willResignActive;

- (void)uiStateChanged;

@end

extern id <PPSSPPViewController> sharedViewController;

#define IS_IPAD() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad)
#define IS_IPHONE() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPhone)
