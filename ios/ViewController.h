#pragma once

#include "ppsspp_config.h"

#import <UIKit/UIKit.h>

#import <GameController/GameController.h>

#if PPSSPP_PLATFORM(TVOS)

// The one view controller. Renders with Vulkan (MoltenVK) to a CAMetalLayer.
// A GCEventViewController, so controller input (the Siri Remote included) reaches us only through
// GCController, instead of also driving UIKit's focus engine.
@interface PPSSPPViewController : GCEventViewController

#else

#import <PhotosUI/PhotosUI.h>

#import "CameraHelper.h"
#import "LocationHelper.h"

// The one view controller. Renders with Vulkan (MoltenVK) to a CAMetalLayer.
@interface PPSSPPViewController : UIViewController<
	PHPickerViewControllerDelegate,
	CameraFrameDelegate, LocationHandlerDelegate,
	UIGestureRecognizerDelegate>

#endif

- (void)hideKeyboard;
- (void)showKeyboard;
- (void)shareText:(NSString *)text;
- (void)shareFile:(NSURL *)url;
- (void)shutdown;
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
- (void)topLevelChanged;
- (void)pickPhoto:(NSString *)saveFilename requestId:(int)requestId;
- (void)updateResolutionWithView:(UIView *)view;

@end

extern PPSSPPViewController *sharedViewController;

#define IS_IPAD() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPad)
#define IS_IPHONE() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomPhone)
#define IS_TV() ([UIDevice currentDevice].userInterfaceIdiom == UIUserInterfaceIdiomTV)
