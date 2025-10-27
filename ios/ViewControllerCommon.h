#pragma once

#import <UIKit/UIKit.h>

#import <GameController/GameController.h>

#import "CameraHelper.h"
#import "LocationHelper.h"
#import "iCade/iCadeReaderView.h"

@interface PPSSPPBaseViewController : UIViewController<
	UIImagePickerControllerDelegate, UINavigationControllerDelegate,
	CameraFrameDelegate, LocationHandlerDelegate, UIKeyInput,
	UIGestureRecognizerDelegate, iCadeEventDelegate>

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
- (void)setupController:(GCController *)controller;

// Forwarded from the AppDelegate
- (void)didBecomeActive;
- (void)willResignActive;

- (void)uiStateChanged;
- (void)pickPhoto:(NSString *)saveFilename requestId:(int)requestId;

@end

extern PPSSPPBaseViewController *sharedViewController;

#define IS_IPAD() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad)
#define IS_IPHONE() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPhone)
