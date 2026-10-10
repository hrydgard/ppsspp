#pragma once

#import <UIKit/UIKit.h>
#import <PhotosUI/PhotosUI.h>

#import <GameController/GameController.h>

#import "CameraHelper.h"
#import "LocationHelper.h"

@interface PPSSPPBaseViewController : UIViewController<
	PHPickerViewControllerDelegate,
	CameraFrameDelegate, LocationHandlerDelegate,
	UIGestureRecognizerDelegate>

- (void)hideKeyboard;
- (void)showKeyboard;
- (void)shareText:(NSString *)text;
- (void)shareFile:(NSURL *)url;
- (void)shutdown;
- (void)bindDefaultFBO;
- (void)startLocation;
- (void)stopLocation;
- (void)startVideo:(int)width height:(int)height;
- (void)stopVideo;
- (void)appSwitchModeChanged;
- (void)immersiveModeChanged;
- (void)setupController:(GCController *)controller;

// Forwarded from the AppDelegate
- (void)didBecomeActive;
- (void)willResignActive;

- (void)uiStateChanged;
- (void)pickPhoto:(NSString *)saveFilename requestId:(int)requestId;
- (void)updateResolutionWithView:(UIView *)view;

@end

extern PPSSPPBaseViewController *sharedViewController;

#define IS_IPAD() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad)
