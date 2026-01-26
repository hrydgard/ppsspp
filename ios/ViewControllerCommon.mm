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
	ICadeTracker g_iCadeTracker;
	TouchTracker g_touchTracker;
}

@property (strong, nonatomic) NSOperationQueue *accelerometerQueue;
@property (nonatomic) GCController *gameController __attribute__((weak_import));
@property (strong, nonatomic) CMMotionManager *motionManager;

@end

@implementation PPSSPPBaseViewController {
	UIScreenEdgePanGestureRecognizer *mBackGestureRecognizer;
}

- (id)init {
	self = [super init];
	if (self) {
		sharedViewController = self;

		g_iCadeTracker.InitKeyMap();

		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(appWillTerminate:) name:UIApplicationWillTerminateNotification object:nil];
		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(controllerDidConnect:) name:GCControllerDidConnectNotification object:nil];
		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(controllerDidDisconnect:) name:GCControllerDidDisconnectNotification object:nil];

		// Observe orientation changes
		[[NSNotificationCenter defaultCenter] addObserver:self
												selector:@selector(onOrientationChanged)
												name:UIDeviceOrientationDidChangeNotification
												object:nil];
	}
	self.accelerometerQueue = [[NSOperationQueue alloc] init];
	self.accelerometerQueue.name = @"AccelerometerQueue";
	self.accelerometerQueue.maxConcurrentOperationCount = 1;

	return self;
}

- (void)shutdown {
	self.gameController = nil;
	[[NSNotificationCenter defaultCenter] removeObserver:self];

	_dbg_assert_(sharedViewController != nil);
	sharedViewController = nil;
}

- (BOOL)prefersHomeIndicatorAutoHidden {
	if (g_Config.iAppSwitchMode == (int)AppSwitchMode::DOUBLE_SWIPE_INDICATOR) {
		return NO;
	} else {
		return YES;
	}
}

- (void)didBecomeActive {
	if (self.motionManager.accelerometerAvailable) {
		self.motionManager.accelerometerUpdateInterval = 1.0 / 60.0;
		INFO_LOG(Log::G3D, "Starting accelerometer updates.");

		[self.motionManager startAccelerometerUpdatesToQueue:self.accelerometerQueue
							withHandler:^(CMAccelerometerData *accelerometerData, NSError *error) {
			if (error) {
				NSLog(@"Accelerometer error: %@", error);
				return;
			}
			ProcessAccelerometerData(accelerometerData);
		}];
	} else {
		INFO_LOG(Log::G3D, "No accelerometer available, not starting updates.");
	}
}

- (void)willResignActive {
	// Stop accelerometer updates
	if (self.motionManager.accelerometerActive) {
		INFO_LOG(Log::G3D, "Stopping accelerometer updates");
		[self.motionManager stopAccelerometerUpdates];
	}
}

- (void)appWillTerminate:(NSNotification *)notification {
	[self shutdown];
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

- (void)controllerDidConnect:(NSNotification *)note
{
	if (![[GCController controllers] containsObject:self.gameController]) self.gameController = nil;

	if (self.gameController != nil) return; // already have a connected controller

	[self setupController:(GCController *)note.object];
}

- (void)controllerDidDisconnect:(NSNotification *)note
{
	if (self.gameController == note.object) {
		ShutdownController(self.gameController);
		self.gameController = nil;

		if ([[GCController controllers] count] > 0) {
			[self setupController:[[GCController controllers] firstObject]];
		}
	}
}

- (void)setupController:(GCController *)controller {
	self.gameController = controller;
	if (!InitController(controller)) {
		self.gameController = nil;
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
		key.flags = KeyInputFlags::DOWN | KeyInputFlags::UP;
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

	self.motionManager = [[CMMotionManager alloc] init];
}

extern float g_safeInsetLeft;
extern float g_safeInsetRight;
extern float g_safeInsetTop;
extern float g_safeInsetBottom;

- (void)viewSafeAreaInsetsDidChange {
	[super viewSafeAreaInsetsDidChange];

	// Converts points to pixels.
	CGFloat scale = UIScreen.mainScreen.scale;

	float xInsetSum = self.view.safeAreaInsets.left + self.view.safeAreaInsets.right;
	float yInsetSum = self.view.safeAreaInsets.top + self.view.safeAreaInsets.bottom;

	// Only use the larger set of insets. The other set, we'll handle through layouts.
	// Also, we'll treat the bottom inset differently: We'll add some space at the end of everything
	// that scrolls so that when you're not at the bottom, we use the full vertical area,
	// just looks better.
	if (xInsetSum > yInsetSum) {
		// Landscape mode insets are larger, use those.
		g_safeInsetLeft = self.view.safeAreaInsets.left * scale;
		g_safeInsetRight = self.view.safeAreaInsets.right * scale;
		g_safeInsetTop = 0.0f;
		g_safeInsetBottom = 0.0f;
	} else {
		// Portrait mode insets are larger, use those.
		g_safeInsetLeft = 0.0f;
		g_safeInsetRight = 0.0f;
		g_safeInsetTop = self.view.safeAreaInsets.top * scale;
		g_safeInsetBottom = self.view.safeAreaInsets.bottom * scale;
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
	input.flags = KeyInputFlags::DOWN | KeyInputFlags::UP;
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

- (void)buttonDown:(iCadeState)button
{
	g_iCadeTracker.ButtonDown(button);
}

- (void)buttonUp:(iCadeState)button
{
	g_iCadeTracker.ButtonUp(button);
}

// See PPSSPPUIApplication.mm for the other method
#if PPSSPP_PLATFORM(IOS_APP_STORE)

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	KeyboardPressesBegan(presses, event);
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	KeyboardPressesEnded(presses, event);
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	KeyboardPressesEnded(presses, event);
}

#endif
#pragma mark - Status Bar Control

// iOS calls this to determine whether to hide the status bar
- (BOOL)prefersStatusBarHidden {
	UIInterfaceOrientation orientation;

	if (@available(iOS 13.0, *)) {
		UIWindowScene *scene = self.view.window.windowScene;
		if (scene != nil) {
			orientation = scene.interfaceOrientation;
		} else {
			orientation = UIApplication.sharedApplication.statusBarOrientation;
		}
	} else {
		orientation = UIApplication.sharedApplication.statusBarOrientation;
	}

	BOOL isLandscape = UIInterfaceOrientationIsLandscape(orientation);

	bool userWantsStatusBar = true; // g_Config.bShowStatusBar;
	// return isLandscape || !userWantsStatusBar;
	return false;
}

// Optional: choose light/dark text for the status bar
- (UIStatusBarStyle)preferredStatusBarStyle {
	return UIStatusBarStyleLightContent;
}

// This should also be called when the user preference changes.
- (void)onOrientationChanged {
	[self setNeedsStatusBarAppearanceUpdate];
}

@end
