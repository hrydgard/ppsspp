#import <QuartzCore/CADisplayLink.h>
#import <Metal/Metal.h>

#import "AppDelegate.h"
#import "ViewControllerMetal.h"
#import "DisplayManager.h"
#import "iOSCoreAudio.h"

#include "ios/iOSVulkanContext.h"

#include "Common/Log.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/TimeUtil.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

#include "GPU/Vulkan/VulkanUtil.h"

#pragma mark -
#pragma mark PPSSPPViewControllerMetal

static std::atomic<bool> exitRenderLoop;
static std::atomic<bool> renderLoopRunning;
static std::thread g_renderLoopThread;

@interface PPSSPPViewControllerMetal () {
	IOSVulkanContext *graphicsContext;
}

@end  // @interface

@implementation PPSSPPViewControllerMetal {}

PPSSPPMetalView *metalView;

- (id)init {
	self = [super init];
	return self;
}

// Should be very similar to the Android one, probably mergeable.
// This is the EmuThread for iOS.
void VulkanRenderLoop(IOSVulkanContext *graphicsContext, CAMetalLayer *metalLayer) {
	SetCurrentThreadName("EmuThreadVulkan");
	INFO_LOG(Log::G3D, "Entering EmuThreadVulkan");

	if (!graphicsContext) {
		ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Tried to enter without a created graphics context.");
		renderLoopRunning = false;
		exitRenderLoop = false;
		return;
	}

	if (exitRenderLoop) {
		WARN_LOG(Log::G3D, "runVulkanRenderLoop: ExitRenderLoop requested at start, skipping the whole thing.");
		renderLoopRunning = false;
		exitRenderLoop = false;
		return;
	}

	// This is up here to prevent race conditions, in case we pause during init.
	renderLoopRunning = true;

	int desiredBackbufferSizeX = g_display.pixel_xres;
	int desiredBackbufferSizeY = g_display.pixel_yres;

	if (!graphicsContext->InitFromRenderThread(metalLayer, desiredBackbufferSizeX, desiredBackbufferSizeY)) {
		// The UI is supposed to render on any device both on OpenGL and Vulkan. If either of those don't work
		// on a device, we blacklist it. Hopefully we should have already failed in InitAPI anyway and reverted to GL back then.
		ERROR_LOG(Log::G3D, "Failed to initialize graphics context.");
		System_Toast("Failed to initialize graphics context.");

		delete graphicsContext;
		graphicsContext = nullptr;
		renderLoopRunning = false;
		return;
	}

	if (!exitRenderLoop) {
		if (!NativeInitGraphics(graphicsContext)) {
			ERROR_LOG(Log::G3D, "Failed to initialize graphics.");
			// Gonna be in a weird state here..
		}
		graphicsContext->ThreadStart();
		while (!exitRenderLoop) {
			NativeFrame(graphicsContext);
		}
		INFO_LOG(Log::G3D, "Leaving Vulkan main loop.");
	} else {
		INFO_LOG(Log::G3D, "Not entering main loop.");
	}

	NativeShutdownGraphics();

	graphicsContext->ThreadEnd();

	// Shut the graphics context down to the same state it was in when we entered the render thread.
	INFO_LOG(Log::G3D, "Shutting down graphics context...");
	graphicsContext->ShutdownFromRenderThread();
	renderLoopRunning = false;
	exitRenderLoop = false;

	WARN_LOG(Log::G3D, "Render loop function exited.");
}

- (bool)runVulkanRenderLoop {
	INFO_LOG(Log::G3D, "runVulkanRenderLoop");

	if (!graphicsContext) {
		ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Tried to enter without a created graphics context.");
		return false;
	}

	if (g_renderLoopThread.joinable()) {
		ERROR_LOG(Log::G3D, "runVulkanRenderLoop: Already running");
		return false;
	}

	CAMetalLayer *metalLayer = (CAMetalLayer *)self.view.layer;
	g_renderLoopThread = std::thread(VulkanRenderLoop, graphicsContext, metalLayer);

	[(PPSSPPMetalView *)self.view startDisplayLink];

	return true;
}

- (void)requestExitVulkanRenderLoop {
	INFO_LOG(Log::G3D, "requestExitVulkanRenderLoop");

	if (!renderLoopRunning) {
		ERROR_LOG(Log::System, "Render loop already exited");
		return;
	}
	[(PPSSPPMetalView *)self.view stopDisplayLink];

	_assert_(g_renderLoopThread.joinable());
	exitRenderLoop = true;
	g_renderLoopThread.join();
	_assert_(!g_renderLoopThread.joinable());
}

// These two are forwarded from the appDelegate
- (void)didBecomeActive {
	[super didBecomeActive];
	INFO_LOG(Log::G3D, "didBecomeActive Metal");
	
	// Spin up the emu thread. It will in turn spin up the Vulkan render thread
	// on its own.
	[self runVulkanRenderLoop];
	[[DisplayManager shared] updateResolution:[UIScreen mainScreen]];
}

- (void)willResignActive {
	INFO_LOG(Log::G3D, "willResignActive Metal");
	[self requestExitVulkanRenderLoop];

	[super willResignActive];
}

- (void)shutdown {
	[super shutdown];

	INFO_LOG(Log::System, "shutdown");

	g_Config.Save("shutdown vk");

	if (graphicsContext) {
		graphicsContext->Shutdown();
		delete graphicsContext;
		graphicsContext = NULL;
	}
}

- (void)dealloc
{
	INFO_LOG(Log::System, "dealloc VK");
}

- (void)loadView {
	INFO_LOG(Log::G3D, "Creating metal view");

	CGRect screenRect = [[UIScreen mainScreen] bounds];
	CGFloat screenWidth = screenRect.size.width;
	CGFloat screenHeight = screenRect.size.height;

	PPSSPPMetalView *metalView = [[PPSSPPMetalView alloc] initWithFrame:CGRectMake(0, 0, screenWidth,screenHeight)];
	self.view = metalView;
}

- (void)viewDidLoad {
	[super viewDidLoad];

	INFO_LOG(Log::System, "Metal viewDidLoad");

	graphicsContext = new IOSVulkanContext();

	[[DisplayManager shared] updateResolution:[UIScreen mainScreen]];

	if (!graphicsContext->InitAPI()) {
		_assert_msg_(false, "Failed to init Vulkan");
	}

	INFO_LOG(Log::G3D, "Detected size: %dx%d", g_display.pixel_xres, g_display.pixel_yres);
	INFO_LOG(Log::System, "Done with metal viewDidLoad");
}

- (UIView *)getView {
	return [self view];
}

- (void)viewWillAppear:(BOOL)animated {
	[super viewWillAppear:animated];
	INFO_LOG(Log::G3D, "viewWillAppear");
	self.view.contentScaleFactor = UIScreen.mainScreen.nativeScale;
}

- (void)viewWillDisappear:(BOOL)animated {
	[super viewWillDisappear:animated];
	INFO_LOG(Log::G3D, "viewWillDisappear");
}

- (void)viewDidDisappear:(BOOL)animated {
	[super viewDidDisappear: animated];
	INFO_LOG(Log::G3D, "viewWillDisappear");
}

- (void)bindDefaultFBO
{
	// Do nothing
}

- (void)viewWillTransitionToSize:(CGSize)size
		withTransitionCoordinator:(id<UIViewControllerTransitionCoordinator>)coordinator {
	[super viewWillTransitionToSize:size withTransitionCoordinator:coordinator];

	[self.view endEditing:YES]; // clears any input focus

	[coordinator animateAlongsideTransition:^(id<UIViewControllerTransitionCoordinatorContext> context) {
		NSLog(@"Rotating to size: %@", NSStringFromCGSize(size));
	} completion:^(id<UIViewControllerTransitionCoordinatorContext> context) {
		NSLog(@"Rotation finished");
		// Reinitialize graphics context to match new size
		[self requestExitVulkanRenderLoop];
		[self runVulkanRenderLoop];
		[[DisplayManager shared] updateResolution:[UIScreen mainScreen]];
	}];
}

@end

@implementation PPSSPPMetalView

/** Returns a Metal-compatible layer. */
+(Class) layerClass { return [CAMetalLayer class]; }

- (void)dealloc {
	[self stopDisplayLink];
}

#pragma mark - Display Link

- (void)startDisplayLink {
	NSLog(@"Starting display link");
	self.displayLink = [CADisplayLink displayLinkWithTarget:self selector:@selector(onVSync:)];
	self.displayLink.preferredFramesPerSecond = 60;   // or 0 for native refresh rate
	[self.displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];
}

- (void)stopDisplayLink {
	NSLog(@"Stopping display link");
	[self.displayLink invalidate];
	self.displayLink = nil;
}

- (void)onVSync:(CADisplayLink *)dl {
	static uint64_t presentId = 0;
	presentId++;

	NSTimeInterval timestamp = dl.timestamp;
	NSTimeInterval targetTimestamp = dl.targetTimestamp;

	NativeVSync(presentId, from_mach_time_interval(timestamp), from_mach_time_interval(targetTimestamp));
}

@end
