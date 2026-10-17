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

#include "ppsspp_config.h"

#include "Common/Log.h"
#include "Common/LogReporting.h"
#include "Common/Profiler/Profiler.h"
#include "Common/StringUtils.h"
#include "Common/GPU/thin3d.h"
#include "GPU/GPUState.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Metal/ShaderManagerMetal.h"

static constexpr size_t CODE_BUFFER_SIZE = 32768;

MetalShader::MetalShader(id<MTLDevice> device, ShaderStage stage, const std::string &src, const std::string &description) : source(src), desc(description) {
	PROFILE_THIS_SCOPE("shadercomp");
	std::string errorMessage;
	if (!TranslateVulkanGLSLToMSL(stage, source.c_str(), &info, &errorMessage)) {
		ERROR_LOG(Log::G3D, "Failed to translate %s shader (%s): %s\n%s", ShaderStageAsString(stage), desc.c_str(), errorMessage.c_str(), LineNumberString(source).c_str());
		Reporting::ReportMessage("Metal shader translation failed: %s / code: %s", errorMessage.c_str(), source.c_str());
		return;
	}
	function = [[MetalShaderFunction alloc] initWithDevice:device shader:info tag:desc];
}

std::string MetalShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE: return source + "\n\n// MSL:\n\n" + info.msl;
	case SHADER_STRING_SHORT_DESC: return desc;
	default: return "N/A";
	}
}

ShaderManagerMetal::ShaderManagerMetal(Draw::DrawContext *draw)
	: ShaderManagerCommon(draw), compat_(GLSL_VULKAN), vsCache_(16), fsCache_(16) {
	codeBuffer_ = new char[CODE_BUFFER_SIZE];
}

ShaderManagerMetal::~ShaderManagerMetal() {
	Clear();
	delete[] codeBuffer_;
}

void ShaderManagerMetal::DeviceLost() {
	Clear();
	draw_ = nullptr;
}

void ShaderManagerMetal::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
}

void ShaderManagerMetal::Clear() {
	vsCache_.Iterate([](const VShaderID &, MetalVertexShader *shader) {
		delete shader;
	});
	fsCache_.Iterate([](const FShaderID &, MetalFragmentShader *shader) {
		delete shader;
	});
	vsCache_.Clear();
	fsCache_.Clear();
	lastVSID_.set_invalid();
	lastFSID_.set_invalid();
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

void ShaderManagerMetal::ClearShaders() {
	Clear();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

uint64_t ShaderManagerMetal::UpdateUniforms(bool useBufferedRendering, bool pixelMapped) {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty != 0) {
		if (dirty & DIRTY_BASE_UNIFORMS)
			BaseUpdateUniforms(&ub_base_, dirty, useBufferedRendering, pixelMapped);
		if (dirty & DIRTY_LIGHT_UNIFORMS)
			LightUpdateUniforms(&ub_lights_, dirty);
	}
	gstate_c.CleanUniforms();
	return dirty;
}

MetalVertexShader *ShaderManagerMetal::GetVertexShaderFromID(VShaderID vsid) {
	MetalVertexShader *vs = vsCache_.GetOrNull(vsid);
	if (vs) {
		return vs;
	}
	std::string genErrorString;
	uint64_t uniformMask = 0;
	uint32_t attributeMask = 0;
	VertexShaderFlags flags{};
	bool success = GenerateVertexShader(vsid, codeBuffer_, compat_, draw_->GetBugs(), &attributeMask, &uniformMask, &flags, &genErrorString);
	_assert_msg_(success, "VS gen error: %s", genErrorString.c_str());
	id<MTLDevice> device = (__bridge id<MTLDevice>)(void *)draw_->GetNativeObject(Draw::NativeObject::DEVICE);
	vs = new MetalVertexShader(device, vsid, flags, codeBuffer_);
	vsCache_.Insert(vsid, vs);
	return vs;
}

MetalFragmentShader *ShaderManagerMetal::GetFragmentShaderFromID(FShaderID fsid) {
	MetalFragmentShader *fs = fsCache_.GetOrNull(fsid);
	if (fs) {
		return fs;
	}
	std::string genErrorString;
	uint64_t uniformMask = 0;
	FragmentShaderFlags flags{};
	bool success = GenerateFragmentShader(fsid, codeBuffer_, compat_, draw_->GetBugs(), &uniformMask, &flags, &genErrorString);
	_assert_msg_(success, "FS gen error: %s", genErrorString.c_str());
	id<MTLDevice> device = (__bridge id<MTLDevice>)(void *)draw_->GetNativeObject(Draw::NativeObject::DEVICE);
	fs = new MetalFragmentShader(device, fsid, flags, codeBuffer_);
	fsCache_.Insert(fsid, fs);
	return fs;
}

void ShaderManagerMetal::GetShaderIDs(int prim, u32 vertexType, VShaderID *vshader, FShaderID *fshader, const ComputedPipelineState &pipelineState, bool useHWTransform, ClipInfoFlags clipInfoFlags) {
	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(&lastVSID_, vertexType, useHWTransform, clipInfoFlags);
	}
	*vshader = lastVSID_;

	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&lastFSID_, pipelineState, draw_->GetBugs(), clipInfoFlags);
	}
	*fshader = lastFSID_;

	_dbg_assert_(lastFSID_.Bit(FS_BIT_FLATSHADE) == lastVSID_.Bit(VS_BIT_FLATSHADE));
	_dbg_assert_msg_(lastVSID_.Bit(VS_BIT_USE_HW_TRANSFORM) == useHWTransform, "Bad vshader ID was computed");
}

bool ShaderManagerMetal::SaveCache(FILE *f) {
	bool ok = true;
	vsCache_.Iterate([&](const VShaderID &id, MetalVertexShader *) {
		ok = ok && fwrite(&id, sizeof(id), 1, f) == 1;
	});
	fsCache_.Iterate([&](const FShaderID &id, MetalFragmentShader *) {
		ok = ok && fwrite(&id, sizeof(id), 1, f) == 1;
	});
	return ok;
}

bool ShaderManagerMetal::LoadCache(FILE *f, int numVertexShaders, int numFragmentShaders) {
	// Only translated here. They're compiled along with the pipelines that use them.
	for (int i = 0; i < numVertexShaders; i++) {
		VShaderID id;
		if (fread(&id, sizeof(id), 1, f) != 1) {
			return false;
		}
		GetVertexShaderFromID(id);
	}
	for (int i = 0; i < numFragmentShaders; i++) {
		FShaderID id;
		if (fread(&id, sizeof(id), 1, f) != 1) {
			return false;
		}
		GetFragmentShaderFromID(id);
	}
	return true;
}

std::vector<std::string> ShaderManagerMetal::DebugGetShaderIDs(DebugShaderType type) {
	std::vector<uint64_t> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
		vsCache_.Iterate([&](const VShaderID &id, MetalVertexShader *) {
			ids.push_back(id.ToUint64());
		});
		break;
	case SHADER_TYPE_FRAGMENT:
		fsCache_.Iterate([&](const FShaderID &id, MetalFragmentShader *) {
			ids.push_back(id.ToUint64());
		});
		break;
	default:
		break;
	}
	return ToSortedDebugShaderIdVec(ids);
}

std::string ShaderManagerMetal::DebugGetShaderString(std::string idStr, DebugShaderType type, DebugShaderStringType stringType) {
	ShaderID shaderId;
	shaderId.FromString(idStr);
	switch (type) {
	case SHADER_TYPE_VERTEX: {
		MetalVertexShader *vs = vsCache_.GetOrNull(VShaderID(shaderId));
		return vs ? vs->GetShaderString(stringType) : "";
	}
	case SHADER_TYPE_FRAGMENT: {
		MetalFragmentShader *fs = fsCache_.GetOrNull(FShaderID(shaderId));
		return fs ? fs->GetShaderString(stringType) : "";
	}
	default:
		return "N/A";
	}
}
