// Copyright (c) 2026- PPSSPP Project.

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

#include "GPU/Common/Slang/LibrashaderFilterChain.h"
#if USE_LIBRASHADER

#include <algorithm>

#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/Vulkan/VulkanContext.h"

LibrashaderFilterChain::LibrashaderFilterChain(Draw::DrawContext *draw)
	: draw_(draw), render_(std::make_shared<RenderState>()) {}

LibrashaderFilterChain::~LibrashaderFilterChain() {
	ReleaseChain();
	ReleaseOutput();
}

bool LibrashaderFilterChain::Load(const Path &presetPath, std::string *error) {
	std::string loadErr;
	if (!Librashader::Load(&loadErr)) {
		if (error) *error = "librashader not available: " + loadErr;
		return false;
	}
	const libra_instance_t &lib = Librashader::Instance();

	// Frees the previous preset/chain through the deletion queue and installs a fresh, empty
	// RenderState which nothing else references yet - so we can fill it in from this thread.
	ReleaseChain();
	valid_ = false;
	loggedCreateError_ = false;
	presetPath_ = presetPath;

	libra_shader_preset_t preset = nullptr;
	libra_preset_ctx_t ctx = nullptr;
	std::string err = Librashader::ErrorToString(lib.preset_ctx_create(&ctx));
	if (err.empty()) err = Librashader::ErrorToString(lib.preset_ctx_set_core_name(&ctx, "PPSSPP"));
	if (err.empty()) err = Librashader::ErrorToString(lib.preset_ctx_set_runtime(&ctx, LIBRA_PRESET_CTX_RUNTIME_VULKAN));
	if (err.empty()) err = Librashader::ErrorToString(lib.preset_create_with_options(presetPath.c_str(), &ctx, nullptr, &preset));
	// preset_create_with_options invalidates the context and nulls it (like every other
	// consuming librashader entry point), so this only frees it if an earlier step failed.
	if (ctx) (void)Librashader::ErrorToString(lib.preset_ctx_free(&ctx));
	if (!err.empty()) {
		if (error) *error = "librashader preset load failed: " + err;
		if (preset) (void)Librashader::ErrorToString(lib.preset_free(&preset));
		return false;
	}
	// Hand the preset over to the render-thread state; it is consumed by chain creation, or
	// freed by the deletion-queue callback if the chain is never created.
	render_->preset = preset;
	valid_ = true;
	INFO_LOG(Log::G3D, "LibrashaderFilterChain: preset parsed: %s", presetPath.c_str());
	return true;
}

bool LibrashaderFilterChain::EnsureOutput(int w, int h) {
	if (output_ && outputW_ == w && outputH_ == h)
		return true;
	ReleaseOutput();
	// Plain RGBA8, no MSAA, single layer: the CALLBACK step only transitions dst->color.
	Draw::FramebufferDesc desc{};
	desc.width = w;
	desc.height = h;
	desc.depth = 1;
	desc.numLayers = 1;
	desc.multiSampleLevel = 0;
	desc.z_stencil = false;
	desc.tag = "librashader_output";
	desc.colorFormat = Draw::DataFormat::R8G8B8A8_UNORM;
	output_ = draw_->CreateFramebuffer(desc);
	if (!output_) {
		ERROR_LOG(Log::G3D, "LibrashaderFilterChain: failed to create %dx%d output framebuffer", w, h);
		return false;
	}
	outputW_ = w;
	outputH_ = h;
	return true;
}

Draw::Framebuffer *LibrashaderFilterChain::Run(Draw::Framebuffer *source, int sourceW, int sourceH,
                                               int viewportW, int viewportH, int frameCount) {
	if (!valid_ || !source)
		return nullptr;
	if (render_->createFailed.load()) {
		if (!loggedCreateError_) {
			std::lock_guard<std::mutex> guard(render_->errorLock);
			ERROR_LOG(Log::G3D, "LibrashaderFilterChain: chain creation failed: %s", render_->lastError.c_str());
			loggedCreateError_ = true;
		}
		return nullptr;
	}
	if (!EnsureOutput(std::max(1, viewportW), std::max(1, viewportH)))
		return nullptr;

	VulkanContext *vulkan = (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT);
	PFN_vkGetInstanceProcAddr getProc = (PFN_vkGetInstanceProcAddr)(uintptr_t)draw_->GetNativeObject(Draw::NativeObject::VULKAN_GET_INSTANCE_PROC_ADDR);
	if (!vulkan || !getProc)
		return nullptr;
	libra_device_vk_t device{};
	device.physical_device = vulkan->GetPhysicalDevice(vulkan->GetCurrentPhysicalDeviceIndex());
	device.instance = vulkan->GetInstance();
	device.device = vulkan->GetDevice();
	device.queue = vulkan->GetGraphicsQueue();
	device.entry = getProc;

	std::shared_ptr<RenderState> rs = render_;
	std::map<std::string, float> overrides = paramOverrides_;

	bool enqueued = draw_->RunNativeCallback(source, output_, [rs, device, overrides, frameCount, sourceW, sourceH](const Draw::NativeCallbackInfo &info) {
		if (!Librashader::IsLoaded() || rs->createFailed.load())
			return;
		const libra_instance_t &lib = Librashader::Instance();
		if (!rs->chain) {
			if (!rs->preset)
				return;  // nothing left to create from
			filter_chain_vk_opt_t opts{};
			opts.version = LIBRASHADER_CURRENT_VERSION;
			opts.frames_in_flight = VulkanContext::MAX_INFLIGHT_FRAMES;
			opts.force_no_mipmaps = false;
			opts.use_dynamic_rendering = false;
			opts.disable_cache = false;
			// Two preconditions from librashader.h (docs above libra_vk_filter_chain_create_deferred):
			// (a) "The provided command buffer must be ready for recording and contain no prior
			//     commands." Knowingly not met: PPSSPP hands us the frame's main command buffer.
			//     All of our own barriers are flushed before this call and librashader only records
			//     LUT/texture uploads into it, so sharing the buffer is benign in practice.
			// (b) "The command buffer must be completely executed before calling
			//     libra_vk_filter_chain_frame." Honoured by the readyAtFrame gate below: PPSSPP
			//     waits on frame N's fence before recording frame N + MAX_INFLIGHT_FRAMES, so by
			//     then this buffer has finished executing.
			std::string err = Librashader::ErrorToString(lib.vk_filter_chain_create_deferred(&rs->preset, device, (VkCommandBuffer)(uintptr_t)info.cmdBuffer, &opts, &rs->chain));
			// The preset is invalidated (consumed) whether or not creation succeeded, so drop our
			// handle without freeing it - librashader owns it from here on.
			rs->preset = nullptr;
			if (!err.empty() || !rs->chain) {
				std::lock_guard<std::mutex> guard(rs->errorLock);
				rs->lastError = err.empty() ? "unknown error" : err;
				rs->createFailed.store(true);
				return;
			}
			rs->readyAtFrame = (int64_t)frameCount + VulkanContext::MAX_INFLIGHT_FRAMES;
			return;
		}
		if ((int64_t)frameCount < rs->readyAtFrame)
			return;  // creation's uploads may still be executing; see (b) above
		rs->ready.store(true);
		for (const auto &kv : overrides) {
			libra_error_t e = lib.vk_filter_chain_set_param(&rs->chain, kv.first.c_str(), kv.second);
			// Unknown parameter names are not fatal; convert (which frees) and drop the error.
			if (e) (void)Librashader::ErrorToString(e);
		}
		libra_image_vk_t in{};
		in.handle = (VkImage)info.srcImage;
		in.format = (VkFormat)info.srcFormat;
		// Native PSP size, matching the in-tree chain's SourceSize/OriginalSize semantics: the
		// upscaled fbo is sampled with 0..1 UVs. See spec §12 "Native size vs image size" — if
		// librashader validates these against the real image extents, the fallback is a blit to
		// a native-sized intermediate before the callback (not implemented yet).
		in.width = (uint32_t)sourceW;
		in.height = (uint32_t)sourceH;
		libra_image_vk_t out{};
		out.handle = (VkImage)info.dstImage;
		out.format = (VkFormat)info.dstFormat;
		out.width = (uint32_t)info.dstWidth;
		out.height = (uint32_t)info.dstHeight;
		libra_viewport_t vp{ 0.0f, 0.0f, (uint32_t)info.dstWidth, (uint32_t)info.dstHeight };
		frame_vk_opt_t fopts{};
		fopts.version = LIBRASHADER_CURRENT_VERSION;
		fopts.clear_history = false;
		fopts.frame_direction = 1;
		fopts.rotation = 0;
		fopts.total_subframes = 1;
		fopts.current_subframe = 1;
		fopts.aspect_ratio = 0.0f;        // librashader.h:449 - 0 infers the ratio from the source image
		fopts.frames_per_second = 60.0f;
		fopts.frametime_delta = 16;      // librashader.h:454 - milliseconds, not microseconds
		fopts.color_space = LIBRA_COLOR_SPACE_SDR;
		std::string err = Librashader::ErrorToString(lib.vk_filter_chain_frame(&rs->chain, (VkCommandBuffer)(uintptr_t)info.cmdBuffer, (size_t)frameCount, in, out, &vp, nullptr, &fopts));
		if (!err.empty()) {
			std::lock_guard<std::mutex> guard(rs->errorLock);
			rs->lastError = err;
			rs->createFailed.store(true);  // stop rendering through a broken chain
		}
	}, "librashader");

	if (!enqueued)
		return nullptr;
	return render_->ready.load() ? output_ : nullptr;
}

void LibrashaderFilterChain::ReleaseChain() {
	if (!render_)
		return;
	// Read the Vulkan context before any caller nulls draw_ (DeviceLost).
	VulkanContext *vulkan = draw_ ? (VulkanContext *)draw_->GetNativeObject(Draw::NativeObject::CONTEXT) : nullptr;
	std::shared_ptr<RenderState> rs = render_;
	render_ = std::make_shared<RenderState>();
	if (!vulkan || !Librashader::IsLoaded()) {
		// Device is gone (or the library never loaded): nothing left to free.
		return;
	}
	// Runs after every frame that could still reference the state has completed on the GPU,
	// so a still-pending CALLBACK step is harmless: it holds a reference and runs first.
	vulkan->Delete().QueueCallback([rs](VulkanContext *) {
		if (!Librashader::IsLoaded())
			return;
		const libra_instance_t &lib = Librashader::Instance();
		if (rs->chain) {
			(void)Librashader::ErrorToString(lib.vk_filter_chain_free(&rs->chain));
			rs->chain = nullptr;
		}
		// Non-null only if the preset was parsed but the chain was never created.
		if (rs->preset) {
			(void)Librashader::ErrorToString(lib.preset_free(&rs->preset));
			rs->preset = nullptr;
		}
	});
}

void LibrashaderFilterChain::ReleaseOutput() {
	if (output_) {
		output_->Release();
		output_ = nullptr;
	}
	outputW_ = outputH_ = 0;
}

void LibrashaderFilterChain::DeviceLost() {
	ReleaseChain();
	ReleaseOutput();
	valid_ = false;
	draw_ = nullptr;
}

void LibrashaderFilterChain::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	if (!presetPath_.empty()) {
		std::string err;
		if (!Load(presetPath_, &err))
			ERROR_LOG(Log::G3D, "LibrashaderFilterChain: reload after device restore failed: %s", err.c_str());
	}
}

#endif  // USE_LIBRASHADER
