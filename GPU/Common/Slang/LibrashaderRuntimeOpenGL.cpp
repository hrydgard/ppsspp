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

// OpenGL half of the librashader integration. Every libra_gl_* call - including the free -
// happens inside a native callback, i.e. on the GL thread with the creating context current.
// librashader's GL API takes plain texture names, so this file needs no GL headers.
class LibrashaderRuntimeOpenGL : public LibrashaderRuntime {
public:
	LIBRA_PRESET_CTX_RUNTIME PresetRuntime() const override { return LIBRA_PRESET_CTX_RUNTIME_GL_CORE; }

	bool Init(Draw::DrawContext *draw, std::string *error) override {
		loader_ = draw ? (libra_gl_loader_t)(uintptr_t)draw->GetNativeObject(Draw::NativeObject::GL_GET_PROC_ADDRESS) : nullptr;
		if (!loader_) {
			if (error) *error = "OpenGL backend exposes no proc-address loader";
			return false;
		}
		return true;
	}

	Draw::NativeCallbackFn MakeFrameCallback(std::shared_ptr<LibrashaderRenderState> rs, LibrashaderFrameArgs args) override {
		libra_gl_loader_t loader = loader_;
		return [rs, loader, args](const Draw::NativeCallbackInfo &info) {
			if (!Librashader::IsLoaded() || rs->failed.load())
				return;
			const libra_instance_t &lib = Librashader::Instance();
			if (!rs->glChain) {
				if (!rs->preset)
					return;  // nothing left to create from
				filter_chain_gl_opt_t opts{};
				opts.version = LIBRASHADER_CURRENT_VERSION;
				opts.glsl_version = 0;      // auto-detect from the current context (330 core / 300 es ...)
				opts.use_dsa = false;       // needs GL 4.5; macOS is 4.1, GLES has none
				opts.force_no_mipmaps = false;
				opts.disable_cache = false; // librashader disables its own cache without DSA
				std::string err = Librashader::ErrorToString(lib.gl_filter_chain_create(&rs->preset, loader, &opts, &rs->glChain));
				// Consumed either way, exactly like the Vulkan deferred create: drop our handle
				// without freeing it, librashader owns it from here on.
				rs->preset = nullptr;
				if (!err.empty() || !rs->glChain) {
					std::lock_guard<std::mutex> guard(rs->errorLock);
					rs->lastError = "create: " + (err.empty() ? std::string("unknown error") : err);
					rs->failed.store(true);
					return;
				}
				// No command-buffer contract on GL: the chain is usable immediately, so no
				// callbacksSinceCreate gate - we create and render in the same callback.
				rs->ready.store(true);
			}
			for (const auto &kv : args.overrides) {
				libra_error_t e = lib.gl_filter_chain_set_param(&rs->glChain, kv.first.c_str(), kv.second);
				// Unknown parameter names are not fatal; convert (which frees) and drop the error.
				if (e) (void)Librashader::ErrorToString(e);
			}
			libra_image_gl_t in{};
			in.handle = info.srcTexture;
			in.format = info.srcFormat;
			// Declared native PSP size, same semantics as the Vulkan adapter: the upscaled fbo is
			// sampled with 0..1 UVs, and Run() hands us a genuinely native-sized image whenever the
			// preset samples OriginalHistoryN. See spec §12 and the comment in Run().
			in.width = (uint32_t)args.sourceW;
			in.height = (uint32_t)args.sourceH;
			libra_image_gl_t out{};
			out.handle = info.dstTexture;
			out.format = info.dstFormat;
			out.width = (uint32_t)info.dstWidth;
			out.height = (uint32_t)info.dstHeight;
			libra_viewport_t vp{ 0.0f, 0.0f, (uint32_t)info.dstWidth, (uint32_t)info.dstHeight };
			frame_gl_opt_t fopts{};
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
			std::string err = Librashader::ErrorToString(lib.gl_filter_chain_frame(&rs->glChain, (size_t)args.frameCount, in, out, &vp, nullptr, &fopts));
			if (!err.empty()) {
				std::lock_guard<std::mutex> guard(rs->errorLock);
				rs->lastError = "frame: " + err;
				rs->failed.store(true);  // stop rendering through a broken chain
			}
		};
	}

	void QueueFree(Draw::DrawContext *draw, std::shared_ptr<LibrashaderRenderState> rs, bool deviceLost) override {
		// deviceLost: the render thread is stopping, and GLRenderManager::ThreadEnd deletes queued
		// CALLBACK functions without running them - so enqueuing a free here would silently do
		// nothing. Drop instead, deliberately.
		if (deviceLost || !draw || !Librashader::IsLoaded()) {
			DropChain(rs);
			return;
		}
		// libra_gl_filter_chain_free requires the creating context to be current: do it on the GL
		// thread. The callback holds a reference to rs, so a still-pending frame callback (queued
		// earlier) runs first.
		bool enqueued = draw->RunNativeCallback(nullptr, nullptr, [rs](const Draw::NativeCallbackInfo &) {
			if (!Librashader::IsLoaded())
				return;
			const libra_instance_t &lib = Librashader::Instance();
			if (rs->glChain) {
				(void)Librashader::ErrorToString(lib.gl_filter_chain_free(&rs->glChain));
				rs->glChain = nullptr;
			}
			// Non-null only if the preset was parsed but the chain was never created.
			if (rs->preset) {
				(void)Librashader::ErrorToString(lib.preset_free(&rs->preset));
				rs->preset = nullptr;
			}
		}, "librashader_free");
		if (!enqueued) {
			WARN_LOG(Log::G3D, "LibrashaderRuntimeOpenGL: free callback could not be queued");
			// A preset needs no GL context, so it can be freed right here (spec §8). The chain
			// cannot, so it is dropped like on the DeviceLost path.
			if (rs->preset && Librashader::IsLoaded()) {
				(void)Librashader::ErrorToString(Librashader::Instance().preset_free(&rs->preset));
				rs->preset = nullptr;
			}
			DropChain(rs);
		}
	}

private:
	// The GL objects die with the context; the Rust-side allocation is leaked knowingly - freeing it
	// needs the (now unusable) creating context current on a render thread that still drains work.
	static void DropChain(const std::shared_ptr<LibrashaderRenderState> &rs) {
		if (rs->glChain || rs->preset)
			WARN_LOG(Log::G3D, "LibrashaderRuntimeOpenGL: dropping chain without freeing (GL objects die with the context; librashader's Rust-side allocation is knowingly leaked)");
	}

	libra_gl_loader_t loader_ = nullptr;
};

std::unique_ptr<LibrashaderRuntime> CreateLibrashaderRuntimeOpenGL() {
	return std::make_unique<LibrashaderRuntimeOpenGL>();
}

#endif  // USE_LIBRASHADER
