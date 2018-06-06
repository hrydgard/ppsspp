// Copyright (c) 2013- PPSSPP Project.

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

#include <functional>

#include "Common/CommonTypes.h"
#include "Core/Core.h"
#include "GPU/Common/GPUDebugInterface.h"

namespace GPUStepping {
	// Should be called from the GPU thread.
	// Begins stepping and calls callback while inside a lock preparing stepping.
	// This would be a good place to deliver a message to code that stepping is ready.
	bool EnterStepping(std::function<void()> callback);
	bool IsStepping();

	bool GPU_GetCurrentFramebuffer(const GPUDebugBuffer *&buffer, GPUDebugFramebufferType type);
	bool GPU_GetCurrentDepthbuffer(const GPUDebugBuffer *&buffer);
	bool GPU_GetCurrentStencilbuffer(const GPUDebugBuffer *&buffer);
	bool GPU_GetCurrentTexture(const GPUDebugBuffer *&buffer, int level);
	bool GPU_GetCurrentClut(const GPUDebugBuffer *&buffer);
	bool GPU_SetCmdValue(u32 op);

	void ResumeFromStepping();
	void ForceUnpause(CoreLifecycle stage);
};
