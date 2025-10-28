#import "AppDelegate.h"
#import "ViewControllerMetal.h"
#import "DisplayManager.h"
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
#include "Common/System/Request.h"
#include "Common/GraphicsContext.h"
#include "Common/Thread/ThreadUtil.h"

#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

#include "GPU/Vulkan/VulkanUtil.h"

// ViewController lifecycle:
// https://www.progressconcepts.com/blog/ios-appdelegate-viewcontroller-method-order/

enum class GraphicsContextState {
	PENDING,
	INITIALIZED,
	FAILED_INIT,
	SHUTDOWN,
};

class IOSVulkanContext : public GraphicsContext {
public:
	IOSVulkanContext() {}
	~IOSVulkanContext() {
		delete g_Vulkan;
		g_Vulkan = nullptr;
	}

	bool InitAPI();

	bool InitFromRenderThread(CAMetalLayer *layer, int desiredBackbufferSizeX, int desiredBackbufferSizeY);
	void ShutdownFromRenderThread() override;  // Inverses InitFromRenderThread.

	void Shutdown() override;
	void Resize() override {}

	void *GetAPIContext() override { return g_Vulkan; }
	Draw::DrawContext *GetDrawContext() override { return draw_; }

private:
	VulkanContext *g_Vulkan = nullptr;
	Draw::DrawContext *draw_ = nullptr;
	GraphicsContextState state_ = GraphicsContextState::PENDING;
};

bool IOSVulkanContext::InitFromRenderThread(CAMetalLayer *layer, int desiredBackbufferSizeX, int desiredBackbufferSizeY) {
	INFO_LOG(Log::G3D, "IOSVulkanContext::InitFromRenderThread: desiredwidth=%d desiredheight=%d", desiredBackbufferSizeX, desiredBackbufferSizeY);
	if (!g_Vulkan) {
		ERROR_LOG(Log::G3D, "IOSVulkanContext::InitFromRenderThread: No Vulkan context");
		return false;
	}

	VkResult res = g_Vulkan->InitSurface(WINDOWSYSTEM_METAL_EXT, (__bridge void *)layer, nullptr);
	if (res != VK_SUCCESS) {
		ERROR_LOG(Log::G3D, "g_Vulkan->InitSurface failed: '%s'", VulkanResultToString(res));
		return false;
	}

	bool useMultiThreading = g_Config.bRenderMultiThreading;
	if (g_Config.iInflightFrames == 1) {
		useMultiThreading = false;
	}

	draw_ = Draw::T3DCreateVulkanContext(g_Vulkan, useMultiThreading);

	VkPresentModeKHR presentMode = ConfigPresentModeToVulkan(draw_);

	// This MUST run on the main thread. We're taking our chances with a dispatch_sync here.
	g_Vulkan->InitSwapchain(presentMode);

	if (false) {
		delete draw_;
		ERROR_LOG(Log::G3D, "InitSwapchain failed");
		g_Vulkan->DestroySwapchain();
		g_Vulkan->DestroySurface();
		g_Vulkan->DestroyDevice();
		g_Vulkan->DestroyInstance();
		return false;
	}

	SetGPUBackend(GPUBackend::VULKAN);
	bool shaderSuccess = draw_->CreatePresets();  // Doesn't fail, we ship the compiler.
	_assert_msg_(shaderSuccess, "Failed to compile preset shaders");
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());

	VulkanRenderManager *renderManager = (VulkanRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);
	renderManager->SetInflightFrames(g_Config.iInflightFrames);
	return true;
}

void IOSVulkanContext::ShutdownFromRenderThread() {
	INFO_LOG(Log::G3D, "IOSVulkanContext::Shutdown");
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, g_Vulkan->GetBackbufferWidth(), g_Vulkan->GetBackbufferHeight());
	delete draw_;
	draw_ = nullptr;
	g_Vulkan->WaitUntilQueueIdle();
	g_Vulkan->PerformPendingDeletes();
	g_Vulkan->DestroySwapchain();
	g_Vulkan->DestroySurface();
	INFO_LOG(Log::G3D, "Done with ShutdownFromRenderThread");
}

void IOSVulkanContext::Shutdown() {
	INFO_LOG(Log::G3D, "Calling NativeShutdownGraphics");
	g_Vulkan->DestroyDevice();
	g_Vulkan->DestroyInstance();
	// We keep the g_Vulkan context around to avoid invalidating a ton of pointers around the app.
	finalize_glslang();
	INFO_LOG(Log::G3D, "IOSVulkanContext::Shutdown completed");
}

bool IOSVulkanContext::InitAPI() {
	INFO_LOG(Log::G3D, "IOSVulkanContext::Init");
	init_glslang();

	g_LogOptions.breakOnError = true;
	g_LogOptions.breakOnWarning = true;
	g_LogOptions.msgBoxOnError = false;

	INFO_LOG(Log::G3D, "Creating Vulkan context");
	Version gitVer(PPSSPP_GIT_VERSION);

	std::string errorStr;
	if (!VulkanLoad(&errorStr)) {
		ERROR_LOG(Log::G3D, "Failed to load Vulkan driver library: %s", errorStr.c_str());
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	if (!g_Vulkan) {
		// TODO: Assert if g_Vulkan already exists here?
		g_Vulkan = new VulkanContext();
	}

	VulkanContext::CreateInfo info{};
	InitVulkanCreateInfoFromConfig(&info);
	if (!g_Vulkan->CreateInstanceAndDevice(info)) {
		delete g_Vulkan;
		g_Vulkan = nullptr;
		state_ = GraphicsContextState::FAILED_INIT;
		return false;
	}

	g_Vulkan->SetCbGetDrawSize([]() {
		return VkExtent2D {(uint32_t)g_display.pixel_xres, (uint32_t)g_display.pixel_yres};
	});

	INFO_LOG(Log::G3D, "Vulkan device created!");
	state_ = GraphicsContextState::INITIALIZED;
	return true;
}


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

	//WARN_LOG(G3D, "runVulkanRenderLoop. desiredBackbufferSizeX=%d desiredBackbufferSizeY=%d",
	//	desiredBackbufferSizeX, desiredBackbufferSizeY);

	if (!graphicsContext->InitFromRenderThread(metalLayer, desiredBackbufferSizeX, desiredBackbufferSizeY)) {
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
	[self hideKeyboard];

	[[DisplayManager shared] setupDisplayListener];

	INFO_LOG(Log::System, "Metal viewDidLoad");

	UIScreen* screen = [(AppDelegate*)[UIApplication sharedApplication].delegate screen];
	self.view.frame = [screen bounds];
	self.view.multipleTouchEnabled = YES;
	// self.view.insetsLayoutMarginsFromSafeArea = NO;
	// self.view.clipsToBounds = YES;

	graphicsContext = new IOSVulkanContext();

	[[DisplayManager shared] updateResolution:[UIScreen mainScreen]];

	if (!graphicsContext->InitAPI()) {
		_assert_msg_(false, "Failed to init Vulkan");
	}

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

@end
