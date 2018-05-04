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

#include "ppsspp_config.h"

#include <map>
#include <wiiu/gx2/shaders.h>
#include <wiiu/gx2/shader_disasm.hpp>
#include <wiiu/gx2/shader_info.h>

#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Common.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/GX2/ShaderManagerGX2.h"
#include "GPU/GX2/FragmentShaderGeneratorGX2.h"
#include "GPU/GX2/VertexShaderGeneratorGX2.h"
#include "GPU/GX2/GX2Util.h"
#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/Vulkan/VertexShaderGeneratorVulkan.h"

GX2PShader::GX2PShader(FShaderID id) : GX2PixelShader(), id_(id) {
	GenerateFragmentShaderGX2(id, this);
	ub_id = (UB_FSID *)MEM2_alloc(sizeof(UB_FSID), GX2_UNIFORM_BLOCK_ALIGNMENT);
	memset(ub_id, 0, sizeof(UB_FSID));
	ub_id->FS_BIT_CLEARMODE = id.Bit(FS_BIT_CLEARMODE);
	ub_id->FS_BIT_DO_TEXTURE = id.Bit(FS_BIT_DO_TEXTURE);
	ub_id->FS_BIT_TEXFUNC = id.Bits(FS_BIT_TEXFUNC, 3);
	ub_id->FS_BIT_TEXALPHA = id.Bit(FS_BIT_TEXALPHA);
	ub_id->FS_BIT_SHADER_DEPAL = id.Bit(FS_BIT_SHADER_DEPAL);
	ub_id->FS_BIT_SHADER_TEX_CLAMP = id.Bit(FS_BIT_SHADER_TEX_CLAMP);
	ub_id->FS_BIT_CLAMP_S = id.Bit(FS_BIT_CLAMP_S);
	ub_id->FS_BIT_CLAMP_T = id.Bit(FS_BIT_CLAMP_T);
	ub_id->FS_BIT_TEXTURE_AT_OFFSET = id.Bit(FS_BIT_TEXTURE_AT_OFFSET);
	ub_id->FS_BIT_LMODE = id.Bit(FS_BIT_LMODE);
	ub_id->FS_BIT_ALPHA_TEST = id.Bit(FS_BIT_ALPHA_TEST);
	ub_id->FS_BIT_ALPHA_TEST_FUNC = id.Bits(FS_BIT_ALPHA_TEST_FUNC, 3);
	ub_id->FS_BIT_ALPHA_AGAINST_ZERO = id.Bit(FS_BIT_ALPHA_AGAINST_ZERO);
	ub_id->FS_BIT_COLOR_TEST = id.Bit(FS_BIT_COLOR_TEST);
	ub_id->FS_BIT_COLOR_TEST_FUNC = id.Bits(FS_BIT_COLOR_TEST_FUNC, 2);
	ub_id->FS_BIT_COLOR_AGAINST_ZERO = id.Bit(FS_BIT_COLOR_AGAINST_ZERO);
	ub_id->FS_BIT_ENABLE_FOG = id.Bit(FS_BIT_ENABLE_FOG);
	ub_id->FS_BIT_DO_TEXTURE_PROJ = id.Bit(FS_BIT_DO_TEXTURE_PROJ);
	ub_id->FS_BIT_COLOR_DOUBLE = id.Bit(FS_BIT_COLOR_DOUBLE);
	ub_id->FS_BIT_STENCIL_TO_ALPHA = id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2);
	ub_id->FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE = id.Bits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4);
	ub_id->FS_BIT_REPLACE_LOGIC_OP_TYPE = id.Bits(FS_BIT_REPLACE_LOGIC_OP_TYPE, 2);
	ub_id->FS_BIT_REPLACE_BLEND = id.Bits(FS_BIT_REPLACE_BLEND, 3);
	ub_id->FS_BIT_BLENDEQ = id.Bits(FS_BIT_BLENDEQ, 3);
	ub_id->FS_BIT_BLENDFUNC_A = id.Bits(FS_BIT_BLENDFUNC_A, 4);
	ub_id->FS_BIT_BLENDFUNC_B = id.Bits(FS_BIT_BLENDFUNC_B, 4);
	ub_id->FS_BIT_FLATSHADE = id.Bit(FS_BIT_FLATSHADE);
	ub_id->FS_BIT_BGRA_TEXTURE = id.Bit(FS_BIT_BGRA_TEXTURE);
	ub_id->GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT = gstate_c.Supports(GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT);
	ub_id->GPU_SUPPORTS_DEPTH_CLAMP = gstate_c.Supports(GPU_SUPPORTS_DEPTH_CLAMP);
	ub_id->GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT = gstate_c.Supports(GPU_ROUND_FRAGMENT_DEPTH_TO_16BIT);
	ub_id->GPU_SUPPORTS_ACCURATE_DEPTH = gstate_c.Supports(GPU_SUPPORTS_ACCURATE_DEPTH);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_UNIFORM_BLOCK, ub_id, sizeof(UB_FSID));
}

std::string GX2PShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SHORT_DESC: return FragmentShaderDesc(id_);
	case SHADER_STRING_SOURCE_CODE: {
		char buffer[0x20000];
		DisassembleGX2Shader(program, size, buffer);
		sprintf(buffer + strlen(buffer), "\n### GPU Regs ###\n");
		GX2PixelShaderInfo(this, buffer + strlen(buffer));
		sprintf(buffer + strlen(buffer), "\n### glsl ###\n");
		GenerateVulkanGLSLFragmentShader(id_, buffer + strlen(buffer), 0);
		_assert_(strlen(buffer) < sizeof(buffer));
		return buffer;
	}
	default: return "N/A";
	}
}

GX2VShader::GX2VShader(VShaderID id) : GX2VertexShader(), id_(id) {
	GenerateVertexShaderGX2(id, this);
	ub_id = (UB_VSID *)MEM2_alloc(sizeof(UB_VSID), GX2_UNIFORM_BLOCK_ALIGNMENT);
	memset(ub_id, 0, sizeof(UB_VSID));
	ub_id->VS_BIT_LMODE = id.Bit(VS_BIT_LMODE);
	ub_id->VS_BIT_IS_THROUGH = id.Bit(VS_BIT_IS_THROUGH);
	ub_id->VS_BIT_ENABLE_FOG = id.Bit(VS_BIT_ENABLE_FOG);
	ub_id->VS_BIT_HAS_COLOR = id.Bit(VS_BIT_HAS_COLOR);
	ub_id->VS_BIT_DO_TEXTURE = id.Bit(VS_BIT_DO_TEXTURE);
	ub_id->VS_BIT_DO_TEXTURE_TRANSFORM = id.Bit(VS_BIT_DO_TEXTURE_TRANSFORM);
	ub_id->VS_BIT_USE_HW_TRANSFORM = id.Bit(VS_BIT_USE_HW_TRANSFORM);
	ub_id->VS_BIT_HAS_NORMAL = id.Bit(VS_BIT_HAS_NORMAL);
	ub_id->VS_BIT_NORM_REVERSE = id.Bit(VS_BIT_NORM_REVERSE);
	ub_id->VS_BIT_HAS_TEXCOORD = id.Bit(VS_BIT_HAS_TEXCOORD);
	ub_id->VS_BIT_HAS_COLOR_TESS = id.Bit(VS_BIT_HAS_COLOR_TESS);
	ub_id->VS_BIT_HAS_TEXCOORD_TESS = id.Bit(VS_BIT_HAS_TEXCOORD_TESS);
	ub_id->VS_BIT_NORM_REVERSE_TESS = id.Bit(VS_BIT_NORM_REVERSE_TESS);
	ub_id->VS_BIT_UVGEN_MODE = id.Bit(VS_BIT_UVGEN_MODE);
	ub_id->VS_BIT_UVPROJ_MODE = id.Bits(VS_BIT_UVPROJ_MODE, 2);
	ub_id->VS_BIT_LS0 = id.Bits(VS_BIT_LS0, 2);
	ub_id->VS_BIT_LS1 = id.Bits(VS_BIT_LS1, 2);
	ub_id->VS_BIT_BONES = id.Bits(VS_BIT_BONES, 3);
	ub_id->VS_BIT_ENABLE_BONES = id.Bit(VS_BIT_ENABLE_BONES);
	ub_id->VS_BIT_LIGHT[0].COMP = id.Bits(VS_BIT_LIGHT0_COMP, 2);
	ub_id->VS_BIT_LIGHT[0].TYPE = id.Bits(VS_BIT_LIGHT0_TYPE, 2);
	ub_id->VS_BIT_LIGHT[1].COMP = id.Bits(VS_BIT_LIGHT1_COMP, 2);
	ub_id->VS_BIT_LIGHT[1].TYPE = id.Bits(VS_BIT_LIGHT1_TYPE, 2);
	ub_id->VS_BIT_LIGHT[2].COMP = id.Bits(VS_BIT_LIGHT2_COMP, 2);
	ub_id->VS_BIT_LIGHT[2].TYPE = id.Bits(VS_BIT_LIGHT2_TYPE, 2);
	ub_id->VS_BIT_LIGHT[3].COMP = id.Bits(VS_BIT_LIGHT3_COMP, 2);
	ub_id->VS_BIT_LIGHT[3].TYPE = id.Bits(VS_BIT_LIGHT3_TYPE, 2);
	ub_id->VS_BIT_MATERIAL_UPDATE = id.Bits(VS_BIT_MATERIAL_UPDATE, 3);
	ub_id->VS_BIT_SPLINE = id.Bit(VS_BIT_SPLINE);
	ub_id->VS_BIT_LIGHT[0].ENABLE = id.Bit(VS_BIT_LIGHT0_ENABLE);
	ub_id->VS_BIT_LIGHT[1].ENABLE = id.Bit(VS_BIT_LIGHT1_ENABLE);
	ub_id->VS_BIT_LIGHT[2].ENABLE = id.Bit(VS_BIT_LIGHT2_ENABLE);
	ub_id->VS_BIT_LIGHT[3].ENABLE = id.Bit(VS_BIT_LIGHT3_ENABLE);
	ub_id->VS_BIT_LIGHTING_ENABLE = id.Bit(VS_BIT_LIGHTING_ENABLE);
	ub_id->VS_BIT_WEIGHT_FMTSCALE = id.Bits(VS_BIT_WEIGHT_FMTSCALE, 2);
	ub_id->VS_BIT_FLATSHADE = id.Bit(VS_BIT_FLATSHADE);
	ub_id->VS_BIT_BEZIER = id.Bit(VS_BIT_BEZIER);
	ub_id->GPU_ROUND_DEPTH_TO_16BIT = gstate_c.Supports(GPU_ROUND_DEPTH_TO_16BIT);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_UNIFORM_BLOCK, ub_id, sizeof(UB_VSID));
}

std::string GX2VShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SHORT_DESC: return VertexShaderDesc(id_);
	case SHADER_STRING_SOURCE_CODE:
	{
		char buffer[0x20000];
		DisassembleGX2Shader(program, size, buffer);
		sprintf(buffer + strlen(buffer), "\n### GPU Regs ###\n");
		GX2VertexShaderInfo(this, buffer + strlen(buffer));
		sprintf(buffer + strlen(buffer), "\n### glsl ###\n");
		GenerateVulkanGLSLVertexShader(id_, buffer + strlen(buffer));
		_assert_(strlen(buffer) < sizeof(buffer));
		return buffer;
	}
	default: return "N/A";
	}
}

ShaderManagerGX2::ShaderManagerGX2(Draw::DrawContext *draw, GX2ContextState *context) :
	ShaderManagerCommon(draw), lastVShader_(nullptr), lastFShader_(nullptr) {
	memset(&ub_base, 0, sizeof(ub_base));
	memset(&ub_lights, 0, sizeof(ub_lights));
	memset(&ub_bones, 0, sizeof(ub_bones));

	INFO_LOG(G3D, "sizeof(ub_base): %d", (int)sizeof(ub_base));
	INFO_LOG(G3D, "sizeof(ub_lights): %d", (int)sizeof(ub_lights));
	INFO_LOG(G3D, "sizeof(ub_bones): %d", (int)sizeof(ub_bones));
}

ShaderManagerGX2::~ShaderManagerGX2() {
	ClearShaders();
}

void ShaderManagerGX2::Clear() {
	for (auto iter = fsCache_.begin(); iter != fsCache_.end(); ++iter) {
		delete iter->second;
	}
	for (auto iter = vsCache_.begin(); iter != vsCache_.end(); ++iter) {
		delete iter->second;
	}
	fsCache_.clear();
	vsCache_.clear();
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

void ShaderManagerGX2::ClearShaders() {
	Clear();
	DirtyLastShader();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);
}

void ShaderManagerGX2::DirtyLastShader() {
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

uint64_t ShaderManagerGX2::UpdateUniforms(PushBufferGX2 *push, bool useBufferedRendering) {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty != 0) {
		if (dirty & DIRTY_BASE_UNIFORMS) {
			BaseUpdateUniforms(&ub_base, dirty, true, useBufferedRendering);
			u32_le *push_base = (u32_le *)push->BeginPush(nullptr, sizeof(ub_base));
			for (int i = 0; i < sizeof(ub_base) / 4; i++)
				push_base[i] = ((u32 *)&ub_base)[i];
			push->EndPush();
			GX2SetVertexUniformBlock(1, sizeof(ub_base), push_base);
			GX2SetPixelUniformBlock(1, sizeof(ub_base), push_base);
		}
		if (dirty & DIRTY_LIGHT_UNIFORMS) {
			LightUpdateUniforms(&ub_lights, dirty);
			u32_le *push_lights = (u32_le *)push->BeginPush(nullptr, sizeof(ub_lights));
			for (int i = 0; i < sizeof(ub_lights) / 4; i++)
				push_lights[i] = ((u32 *)&ub_lights)[i];
			push->EndPush();
			GX2SetVertexUniformBlock(2, sizeof(ub_lights), push_lights);
		}
		if (dirty & DIRTY_BONE_UNIFORMS) {
			BoneUpdateUniforms(&ub_bones, dirty);
			u32_le *push_bones = (u32_le *)push->BeginPush(nullptr, sizeof(ub_bones));
			for (int i = 0; i < sizeof(ub_bones) / 4; i++)
				push_bones[i] = ((u32 *)&ub_bones)[i];
			push->EndPush();
			GX2SetVertexUniformBlock(3, sizeof(ub_bones), push_bones);
		}
	}
	gstate_c.CleanUniforms();
	return dirty;
}

void ShaderManagerGX2::GetShaders(int prim, u32 vertType, GX2VShader **vshader, GX2PShader **fshader,
											 bool useHWTransform, bool useHWTessellation) {
	VShaderID VSID;
	FShaderID FSID;

	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(&VSID, vertType, useHWTransform, useHWTessellation);
	} else {
		VSID = lastVSID_;
	}

	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&FSID, draw_->GetBugs());
	} else {
		FSID = lastFSID_;
	}

	// Just update uniforms if this is the same shader as last time.
	if (lastVShader_ != nullptr && lastFShader_ != nullptr && VSID == lastVSID_ && FSID == lastFSID_) {
		*vshader = lastVShader_;
		*fshader = lastFShader_;
		// Already all set, no need to look up in shader maps.
		return;
	}

	VSCache::iterator vsIter = vsCache_.find(VSID);
	GX2VShader *vs;
	if (vsIter == vsCache_.end()) {
		// Vertex shader not in cache. Let's generate it.
		vs = new GX2VShader(VSID);
		vsCache_[VSID] = vs;
	} else {
		vs = vsIter->second;
	}
	lastVSID_ = VSID;

	FSCache::iterator fsIter = fsCache_.find(FSID);
	GX2PShader *fs;
	if (fsIter == fsCache_.end()) {
		// Fragment shader not in cache. Let's generate it.
		fs = new GX2PShader(FSID);
		fsCache_[FSID] = fs;
	} else {
		fs = fsIter->second;
	}

	lastFSID_ = FSID;

	lastVShader_ = vs;
	lastFShader_ = fs;

	*vshader = vs;
	*fshader = fs;

	GX2SetVertexUniformBlock(4, sizeof(UB_VSID), vs->ub_id);
	GX2SetPixelUniformBlock(5, sizeof(UB_FSID), fs->ub_id);
}

std::vector<std::string> ShaderManagerGX2::DebugGetShaderIDs(DebugShaderType type) {
	std::string id;
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX: {
		for (auto iter : vsCache_) {
			iter.first.ToString(&id);
			ids.push_back(id);
		}
		break;
	}
	case SHADER_TYPE_FRAGMENT: {
		for (auto iter : fsCache_) {
			iter.first.ToString(&id);
			ids.push_back(id);
		}
		break;
	}
	default: break;
	}
	return ids;
}

std::string ShaderManagerGX2::DebugGetShaderString(std::string id, DebugShaderType type,
																	DebugShaderStringType stringType) {
	ShaderID shaderId;
	shaderId.FromString(id);
	switch (type) {
	case SHADER_TYPE_VERTEX: {
		auto iter = vsCache_.find(VShaderID(shaderId));
		if (iter == vsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}

	case SHADER_TYPE_FRAGMENT: {
		auto iter = fsCache_.find(FShaderID(shaderId));
		if (iter == fsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}
	default: return "N/A";
	}
}
