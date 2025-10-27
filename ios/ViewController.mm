//
// ViewController.m
//
// Created by rock88
// Modified by xSacha
// Reworked by hrydgard

#import "AppDelegate.h"
#import "ViewController.h"
#import "DisplayManager.h"
#import "iOSCoreAudio.h"
#import "IAPManager.h"

#import <GLKit/GLKit.h>
#import "Controls.h"
#import <QuartzCore/QuartzCore.h>

#include <cassert>
#include "Common/Net/Resolve.h"
#include "Common/UI/Screen.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/File/VFS/VFS.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Common/Input/InputState.h"
#include "Common/Input/KeyCodes.h"
#include "Common/GraphicsContext.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/KeyMap.h"
#include "Core/System.h"

#if !__has_feature(objc_arc)
#error Must be built with ARC, please revise the flags for ViewController.mm to include -fobjc-arc.
#endif

class IOSGLESContext : public GraphicsContext {
public:
	IOSGLESContext() {
		CheckGLExtensions();
		draw_ = Draw::T3DCreateGLContext(false);
		renderManager_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
		renderManager_->SetInflightFrames(g_Config.iInflightFrames);
		SetGPUBackend(GPUBackend::OPENGL);
		bool success = draw_->CreatePresets();
		_assert_msg_(success, "Failed to compile preset shaders");
	}
	~IOSGLESContext() {
		delete draw_;
	}
	Draw::DrawContext *GetDrawContext() override {
		return draw_;
	}

	void Resize() override {}
	void Shutdown() override {}

	void BeginShutdown() {
		renderManager_->SetSkipGLCalls();
	}

	void ThreadStart() override {
		renderManager_->ThreadStart(draw_);
	}

	bool ThreadFrame(bool waitIfEmpty) override {
		return renderManager_->ThreadFrame(waitIfEmpty);
	}

	void ThreadEnd() override {
		renderManager_->ThreadEnd();
	}

	void StartThread() {
		renderManager_->StartThread();
	}

private:
	Draw::DrawContext *draw_;
	GLRenderManager *renderManager_;
};

static std::atomic<bool> exitRenderLoop;
static std::atomic<bool> renderLoopRunning;
static std::thread g_renderLoopThread;

PPSSPPBaseViewController *sharedViewController;

@interface PPSSPPViewControllerGL () {
	ICadeTracker g_iCadeTracker;

	IOSGLESContext *graphicsContext;

	int imageRequestId;
	NSString *imageFilename;
}

@property (nonatomic, strong) EAGLContext *glContext;
@property (nonatomic, strong) GLKView *glView;
@property (nonatomic, strong) CADisplayLink *displayLink;
@property (nonatomic, assign) NSTimeInterval lastTimestamp;

@property (nonatomic, strong) EAGLContext* context;

@property (nonatomic) GCController *gameController __attribute__((weak_import));
@property (strong, nonatomic) CMMotionManager *motionManager;
@property (strong, nonatomic) NSOperationQueue *accelerometerQueue;

@end

@implementation PPSSPPViewControllerGL {}

-(id) init {
	self = [super init];
	if (self) {
		_preferredFramesPerSecond = 60; // default
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

- (BOOL)prefersHomeIndicatorAutoHidden {
	if (g_Config.iAppSwitchMode == (int)AppSwitchMode::DOUBLE_SWIPE_INDICATOR) {
		return NO;
	} else {
		return YES;
	}
}

// The actual rendering is NOT on this thread, this is the emu thread
// that runs game logic.
void GLRenderLoop(IOSGLESContext *graphicsContext) {
	SetCurrentThreadName("EmuThreadGL");
	renderLoopRunning = true;

	NativeInitGraphics(graphicsContext);

	INFO_LOG(Log::System, "Emulation thread starting\n");
	while (!exitRenderLoop) {
		NativeFrame(graphicsContext);
	}

	INFO_LOG(Log::System, "Emulation thread shutting down\n");
	NativeShutdownGraphics();

	// Also ask the main thread to stop, so it doesn't hang waiting for a new frame.
	INFO_LOG(Log::System, "Emulation thread stopping\n");

	exitRenderLoop = false;
	renderLoopRunning = false;
}

- (bool)runGLRenderLoop {
	if (!graphicsContext) {
		ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Tried to enter without a created graphics context.");
		return false;
	}

	if (g_renderLoopThread.joinable()) {
		ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Already running");
		return false;
	}

	_dbg_assert_(!renderLoopRunning);
	_dbg_assert_(!exitRenderLoop);

	graphicsContext->StartThread();

	g_renderLoopThread = std::thread(GLRenderLoop, graphicsContext);
	return true;
}

- (void)requestExitGLRenderLoop {
	if (!renderLoopRunning) {
		ERROR_LOG(Log::System, "Render loop already exited");
		return;
	}
	_assert_(g_renderLoopThread.joinable());
	exitRenderLoop = true;
	graphicsContext->ThreadFrameUntilCondition([]() -> bool {
		return !renderLoopRunning.load();
	});
	g_renderLoopThread.join();
	_assert_(!g_renderLoopThread.joinable());
}

- (void)viewDidLoad {
	[super viewDidLoad];

	// 1) Create GL context
	self.glContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES3];
	if (!self.glContext) {
		self.glContext = [[EAGLContext alloc] initWithAPI:kEAGLRenderingAPIOpenGLES2];
	}
	NSAssert(self.glContext != nil, @"Failed to create EAGLContext");

	// 2) Create GLKView
	self.glView = [[GLKView alloc] initWithFrame:self.view.bounds context:self.glContext];
	self.glView.delegate = self;
	self.glView.enableSetNeedsDisplay = NO; // We'll call display manually
	self.glView.drawableDepthFormat = GLKViewDrawableDepthFormat24;
	self.glView.drawableStencilFormat = GLKViewDrawableStencilFormat8;
	self.glView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
	[self.view addSubview:self.glView];

	// Put context current for initial GL setup
	[EAGLContext setCurrentContext:self.glContext];

	// Here we can do one time GL init if we want.

	// 3) Setup display link
	self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(displayLinkFired:)];
	if (@available(iOS 10.0, *)) {
		self.displayLink.preferredFramesPerSecond = (NSInteger)self.preferredFramesPerSecond;
	} else {
		// older iOS: approximate with frameInterval
		self.displayLink.frameInterval = MAX(1, (NSInteger)round(60.0 / self.preferredFramesPerSecond));
	}
	[self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];

	self.lastTimestamp = 0;

	[[DisplayManager shared] setupDisplayListener];

	UIScreen* screen = [(AppDelegate*)[UIApplication sharedApplication].delegate screen];
	self.view.frame = [screen bounds];
	self.view.multipleTouchEnabled = YES;

	graphicsContext = new IOSGLESContext();

	graphicsContext->GetDrawContext()->SetErrorCallback([](const char *shortDesc, const char *details, void *userdata) {
		g_OSD.Show(OSDType::MESSAGE_ERROR, details, 0.0f, "error_callback");
	}, nullptr);

	graphicsContext->ThreadStart();

	/*self.iCadeView = [[iCadeReaderView alloc] init];
	[self.view addSubview:self.iCadeView];
	self.iCadeView.delegate = self;
	self.iCadeView.active = YES;*/

	if ([[GCController controllers] count] > 0) {
		[self setupController:[[GCController controllers] firstObject]];
	}

	[self hideKeyboard];

	// Initialize the motion manager for accelerometer control.
	self.motionManager = [[CMMotionManager alloc] init];
	INFO_LOG(Log::G3D, "Done with viewDidLoad.");
}

- (void)viewDidLayoutSubviews {
	[super viewDidLayoutSubviews];
	self.glView.frame = self.view.bounds;
	/*
	// if you need to update viewport:
	CGSize s = self.glView.bounds.size;
	[EAGLContext setCurrentContext:self.glContext];
	glViewport(0, 0, (GLsizei)round(s.width), (GLsizei)round(s.height));
	*/
}

- (void)viewWillAppear:(BOOL)animated {
	[super viewWillAppear:animated];
	// Resume display link unless explicitly paused
	INFO_LOG(Log::G3D, "viewWillAppear - resuming display link");
}

- (void)viewWillDisappear:(BOOL)animated {
	[super viewWillDisappear:animated];
	// stop rendering while not visible
	INFO_LOG(Log::G3D, "viewWillDisappear - pausing display link");
}

- (void)dealloc {
	[self.displayLink invalidate];
	self.displayLink = nil;

	if ([EAGLContext currentContext] == self.glContext) {
		[EAGLContext setCurrentContext:nil];
	}
	self.glContext = nil;
}

- (void)setPreferredFramesPerSecond:(NSInteger)preferredFramesPerSecond {
	_preferredFramesPerSecond = preferredFramesPerSecond;
	if (self.displayLink) {
		if (@available(iOS 10.0, *)) {
			self.displayLink.preferredFramesPerSecond = (NSInteger)preferredFramesPerSecond;
		} else {
			self.displayLink.frameInterval = MAX(1, (NSInteger)round(60.0 / preferredFramesPerSecond));
		}
	}
}

- (void)displayLinkFired:(CADisplayLink *)dl {
	// compute delta time
	NSTimeInterval timestamp = dl.timestamp;
	NSTimeInterval delta = 0;
	if (self.lastTimestamp > 0) {
		delta = timestamp - self.lastTimestamp;
	} else {
		delta = dl.duration; // fallback
	}
	self.lastTimestamp = timestamp;

	// Ensure context is current before drawing
	[EAGLContext setCurrentContext:self.glContext];

	// Trigger GLKView draw
	[self.glView display];
}

- (void)glkView:(GLKView *)view drawInRect:(CGRect)rect {
	if (!renderLoopRunning) {
		INFO_LOG(Log::G3D, "Ignoring drawInRect");
		return;
	}
	if (sharedViewController) {
		graphicsContext->ThreadFrame(true);
	}
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

- (void)appWillTerminate:(NSNotification *)notification
{
	[self shutdown];
}

- (void)didBecomeActive {
	INFO_LOG(Log::System, "didBecomeActive begin");
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
	[self runGLRenderLoop];
	[[DisplayManager shared] updateResolution:[UIScreen mainScreen]];

	INFO_LOG(Log::System, "didBecomeActive end");

	self.displayLink.paused = NO;
}

- (void)willResignActive {
	INFO_LOG(Log::System, "willResignActive begin");
	[self requestExitGLRenderLoop];

	// Stop accelerometer updates
	if (self.motionManager.accelerometerActive) {
		INFO_LOG(Log::G3D, "Stopping accelerometer updates");
		[self.motionManager stopAccelerometerUpdates];
	}
	INFO_LOG(Log::System, "willResignActive end");

	self.displayLink.paused = YES;
}

- (void)shutdown
{
	INFO_LOG(Log::System, "shutdown GL");

	g_Config.Save("shutdown GL");

	_dbg_assert_(graphicsContext);
	_dbg_assert_(sharedViewController != nil);
	sharedViewController = nil;

	if (self.context) {
		if ([EAGLContext currentContext] == self.context) {
			[EAGLContext setCurrentContext:nil];
		}
		self.context = nil;
	}

	[[NSNotificationCenter defaultCenter] removeObserver:self];

	self.gameController = nil;

	graphicsContext->BeginShutdown();
	// Skipping GL calls here because the old context is lost.
	graphicsContext->ThreadFrameUntilCondition([]() -> bool {
		return !renderLoopRunning;
	});
	graphicsContext->ThreadEnd();

	graphicsContext->Shutdown();
	delete graphicsContext;
	graphicsContext = nullptr;
	INFO_LOG(Log::System, "Done shutting down GL");
}

- (UIInterfaceOrientationMask)supportedInterfaceOrientations
{
	return UIInterfaceOrientationMaskAll;
}

- (void)bindDefaultFBO
{
	[(GLKView*)self.glView bindDrawable];
}

- (void)buttonDown:(iCadeState)button
{
	g_iCadeTracker.ButtonDown(button);
}

- (void)buttonUp:(iCadeState)button
{
	g_iCadeTracker.ButtonUp(button);
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
		self.gameController = nil;

		if ([[GCController controllers] count] > 0) {
			[self setupController:[[GCController controllers] firstObject]];
		}
	}
}

- (UIView *)getView {
	return [self view];
}

- (void)setupController:(GCController *)controller
{
	self.gameController = controller;
	if (!InitController(controller)) {
		self.gameController = nil;
	}
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

- (void)viewWillTransitionToSize:(CGSize)size
		withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)coordinator {
	[super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];

	[self.view endEditing:YES]; // clears any input focus

	[coordinator animateAlongsideTransition:^(id<UIViewControllerTransitionCoordinatorContext> context) {
		NSLog(@"Rotating to size: %@", NSStringFromCGSize(size));
	} completion:^(id<UIViewControllerTransitionCoordinatorContext> context) {
		NSLog(@"Rotation finished");
		// Reinitialize graphics context to match new size
		[self requestExitGLRenderLoop];
		[self runGLRenderLoop];
		[[DisplayManager shared] updateResolution:[UIScreen mainScreen]];
	}];
}

@end

void bindDefaultFBO()
{
	[sharedViewController bindDefaultFBO];
}

void EnableFZ(){};
void DisableFZ(){};
