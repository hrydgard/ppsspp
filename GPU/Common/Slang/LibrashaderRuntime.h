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

#include "Common/GPU/thin3d.h"
#include "Common/GPU/Librashader/LibrashaderLoader.h"
#include "Core/ConfigValues.h"

namespace Draw { class DrawContext; }

// Everything the render thread touches, so nothing in a callback points back into the
// LibrashaderFilterChain object. Held by shared_ptr, so a pending callback (or the deletion
// queue) keeps it alive even if the chain object dies first. See spec §6.5, §8.
struct LibrashaderRenderState {
	// Parsed on the emu thread in Load() while nothing else holds this state, then owned by
	// the render thread: consumed by chain creation, or freed by the deletion-queue callback.
	libra_shader_preset_t preset = nullptr;
	libra_vk_filter_chain_t vkChain = nullptr;
	libra_gl_filter_chain_t glChain = nullptr;
	// Callbacks the render thread has run since the chain was created (still 0 in the callback
	// that creates it). Render thread only; the gate in the callback explains the bound.
	int callbacksSinceCreate = 0;
	std::atomic<bool> ready{false};
	std::atomic<bool> failed{false};
	std::mutex errorLock;
	// Set on the render thread, read on the emu thread. Prefixed "create: " or "frame: " so the
	// emu-side log says which librashader call actually failed.
	std::string lastError;
};

// Per-frame inputs the callback needs, copied out of the emu-thread state before enqueuing.
struct LibrashaderFrameArgs {
	int frameCount;
	int sourceW, sourceH;
	std::map<std::string, float> overrides;
};

// The backend-specific half of the librashader integration: device/loader acquisition, the
// render-thread frame callback, and the backend's rule for when librashader handles may be freed.
class LibrashaderRuntime {
public:
	virtual ~LibrashaderRuntime() = default;
	virtual LIBRA_PRESET_CTX_RUNTIME PresetRuntime() const = 0;
	// Emu thread. Grabs the device handles / proc loader from draw.
	virtual bool Init(Draw::DrawContext *draw, std::string *error) = 0;
	// Emu thread. Returns the function RunNativeCallback executes on the render thread.
	virtual Draw::NativeCallbackFn MakeFrameCallback(std::shared_ptr<LibrashaderRenderState> rs, LibrashaderFrameArgs args) = 0;
	// Emu thread. Frees rs->preset / chain on the thread the backend requires; draw may be null (device gone).
	// deviceLost means the backend is tearing down: a queue that never drains again must not be used.
	virtual void QueueFree(Draw::DrawContext *draw, std::shared_ptr<LibrashaderRenderState> rs, bool deviceLost) = 0;
};

// Implemented by the per-backend adapter .cpp files.
std::unique_ptr<LibrashaderRuntime> CreateLibrashaderRuntimeVulkan();
std::unique_ptr<LibrashaderRuntime> CreateLibrashaderRuntimeOpenGL();

// nullptr if librashader has no runtime for this backend.
std::unique_ptr<LibrashaderRuntime> CreateLibrashaderRuntime(GPUBackend backend);

#endif  // USE_LIBRASHADER
