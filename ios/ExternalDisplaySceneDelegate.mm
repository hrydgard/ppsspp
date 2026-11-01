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
#include "Core/System.h"

static NSString *const PPSSPPExternalDisplayUpdateNotification = @"PPSSPPExternalDisplayUpdate";

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
// A reference to the layer for the output's requests, which hold it unretained. Released once it's removed.
@property (nonatomic) void *retainedLayer;
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
	self.retainedLayer = (void *)CFBridgingRetain(self.view.layer);
	desc.data1 = self.retainedLayer;
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
	[[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(update:) name:PPSSPPExternalDisplayUpdateNotification object:nil];
	if (g_launched) {
		[self attach];
	}
}

- (void)update:(NSNotification *)notification {
	// After a restart, the outputs are gone along with the old screen manager, and the backend may have changed.
	// Or the setting did.
	[self detach];
	[self attach];
}

- (void)attach {
	if (!self.windowScene || self.window) {
		return;
	}
	// Without a window, iOS mirrors the phone.
	// The running backend: the setting is already the new one while the restart prompt for a change is up.
	if (GetGPUBackend() != GPUBackend::METAL && GetGPUBackend() != GPUBackend::VULKAN) {
		INFO_LOG(Log::G3D, "External display: mirroring, as the backend can't present to it");
		return;
	}
	if (g_Config.iExternalDisplay != (int)ExternalDisplayMode::SHOW_GAME) {
		INFO_LOG(Log::G3D, "External display: mirroring, as set");
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
		// The window and its layer go now, but the add may still be queued, so our reference to the layer has to
		// stay until the removal has been handled. Waited for off the main thread, which the render thread may
		// be waiting on. If that times out, the layer is kept, as the add may still come.
		const int outputId = viewController.outputId;
		void *layer = viewController.retainedLayer;
		dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
			if (Native_RemoveOutputAndWait(outputId)) {
				CFRelease(layer);
			}
		});
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
	[[NSNotificationCenter defaultCenter] postNotificationName:PPSSPPExternalDisplayUpdateNotification object:nil];
}

void ExternalDisplaySettingChanged() {
	if (g_launched) {
		[[NSNotificationCenter defaultCenter] postNotificationName:PPSSPPExternalDisplayUpdateNotification object:nil];
	}
}
