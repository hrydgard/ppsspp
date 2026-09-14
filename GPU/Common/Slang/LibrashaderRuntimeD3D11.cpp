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

#include "ppsspp_config.h"
#include "GPU/Common/Slang/LibrashaderRuntime.h"
#if USE_LIBRASHADER && PPSSPP_PLATFORM(WINDOWS)

#include "Common/Log.h"
#include "Common/GPU/thin3d.h"

// Direct3D 11 half of the librashader integration. D3D11 has no render thread: the native callback
// runs synchronously on the emu thread with thin3d's immediate context, so every libra_d3d11_* call
// - including the free - happens on that one thread. librashader takes the source as a shader
// resource view and the destination as a render target view; thin3d unbinds both around the callback
// and re-applies its cached state afterwards (see D3D11DrawContext::RunNativeCallback).
class LibrashaderRuntimeD3D11 : public LibrashaderRuntime {
public:
	LIBRA_PRESET_CTX_RUNTIME PresetRuntime() const override { return LIBRA_PRESET_CTX_RUNTIME_D3D11; }

	bool Init(Draw::DrawContext *draw, std::string *error) override {
		device_ = draw ? (ID3D11Device *)(uintptr_t)draw->GetNativeObject(Draw::NativeObject::DEVICE) : nullptr;
		if (!device_) {
			if (error) *error = "D3D11 backend exposes no device";
			return false;
		}
		return true;
	}

	// libra_d3d11_filter_chain_frame takes a bare ID3D11ShaderResourceView, so there is no width/height
	// to declare: librashader always reads the size off the view's resource. Size-dependent presets
	// therefore need a genuinely native-sized input from the core (spec §12, §13 Q4).
	bool RequiresNativeSizedInput() const override { return true; }

	Draw::NativeCallbackFn MakeFrameCallback(std::shared_ptr<LibrashaderRenderState> rs, LibrashaderFrameArgs args) override {
		ID3D11Device *device = device_;
		return [rs, device, args](const Draw::NativeCallbackInfo &info) {
			if (!Librashader::IsLoaded() || rs->failed.load())
				return;
			const libra_instance_t &lib = Librashader::Instance();
			if (!rs->d3d11Chain) {
				if (!rs->preset)
					return;  // nothing left to create from
				filter_chain_d3d11_opt_t opts{};
				opts.version = LIBRASHADER_CURRENT_VERSION;
				opts.force_no_mipmaps = false;
				opts.disable_cache = false;
				std::string err = Librashader::ErrorToString(lib.d3d11_filter_chain_create(&rs->preset, device, &opts, &rs->d3d11Chain));
				// Consumed either way, exactly like the GL and Vulkan adapters: drop our handle
				// without freeing it, librashader owns it from here on.
				rs->preset = nullptr;
				if (!err.empty() || !rs->d3d11Chain) {
					std::lock_guard<std::mutex> guard(rs->errorLock);
					rs->lastError = "create: " + (err.empty() ? std::string("unknown error") : err);
					rs->failed.store(true);
					return;
				}
				// The non-deferred create records nothing into a command list, so the chain is
				// usable immediately: create and render in the same callback, like GL.
				rs->ready.store(true);
			}
			for (const auto &kv : args.overrides) {
				libra_error_t e = lib.d3d11_filter_chain_set_param(&rs->d3d11Chain, kv.first.c_str(), kv.second);
				// Unknown parameter names are not fatal; convert (which frees) and drop the error.
				if (e) (void)Librashader::ErrorToString(e);
			}
			// librashader reads the input's pixel size off the view's resource, so unlike GL/Vulkan
			// there is nowhere to declare the native PSP size - see spec §12. Instead Run() hands us a
			// genuinely native-sized image whenever the preset samples OriginalHistoryN or its result
			// depends on the input size (RequiresNativeSizedInput above); other presets get the
			// upscaled fbo, which they sample with 0..1 UVs like on every other backend.
			libra_viewport_t vp{ 0.0f, 0.0f, (uint32_t)info.dstWidth, (uint32_t)info.dstHeight };
			frame_d3d11_opt_t fopts{};
			fopts.version = LIBRASHADER_CURRENT_VERSION;
			fopts.clear_history = false;
			fopts.frame_direction = 1;
			fopts.rotation = 0;
			fopts.total_subframes = 1;
			fopts.current_subframe = 1;
			fopts.aspect_ratio = 0.0f;   // 0 infers the ratio from the source image
			fopts.frames_per_second = 60.0f;
			fopts.frametime_delta = 16;  // librashader.h: milliseconds, not microseconds
			fopts.color_space = LIBRA_COLOR_SPACE_SDR;
			std::string err = Librashader::ErrorToString(lib.d3d11_filter_chain_frame(
				&rs->d3d11Chain, (ID3D11DeviceContext *)(uintptr_t)info.cmdBuffer, (size_t)args.frameCount,
				(ID3D11ShaderResourceView *)(uintptr_t)info.srcView, (ID3D11RenderTargetView *)(uintptr_t)info.dstView,
				&vp, nullptr, &fopts));
			if (!err.empty()) {
				std::lock_guard<std::mutex> guard(rs->errorLock);
				rs->lastError = "frame: " + err;
				rs->failed.store(true);  // stop rendering through a broken chain
			}
		};
	}

	void QueueFree(Draw::DrawContext *draw, std::shared_ptr<LibrashaderRenderState> rs, bool deviceLost) override {
		// Nothing to defer to: D3D11 devices are free-threaded, the callbacks ran on this very
		// thread and have already completed, and there is no deletion queue whose drain we would
		// have to wait for. So free right here, even when the device is going away (spec §8).
		if (!Librashader::IsLoaded())
			return;
		const libra_instance_t &lib = Librashader::Instance();
		if (rs->d3d11Chain) {
			(void)Librashader::ErrorToString(lib.d3d11_filter_chain_free(&rs->d3d11Chain));
			rs->d3d11Chain = nullptr;
		}
		// Non-null only if the preset was parsed but the chain was never created.
		if (rs->preset) {
			(void)Librashader::ErrorToString(lib.preset_free(&rs->preset));
			rs->preset = nullptr;
		}
	}

private:
	// Owned by thin3d; the chain keeps its own reference for as long as it lives.
	ID3D11Device *device_ = nullptr;
};

std::unique_ptr<LibrashaderRuntime> CreateLibrashaderRuntimeD3D11() {
	return std::make_unique<LibrashaderRuntimeD3D11>();
}

#endif  // USE_LIBRASHADER && PPSSPP_PLATFORM(WINDOWS)
