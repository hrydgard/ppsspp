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
#include "GPU/Common/Slang/SlangFilterChain.h"
#include "Core/ConfigValues.h"
#if 0 // Task 5
#if USE_LIBRASHADER
#include "GPU/Common/Slang/LibrashaderFilterChain.h"
#endif
#endif

const char *SlangChainBackendName(SlangChainBackend backend) {
	switch (backend) {
	case SlangChainBackend::Librashader: return "librashader";
	default: return "in-tree";
	}
}

SlangChainBackend ChooseSlangChainBackend(bool userPrefersLibrashader, bool librashaderLoaded,
                                          GPUBackend gpuBackend, bool drawSupportsNativeCallback) {
	if (!userPrefersLibrashader || !librashaderLoaded || !drawSupportsNativeCallback)
		return SlangChainBackend::InTree;
	if (gpuBackend != GPUBackend::VULKAN)
		return SlangChainBackend::InTree;
	return SlangChainBackend::Librashader;
}

ISlangFilterChain *CreateSlangFilterChain(Draw::DrawContext *draw, SlangChainBackend backend) {
#if 0 // Task 5
#if USE_LIBRASHADER
	if (backend == SlangChainBackend::Librashader)
		return new LibrashaderFilterChain(draw);
#endif
#endif
	return new SlangFilterChain(draw);
}
