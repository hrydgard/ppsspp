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
#include <map>
#include <string>
#include "Common/File/Path.h"

namespace Draw { class DrawContext; class Framebuffer; }
enum class GPUBackend;

enum class SlangChainBackend {
	InTree,       // GPU/Common/Slang/SlangFilterChain (thin3d-based)
	Librashader,  // GPU/Common/Slang/LibrashaderFilterChain (librashader shared library)
};

// Contract shared by both slang filter-chain implementations. All methods are emu-thread.
class ISlangFilterChain {
public:
	virtual ~ISlangFilterChain() = default;
	virtual bool Load(const Path &presetPath, std::string *error) = 0;
	virtual bool IsValid() const = 0;
	// Returns the framebuffer holding the filtered image, or nullptr if the caller should
	// present the unfiltered source this frame.
	virtual Draw::Framebuffer *Run(Draw::Framebuffer *source, int sourceW, int sourceH,
	                               int viewportW, int viewportH, int frameCount) = 0;
	virtual void SetParamOverrides(const std::map<std::string, float> &overrides) = 0;
	virtual void DeviceLost() = 0;
	virtual void DeviceRestore(Draw::DrawContext *draw) = 0;
	virtual SlangChainBackend Backend() const = 0;
};

const char *SlangChainBackendName(SlangChainBackend backend);

// Pure decision: librashader iff the user prefers it, the library is loaded, the GPU backend
// is one librashader supports in this phase (Vulkan), and the draw context can run native callbacks.
SlangChainBackend ChooseSlangChainBackend(bool userPrefersLibrashader, bool librashaderLoaded,
                                          GPUBackend gpuBackend, bool drawSupportsNativeCallback);

// Never returns null. Falls back to the in-tree chain if the librashader chain cannot be constructed.
ISlangFilterChain *CreateSlangFilterChain(Draw::DrawContext *draw, SlangChainBackend backend);
