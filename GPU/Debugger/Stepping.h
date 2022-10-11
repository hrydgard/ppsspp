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
#include "GPU/GPUState.h"

namespace GPUStepping {
	// Should be called from the emu thread.
	// Begins stepping and increments the stepping counter while inside a lock.
	bool EnterStepping();
	bool SingleStep();
	bool IsStepping();
	int GetSteppingCounter();

	bool GPU_GetOutputFramebuffer(const GPUDebugBuffer *&buffer);
	bool GPU_GetCurrentFramebuffer(const GPUDebugBuffer *&buffer, GPUDebugFramebufferType type);
	bool GPU_GetCurrentDepthbuffer(const GPUDebugBuffer *&buffer);
	bool GPU_GetCurrentStencilbuffer(const GPUDebugBuffer *&buffer);
	bool GPU_GetCurrentTexture(const GPUDebugBuffer *&buffer, int level, bool *isFramebuffer);
	bool GPU_GetCurrentClut(const GPUDebugBuffer *&buffer);
	bool GPU_SetCmdValue(u32 op);
	bool GPU_FlushDrawing();

	void ResumeFromStepping();
	void ForceUnpause();

	GPUgstate LastState();
};
