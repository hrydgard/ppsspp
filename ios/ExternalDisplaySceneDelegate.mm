// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#import "ExternalDisplaySceneDelegate.h"

#import <QuartzCore/CAMetalLayer.h>

#include "Common/GPU/thin3d.h"
#include "Common/Log.h"
#include "Common/System/NativeApp.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"

static NSString *const PPSSPPDidLaunchNotification = @"PPSSPPDidLaunch";

// Set by the SceneDelegate. Before that, the config (and so the graphics backend) isn't known.
static bool g_launched = false;

@interface ExternalDisplayView : UIView
@end

@implementation ExternalDisplayView
+ (Class)layerClass {
	return [CAMetalLayer class];
}
@end

// Adds the view's layer as a Game output once it has a size, and keeps the output's size up to date.
@interface ExternalDisplayViewController : UIViewController
@property (nonatomic) int outputId;
@end

@implementation ExternalDisplayViewController {
	int width_;
	int height_;
}

- (void)loadView {
	self.view = [[ExternalDisplayView alloc] init];
	self.view.backgroundColor = UIColor.blackColor;
}

- (void)viewDidLayoutSubviews {
	[super viewDidLayoutSubviews];
	UIScreen *screen = self.view.window.windowScene.screen;
	const CGSize size = self.view.bounds.size;
	const CGFloat scale = screen ? screen.nativeScale : 1.0;
	self.view.contentScaleFactor = scale;
	const int width = (int)(size.width * scale);
	const int height = (int)(size.height * scale);
	if (width <= 0 || height <= 0 || (width == width_ && height == height_)) {
		return;
	}
	width_ = width;
	height_ = height;
	if (self.outputId) {
		Native_ResizeOutput(self.outputId, width, height);
		return;
	}
	Draw::SwapchainDesc desc{};
	desc.winsys = WINDOWSYSTEM_METAL_EXT;
	desc.data1 = (__bridge void *)self.view.layer;
	desc.width = width;
	desc.height = height;
	desc.tag = "ExternalDisplay";
	self.outputId = Native_AddOutput(OutputRole::Game, desc);
	INFO_LOG(Log::G3D, "External display: %dx%d, output %d", width, height, self.outputId);
}

@end

@interface ExternalDisplaySceneDelegate ()
@property (weak, nonatomic) UIWindowScene *windowScene;
@end

@implementation ExternalDisplaySceneDelegate

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
	if (![scene isKindOfClass:[UIWindowScene class]]) {
		return;
	}
	self.windowScene = (UIWindowScene *)scene;
	INFO_LOG(Log::G3D, "External display connected");
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(didLaunch:) name:PPSSPPDidLaunchNotification object:nil];
	if (g_launched) {
		[self attach];
	}
}

- (void)didLaunch:(NSNotification *)notification {
	// After a restart, the outputs are gone along with the old screen manager, and the backend may have changed.
	[self detach];
	[self attach];
}

- (void)attach {
	if (!self.windowScene || self.window) {
		return;
	}
	// Without a window, iOS mirrors the phone.
	if (g_Config.iGPUBackend != (int)GPUBackend::METAL && g_Config.iGPUBackend != (int)GPUBackend::VULKAN) {
		INFO_LOG(Log::G3D, "External display: mirroring, as the backend can't present to it");
		return;
	}
#if !TARGET_OS_TV
	for (UIScreenMode *mode in self.windowScene.screen.availableModes) {
		INFO_LOG(Log::G3D, "External display mode: %dx%d%s", (int)mode.size.width, (int)mode.size.height, mode == self.windowScene.screen.currentMode ? " (current)" : "");
	}
#endif
	self.window = [[UIWindow alloc] initWithWindowScene:self.windowScene];
	self.window.rootViewController = [[ExternalDisplayViewController alloc] init];
	self.window.hidden = NO;
}

- (void)detach {
	if (!self.window) {
		return;
	}
	ExternalDisplayViewController *viewController = (ExternalDisplayViewController *)self.window.rootViewController;
	if (viewController.outputId) {
		// The swapchain keeps the layer alive until the removal has gone through.
		Native_RemoveOutput(viewController.outputId);
	}
	self.window = nil;
}

- (void)sceneDidDisconnect:(UIScene *)scene {
	INFO_LOG(Log::G3D, "External display disconnected");
	[[NSNotificationCenter defaultCenter] removeObserver:self];
	[self detach];
	self.windowScene = nil;
}

@end

void ExternalDisplayNotifyLaunched() {
	g_launched = true;
	[[NSNotificationCenter defaultCenter] postNotificationName:PPSSPPDidLaunchNotification object:nil];
}
