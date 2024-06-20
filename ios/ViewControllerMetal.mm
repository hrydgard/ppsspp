#import "AppDelegate.h"
#import "ViewControllerMetal.h"
#import "DisplayManager.h"
#include "Controls.h"
#import "iOSCoreAudio.h"

#include "Common/Log.h"

#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/NativeApp.h"
#include "Common/GraphicsContext.h"
#include "Common/Thread/ThreadUtil.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HLE/sceUsbGps.h"

// ViewController lifecycle:
// https://www.progressconcepts.com/blog/ios-appdelegate-viewcontroller-method-order/

// TODO: Share this between backends.
static uint32_t FlagsFromConfig() {
	uint32_t flags;
	if (g_Config.bVSync) {
		flags = VULKAN_FLAG_PRESENT_FIFO;
	} else {
		flags = VULKAN_FLAG_PRESENT_MAILBOX | VULKAN_FLAG_PRESENT_IMMEDIATE;
	}
	return flags;
}

enum class GraphicsContextState {
	PENDING,
	INITIALIZED,
	FAILED_INIT,
	SHUTDOWN,
};

class IOSVulkanContext : public GraphicsContext {
public:
	IOSVulkanContext();
	~IOSVulkanContext() {
		delete g_Vulkan;
		g_Vulkan = nullptr;
	}

	bool InitAPI();

	bool InitFromRenderThread(CAMetalLayer *layer, int desiredBackbufferSizeX, int desiredBackbufferSizeY);
	void ShutdownFromRenderThread();  // Inverses InitFromRenderThread.

	void Shutdown();
	void Resize();

	void *GetAPIContext() { return g_Vulkan; }
	Draw::DrawContext *GetDrawContext() { return draw_; }

private:
	VulkanContext *g_Vulkan = nullptr;
	Draw::DrawContext *draw_ = nullptr;
	GraphicsContextState state_ = GraphicsContextState::PENDING;
};

IOSVulkanContext::IOSVulkanContext() {}

bool IOSVulkanContext::InitFromRenderThread(CAMetalLayer *layer, int desiredBackbufferSizeX, int desiredBackbufferSizeY) {
	INFO_LOG(G3D, "IOSVulkanContext::InitFromRenderThread: desiredwidth=%d desiredheight=%d", desiredBackbufferSizeX, desiredBackbufferSizeY);
	if (!g_Vulkan) {
		ERROR_LOG(G3D, "IOSVulkanContext::InitFromRenderThread: No Vulkan context");
		return false;
	}

	VkResult res = g_Vulkan->InitSurface(WINDOWSYSTEM_METAL_EXT, (__bridge void *)layer, nullptr);
	if (res != VK_SUCCESS) {
		ERROR_LOG(G3D, "g_Vulkan->InitSurface failed: '%s'", VulkanResultToString(res));
		return false;
	}

	bool success = true;
	if (g_Vulkan->InitSwapchain()) {
		bool useMultiThreading = g_Config.bRenderMultiThreading;
		if (g_Config.iInflightFrames == 1) {
			useMultiThreading = false;
		}
		draw_ = Draw::T3DCreateVulkanContext(g_Vulkan, useMultiThreading);
		SetGPUBackend(GPUBackend::VULKAN);
		success = draw_->CreatePresets();  // Doesn't fail, we ship the compiler.
		_assert_msg_(success, "Failed to compile preset shaders");
		draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

		VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		renderManager->SetInflightFrames(g_Config.iInflightFrames);
		success = renderManager->HasBackbuffers();
	} else {
		success = false;
	}

	INFO_LOG(G3D, "IOSVulkanContext::Init completed, %s", success ? "successfully" : "but failed");
	if (!success) {
		g_Vulkan->DestroySwapchain();
		g_Vulkan->DestroySurface();
		g_Vulkan->DestroyDevice();
		g_Vulkan->DestroyInstance();
	}
	return success;
}

void IOSVulkanContext::ShutdownFromRenderThread() {
	INFO_LOG(G3D, "IOSVulkanContext::Shutdown");
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	delete draw_;
	draw_ = nullptr;
	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->PerformPendingDeletes();
	g_Vulkan->DestroySwapchain();
	g_Vulkan->DestroySurface();
	INFO_LOG(G3D, "Done with ShutdownFromRenderThread");
}

void IOSVulkanContext::Shutdown() {
	INFO_LOG(G3D, "Calling NativeShutdownGraphics");
	g_Vulkan->DestroyDevice();
	g_Vulkan->DestroyInstance();
	// We keep the g_Vulkan context around to avoid invalidating a ton of pointers around the app.
	finalize_glslang();
	INFO_LOG(G3D, "IOSVulkanContext::Shutdown completed");
}

void IOSVulkanContext::Resize() {
	INFO_LOG(G3D, "IOSVulkanContext::Resize begin (oldsize: %dx%d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	g_Vulkan->DestroySwapchain();
	g_Vulkan->DestroySurface();

	g_Vulkan->UpdateFlags(FlagsFromConfig());

	g_Vulkan->ReinitSurface();
	g_Vulkan->InitSwapchain();
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	INFO_LOG(G3D, "IOSVulkanContext::Resize end (final size: %dx%d)", g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
}

bool IOSVulkanContext::InitAPI() {
	INFO_LOG(G3D, "IOSVulkanContext::Init");
	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	INFO_LOG(G3D, "Creating Vulkan context");
	Version gitVer(PPSSPP_GIT_VERSION);

	std::string errorStr;
	if (!VulkanLoad(&errorStr)) {
		ERROR_LOG(G3D, "Failed to load Vulkan driver library: %s", errorStr.c_str());
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	if (!g_Vulkan) {
		// TODO: Assert if g_Vulkan already exists here?
		g_Vulkan = new VulkanContext();
	}

	VulkanContext::CreateInfo info{};
	info.app_name = "PPSSPP";
	info.app_ver = gitVer.ToInteger();
	info.flags = FlagsFromConfig();
	if (!g_Vulkan->CreateInstanceAndDevice(info)) {
		delete g_Vulkan;
		g_Vulkan = nullptr;
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	g_Vulkan->SetCbGetDrawSize([]() {
		return VkExtent2D {(uint32_t)g_display.pixel_xres, (uint32_t)g_display.pixel_yres};
	});

	INFO_LOG(G3D, "Vulkan device created!");
	state_ = GraphicsContextState::INITIALIZED;
	return true;
}


#pragma mark -
#pragma mark PPSSPPViewControllerMetal

static std::atomic<bool> exitRenderLoop;
static std::atomic<bool> renderLoopRunning;
static std::thread g_renderLoopThread;

@interface PPSSPPViewControllerMetal () {
	ICadeTracker g_iCadeTracker;
	TouchTracker g_touchTracker;

	IOSVulkanContext *graphicsContext;
	LocationHelper *locationHelper;
	CameraHelper *cameraHelper;
}

@property (nonatomic) GCController *gameController __attribute__((weak_import));
@property (strong, nonatomic) CMMotionManager *motionManager;
@property (strong, nonatomic) NSOperationQueue *accelerometerQueue;

@end  // @interface

@implementation PPSSPPViewControllerMetal {
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
	}
	self.accelerometerQueue = [[NSOperationQueue alloc] init];
	self.accelerometerQueue.name = @"AccelerometerQueue";
	self.accelerometerQueue.maxConcurrentOperationCount = 1;
	return self;
}

- (void)appWillTerminate:(NSNotification *)notification
{
	[self shutdown];
}

// Should be very similar to the Android one, probably mergeable.
void VulkanRenderLoop(IOSVulkanContext *graphicsContext, CAMetalLayer *metalLayer) {
	SetCurrentThreadName("EmuThreadVulkan");
	INFO_LOG(G3D, "Entering EmuThreadVulkan");

	if (!graphicsContext) {
		ERROR_LOG(G3D, "runVulkanRenderLoop: Tried to enter without a created graphics context.");
		renderLoopRunning = false;
		exitRenderLoop = false;
		return;
	}

	if (exitRenderLoop) {
		WARN_LOG(G3D, "runVulkanRenderLoop: ExitRenderLoop requested at start, skipping the whole thing.");
		renderLoopRunning = false;
		exitRenderLoop = false;
		return;
	}

	// This is up here to prevent race conditions, in case we pause during init.
	renderLoopRunning = true;

	int desiredBackbufferSizeX = g_display.pixel_xres;
	int desiredBackbufferSizeY = g_display.pixel_yres;

	//WARN_LOG(G3D, "runVulkanRenderLoop. desiredBackbufferSizeX=%d desiredBackbufferSizeY=%d",
	//	desiredBackbufferSizeX, desiredBackbufferSizeY);

	if (!graphicsContext->InitFromRenderThread(metalLayer, desiredBackbufferSizeX, desiredBackbufferSizeY)) {
		// On Android, if we get here, really no point in continuing.
		// The UI is supposed to render on any device both on OpenGL and Vulkan. If either of those don't work
		// on a device, we blacklist it. Hopefully we should have already failed in InitAPI anyway and reverted to GL back then.
		ERROR_LOG(G3D, "Failed to initialize graphics context.");
		System_Toast("Failed to initialize graphics context.");

		delete graphicsContext;
		graphicsContext = nullptr;
		renderLoopRunning = false;
		return;
	}

	if (!exitRenderLoop) {
		if (!NativeInitGraphics(graphicsContext)) {
			ERROR_LOG(G3D, "Failed to initialize graphics.");
			// Gonna be in a weird state here..
		}
		graphicsContext->ThreadStart();
		while (!exitRenderLoop) {
			NativeFrame(graphicsContext);
		}
		INFO_LOG(G3D, "Leaving Vulkan main loop.");
	} else {
		INFO_LOG(G3D, "Not entering main loop.");
	}

	NativeShutdownGraphics();

	graphicsContext->ThreadEnd();

	// Shut the graphics context down to the same state it was in when we entered the render thread.
	INFO_LOG(G3D, "Shutting down graphics context...");
	graphicsContext->ShutdownFromRenderThread();
	renderLoopRunning = false;
	exitRenderLoop = false;

	WARN_LOG(G3D, "Render loop function exited.");
}

- (bool)runVulkanRenderLoop {
	INFO_LOG(G3D, "runVulkanRenderLoop");

	if (!graphicsContext) {
		ERROR_LOG(G3D, "runVulkanRenderLoop: Tried to enter without a created graphics context.");
		return false;
	}

	if (g_renderLoopThread.joinable()) {
		ERROR_LOG(G3D, "runVulkanRenderLoop: Already running");
		return false;
	}

	CAMetalLayer *metalLayer = (CAMetalLayer *)self.view.layer;
	g_renderLoopThread = std::thread(VulkanRenderLoop, graphicsContext, metalLayer);
	return true;
}

- (void)requestExitVulkanRenderLoop {
	INFO_LOG(G3D, "requestExitVulkanRenderLoop");

	if (!renderLoopRunning) {
		ERROR_LOG(SYSTEM, "Render loop already exited");
		return;
	}
	_assert_(g_renderLoopThread.joinable());
	exitRenderLoop = true;
	g_renderLoopThread.join();
	_assert_(!g_renderLoopThread.joinable());
}

// These two are forwarded from the appDelegate
- (void)didBecomeActive {
	INFO_LOG(G3D, "didBecomeActive GL");
	if (self.motionManager.accelerometerAvailable) {
		self.motionManager.accelerometerUpdateInterval = 1.0 / 60.0;
		INFO_LOG(G3D, "Starting accelerometer updates.");

		[self.motionManager startAccelerometerUpdatesToQueue:self.accelerometerQueue
							withHandler:^(CMAccelerometerData *accelerometerData, NSError *error) {
			if (error) {
				NSLog(@"Accelerometer error: %@", error);
				return;
			}
			ProcessAccelerometerData(accelerometerData);
		}];
	} else {
		INFO_LOG(G3D, "No accelerometer available, not starting updates.");
	}
	// Spin up the emu thread. It will in turn spin up the Vulkan render thread
	// on its own.
	[self runVulkanRenderLoop];
}

- (void)willResignActive {
	INFO_LOG(G3D, "willResignActive GL");
	[self requestExitVulkanRenderLoop];

	// Stop accelerometer updates
	if (self.motionManager.accelerometerActive) {
		INFO_LOG(G3D, "Stopping accelerometer updates");
		[self.motionManager stopAccelerometerUpdates];
	}
}

- (void)shutdown
{
	INFO_LOG(SYSTEM, "shutdown VK");

	g_Config.Save("shutdown vk");

	_dbg_assert_(sharedViewController != nil);
	sharedViewController = nil;

	[[NSNotificationCenter defaultCenter] removeObserver:self];

	self.gameController = nil;

	if (graphicsContext) {
		graphicsContext->Shutdown();
		delete graphicsContext;
		graphicsContext = NULL;
	}
}

- (void)dealloc
{
	INFO_LOG(SYSTEM, "dealloc VK");
}

- (void)loadView {
	INFO_LOG(G3D, "Creating metal view");

	CGRect screenRect = [[UIScreen mainScreen] bounds];
	CGFloat screenWidth = screenRect.size.width;
	CGFloat screenHeight = screenRect.size.height;

	PPSSPPMetalView *metalView = [[PPSSPPMetalView alloc] initWithFrame:CGRectMake(0, 0, screenWidth,screenHeight)];
	self.view = metalView;
}

- (void)viewDidLoad {
	[super viewDidLoad];
	[self hideKeyboard];

	[[DisplayManager shared] setupDisplayListener];

	INFO_LOG(SYSTEM, "Metal viewDidLoad");

	UIScreen* screen = [(AppDelegate*)[UIApplication sharedApplication].delegate screen];
	self.view.frame = [screen bounds];
	self.view.multipleTouchEnabled = YES;
	graphicsContext = new IOSVulkanContext();

	[[DisplayManager shared] updateResolution:[UIScreen mainScreen]];

	if (!graphicsContext->InitAPI()) {
		_assert_msg_(false, "Failed to init Vulkan");
	}

	if ([[GCController controllers] count] > 0) {
		[self setupController:[[GCController controllers] firstObject]];
	}

	INFO_LOG(G3D, "Detected size: %dx%d", g_display.pixel_xres, g_display.pixel_yres);

	cameraHelper = [[CameraHelper alloc] init];
	[cameraHelper setDelegate:self];

	locationHelper = [[LocationHelper alloc] init];
	[locationHelper setDelegate:self];

	// Initialize the motion manager for accelerometer control.
	self.motionManager = [[CMMotionManager alloc] init];
}

// Allow device rotation to resize the swapchain
-(void)viewWillTransitionToSize:(CGSize)size withTransitionCoordinator:(id)coordinator {
	[super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];
	// TODO: Handle resizing properly.
}

- (UIView *)getView {
	return [self view];
}

- (void)viewWillAppear:(BOOL)animated {
	[super viewWillAppear:animated];
	INFO_LOG(G3D, "viewWillAppear");
	self.view.contentScaleFactor = UIScreen.mainScreen.nativeScale;
}

- (void)viewWillDisappear:(BOOL)animated {
	[super viewWillDisappear:animated];
	INFO_LOG(G3D, "viewWillDisappear");
}

- (void)viewDidDisappear:(BOOL)animated {
	[super viewDidDisappear: animated];
	INFO_LOG(G3D, "viewWillDisappear");
}

- (void)viewDidAppear:(BOOL)animated {
	[super viewDidAppear:animated];
	INFO_LOG(G3D, "viewDidAppear");
	[self hideKeyboard];
	[self updateGesture];

}

- (BOOL)prefersHomeIndicatorAutoHidden {
	// Would love to hide it, but it prevents the double-swipe protection from working.
	return NO;
}

- (void)shareText:(NSString *)text {
	NSArray *items = @[text];
	UIActivityViewController * viewController = [[UIActivityViewController alloc] initWithActivityItems:items applicationActivities:nil];
	dispatch_async(dispatch_get_main_queue(), ^{
		[self presentViewController:viewController animated:YES completion:nil];
	});
}

extern float g_safeInsetLeft;
extern float g_safeInsetRight;
extern float g_safeInsetTop;
extern float g_safeInsetBottom;

- (void)viewSafeAreaInsetsDidChange {
	if (@available(iOS 11.0, *)) {
		[super viewSafeAreaInsetsDidChange];
		// we use 0.0f instead of safeAreaInsets.bottom because the bottom overlay isn't disturbing (for now)
		g_safeInsetLeft = self.view.safeAreaInsets.left;
		g_safeInsetRight = self.view.safeAreaInsets.right;
		g_safeInsetTop = self.view.safeAreaInsets.top;
		g_safeInsetBottom = 0.0f;
	}
}

// Enables tapping for edge area.
-(UIRectEdge)preferredScreenEdgesDeferringSystemGestures
{
	if (GetUIState() == UISTATE_INGAME) {
		// In-game, we need all the control we can get. Though, we could possibly
		// allow the top edge?
		INFO_LOG(SYSTEM, "Defer system gestures on all edges");
		return UIRectEdgeAll;
	} else {
		INFO_LOG(SYSTEM, "Allow system gestures on the bottom");
		// Allow task switching gestures to take precedence, without causing
		// scroll events in the UI.
		return UIRectEdgeTop | UIRectEdgeLeft | UIRectEdgeRight;
	}
}

- (void)uiStateChanged
{
	[self setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
	[self hideKeyboard];
	[self updateGesture];
}

- (void)updateGesture {
	INFO_LOG(SYSTEM, "Updating swipe gesture.");

	if (mBackGestureRecognizer) {
		INFO_LOG(SYSTEM, "Removing swipe gesture.");
		[[self view] removeGestureRecognizer:mBackGestureRecognizer];
		mBackGestureRecognizer = nil;
	}

	if (GetUIState() != UISTATE_INGAME) {
		INFO_LOG(SYSTEM, "Adding swipe gesture.");
		mBackGestureRecognizer = [[UIScreenEdgePanGestureRecognizer alloc] initWithTarget:self action:@selector(handleSwipeFrom:) ];
		[mBackGestureRecognizer setEdges:UIRectEdgeLeft];
		[[self view] addGestureRecognizer:mBackGestureRecognizer];
	}
}

- (void)bindDefaultFBO
{
	// Do nothing
}

- (NSUInteger)supportedInterfaceOrientations
{
	return UIInterfaceOrientationMaskLandscape;
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

- (void)controllerDidConnect:(NSNotification *)note
{
	if (![[GCController controllers] containsObject:self.gameController]) self.gameController = nil;

	if (self.gameController != nil) return; // already have a connected controller

	[self setupController:(GCController *)note.object];
}

- (void)controllerDidDisconnect:(NSNotification *)note
{
	if (self.gameController == note.object) {
		self.gameController = nil;

		if ([[GCController controllers] count] > 0) {
			[self setupController:[[GCController controllers] firstObject]];
		}
	}
}

- (void)setupController:(GCController *)controller
{
	self.gameController = controller;
	if (!InitController(controller)) {
		self.gameController = nil;
	}
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

- (void)handleSwipeFrom:(UIScreenEdgePanGestureRecognizer *)recognizer
{
	if (recognizer.state == UIGestureRecognizerStateEnded) {
		KeyInput key;
		key.flags = KEY_DOWN | KEY_UP;
		key.keyCode = NKCODE_BACK;
		key.deviceId = DEVICE_ID_TOUCH;
		NativeKey(key);
		INFO_LOG(SYSTEM, "Detected back swipe");
	}
}
// The below is inspired by https://stackoverflow.com/questions/7253477/how-to-display-the-iphone-ipad-keyboard-over-a-full-screen-opengl-es-app
// It's a bit limited but good enough.

-(void) deleteBackward {
	KeyInput input{};
	input.deviceId = DEVICE_ID_KEYBOARD;
	input.flags = KEY_DOWN | KEY_UP;
	input.keyCode = NKCODE_DEL;
	NativeKey(input);
	INFO_LOG(SYSTEM, "Backspace");
}

-(BOOL) hasText
{
	return YES;
}

-(void) insertText:(NSString *)text
{
	std::string str([text UTF8String]);
	INFO_LOG(SYSTEM, "Chars: %s", str.c_str());
	SendKeyboardChars(str);
}

-(BOOL) canBecomeFirstResponder
{
	return YES;
}

-(void) showKeyboard {
	dispatch_async(dispatch_get_main_queue(), ^{
		[self becomeFirstResponder];
	});
}

-(void) hideKeyboard {
	dispatch_async(dispatch_get_main_queue(), ^{
		[self resignFirstResponder];
	});
}

@end

@implementation PPSSPPMetalView

/** Returns a Metal-compatible layer. */
+(Class) layerClass { return [CAMetalLayer class]; }

@end
