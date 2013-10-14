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

#include "Windows/GEDebugger/GEDebugger.h"
#include "Windows/GEDebugger/SimpleGLWindow.h"
#include "GPU/GPUInterface.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/GPUState.h"

// TODO: Possibly a shader here to do the transforms?

void CGEDebugger::UpdatePrimPreview(u32 op) {
	const u32 prim_type = (op >> 16) & 0xFF;
	const u32 count = op & 0xFFFF;
	if (prim_type >= 7) {
		ERROR_LOG(COMMON, "Unsupported prim type: %x", op);
		return;
	}
	if (!gpuDebug) {
		ERROR_LOG(COMMON, "Invalid debugging environment, shutting down?", op);
		return;
	}
	if (count == 0) {
		return;
	}

	const GEPrimitiveType prim = static_cast<GEPrimitiveType>(prim_type);

	frameWindow->Begin();
	// TODO: Get the coords, render on top of the texture and framebuffer previews.
	// TODO: glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);?
	frameWindow->End();

	texWindow->Begin();
	texWindow->End();
}