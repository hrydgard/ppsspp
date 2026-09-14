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

#pragma once

#include "ppsspp_config.h"
#if USE_LIBRASHADER

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "Common/File/Path.h"
#include "Common/GPU/Librashader/LibrashaderLoader.h"
#include "GPU/Common/Slang/ISlangFilterChain.h"

namespace Draw { class DrawContext; class Framebuffer; }

// Slang filter chain backed by the librashader shared library (Vulkan runtime).
// All public methods are emu-thread only; the librashader runtime calls happen on the
// render thread inside native callback steps. See spec §6.5, §8, §9.
class LibrashaderFilterChain : public ISlangFilterChain {
public:
	explicit LibrashaderFilterChain(Draw::DrawContext *draw);
	~LibrashaderFilterChain() override;

	bool Load(const Path &presetPath, std::string *error) override;
	bool IsValid() const override { return valid_; }
	Draw::Framebuffer *Run(Draw::Framebuffer *source, int sourceW, int sourceH,
	                       int viewportW, int viewportH, int frameCount) override;
	void SetParamOverrides(const std::map<std::string, float> &overrides) override { paramOverrides_ = overrides; }
	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;
	SlangChainBackend Backend() const override { return SlangChainBackend::Librashader; }

private:
	// Owns everything the render thread touches, so nothing in the callback points back into
	// this object. Held by shared_ptr, so a pending callback (or the deletion queue) keeps it
	// alive even if the chain object dies first.
	struct RenderState {
		// Parsed on the emu thread in Load() while nothing else holds this state, then owned by
		// the render thread: consumed by chain creation, or freed by the deletion-queue callback.
		libra_shader_preset_t preset = nullptr;
		libra_vk_filter_chain_t chain = nullptr;
		// First frame at which the deferred creation's uploads are guaranteed to have executed.
		// Render thread only.
		int64_t readyAtFrame = -1;
		std::atomic<bool> ready{false};
		std::atomic<bool> createFailed{false};
		std::mutex errorLock;
		std::string lastError;  // set on the render thread, read on the emu thread
	};

	// Queues the librashader frees on the Vulkan deletion queue and installs a fresh RenderState.
	void ReleaseChain();
	void ReleaseOutput();
	bool EnsureOutput(int w, int h);

	Draw::DrawContext *draw_ = nullptr;
	Path presetPath_;
	std::shared_ptr<RenderState> render_;
	Draw::Framebuffer *output_ = nullptr;
	int outputW_ = 0, outputH_ = 0;
	std::map<std::string, float> paramOverrides_;
	bool valid_ = false;
	bool loggedCreateError_ = false;
	bool warnedNativeSize_ = false;
};

#endif  // USE_LIBRASHADER
