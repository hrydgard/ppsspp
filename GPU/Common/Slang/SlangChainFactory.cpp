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
#include "GPU/Common/Slang/ISlangFilterChain.h"
#include "Core/ConfigValues.h"
#if USE_LIBRASHADER
#include "GPU/Common/Slang/LibrashaderFilterChain.h"
#endif

const char *SlangChainBackendName(SlangChainBackend backend) {
	switch (backend) {
	case SlangChainBackend::Librashader: return "librashader";
	default: return "none";
	}
}

SlangChainBackend ChooseSlangChainBackend(bool librashaderLoaded, GPUBackend gpuBackend,
                                          bool drawSupportsNativeCallback) {
	if (!librashaderLoaded || !drawSupportsNativeCallback)
		return SlangChainBackend::None;
	// librashader has a runtime for each of these three, which is every backend PPSSPP still has.
	// Kept explicit so a backend added later has to opt in rather than silently get a null runtime.
	if (gpuBackend != GPUBackend::VULKAN && gpuBackend != GPUBackend::OPENGL && gpuBackend != GPUBackend::DIRECT3D11)
		return SlangChainBackend::None;
	return SlangChainBackend::Librashader;
}

ISlangFilterChain *CreateSlangFilterChain(Draw::DrawContext *draw, SlangChainBackend backend) {
#if USE_LIBRASHADER
	if (backend == SlangChainBackend::Librashader)
		return new LibrashaderFilterChain(draw);
#endif
	return nullptr;
}
