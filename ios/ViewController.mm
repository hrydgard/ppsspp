#import "ios/ViewController.h"
#import "ios/Controls.h"
#import "ios/IAPManager.h"
#if !PPSSPP_PLATFORM(TVOS)
#import "ios/CameraHelper.h"
#import <Photos/Photos.h>
#endif
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <thread>
#include "Common/GPU/GraphicsContext.h"
#include "Common/GPU/Vulkan/VulkanGraphicsContext.h"
#include "Common/System/Request.h"
#include "Common/System/System.h"
#include "Common/Input/InputState.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Display.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Log.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbGps.h"
#include "Core/EmuThread.h"
#include "Core/System.h"
#include "Core/Config.h"

// ViewController lifecycle:
// https://www.progressconcepts.com/blog/ios-appdelegate-viewcontroller-method-order/

// The target of the soft keyboard. It's a view of its own so the keyboard only comes up when we ask
// for it: UIKit hands first responder back to the view controller whenever something it presented
// (a share sheet, a picker, an alert) goes away, and if that were the text input, up it would come.
@interface KeyboardInputView : UIView <UIKeyInput>
@end

@implementation KeyboardInputView

- (BOOL)canBecomeFirstResponder {
	return YES;
}

- (BOOL)hasText {
	// Otherwise the keyboard wouldn't send backspaces.
	return YES;
}

- (void)insertText:(NSString *)text {
	SendKeyboardChars([text UTF8String]);
}

- (void)deleteBackward {
	KeyInput input{};
	input.deviceId = DEVICE_ID_KEYBOARD;
	input.flags = KeyInputFlags::DOWN | KeyInputFlags::UP;
	input.keyCode = NKCODE_DEL;
	NativeKey(input);
}

@end

// The view, backed by the CAMetalLayer we render to.
@interface PPSSPPMetalView : UIView
@end

@implementation PPSSPPMetalView

+ (Class)layerClass {
	return [CAMetalLayer class];
}

@end

PPSSPPViewController *sharedViewController;

static std::atomic<bool> exitRenderLoop;
static std::atomic<bool> renderLoopRunning;
static std::thread g_renderLoopThread;

// A size change from the main thread, for the render thread to apply between frames. Changing
// g_display while a frame is being drawn gives a frame laid out for one size and drawn at another.
struct PendingResolution {
	float pixelWidth;
	float pixelHeight;
	float dpiScale;
	float uiScale;
};
static std::mutex g_pendingResolutionLock;
static std::optional<PendingResolution> g_pendingResolution;

static void ApplyResolution(const PendingResolution &res) {
	g_display.Recalculate(res.pixelWidth, res.pixelHeight, res.dpiScale, res.dpiScale, res.uiScale);
	// NativeFrame updates the PSP_CoreParameter() size along with the rest.
	NativeResized();
}

static void ApplyPendingResolution() {
	std::optional<PendingResolution> res;
	{
		std::lock_guard<std::mutex> guard(g_pendingResolutionLock);
		res.swap(g_pendingResolution);
	}
	if (res) {
		ApplyResolution(*res);
	}
}

static void VulkanRenderLoop(GraphicsContext *graphicsContext, CAMetalLayer *metalLayer) {
	// Only a provisional name, RunGraphicsLoop renames the thread once it knows its role.
	SetCurrentThreadName("RenderLoop");
	_assert_(graphicsContext);

	std::string errorMessage;
	if (!graphicsContext->InitSurface(WINDOWSYSTEM_METAL_EXT, (__bridge void *)metalLayer, nullptr, &errorMessage)) {
		ERROR_LOG(Log::G3D, "Failed to initialize graphics context for surface: %s", errorMessage.c_str());
		System_Toast("Failed to initialize graphics context.");
		renderLoopRunning = false;
		return;
	}

	const auto frame = [](GraphicsContext *graphicsContext) {
		ApplyPendingResolution();
		NativeFrame(graphicsContext);
		return !exitRenderLoop;
	};
	RunGraphicsLoop(graphicsContext, new NativeApplication(), frame, []() { return exitRenderLoop.load(); });

	// Shut the graphics context down to the same state it was in when we entered the render thread.
	INFO_LOG(Log::G3D, "Shutting down graphics context...");
	graphicsContext->ShutdownSurface();

	// exitRenderLoop is reset by whoever set it, after joining us.
	renderLoopRunning = false;
	WARN_LOG(Log::G3D, "Render loop function exited.");
}

@interface PPSSPPViewController () {
	GraphicsContext *graphicsContext;
#if !PPSSPP_PLATFORM(TVOS)
	CameraHelper *cameraHelper;
	LocationHelper *locationHelper;
	TouchTracker g_touchTracker;
#endif
}

@property (nonatomic) GCController *gameController;
#if !PPSSPP_PLATFORM(TVOS)
@property (strong, nonatomic) NSOperationQueue *accelerometerQueue;
@property (strong, nonatomic) CMMotionManager *motionManager;
#endif

@end

@implementation PPSSPPViewController {
#if !PPSSPP_PLATFORM(TVOS)
	UIScreenEdgePanGestureRecognizer *mBackGestureRecognizer;
#endif
	CGSize lastLayoutSize_;
	KeyboardInputView *keyboardInputView_;
}

#if !PPSSPP_PLATFORM(TVOS)

// Strange idiom for generating unique IDs (within the process, at least).
static const void *kPickerFilenameKey = &kPickerFilenameKey;
static const void *kPickerRequestIdKey = &kPickerRequestIdKey;

static void SetPickerContext(id picker, NSString *filename, int requestId) {
	objc_setAssociatedObject(picker, kPickerFilenameKey, filename, OBJC_ASSOCIATION_COPY_NONATOMIC);
	objc_setAssociatedObject(picker, kPickerRequestIdKey, @(requestId), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static NSString *GetPickerFilename(id picker) {
	return (NSString *)objc_getAssociatedObject(picker, kPickerFilenameKey);
}

static int GetPickerRequestId(id picker) {
	NSNumber *requestIdNumber = (NSNumber *)objc_getAssociatedObject(picker, kPickerRequestIdKey);
	return requestIdNumber ? requestIdNumber.intValue : -1;
}

- (void)savePickedImage:(UIImage *)image toFilename:(NSString *)targetFilename requestId:(int)requestId {
	NSData *jpegData = UIImageJPEGRepresentation(image, 0.9);
	if (jpegData) {
		[jpegData writeToFile:targetFilename atomically:YES];
		NSLog(@"Saved JPEG image to %@", targetFilename);
		g_requestManager.PostSystemSuccess(requestId, "", 1);
	} else {
		g_requestManager.PostSystemFailure(requestId);
	}
}
#endif

- (id)init {
	self = [super init];
	if (self) {
		sharedViewController = self;

		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(appWillTerminate:) name:UIApplicationWillTerminateNotification object:nil];
		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(controllerDidConnect:) name:GCControllerDidConnectNotification object:nil];
		[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(controllerDidDisconnect:) name:GCControllerDidDisconnectNotification object:nil];

#if PPSSPP_PLATFORM(TVOS)
		// Already the default. See topLevelChanged.
		self.controllerUserInteractionEnabled = NO;
#else
		// Observe orientation changes
		[[NSNotificationCenter defaultCenter] addObserver:self
												selector:@selector(onOrientationChanged)
												name:UIDeviceOrientationDidChangeNotification
												object:nil];

		self.accelerometerQueue = [[NSOperationQueue alloc] init];
		self.accelerometerQueue.name = @"AccelerometerQueue";
		self.accelerometerQueue.maxConcurrentOperationCount = 1;
#endif
	}
	return self;
}

- (void)shutdown {
	self.gameController = nil;
	[[NSNotificationCenter defaultCenter] removeObserver:self];

	_dbg_assert_(sharedViewController != nil);
	sharedViewController = nil;

	g_Config.Save("shutdown vk");

	// Normally already done by willResignActive, but not when the app is terminated.
	[self requestExitVulkanRenderLoop];

	if (graphicsContext) {
		graphicsContext->ShutdownAPI();
		delete graphicsContext;
		graphicsContext = nullptr;
	}
}

- (void)dealloc {
	INFO_LOG(Log::System, "dealloc VK");
}

- (bool)runVulkanRenderLoop {
	INFO_LOG(Log::G3D, "runVulkanRenderLoop");

	if (!graphicsContext) {
		ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Tried to enter without a created graphics context.");
		return false;
	}

	if (g_renderLoopThread.joinable()) {
		if (renderLoopRunning) {
			ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Already running");
			return false;
		}
		// The previous thread gave up by itself (failed surface init). Reap it so we can try again.
		WARN_LOG(Log::G3D, "runVulkanRenderLoop: Joining a render thread that had already exited");
		g_renderLoopThread.join();
		g_renderLoopThread = std::thread();
	}

	_assert_(!exitRenderLoop);

	CAMetalLayer *metalLayer = (CAMetalLayer *)self.view.layer;
	// Set before the thread exists, so an exit request can't slip in ahead of it.
	renderLoopRunning = true;
	g_renderLoopThread = std::thread(VulkanRenderLoop, graphicsContext, metalLayer);
	return true;
}

- (void)requestExitVulkanRenderLoop {
	INFO_LOG(Log::G3D, "requestExitVulkanRenderLoop");

	// Don't go by renderLoopRunning - the thread might have bailed by itself, and still needs joining.
	if (!g_renderLoopThread.joinable()) {
		INFO_LOG(Log::G3D, "Render loop not running, nothing to join");
		return;
	}
	exitRenderLoop = true;
	g_renderLoopThread.join();
	g_renderLoopThread = std::thread();
	exitRenderLoop = false;
}

#if !PPSSPP_PLATFORM(TVOS)
- (BOOL)prefersHomeIndicatorAutoHidden {
	if (g_Config.iAppSwitchMode == (int)AppSwitchMode::DOUBLE_SWIPE_INDICATOR) {
		return NO;
	} else {
		return YES;
	}
}
#endif

- (void)didBecomeActive {
#if !PPSSPP_PLATFORM(TVOS)
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
#endif

	// Spin up the render thread, which in turn starts the emu thread.
	[self updateResolutionWithView:self.view];
	[self runVulkanRenderLoop];
}

- (void)willResignActive {
	[self requestExitVulkanRenderLoop];

#if !PPSSPP_PLATFORM(TVOS)
	// Stop accelerometer updates
	if (self.motionManager.accelerometerActive) {
		INFO_LOG(Log::G3D, "Stopping accelerometer updates");
		[self.motionManager stopAccelerometerUpdates];
	}
#endif
}

- (void)appWillTerminate:(NSNotification *)notification {
	[self shutdown];
}

- (void)viewDidAppear:(BOOL)animated {
	[super viewDidAppear:animated];
	INFO_LOG(Log::G3D, "viewDidAppear");
	// For hardware keyboard presses.
	[self becomeFirstResponder];
#if !PPSSPP_PLATFORM(TVOS)
	[self updateGesture];
#endif

	// This needs to be called really late during startup, unfortunately.
#if PPSSPP_PLATFORM(IOS_APP_STORE)
	[IAPManager sharedIAPManager];  // Kick off the IAPManager early.
	NSLog(@"viewDidAppear. updating icon");
	dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(4.0 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
		[[IAPManager sharedIAPManager] updateIcon:false];
	});
#endif  // IOS_APP_STORE
}

#if !PPSSPP_PLATFORM(TVOS)
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
#endif

- (void)controllerDidConnect:(NSNotification *)note
{
	if (![[GCController controllers] containsObject:self.gameController]) self.gameController = nil;

	if (InitSiriRemote((GCController *)note.object)) {
		return;
	}

	if (self.gameController != nil) return; // already have a connected controller

	[self setupController:(GCController *)note.object];
}

- (void)controllerDidDisconnect:(NSNotification *)note
{
	if (self.gameController == note.object) {
		ShutdownController(self.gameController);
		self.gameController = nil;
		[self setupFirstController];
	}
}

// Like Android, Back on the main screen leaves the app. tvOS only does that with a Back press it
// gets to see, so let controller events through to UIKit there. They still reach GCController.
- (void)topLevelChanged {
#if PPSSPP_PLATFORM(TVOS)
	self.controllerUserInteractionEnabled = NativeIsAtTopLevel() ? YES : NO;
	INFO_LOG(Log::System, "Controller events to UIKit: %s", self.controllerUserInteractionEnabled ? "yes" : "no");
#endif
}

- (void)setupController:(GCController *)controller {
	self.gameController = controller;
	if (!InitController(controller)) {
		self.gameController = nil;
	}
}

// Picks the first gamepad. On tvOS, the Siri Remote is in the list too, and gets set up separately.
- (void)setupFirstController {
	for (GCController *controller in [GCController controllers]) {
		if (InitSiriRemote(controller)) {
			continue;
		}
		if (self.gameController == nil) {
			[self setupController:controller];
		}
	}
}

#if !PPSSPP_PLATFORM(TVOS)
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
#endif

#if PPSSPP_PLATFORM(TVOS)

- (void)pickPhoto:(NSString *)saveFilename requestId:(int)requestId {
	g_requestManager.PostSystemFailure(requestId);
}

- (void)uiStateChanged {
	[self hideKeyboard];
}

- (void)startVideo:(int)width height:(int)height {}
- (void)stopVideo {}
- (void)startLocation {}
- (void)stopLocation {}

#else

- (void)pickPhoto:(NSString *)saveFilename requestId:(int)requestId {
	NSLog(@"Picking photo to save to %@ (id: %d)", saveFilename, requestId);
	NSString *targetFilename = [saveFilename copy];

	PHPickerConfiguration *config = [[PHPickerConfiguration alloc] initWithPhotoLibrary:PHPhotoLibrary.sharedPhotoLibrary];
	config.selectionLimit = 1;
	config.filter = [PHPickerFilter imagesFilter];

	PHPickerViewController *picker = [[PHPickerViewController alloc] initWithConfiguration:config];
	picker.delegate = self;
	SetPickerContext(picker, targetFilename, requestId);
	[self presentViewController:picker animated:YES completion:nil];
}

- (void)picker:(PHPickerViewController *)picker didFinishPicking:(NSArray<PHPickerResult *> *)results {
	NSString *targetFilename = GetPickerFilename(picker);
	int requestId = GetPickerRequestId(picker);

	[picker dismissViewControllerAnimated:YES completion:nil];

	if (results.count == 0) {
		NSLog(@"User cancelled photo picker");
		g_requestManager.PostSystemFailure(requestId);
		return;
	}

	NSItemProvider *itemProvider = results.firstObject.itemProvider;
	if (![itemProvider canLoadObjectOfClass:[UIImage class]]) {
		NSLog(@"Photo picker result does not provide UIImage");
		g_requestManager.PostSystemFailure(requestId);
		return;
	}

	[itemProvider loadObjectOfClass:[UIImage class] completionHandler:^(UIImage *image, NSError *error) {
		dispatch_async(dispatch_get_main_queue(), ^{
			if (error || !image) {
				NSLog(@"Failed loading picked image: %@", error);
				g_requestManager.PostSystemFailure(requestId);
			} else {
				[self savePickedImage:image toFilename:targetFilename requestId:requestId];
			}
		});
	}];
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

#endif  // PPSSPP_PLATFORM(TVOS)

- (void)loadView {
	INFO_LOG(Log::G3D, "Creating metal view");
	// The view gets auto-resized later.
	self.view = [[PPSSPPMetalView alloc] initWithFrame:CGRectMake(0, 0, 0, 0)];
}

- (void)viewDidLoad {
	[super viewDidLoad];

	INFO_LOG(Log::System, "viewDidLoad");

#if !PPSSPP_PLATFORM(TVOS)
	self.view.multipleTouchEnabled = YES;

	cameraHelper = [[CameraHelper alloc] init];
	[cameraHelper setDelegate:self];

	locationHelper = [[LocationHelper alloc] init];
	[locationHelper setDelegate:self];

	self.motionManager = [[CMMotionManager alloc] init];
#endif

	keyboardInputView_ = [[KeyboardInputView alloc] initWithFrame:CGRectZero];
	[self.view addSubview:keyboardInputView_];

	graphicsContext = new VulkanGraphicsContext();
	std::string errorMessage;
	if (!graphicsContext->InitAPI(nullptr, &g_Config.sVulkanDevice, &errorMessage)) {
		delete graphicsContext;
		graphicsContext = nullptr;  // The render loop and shutdown check for this.
		// There's no other backend to fall back to.
		ERROR_LOG(Log::System, "Failed to initialize Vulkan: %s", errorMessage.c_str());
	}

	[self updateResolutionWithView:self.view];

	[self setupFirstController];

	INFO_LOG(Log::G3D, "Detected size: %dx%d", g_display.pixel_xres, g_display.pixel_yres);
}

- (void)viewWillAppear:(BOOL)animated {
	[super viewWillAppear:animated];
	INFO_LOG(Log::G3D, "viewWillAppear");
	// This is to make sure we get 1:1 pixels.
	UIWindowScene *scene = self.view.window.windowScene;
	if (scene) {
		self.view.contentScaleFactor = scene.screen.nativeScale;
	}
#if !PPSSPP_PLATFORM(TVOS)
	if (@available(iOS 16.0, *)) {
		[self setNeedsUpdateOfSupportedInterfaceOrientations];
	}
#endif
}

extern float g_safeInsetLeft;
extern float g_safeInsetRight;
extern float g_safeInsetTop;
extern float g_safeInsetBottom;

- (void)viewSafeAreaInsetsDidChange {
	[super viewSafeAreaInsetsDidChange];

	// Converts points to pixels, the same way updateResolutionWithView does.
	CGFloat scale = [self displayScaleForView:self.view];

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
	[self shareItems:@[text]];
}

- (void)shareFile:(NSURL *)url {
	// The share sheet works out the type from the extension.
	[self shareItems:@[url]];
}

- (void)shareItems:(NSArray *)items {
#if PPSSPP_PLATFORM(TVOS)
	// No share sheet on tvOS. SYSPROP_SUPPORTS_SHARE_* is false there, so this isn't reached.
	WARN_LOG(Log::System, "Sharing isn't available on tvOS");
#else
	UIActivityViewController *viewController = [[UIActivityViewController alloc] initWithActivityItems:items applicationActivities:nil];
	// On iPad, the share sheet is a popover and needs an anchor.
	viewController.popoverPresentationController.sourceView = self.view;
	viewController.popoverPresentationController.sourceRect = CGRectMake(CGRectGetMidX(self.view.bounds), CGRectGetMidY(self.view.bounds), 0, 0);
	viewController.popoverPresentationController.permittedArrowDirections = 0;
	[self presentViewController:viewController animated:YES completion:nil];
#endif
}

- (void)appSwitchModeChanged {
#if !PPSSPP_PLATFORM(TVOS)
	[self setNeedsUpdateOfHomeIndicatorAutoHidden];
#endif
}

// Main thread, like the rest of these.
- (void)showKeyboard {
	INFO_LOG(Log::System, "showKeyboard");
	[keyboardInputView_ becomeFirstResponder];
}

- (void)hideKeyboard {
	if (keyboardInputView_.isFirstResponder) {
		INFO_LOG(Log::System, "hideKeyboard");
		[keyboardInputView_ resignFirstResponder];
		// Back to us, for hardware keyboard presses. We don't take text, so no keyboard comes with it.
		[self becomeFirstResponder];
	}
}

- (BOOL)canBecomeFirstResponder {
	return YES;
}

// Presses that aren't keyboard keys, like the remote's buttons, go on up the responder chain. On
// tvOS, that's what takes Back on the main screen to the home screen (see topLevelChanged).
static NSSet<UIPress *> *NonKeyboardPresses(NSSet<UIPress *> *presses) {
	return [presses objectsPassingTest:^BOOL(UIPress *press, BOOL *stop) {
		return press.key == nil;
	}];
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	KeyboardPressesBegan(presses, event);
	NSSet<UIPress *> *others = NonKeyboardPresses(presses);
	if (others.count) {
		[super pressesBegan:others withEvent:event];
	}
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	KeyboardPressesEnded(presses, event);
	NSSet<UIPress *> *others = NonKeyboardPresses(presses);
	if (others.count) {
		[super pressesEnded:others withEvent:event];
	}
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
	KeyboardPressesEnded(presses, event);
	NSSet<UIPress *> *others = NonKeyboardPresses(presses);
	if (others.count) {
		[super pressesCancelled:others withEvent:event];
	}
}

#pragma mark - Status Bar Control

#if PPSSPP_PLATFORM(TVOS)

// No status bar or rotation on tvOS.
- (void)immersiveModeChanged {}

#else

// The immersive mode setting is per-orientation, so we need to know which way we're facing.
// Can't just use g_display for this, since it lags behind during rotation.
// Note: Using viewIfLoaded, since this can get called before the view exists, and we don't want to force it into existence.
- (DeviceOrientation)currentDeviceOrientation {
	UIWindowScene *scene = self.viewIfLoaded.window.windowScene;
	if (scene != nil) {
		return UIInterfaceOrientationIsPortrait(scene.interfaceOrientation) ? DeviceOrientation::Portrait : DeviceOrientation::Landscape;
	}
	CGSize size = self.viewIfLoaded.bounds.size;
	return size.height > size.width ? DeviceOrientation::Portrait : DeviceOrientation::Landscape;
}

// iOS calls this to determine whether to hide the status bar.
// Note that on iPhone, iOS hides it in landscape regardless of what we return here.
- (BOOL)prefersStatusBarHidden {
	return g_Config.GetDisplayLayoutConfig([self currentDeviceOrientation]).bImmersiveMode ? YES : NO;
}

// Optional: choose light/dark text for the status bar
- (UIStatusBarStyle)preferredStatusBarStyle {
	return UIStatusBarStyleLightContent;
}

- (void)onOrientationChanged {
	[self setNeedsStatusBarAppearanceUpdate];
}

// Called from the C++ side when the user toggles the immersive mode setting.
- (void)immersiveModeChanged {
	[self setNeedsStatusBarAppearanceUpdate];
}

- (void)viewWillTransitionToSize:(CGSize)size
		withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)coordinator {
	[super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];
	// The immersive mode setting is per-orientation, so the status bar may need to change along with the rotation.
	[self setNeedsStatusBarAppearanceUpdate];
}

#endif  // PPSSPP_PLATFORM(TVOS)

// Rotations and window resizes land here. UIKit lays out at the final size at the start of a
// rotation animation, so resizing here (rather than when the rotation completes) keeps the
// backbuffer matching the view while it animates.
- (void)viewDidLayoutSubviews {
	[super viewDidLayoutSubviews];
	CGSize size = self.view.bounds.size;
	if (size.width <= 0.0 || size.height <= 0.0 || CGSizeEqualToSize(size, lastLayoutSize_)) {
		return;
	}
	lastLayoutSize_ = size;
#if !PPSSPP_PLATFORM(TVOS)
	if (self.view.window.windowScene) {
		[cameraHelper setInterfaceOrientation:self.view.window.windowScene.interfaceOrientation];
	}
#endif
	INFO_LOG(Log::G3D, "Laid out at %dx%d", (int)size.width, (int)size.height);
	// Resized in place: NativeFrame picks up the new size and lets the graphics context resize.
	[self updateResolutionWithView:self.view];
}

// Pixels per point. nativeScale, not scale, since that's what the drawable is - they differ on
// some phones, like the Plus models and the minis.
- (CGFloat)displayScaleForView:(UIView *)view {
	if (view.window.windowScene) {
		return view.window.windowScene.screen.nativeScale;
	}
	return [UITraitCollection currentTraitCollection].displayScale;
}

- (void)updateResolutionWithView:(UIView *)view {
	CGFloat scale = [self displayScaleForView:view];

	// The bounds are already corrected for orientation.
	CGSize size = view.bounds.size;

	float dpi;
	if (System_GetPropertyInt(SYSPROP_DEVICE_TYPE) == DEVICE_TYPE_TV) {
		// Like a 1080p Android TV, which reports 320 dpi: 1920x1080 points come out as 1440x810 dp.
		dpi = 320.0f * scale;
	} else {
		dpi = (IS_IPAD() ? 200.0f : 150.0f) * scale;
	}
	PendingResolution res{
		(float)(size.width * scale), (float)(size.height * scale),
		240.0f / dpi,
		UIScaleFactorToMultiplier(g_Config.iUIScaleFactor),
	};

	[view setContentScaleFactor:scale];

	{
		std::lock_guard<std::mutex> guard(g_pendingResolutionLock);
		if (renderLoopRunning) {
			// Applied by the render thread before its next frame.
			g_pendingResolution = res;
		} else {
			g_pendingResolution.reset();
			ApplyResolution(res);
		}
	}

	NSLog(@"Updated display resolution: (%d, %d) @%.1fx", (int)res.pixelWidth, (int)res.pixelHeight, (float)scale);
}

@end
