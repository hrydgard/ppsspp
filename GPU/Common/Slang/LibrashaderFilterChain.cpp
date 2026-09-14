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
#include <string>

#include "Common/Log.h"
#include "Common/File/FileUtil.h"
#include "Common/File/VFS/VFS.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/Vulkan/VulkanContext.h"
#include "GPU/Common/Slang/SlangPreset.h"
#include "GPU/Common/Slang/SlangpParser.h"

// Read a slang asset: VFS first (bundled assets), then the real filesystem (custom shader dir).
// Same lookup order as the in-tree chain's private helper of the same name.
static bool ReadSlangFile(const Path &path, std::string *out) {
	size_t sz = 0;
	uint8_t *data = g_VFS.ReadFile(path.c_str(), &sz);
	if (data) {
		out->assign((const char *)data, sz);
		delete[] data;
		return true;
	}
	return File::ReadBinaryFileToString(path, out);
}

// True if the text references OriginalHistory1..9 or OriginalHistorySize1..9. Index 0 is the
// current frame, which librashader binds as a view of the real image and therefore never
// snapshots, so it does not need the native-sized input.
static bool ReferencesOriginalHistory(const std::string &src) {
	static const char *kNeedle = "OriginalHistory";
	const size_t needleLen = strlen(kNeedle);
	for (size_t pos = src.find(kNeedle); pos != std::string::npos; pos = src.find(kNeedle, pos + needleLen)) {
		size_t after = pos + needleLen;
		if (src.compare(after, 4, "Size") == 0)
			after += 4;  // OriginalHistorySizeN
		if (after < src.size() && src[after] >= '1' && src[after] <= '9')
			return true;
	}
	return false;
}

// librashader's C API exposes no way to enumerate a preset's semantics, so re-parse the preset with
// the in-tree parser and scan the (include-resolved) pass sources. On any read/parse failure we
// answer "no history" - that is the pre-existing behaviour, and librashader's own parser is the one
// that decides whether the preset loads at all.
static bool PresetUsesOriginalHistory(const Path &presetPath) {
	std::string presetText;
	if (!ReadSlangFile(presetPath, &presetText)) {
		WARN_LOG(Log::G3D, "LibrashaderFilterChain: could not re-read '%s' to scan for OriginalHistoryN", presetPath.c_str());
		return false;
	}
	SlangPreset preset;
	std::string err;
	if (!ParseSlangPreset(presetText, Path(presetPath.GetDirectory()), &preset, &err)) {
		WARN_LOG(Log::G3D, "LibrashaderFilterChain: OriginalHistoryN scan skipped, preset re-parse failed: %s", err.c_str());
		return false;
	}
	const SlangFileReader reader = [](const Path &path, std::string *out) -> bool {
		return ReadSlangFile(path, out);
	};
	for (const SlangPassDesc &pass : preset.passes) {
		std::string shaderSrc;
		if (!ReadSlangFile(Path(pass.shaderPath), &shaderSrc))
			continue;
		std::string resolved;
		if (!ResolveSlangIncludes(shaderSrc, Path(Path(pass.shaderPath).GetDirectory()), reader, &resolved, &err))
			resolved = shaderSrc;  // includes unresolved: scan what we could read
		if (ReferencesOriginalHistory(resolved))
			return true;
	}
	return false;
}

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
	loggedError_ = false;
	warnedNativeSize_ = false;
	needsNativeInput_ = false;
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
	needsNativeInput_ = PresetUsesOriginalHistory(presetPath);
	INFO_LOG(Log::G3D, "LibrashaderFilterChain: preset parsed: %s (input mode: %s)", presetPath.c_str(),
		needsNativeInput_ ? "native-sized copy, preset samples OriginalHistoryN" : "upscaled framebuffer with declared native size");
	return true;
}

bool LibrashaderFilterChain::EnsureOutput(int w, int h) {
	if (output_ && outputW_ == w && outputH_ == h)
		return true;
	if (output_) {
		output_->Release();
		output_ = nullptr;
	}
	outputW_ = outputH_ = 0;
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

bool LibrashaderFilterChain::EnsureNativeInput(int w, int h) {
	if (nativeInput_ && nativeInputW_ == w && nativeInputH_ == h)
		return true;
	if (nativeInput_) {
		nativeInput_->Release();
		nativeInput_ = nullptr;
	}
	nativeInputW_ = nativeInputH_ = 0;
	Draw::FramebufferDesc desc{};
	desc.width = w;
	desc.height = h;
	desc.depth = 1;
	desc.numLayers = 1;
	desc.multiSampleLevel = 0;
	desc.z_stencil = false;
	desc.tag = "librashader_native";
	desc.colorFormat = Draw::DataFormat::R8G8B8A8_UNORM;
	nativeInput_ = draw_->CreateFramebuffer(desc);
	if (!nativeInput_) {
		ERROR_LOG(Log::G3D, "LibrashaderFilterChain: failed to create %dx%d native input framebuffer", w, h);
		return false;
	}
	nativeInputW_ = w;
	nativeInputH_ = h;
	return true;
}

Draw::Framebuffer *LibrashaderFilterChain::Run(Draw::Framebuffer *source, int sourceW, int sourceH,
                                               int viewportW, int viewportH, int frameCount) {
	if (!valid_ || !source)
		return nullptr;
	if (render_->failed.load()) {
		if (!loggedError_) {
			std::lock_guard<std::mutex> guard(render_->errorLock);
			ERROR_LOG(Log::G3D, "LibrashaderFilterChain: disabled after librashader error (%s)", render_->lastError.c_str());
			loggedError_ = true;
		}
		return nullptr;
	}
	if (!EnsureOutput(std::max(1, viewportW), std::max(1, viewportH)))
		return nullptr;

	// Spec §12, verified in Task 8: librashader reads SourceSize/OriginalSize and the pass scale
	// base from libra_image_vk_t::width/height (good - we report the native PSP size, so
	// SourceSize-driven masks tile at the same frequency as the in-tree chain), but it also uses
	// those numbers as the copy extent when it snapshots the input into its OriginalHistoryN ring.
	// With PPSSPP's upscaled render target that snapshot would capture only the native-sized
	// top-left corner, so for presets that actually sample OriginalHistoryN (detected in Load) we
	// downscale into a native-sized intermediate first, making the declared size the true extent.
	// Presets that do not use history keep sampling the full upscaled framebuffer, which is what
	// makes them bit-identical to the in-tree chain.
	int actualW = sourceW, actualH = sourceH;
	draw_->GetFramebufferDimensions(source, &actualW, &actualH);
	Draw::Framebuffer *chainInput = source;
	if (needsNativeInput_ && (actualW != sourceW || actualH != sourceH)) {
		if (!EnsureNativeInput(sourceW, sourceH))
			return nullptr;
		draw_->BlitFramebuffer(source, 0, 0, actualW, actualH, nativeInput_, 0, 0, sourceW, sourceH,
			Draw::Aspect::COLOR_BIT, Draw::FB_BLIT_LINEAR, "librashader_native");
		chainInput = nativeInput_;
	} else if (!needsNativeInput_ && !warnedNativeSize_) {
		if (actualW != sourceW || actualH != sourceH) {
			WARN_LOG(Log::G3D, "LibrashaderFilterChain: source is %dx%d but reported as %dx%d (native); "
				"harmless here because this preset does not sample OriginalHistory1+",
				actualW, actualH, sourceW, sourceH);
		}
		warnedNativeSize_ = true;
	}

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

	bool enqueued = draw_->RunNativeCallback(chainInput, output_, [rs, device, overrides, frameCount, sourceW, sourceH](const Draw::NativeCallbackInfo &info) {
		if (!Librashader::IsLoaded() || rs->failed.load())
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
			//     libra_vk_filter_chain_frame." Honoured by the callbacksSinceCreate gate below:
			//     we wait until MAX_INFLIGHT_FRAMES further frames have been *recorded* on this
			//     thread, and PPSSPP waits on frame N's fence before recording frame
			//     N + MAX_INFLIGHT_FRAMES, so the create's frame has completed by then.
			std::string err = Librashader::ErrorToString(lib.vk_filter_chain_create_deferred(&rs->preset, device, (VkCommandBuffer)(uintptr_t)info.cmdBuffer, &opts, &rs->chain));
			// The preset is invalidated (consumed) whether or not creation succeeded, so drop our
			// handle without freeing it - librashader owns it from here on.
			rs->preset = nullptr;
			if (!err.empty() || !rs->chain) {
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
		for (const auto &kv : overrides) {
			libra_error_t e = lib.vk_filter_chain_set_param(&rs->chain, kv.first.c_str(), kv.second);
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
			rs->lastError = "frame: " + err;
			rs->failed.store(true);  // stop rendering through a broken chain
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
	if (nativeInput_) {
		nativeInput_->Release();
		nativeInput_ = nullptr;
	}
	nativeInputW_ = nativeInputH_ = 0;
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
