// Copyright (c) 2012- PPSSPP Project.

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

class GPUCommon;
class GraphicsContext;

// A GPU that runs display lists (flow control, bounding boxes, matrices, block transfers, the vertex and
// index pointers moving past each draw, and the GE timing) but draws nothing, and needs no thin3d context.
// For measuring the CPU emulation without graphics work in the way (headless --graphics=null).
GPUCommon *CreateNullGPU(GraphicsContext *gfxCtx);
