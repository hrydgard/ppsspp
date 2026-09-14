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

#include "GPU/Common/Slang/LibrashaderRuntime.h"
#if USE_LIBRASHADER

#include "Common/Log.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/Vulkan/VulkanContext.h"

// Vulkan half of the librashader integration. All libra_vk_* calls happen on the render thread:
// the frame callback runs inside a CALLBACK step, and the frees run from the deletion queue.
class LibrashaderRuntimeVulkan : public LibrashaderRuntime {
public:
	LIBRA_PRESET_CTX_RUNTIME PresetRuntime() const override { return LIBRA_PRESET_CTX_RUNTIME_VULKAN; }

	bool Init(Draw::DrawContext *draw, std::string *error) override {
		VulkanContext *vulkan = draw ? (VulkanContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT) : nullptr;
		PFN_vkGetInstanceProcAddr getProc = draw ? (PFN_vkGetInstanceProcAddr)(uintptr_t)draw->GetNativeObject(Draw::NativeObject::VULKAN_GET_INSTANCE_PROC_ADDR) : nullptr;
		if (!vulkan || !getProc) {
			if (error) *error = "Vulkan backend exposes no context or instance proc loader";
			return false;
		}
		device_ = libra_device_vk_t{};
		device_.physical_device = vulkan->GetPhysicalDevice(vulkan->GetCurrentPhysicalDeviceIndex());
		device_.instance = vulkan->GetInstance();
		device_.device = vulkan->GetDevice();
		device_.queue = vulkan->GetGraphicsQueue();
		device_.entry = getProc;
		return true;
	}

	Draw::NativeCallbackFn MakeFrameCallback(std::shared_ptr<LibrashaderRenderState> rs, LibrashaderFrameArgs args) override {
		libra_device_vk_t device = device_;
		return [rs, device, args](const Draw::NativeCallbackInfo &info) {
			if (!Librashader::IsLoaded() || rs->failed.load())
				return;
			const libra_instance_t &lib = Librashader::Instance();
			if (!rs->vkChain) {
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
				//     libra_vk_filter_chain_frame." Honoured by the callbacksSinceCreate gate below:
				//     we wait until MAX_INFLIGHT_FRAMES further frames have been *recorded* on this
				//     thread, and PPSSPP waits on frame N's fence before recording frame
				//     N + MAX_INFLIGHT_FRAMES, so the create's frame has completed by then.
				std::string err = Librashader::ErrorToString(lib.vk_filter_chain_create_deferred(&rs->preset, device, (VkCommandBuffer)(uintptr_t)info.cmdBuffer, &opts, &rs->vkChain));
				// The preset is invalidated (consumed) whether or not creation succeeded, so drop our
				// handle without freeing it - librashader owns it from here on.
				rs->preset = nullptr;
				if (!err.empty() || !rs->vkChain) {
					std::lock_guard<std::mutex> guard(rs->errorLock);
					rs->lastError = "create: " + (err.empty() ? std::string("unknown error") : err);
					rs->failed.store(true);
					return;
				}
				return;
			}
			// One RenderState holds at most one chain, so this counts callbacks since *this* chain's
			// creation. The guarantee we need is "MAX_INFLIGHT_FRAMES frames were submitted after the
			// create was recorded", which PPSSPP's per-frame fence wait turns into "the create's frame
			// has completed on the GPU". Keyed on render-thread callbacks, not on the emu thread's flip
			// counter, which advances on skipped frames and runs ahead of the render thread.
			rs->callbacksSinceCreate++;
			if (rs->callbacksSinceCreate < VulkanContext::MAX_INFLIGHT_FRAMES)
				return;  // creation's uploads may still be executing; see (b) above
			rs->ready.store(true);
			for (const auto &kv : args.overrides) {
				libra_error_t e = lib.vk_filter_chain_set_param(&rs->vkChain, kv.first.c_str(), kv.second);
				// Unknown parameter names are not fatal; convert (which frees) and drop the error.
				if (e) (void)Librashader::ErrorToString(e);
			}
			libra_image_vk_t in{};
			in.handle = (VkImage)info.srcImage;
			in.format = (VkFormat)info.srcFormat;
			// Native PSP size, matching the in-tree chain's SourceSize/OriginalSize semantics: the
			// upscaled fbo is sampled with 0..1 UVs. Verified on device in Task 8: librashader does
			// not validate these against the real image extents, and SourceSize-driven masks match the
			// in-tree chain pixel-for-pixel in period. The one place it does treat them as the real
			// extents is its OriginalHistoryN snapshot, so Run() hands us a genuinely native-sized
			// image whenever the preset samples history - see the comment there.
			in.width = (uint32_t)args.sourceW;
			in.height = (uint32_t)args.sourceH;
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
			std::string err = Librashader::ErrorToString(lib.vk_filter_chain_frame(&rs->vkChain, (VkCommandBuffer)(uintptr_t)info.cmdBuffer, (size_t)args.frameCount, in, out, &vp, nullptr, &fopts));
			if (!err.empty()) {
				std::lock_guard<std::mutex> guard(rs->errorLock);
				rs->lastError = "frame: " + err;
				rs->failed.store(true);  // stop rendering through a broken chain
			}
		};
	}

	// deviceLost is ignored: Vulkan's deletion queue is fully drained before vkDestroyDevice, so a
	// callback queued during teardown still runs.
	void QueueFree(Draw::DrawContext *draw, std::shared_ptr<LibrashaderRenderState> rs, bool deviceLost) override {
		VulkanContext *vulkan = draw ? (VulkanContext *)draw->GetNativeObject(Draw::NativeObject::CONTEXT) : nullptr;
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
			if (rs->vkChain) {
				(void)Librashader::ErrorToString(lib.vk_filter_chain_free(&rs->vkChain));
				rs->vkChain = nullptr;
			}
			// Non-null only if the preset was parsed but the chain was never created.
			if (rs->preset) {
				(void)Librashader::ErrorToString(lib.preset_free(&rs->preset));
				rs->preset = nullptr;
			}
		});
	}

private:
	libra_device_vk_t device_{};
};

std::unique_ptr<LibrashaderRuntime> CreateLibrashaderRuntimeVulkan() {
	return std::make_unique<LibrashaderRuntimeVulkan>();
}

#endif  // USE_LIBRASHADER
