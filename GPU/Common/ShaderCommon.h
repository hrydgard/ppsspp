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
#include <vector>
#include <string>

namespace Draw {
	class DrawContext;
}

enum DebugShaderType {
	SHADER_TYPE_VERTEX = 0,
	SHADER_TYPE_FRAGMENT = 1,
	SHADER_TYPE_GEOMETRY = 2,
	SHADER_TYPE_VERTEXLOADER = 3,  // Not really a shader, but might as well re-use this mechanism
	SHADER_TYPE_PIPELINE = 4,  // Vulkan and DX12 combines a bunch of state into pipeline objects. Might as well make them inspectable.
	SHADER_TYPE_TEXTURE = 5,
	SHADER_TYPE_SAMPLER = 6,  // Not really a shader either. Need to rename this enum...
};

enum DebugShaderStringType {
	SHADER_STRING_SHORT_DESC = 0,
	SHADER_STRING_SOURCE_CODE = 1,
	SHADER_STRING_STATS = 2,
};

// Shared between the backends. Not all are necessarily used by each backend, but this lets us share
// more code than before. TODO: Can probably cut the number of these down without too much slowdown.
enum : uint64_t {
	DIRTY_PROJMATRIX = 1ULL << 0,
	DIRTY_PROJTHROUGHMATRIX = 1ULL << 1,
	DIRTY_FOGCOLOR = 1ULL << 2,
	DIRTY_FOGCOEF = 1ULL << 3,
	DIRTY_TEXENV = 1ULL << 4,
	DIRTY_ALPHACOLORREF = 1ULL << 5,

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
	DIRTY_DEPTHRANGE = 1ULL << 19,

	DIRTY_WORLDMATRIX = 1ULL << 21,
	DIRTY_VIEWMATRIX = 1ULL << 22,
	DIRTY_TEXMATRIX = 1ULL << 23,
	DIRTY_BONEMATRIX0 = 1ULL << 24,  // NOTE: These must be under 32
	DIRTY_BONEMATRIX1 = 1ULL << 25,
	DIRTY_BONEMATRIX2 = 1ULL << 26,
	DIRTY_BONEMATRIX3 = 1ULL << 27,
	DIRTY_BONEMATRIX4 = 1ULL << 28,
	DIRTY_BONEMATRIX5 = 1ULL << 29,
	DIRTY_BONEMATRIX6 = 1ULL << 30,
	DIRTY_BONEMATRIX7 = 1ULL << 31,

	DIRTY_BEZIERSPLINE = 1ULL << 32,
	DIRTY_TEXCLAMP = 1ULL << 33,
	DIRTY_CULLRANGE = 1ULL << 34,

	DIRTY_DEPAL = 1ULL << 35,
	DIRTY_COLORWRITEMASK = 1ULL << 36,

	DIRTY_MIPBIAS = 1ULL << 37,
	DIRTY_LIGHT_CONTROL = 1ULL << 38,
	DIRTY_TEX_ALPHA_MUL = 1ULL << 39,

	// Bits 40-42 are free for new uniforms. Then we're really out and need to start merging.
	// Don't forget to update DIRTY_ALL_UNIFORMS when you start using them.

	DIRTY_BONE_UNIFORMS = 0xFF000000ULL,

	DIRTY_ALL_UNIFORMS = 0x0FFFFFFFFFFULL,

	// Other dirty elements that aren't uniforms
	DIRTY_CULL_PLANES = 1ULL << 43,
	DIRTY_FRAMEBUF = 1ULL << 44,
	DIRTY_TEXTURE_IMAGE = 1ULL << 45,  // Means that the definition of the texture image has changed (address, stride etc), and we need to look up again.
	DIRTY_TEXTURE_PARAMS = 1ULL << 46,

	// Render State
	DIRTY_BLEND_STATE = 1ULL << 47,
	DIRTY_DEPTHSTENCIL_STATE = 1ULL << 48,
	DIRTY_RASTER_STATE = 1ULL << 49,
	DIRTY_VIEWPORTSCISSOR_STATE = 1ULL << 50,
	DIRTY_VERTEXSHADER_STATE = 1ULL << 51,
	DIRTY_FRAGMENTSHADER_STATE = 1ULL << 52,
	DIRTY_GEOMETRYSHADER_STATE = 1ULL << 53,

	// Note that the top 8 bits (54-63) cannot be dirtied through the commonCommandTable due to packing of other flags.

	// Everything that's not uniforms. Use this after using thin3d.
	// TODO: Should we also add DIRTY_FRAMEBUF here? It kinda generally takes care of itself.
	DIRTY_ALL_RENDER_STATE = DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_GEOMETRYSHADER_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS,

	DIRTY_ALL = 0xFFFFFFFFFFFFFFFF
};

class ShaderManagerCommon {
public:
	ShaderManagerCommon(Draw::DrawContext *draw) : draw_(draw) {}
	virtual ~ShaderManagerCommon() {}

	virtual void ClearShaders() = 0;
	virtual void DirtyLastShader() = 0;

	virtual void DeviceLost() = 0;
	virtual void DeviceRestore(Draw::DrawContext *draw) = 0;   // must set draw_ to draw

	virtual std::vector<std::string> DebugGetShaderIDs(DebugShaderType type) = 0;
	virtual std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) = 0;

protected:
	Draw::DrawContext *draw_ = nullptr;
};

enum DoLightComputation {
	LIGHT_OFF,
	LIGHT_SHADE,
	LIGHT_FULL,
};

// PSP vertex format.
enum class PspAttributeLocation {
	POSITION = 0,
	TEXCOORD = 1,
	NORMAL = 2,
	W1 = 3,
	W2 = 4,
	COLOR0 = 5,
	COLOR1 = 6,

	COUNT
};

// Pre-fetched attrs and uniforms (used by GL only).
enum {
	ATTR_POSITION = 0,
	ATTR_TEXCOORD = 1,
	ATTR_NORMAL = 2,
	ATTR_W1 = 3,
	ATTR_W2 = 4,
	ATTR_COLOR0 = 5,
	ATTR_COLOR1 = 6,

	ATTR_COUNT,
};
