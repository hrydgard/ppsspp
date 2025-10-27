#import "ios/CameraHelper.h"
#import "ios/ViewControllerCommon.h"
#import "ios/Controls.h"
#import "ios/IAPManager.h"
#include "Common/System/Request.h"
#include "Common/Input/InputState.h"
#include "Common/System/NativeApp.h"
#include "Common/Log.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbGps.h"
#include "Core/System.h"
#include "Core/Config.h"

@interface PPSSPPBaseViewController () {
	int imageRequestId;
	NSString *imageFilename;
	CameraHelper *cameraHelper;
	LocationHelper *locationHelper;
	TouchTracker g_touchTracker;
}

@end

@implementation PPSSPPBaseViewController {
	UIScreenEdgePanGestureRecognizer *mBackGestureRecognizer;
}

- (void)viewDidAppear:(BOOL)animated {
	[super viewDidAppear:animated];
	INFO_LOG(Log::G3D, "viewDidAppear");
	[self hideKeyboard];
	[self updateGesture];

	// This needs to be called really late during startup, unfortunately.
#if PPSSPP_PLATFORM(IOS_APP_STORE)
	[IAPManager sharedIAPManager];  // Kick off the IAPManager early.
	NSLog(@"Metal viewDidAppear. updating icon");
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(4.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		[[IAPManager sharedIAPManager] updateIcon:false];
		[self hideKeyboard];
	});
#endif  // IOS_APP_STORE
}

// Enables tapping for edge area.
-(UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
	if (GetUIState() == UISTATE_INGAME) {
		// In-game, we need all the control we can get. Though, we could possibly
		// allow the top edge?
		INFO_LOG(Log::System, "Defer system gestures on all edges");
		return UIRectEdgeAll;
	} else {
		INFO_LOG(Log::System, "Allow system gestures on the bottom");
		// Allow task switching gestures to take precedence, without causing
		// scroll events in the UI. Otherwise, we get "ghost" scrolls when switching tasks.
		return UIRectEdgeTop | UIRectEdgeLeft | UIRectEdgeRight;
	}
}

- (void)touchesBegan:(NSSet *)touches withEvent:(UIEvent *)event
{
	g_touchTracker.Began(touches, self.view);
}

- (void)touchesMoved:(NSSet *)touches withEvent:(UIEvent *)event
{
	g_touchTracker.Moved(touches, self.view);
}

- (void)touchesEnded:(NSSet *)touches withEvent:(UIEvent *)event
{
	g_touchTracker.Ended(touches, self.view);
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
	g_touchTracker.Cancelled(touches, self.view);
}

- (void)pickPhoto:(NSString *)saveFilename requestId:(int)requestId {
	imageRequestId = requestId;
	imageFilename = saveFilename;
	NSLog(@"Picking photo to save to %@ (id: %d)", saveFilename, requestId);

	UIImagePickerController *picker = [[UIImagePickerController alloc] init];
	picker.sourceType = UIImagePickerControllerSourceTypePhotoLibrary;
	picker.delegate = self;
	[self presentViewController:picker animated:YES completion:nil];
}

- (void)imagePickerController:(UIImagePickerController *)picker
		didFinishPickingMediaWithInfo:(NSDictionary<UIImagePickerControllerInfoKey,id> *)info {

	UIImage *image = info[UIImagePickerControllerOriginalImage];

	// Convert to JPEG with 90% quality
	NSData *jpegData = UIImageJPEGRepresentation(image, 0.9);
	if (jpegData) {
		// Do something with the JPEG data (e.g., save to file)
		[jpegData writeToFile:imageFilename atomically:YES];
		NSLog(@"Saved JPEG image to %@", imageFilename);
		g_requestManager.PostSystemSuccess(imageRequestId, "", 1);
	} else {
		g_requestManager.PostSystemFailure(imageRequestId);
	}

	[picker dismissViewControllerAnimated:YES completion:nil];
	[self hideKeyboard];
}

- (void)imagePickerControllerDidCancel:(UIImagePickerController *)picker {
	NSLog(@"User cancelled image picker");

	[picker dismissViewControllerAnimated:YES completion:nil];

	// You can also call your custom callback or use the requestId here
	g_requestManager.PostSystemFailure(imageRequestId);
	[self hideKeyboard];
}

- (void)handleSwipeFrom:(UIScreenEdgePanGestureRecognizer *)recognizer {
	if (recognizer.state == UIGestureRecognizerStateEnded) {
		KeyInput key;
		key.flags = KEY_DOWN | KEY_UP;
		key.keyCode = NKCODE_BACK;
		key.deviceId = DEVICE_ID_TOUCH;
		NativeKey(key);
		INFO_LOG(Log::System, "Detected back swipe");
	}
}

- (void)updateGesture {
	INFO_LOG(Log::System, "Updating swipe gesture.");

	if (mBackGestureRecognizer) {
		INFO_LOG(Log::System, "Removing swipe gesture.");
		[[self view] removeGestureRecognizer:mBackGestureRecognizer];
		mBackGestureRecognizer = nil;
	}

	if (GetUIState() != UISTATE_INGAME) {
		INFO_LOG(Log::System, "Adding swipe gesture.");
		mBackGestureRecognizer = [[UIScreenEdgePanGestureRecognizer alloc] initWithTarget:self action:@selector(handleSwipeFrom:) ];
		[mBackGestureRecognizer setEdges:UIRectEdgeLeft];
		[[self view] addGestureRecognizer:mBackGestureRecognizer];
	}
}

- (void)uiStateChanged {
	[self setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
	[self hideKeyboard];
	[self updateGesture];
}

- (void)startVideo:(int)width height:(int)height {
	[cameraHelper startVideo:width h:height];
}

- (void)stopVideo {
	[cameraHelper stopVideo];
}

- (void)PushCameraImageIOS:(long long)len buffer:(unsigned char*)data {
	Camera::pushCameraImage(len, data);
}

- (void)startLocation {
	[locationHelper startLocationUpdates];
}

- (void)stopLocation {
	[locationHelper stopLocationUpdates];
}

- (void)SetGpsDataIOS:(CLLocation *)newLocation {
	GPS::setGpsData((long long)newLocation.timestamp.timeIntervalSince1970,
					newLocation.horizontalAccuracy/5.0,
					newLocation.coordinate.latitude, newLocation.coordinate.longitude,
					newLocation.altitude,
					MAX(newLocation.speed * 3.6, 0.0), /* m/s to km/h */
					0 /* bearing */);
}

- (void)viewDidLoad {
	[super viewDidLoad];

	cameraHelper = [[CameraHelper alloc] init];
	[cameraHelper setDelegate:self];

	locationHelper = [[LocationHelper alloc] init];
	[locationHelper setDelegate:self];
}

extern float g_safeInsetLeft;
extern float g_safeInsetRight;
extern float g_safeInsetTop;
extern float g_safeInsetBottom;

static float BoostInset(float inset) {
	if (inset > 0.0f) {
		// If there's some inset, add a few pixels extra. Really needed on iPhone 12, at least.
		inset += 4.0f;
	}
	return inset;
}

- (void)viewSafeAreaInsetsDidChange {
	if (@available(iOS 11.0, *)) {
		[super viewSafeAreaInsetsDidChange];
		// we use 0.0f instead of safeAreaInsets.bottom because the bottom overlay isn't disturbing (for now)
		g_safeInsetLeft = BoostInset(self.view.safeAreaInsets.left);
		g_safeInsetRight = BoostInset(self.view.safeAreaInsets.right);
		g_safeInsetTop = BoostInset(self.view.safeAreaInsets.top);

		// TODO: In portrait mode, should probably use safeAreaInsets.bottom.
		// However, in landscape mode, it's not really needed.
		// g_safeInsetBottom = BoostInset(self.view.safeAreaInsets.bottom);
		g_safeInsetBottom = 0.0f;
	}
}

- (void)shareText:(NSString *)text {
	NSArray *items = @[text];
	UIActivityViewController * viewController = [[UIActivityViewController alloc] initWithActivityItems:items applicationActivities:nil];
	dispatch_async(dispatch_get_main_queue(), ^{
		[self presentViewController:viewController animated:YES completion:nil];
	});
}

- (void)appSwitchModeChanged {
	[self setNeedsUpdateOfHomeIndicatorAutoHidden];
}

// The below is inspired by https://stackoverflow.com/questions/7253477/how-to-display-the-iphone-ipad-keyboard-over-a-full-screen-opengl-es-app
// It's a bit limited but good enough.

- (void)deleteBackward {
	KeyInput input{};
	input.deviceId = DEVICE_ID_KEYBOARD;
	input.flags = KEY_DOWN | KEY_UP;
	input.keyCode = NKCODE_DEL;
	NativeKey(input);
	INFO_LOG(Log::System, "Backspace");
}

- (void)insertText:(NSString *)text {
	std::string str([text UTF8String]);
	INFO_LOG(Log::System, "Chars: %s", str.c_str());
	SendKeyboardChars(str);
}

- (BOOL)hasText {
	return true;
}

- (void)showKeyboard {
	dispatch_async(dispatch_get_main_queue(), ^{
		INFO_LOG(Log::System, "becomeFirstResponder");
		[self becomeFirstResponder];
	});
}

- (void)hideKeyboard {
	dispatch_async(dispatch_get_main_queue(), ^{
		INFO_LOG(Log::System, "resignFirstResponder");
		[self resignFirstResponder];
	});
}

- (BOOL)canBecomeFirstResponder {
	return YES;
}

@end
