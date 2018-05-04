// Copyright (c) 2017- PPSSPP Project.

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

#include <map>
#include <vector>
#include <wiiu/gx2.h>
#include <wiiu/os/memory.h>

#include "Common/Swap.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderUniforms.h"

#include "GPU/GX2/GX2Shaders.h"
#include "GPU/GX2/GX2Util.h"

namespace GX2Gen {

enum class VSInput : u32 {
	POSITION,
	COORDS,
	COLOR0,
	COLOR1,
	NORMAL,
	WEIGHT0,
	WEIGHT1,
};
enum class PSInput : u32 {
	COLOR0,
	COLOR1,
	FOGDEPTH,
	COORDS,
};

enum class UB_Bindings : u32 {
	Reserved,
	Base,
	Lights,
	Bones,
};

} // namespace GX2Gen

struct UB_VSID {
	struct {
		u32_le COMP;
		u32_le TYPE;
		u32_le ENABLE;
		u32_le pad_;
	} VS_BIT_LIGHT[4];
	u32_le VS_BIT_LMODE;
	u32_le VS_BIT_IS_THROUGH;
	u32_le VS_BIT_ENABLE_FOG;
	u32_le VS_BIT_HAS_COLOR;
	u32_le VS_BIT_DO_TEXTURE;
	u32_le VS_BIT_DO_TEXTURE_TRANSFORM;
	u32_le VS_BIT_USE_HW_TRANSFORM;
	u32_le VS_BIT_HAS_NORMAL;
	u32_le VS_BIT_NORM_REVERSE;
	u32_le VS_BIT_HAS_TEXCOORD;
	u32_le VS_BIT_HAS_COLOR_TESS;
	u32_le VS_BIT_HAS_TEXCOORD_TESS;
	u32_le VS_BIT_NORM_REVERSE_TESS;
	u32_le VS_BIT_UVGEN_MODE;
	u32_le VS_BIT_UVPROJ_MODE;
	u32_le VS_BIT_LS0;
	u32_le VS_BIT_LS1;
	u32_le VS_BIT_BONES;
	u32_le VS_BIT_ENABLE_BONES;
	u32_le VS_BIT_MATERIAL_UPDATE;
	u32_le VS_BIT_SPLINE;
	u32_le VS_BIT_LIGHTING_ENABLE;
	u32_le VS_BIT_WEIGHT_FMTSCALE;
	u32_le VS_BIT_FLATSHADE;
	u32_le VS_BIT_BEZIER;
	u32_le GPU_ROUND_DEPTH_TO_16BIT;
} __attribute__((aligned(64)));

struct UB_FSID {
	u32_le FS_BIT_CLEARMODE;
	u32_le FS_BIT_DO_TEXTURE;
	u32_le FS_BIT_TEXFUNC;
	u32_le FS_BIT_TEXALPHA;
	u32_le FS_BIT_SHADER_DEPAL;
	u32_le FS_BIT_SHADER_TEX_CLAMP;
	u32_le FS_BIT_CLAMP_S;
	u32_le FS_BIT_CLAMP_T;
	u32_le FS_BIT_TEXTURE_AT_OFFSET;
	u32_le FS_BIT_LMODE;
	u32_le FS_BIT_ALPHA_TEST;
	u32_le FS_BIT_ALPHA_TEST_FUNC;
	u32_le FS_BIT_ALPHA_AGAINST_ZERO;
	u32_le FS_BIT_COLOR_TEST;
	u32_le FS_BIT_COLOR_TEST_FUNC;
	u32_le FS_BIT_COLOR_AGAINST_ZERO;
	u32_le FS_BIT_ENABLE_FOG;
	u32_le FS_BIT_DO_TEXTURE_PROJ;
	u32_le FS_BIT_COLOR_DOUBLE;
	u32_le FS_BIT_STENCIL_TO_ALPHA;
	u32_le FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE;
	u32_le FS_BIT_REPLACE_LOGIC_OP_TYPE;
	u32_le FS_BIT_REPLACE_BLEND;
	u32_le FS_BIT_BLENDEQ;
	u32_le FS_BIT_BLENDFUNC_A;
	u32_le FS_BIT_BLENDFUNC_B;
	u32_le FS_BIT_FLATSHADE;
	u32_le FS_BIT_BGRA_TEXTURE;
	u32_le GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT;
	u32_le GPU_SUPPORTS_DEPTH_CLAMP;
	u32_le GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT;
	u32_le GPU_SUPPORTS_ACCURATE_DEPTH;
} __attribute__((aligned(64)));

class GX2PShader : public GX2PixelShader {
public:
	GX2PShader(FShaderID id);
	~GX2PShader() {
		MEM2_free(ub_id);
		if (!(gx2rBuffer.flags & GX2R_RESOURCE_LOCKED_READ_ONLY))
			MEM2_free(program);
	}

	const std::string source() const { return "N/A"; }
	const u8 *bytecode() const { return program; }
	std::string GetShaderString(DebugShaderStringType type) const;

	UB_FSID *ub_id;

protected:
	FShaderID id_;
};

class GX2VShader : public GX2VertexShader {
public:
	GX2VShader(VShaderID id);
	~GX2VShader() {
		MEM2_free(ub_id);
		if (!(gx2rBuffer.flags & GX2R_RESOURCE_LOCKED_READ_ONLY))
			MEM2_free(program);
	}

	const std::string source() const { return "N/A"; }
	const u8 *bytecode() const { return program; }
	std::string GetShaderString(DebugShaderStringType type) const;

	UB_VSID *ub_id;

protected:
	VShaderID id_;
};

class ShaderManagerGX2 : public ShaderManagerCommon {
public:
	ShaderManagerGX2(Draw::DrawContext *draw, GX2ContextState *context);
	~ShaderManagerGX2();

	void GetShaders(int prim, u32 vertType, GX2VShader **vshader, GX2PShader **fshader, bool useHWTransform, bool useHWTessellation);
	void ClearShaders();
	void DirtyLastShader() override;

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

	uint64_t UpdateUniforms(PushBufferGX2 *push, bool useBufferedRendering);

	// TODO: Avoid copying these buffers if same as last draw, can still point to it assuming we're still in the same pushbuffer.
	// Applies dirty changes and copies the buffer.
	bool IsBaseDirty() { return true; }
	bool IsLightDirty() { return true; }
	bool IsBoneDirty() { return true; }

private:
	void Clear();

	typedef std::map<FShaderID, GX2PShader *> FSCache;
	FSCache fsCache_;

	typedef std::map<VShaderID, GX2VShader *> VSCache;
	VSCache vsCache_;

	// Uniform block scratchpad. These (the relevant ones) are copied to the current pushbuffer at draw time.
	UB_VS_FS_Base ub_base;
	UB_VS_Lights ub_lights;
	UB_VS_Bones ub_bones;

	GX2PShader *lastFShader_;
	GX2VShader *lastVShader_;

	FShaderID lastFSID_;
	VShaderID lastVSID_;
};
