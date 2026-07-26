#import "AppDelegate.h"
#import "ViewControllerMetal.h"
#import "iOSCoreAudio.h"

#include "Common/Log.h"

#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/Vulkan/VulkanRenderManager.h"
#include "Common/GPU/Vulkan/VulkanGraphicsContext.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/Data/Text/Parsers.h"
#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/System/NativeApp.h"
#include "Common/System/Request.h"
#include "Common/GPU/GraphicsContext.h"
#include "Common/Thread/ThreadUtil.h"
#include "Common/StringUtils.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

#include "GPU/Vulkan/VulkanUtil.h"

// ViewController lifecycle:
// https://www.progressconcepts.com/blog/ios-appdelegate-viewcontroller-method-order/

#pragma mark -
#pragma mark PPSSPPViewControllerMetal

static std::atomic<bool> exitRenderLoop;
static std::atomic<bool> renderLoopRunning;
static std::thread g_renderLoopThread;

@interface PPSSPPViewControllerMetal () {
	GraphicsContext *graphicsContext;
}

@end  // @interface

@implementation PPSSPPViewControllerMetal {}

- (id)init {
	self = [super init];
	return self;
}

// Should be very similar to the Android one, probably mergeable.
// This is the EmuThread for iOS.
static void VulkanRenderLoop(GraphicsContext *graphicsContext, CAMetalLayer *metalLayer) {
	SetCurrentThreadName("EmuThreadVulkan");
	INFO_LOG(Log::G3D, "Entering EmuThreadVulkan");
	_assert_(graphicsContext);

	if (exitRenderLoop) {
		WARN_LOG(Log::G3D, "runVulkanRenderLoop: ExitRenderLoop requested at start, skipping the whole thing.");
		renderLoopRunning = false;
		exitRenderLoop = false;
		return;
	}

	// This is up here to prevent race conditions, in case we pause during init.
	renderLoopRunning = true;

	//WARN_LOG(G3D, "runVulkanRenderLoop. desiredBackbufferSizeX=%d desiredBackbufferSizeY=%d",
	//	desiredBackbufferSizeX, desiredBackbufferSizeY);
	std::string errorMessage;
	if (!graphicsContext->InitSurface(WINDOWSYSTEM_METAL_EXT, (__bridge void *)metalLayer, nullptr, &errorMessage)) {
		// On Android, if we get here, really no point in continuing.
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

	NativeShutdownGraphics(graphicsContext);

	graphicsContext->ThreadEnd();

	// Shut the graphics context down to the same state it was in when we entered the render thread.
	INFO_LOG(Log::G3D, "Shutting down graphics context...");
	graphicsContext->ShutdownSurface();
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
	return true;
}

- (void)requestExitVulkanRenderLoop {
	INFO_LOG(Log::G3D, "requestExitVulkanRenderLoop");

	if (!renderLoopRunning) {
		ERROR_LOG(Log::System, "Render loop already exited");
		return;
	}
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
	[self updateResolutionWithView:self.view];
	[self runVulkanRenderLoop];
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

	// Hopefully requestExitVulkanRenderLoop has been called from willResignActive here...

	if (graphicsContext) {
		graphicsContext->ShutdownAPI();
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
	// The view gets auto-resized later.
	PPSSPPMetalView *metalView = [[PPSSPPMetalView alloc] initWithFrame:CGRectMake(0, 0, 0, 0)];
	self.view = metalView;
}

- (void)viewDidLoad {
	[super viewDidLoad];
	[self hideKeyboard];

	INFO_LOG(Log::System, "Metal viewDidLoad");

	UIScreen* screen = [(AppDelegate*)[UIApplication sharedApplication].delegate screen];
	self.view.frame = [screen bounds];
	self.view.multipleTouchEnabled = YES;
	// self.view.insetsLayoutMarginsFromSafeArea = NO;
	// self.view.clipsToBounds = YES;

	graphicsContext = new VulkanGraphicsContext();
	std::string errorMessage;
	if (!graphicsContext->InitAPI(nullptr, &g_Config.sVulkanDevice, &errorMessage)) {
		ERROR_LOG(Log::System, "Failed to initialize Vulkan, switching to OpenGL: %s", errorMessage.c_str());
		g_Config.iGPUBackend = (int)GPUBackend::OPENGL;
		SetGPUBackend(GPUBackend::OPENGL);
		delete graphicsContext;
		// TODO: What to do here?
	}

	[self updateResolutionWithView:self.view];

	if ([[GCController controllers] count] > 0) {
		[self setupController:[[GCController controllers] firstObject]];
	}

	INFO_LOG(Log::G3D, "Detected size: %dx%d", g_display.pixel_xres, g_display.pixel_yres);
}

- (UIView *)getView {
	return [self view];
}

- (void)viewWillAppear:(BOOL)animated {
	[super viewWillAppear:animated];
	INFO_LOG(Log::G3D, "viewWillAppear");
	// This is to make sure we get 1:1 pixels.
	UIWindowScene *scene = self.view.window.windowScene;
	if (scene) {
		self.view.contentScaleFactor = scene.screen.nativeScale;
	}
	if (@available(iOS 16.0, *)) {
        [self setNeedsUpdateOfSupportedInterfaceOrientations];
    }
}

- (void)viewWillDisappear:(BOOL)animated {
	[super viewWillDisappear:animated];
	INFO_LOG(Log::G3D, "viewWillDisappear");
}

- (void)viewDidDisappear:(BOOL)animated {
	[super viewDidDisappear: animated];
	INFO_LOG(Log::G3D, "viewDidDisappear");
}

- (void)bindDefaultFBO {
	// Do nothing
}

- (void)viewWillLayoutSubviews {
	[super viewWillLayoutSubviews];

	// This is the first reliable place where self.view.bounds
	// matches the forced orientation.
	CGRect bounds = self.view.bounds;

	INFO_LOG(Log::G3D, "Correcting metal view layout: %dx%d",
			 (int)bounds.size.width, (int)bounds.size.height);

	// Update your Metal layer/viewport here if necessary
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
		[self updateResolutionWithView:self.view];
		[self runVulkanRenderLoop];
	}];
}

@end

@implementation PPSSPPMetalView

/** Returns a Metal-compatible layer. */
+(Class) layerClass { return [CAMetalLayer class]; }

@end
