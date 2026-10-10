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
#include "Core/EmuThread.h"

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

	// Normally already done by willResignActive, but not when the app is terminated.
	[self requestExitVulkanRenderLoop];

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
		graphicsContext = nullptr;  // The render loop and shutdown check for this.
		// TODO: What to do here? We've switched the config over to GL, but we're still the Metal view controller,
		// so we won't render anything until the app gets restarted.
	}

	[self updateResolutionWithView:self.view];

	if ([[GCController controllers] count] > 0) {
		[self setupController:[[GCController controllers] firstObject]];
	}

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
