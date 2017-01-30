// Copyright (c) 2015- PPSSPP Project.

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

#include <cstdint>

enum ShaderLanguage {
	GLSL_140,
	GLSL_300,
	GLSL_VULKAN,
	HLSL_DX9,
	HLSL_D3D11,
	HLSL_D3D11_LEVEL9,
};

enum DebugShaderType {
	SHADER_TYPE_VERTEX = 0,
	SHADER_TYPE_FRAGMENT = 1,
	SHADER_TYPE_GEOMETRY = 2,
	SHADER_TYPE_VERTEXLOADER = 3,  // Not really a shader, but might as well re-use this mechanism
	SHADER_TYPE_PIPELINE = 4,  // Vulkan and DX12 combines a bunch of state into pipeline objects. Might as well make them inspectable.
};

enum DebugShaderStringType {
	SHADER_STRING_SHORT_DESC = 0,
	SHADER_STRING_SOURCE_CODE = 1,
	SHADER_STRING_STATS = 2,
};

// Shared between the backends. Not all are necessarily used by each backend, but this lets us share
// more code than before.
enum : uint64_t {
	DIRTY_PROJMATRIX = 1ULL << 0,
	DIRTY_PROJTHROUGHMATRIX = 1ULL << 1,
	DIRTY_FOGCOLOR = 1ULL << 2,
	DIRTY_FOGCOEF = 1ULL << 3,
	DIRTY_TEXENV = 1ULL << 4,
	DIRTY_ALPHACOLORREF = 1ULL << 5,

	// 1 << 6 is free! Wait, not anymore...
	DIRTY_STENCILREPLACEVALUE = 1ULL << 6,

	DIRTY_ALPHACOLORMASK = 1ULL << 7,
	DIRTY_LIGHT0 = 1ULL << 8,
	DIRTY_LIGHT1 = 1ULL << 9,
	DIRTY_LIGHT2 = 1ULL << 10,
	DIRTY_LIGHT3 = 1ULL << 11,

	DIRTY_MATDIFFUSE = 1ULL << 12,
	DIRTY_MATSPECULAR = 1ULL << 13,
	DIRTY_MATEMISSIVE = 1ULL << 14,
	DIRTY_AMBIENT = 1ULL << 15,
	DIRTY_MATAMBIENTALPHA = 1ULL << 16,

	DIRTY_SHADERBLEND = 1ULL << 17,  // Used only for in-shader blending.

	DIRTY_UVSCALEOFFSET = 1ULL << 18,

	// Texclamp is fairly rare so let's share it's bit with DIRTY_DEPTHRANGE.
	DIRTY_TEXCLAMP = 1ULL << 19,
	DIRTY_DEPTHRANGE = 1ULL << 19,

	DIRTY_WORLDMATRIX = 1ULL << 21,
	DIRTY_VIEWMATRIX = 1ULL << 22,
	DIRTY_TEXMATRIX = 1ULL << 23,
	DIRTY_BONEMATRIX0 = 1ULL << 24,
	DIRTY_BONEMATRIX1 = 1ULL << 25,
	DIRTY_BONEMATRIX2 = 1ULL << 26,
	DIRTY_BONEMATRIX3 = 1ULL << 27,
	DIRTY_BONEMATRIX4 = 1ULL << 28,
	DIRTY_BONEMATRIX5 = 1ULL << 29,
	DIRTY_BONEMATRIX6 = 1ULL << 30,
	DIRTY_BONEMATRIX7 = 1ULL << 31,

	// These are for hardware tessellation
	DIRTY_BEZIERCOUNTU = 1ULL << 32,
	DIRTY_SPLINECOUNTU = 1ULL << 33,
	DIRTY_SPLINECOUNTV = 1ULL << 34,
	DIRTY_SPLINETYPEU = 1ULL << 35,
	DIRTY_SPLINETYPEV = 1ULL << 36,

	DIRTY_BONE_UNIFORMS = 0xFF000000ULL,

	DIRTY_ALL_UNIFORMS = 0x1FFFFFFFFFULL,

	// Other dirty elements that aren't uniforms!
	DIRTY_FRAMEBUF = 1ULL << 40,
	DIRTY_TEXTURE_IMAGE = 1ULL << 41,
	DIRTY_TEXTURE_PARAMS = 1ULL << 42,

	// Now we can add further dirty flags that are not uniforms.
	DIRTY_BLEND_STATE = 1ULL << 43,
	DIRTY_DEPTHSTENCIL_STATE = 1ULL << 44,

	DIRTY_ALL = 0xFFFFFFFFFFFFFFFF
};

class ShaderManagerCommon {
public:
	ShaderManagerCommon() {}
	virtual ~ShaderManagerCommon() {}

	virtual void DirtyLastShader() = 0;
};

struct TBuiltInResource;
void init_resources(TBuiltInResource &Resources);
