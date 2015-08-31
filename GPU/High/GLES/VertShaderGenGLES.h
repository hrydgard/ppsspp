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

#include "Globals.h"
#include "GPU/High/Command.h"

// #define USE_BONE_ARRAY
//

namespace HighGpu {

struct ShaderID;

bool CanUseHardwareTransform(int prim, bool isModeThrough);

void ComputeVertexShaderID(ShaderID *id_out, u32 enabled, u32 vertType,
		const HighGpu::RasterState *raster, const HighGpu::TexScaleState *ts,
		const HighGpu::LightGlobalState *lgs, const HighGpu::LightState **ls,
		bool flipTexture, bool useHWTransform);

void GenerateVertexShader(ShaderID id, char *buffer);

}  // namespace HighGpu
