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

#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderCommon.h"

namespace DX9 {

bool GenerateFragmentShaderHLSL(const FShaderID &id, char *buffer, ShaderLanguage lang = HLSL_DX9);

#define CONST_PS_TEXENV 0
#define CONST_PS_ALPHACOLORREF 1
#define CONST_PS_ALPHACOLORMASK 2
#define CONST_PS_FOGCOLOR 3
#define CONST_PS_STENCILREPLACE 4
#define CONST_PS_BLENDFIXA 5
#define CONST_PS_BLENDFIXB 6
#define CONST_PS_FBOTEXSIZE 7
#define CONST_PS_TEXCLAMP 8
#define CONST_PS_TEXCLAMPOFF 9

// For stencil upload
#define CONST_PS_STENCILVALUE 10

};
