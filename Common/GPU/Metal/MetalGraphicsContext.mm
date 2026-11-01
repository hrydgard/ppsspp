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

#include "ppsspp_config.h"

#include "Common/GPU/Metal/MetalGraphicsContext.h"
#include "Common/GPU/Metal/MetalRenderManager.h"
#include "Common/GPU/Vulkan/VulkanLoader.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "Common/GPU/thin3d_create.h"
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"

// The drawable size the layer should have, in pixels.
static void LayerPixelSize(CAMetalLayer *layer, int *width, int *height) {
	CGSize size = layer.bounds.size;
	CGFloat scale = layer.contentsScale;
	*width = (int)(size.width * scale);
	*height = (int)(size.height * scale);
}

MetalGraphicsContext::~MetalGraphicsContext() {
	_dbg_assert_(!draw_ && !renderManager_ && !device_);
}

bool MetalGraphicsContext::InitAPI(void *wnd, std::string *deviceName, std::string *errorMessage) {
	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	if (!device) {
		*errorMessage = "No Metal device";
		return false;
	}
	device_ = (__bridge_retained void *)device;
	// The shaders go through glslang.
	init_glslang();
	return true;
}

bool MetalGraphicsContext::InitSurface(WindowSystem winsys, void *data1, void *data2, std::string *errorMessage) {
	id<MTLDevice> device = (__bridge id<MTLDevice>)device_;
	CAMetalLayer *layer = nil;
	if (offscreenWidth_ == 0) {
		layer = (__bridge CAMetalLayer *)data1;
		if (!layer) {
			*errorMessage = "No CAMetalLayer";
			return false;
		}
		layer_ = data1;
	}
	// Like Vulkan: encoding on a thread of its own, unless that's turned off or there's only one frame in flight.
	const bool useThread = g_Config.bRenderMultiThreading && g_Config.iInflightFrames != 1;
	renderManager_ = new MetalRenderManager(device, layer, offscreenWidth_, offscreenHeight_, useThread);
	renderManager_->SetInflightFrames(g_Config.iInflightFrames);
	draw_ = Draw::T3DCreateMetalContext(renderManager_);
	SetGPUBackend(GPUBackend::METAL, [[device name] UTF8String]);
	if (!draw_->CreatePresets()) {
		*errorMessage = "Failed to compile preset shaders";
		ShutdownSurface();
		return false;
	}
	int width = renderManager_->BackbufferWidth();
	int height = renderManager_->BackbufferHeight();
	if (layer) {
		LayerPixelSize(layer, &width, &height);
	}
	INFO_LOG(Log::G3D, "Metal: Using %s, backbuffer %dx%d%s, %s", [[device name] UTF8String], width, height, layer ? "" : " (offscreen)", useThread ? "render thread" : "no render thread");
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, width, height);
	return true;
}

void MetalGraphicsContext::ShutdownSurface() {
	if (draw_) {
		draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, 0, 0);
	}
	delete draw_;
	draw_ = nullptr;
	delete renderManager_;
	renderManager_ = nullptr;
	layer_ = nullptr;
}

void MetalGraphicsContext::ShutdownAPI() {
	if (device_) {
		id<MTLDevice> device = (__bridge_transfer id<MTLDevice>)device_;
		device = nil;
		device_ = nullptr;
	}
	finalize_glslang();
}

void MetalGraphicsContext::Resize() {
	if (offscreenWidth_ > 0 || !layer_) {
		return;
	}
	int width, height;
	LayerPixelSize((__bridge CAMetalLayer *)layer_, &width, &height);
	draw_->HandleEvent(Draw::Event::LOST_BACKBUFFER, renderManager_->BackbufferWidth(), renderManager_->BackbufferHeight());
	draw_->HandleEvent(Draw::Event::GOT_BACKBUFFER, width, height);
}
