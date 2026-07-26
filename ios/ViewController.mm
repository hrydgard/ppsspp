//
// ViewController.m
//
// Created by rock88
// Modified by xSacha
// Reworked by hrydgard

#import "AppDelegate.h"
#import "ViewController.h"
#import "iOSCoreAudio.h"

#import <GLKit/GLKit.h>
#import <QuartzCore/QuartzCore.h>

#include <cassert>
#include "Common/Net/Resolve.h"
#include "Common/UI/Screen.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/GPU/OpenGL/OpenGLGraphicsContext.h"
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
#include "Common/GPU/GraphicsContext.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/EmuThread.h"
#include "Core/KeyMap.h"
#include "Core/System.h"

#if !__has_feature(objc_arc)
#error Must be built with ARC, please revise the flags for ViewController.mm to include -fobjc-arc.
#endif

static std::atomic<bool> renderLoopRunning;
static std::thread g_emuThread;

PPSSPPBaseViewController *sharedViewController;

@interface PPSSPPViewControllerGL () {
	GraphicsContext *graphicsContext;

	int imageRequestId;
	NSString *imageFilename;
}

@property (nonatomic, strong) EAGLContext *glContext;
@property (nonatomic, strong) GLKView *glView;
@property (nonatomic, strong) CADisplayLink *displayLink;
@property (nonatomic, assign) NSTimeInterval lastTimestamp;

@property (nonatomic, strong) EAGLContext* context;

@end

@implementation PPSSPPViewControllerGL {}

-(id) init {
	self = [super init];
	if (self) {
		_preferredFramesPerSecond = 60; // default
	}
	return self;
}

- (bool)runGLRenderLoop {
	if (!graphicsContext) {
		ERROR_LOG(Log::G3D, "runGLRenderLoop: Tried to enter without a created graphics context.");
		return false;
	}

	if (g_emuThread.joinable()) {
		ERROR_LOG(Log::G3D, "runGLRenderLoop: Already running");
		return false;
	}

	g_emuThread = EmuThread_Start(graphicsContext, new NativeApplication(), [](){});
	return true;
}

- (void)requestExitGLRenderLoop {
	_assert_(g_emuThread.joinable());
	EmuThread_Join(graphicsContext, g_emuThread);
	_assert_(!g_emuThread.joinable());
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
	self.glView.multipleTouchEnabled = YES;
	[self.view addSubview:self.glView];

	// self.view.insetsLayoutMarginsFromSafeArea = NO;
	// self.view.clipsToBounds = YES;

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

	UIScreen* screen = [(AppDelegate*)[UIApplication sharedApplication].delegate screen];
	self.view.frame = [screen bounds];
	self.view.multipleTouchEnabled = YES;

	graphicsContext = new OpenGLGraphicsContext();

	std::string errorMessage;
	if (!graphicsContext->InitAPI(nullptr, nullptr, &errorMessage)) {
		ERROR_LOG(Log::G3D, "InitAPI failed: %s", errorMessage.c_str());
	}

	if (!graphicsContext->InitSurface(WINDOWSYSTEM_NONE, nullptr, nullptr, &errorMessage)) {
		ERROR_LOG(Log::G3D, "InitSurface failed: %s", errorMessage.c_str());
	}

	/*self.iCadeView = [[iCadeReaderView alloc] init];
	[self.view addSubview:self.iCadeView];
	self.iCadeView.delegate = self;
	self.iCadeView.active = YES;*/

	if ([[GCController controllers] count] > 0) {
		[self setupController:[[GCController controllers] firstObject]];
	}

	[self hideKeyboard];

	// Initialize the motion manager for accelerometer control.
	INFO_LOG(Log::G3D, "Done with viewDidLoad.");
}

- (void)viewDidLayoutSubviews {
	[super viewDidLayoutSubviews];
	self.glView.frame = self.view.bounds;
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
	if (!g_emuThread.joinable()) {
		INFO_LOG(Log::G3D, "Ignoring drawInRect");
		return;
	}
	if (sharedViewController) {
		graphicsContext->ThreadFrame(true);
	}
}

- (void)didBecomeActive {
	[super didBecomeActive];

	INFO_LOG(Log::System, "didBecomeActive begin");

	[self updateResolutionWithView:self.view];
	[self runGLRenderLoop];

	INFO_LOG(Log::System, "didBecomeActive end");

	self.displayLink.paused = NO;
}

- (void)willResignActive {
	INFO_LOG(Log::System, "willResignActive GL");
	[self requestExitGLRenderLoop];

	self.displayLink.paused = YES;

	[super willResignActive];
}

- (void)shutdown {
	[super shutdown];

	INFO_LOG(Log::System, "shutdown GL");

	g_Config.Save("shutdown GL");

	_dbg_assert_(graphicsContext);

	if (self.context) {
		if ([EAGLContext currentContext] == self.context) {
			[EAGLContext setCurrentContext:nil];
		}
		self.context = nil;
	}

	[[NSNotificationCenter defaultCenter] removeObserver:self];

	graphicsContext->ShutdownSurface();
	graphicsContext->ShutdownAPI();
	delete graphicsContext;
	graphicsContext = nullptr;
	INFO_LOG(Log::System, "Done shutting down GL");
}

- (void)bindDefaultFBO
{
	[(GLKView*)self.glView bindDrawable];
}

- (UIView *)getView {
	return [self view];
}

// Can't consolidate this yet.
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
		[self updateResolutionWithView:self.view];
		[self runGLRenderLoop];
	}];
}

@end

void bindDefaultFBO()
{
	[sharedViewController bindDefaultFBO];
}

void EnableFZ(){};
void DisableFZ(){};
