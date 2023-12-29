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

#include <d3d11.h>
#include <D3Dcompiler.h>

#include <map>

#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/GPU/thin3d.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/Log.h"
#include "Common/CommonTypes.h"
#include "Core/Config.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/VertexShaderGenerator.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/D3D11Util.h"

D3D11FragmentShader::D3D11FragmentShader(ID3D11Device *device, D3D_FEATURE_LEVEL featureLevel, FShaderID id, const char *code, bool useHWTransform)
	: device_(device), useHWTransform_(useHWTransform), id_(id) {
	source_ = code;

	module_ = CreatePixelShaderD3D11(device, code, strlen(code), featureLevel);
	if (!module_)
		failed_ = true;
}

D3D11FragmentShader::~D3D11FragmentShader() {
	if (module_)
		module_->Release();
}

std::string D3D11FragmentShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return FragmentShaderDesc(id_);
	default:
		return "N/A";
	}
}

D3D11VertexShader::D3D11VertexShader(ID3D11Device *device, D3D_FEATURE_LEVEL featureLevel, VShaderID id, const char *code, bool useHWTransform)
	: device_(device), useHWTransform_(useHWTransform), id_(id) {
	source_ = code;

	module_ = CreateVertexShaderD3D11(device, code, strlen(code), &bytecode_, featureLevel);
	if (!module_)
		failed_ = true;
}

D3D11VertexShader::~D3D11VertexShader() {
	if (module_)
		module_->Release();
}

std::string D3D11VertexShader::GetShaderString(DebugShaderStringType type) const {
	switch (type) {
	case SHADER_STRING_SOURCE_CODE:
		return source_;
	case SHADER_STRING_SHORT_DESC:
		return VertexShaderDesc(id_);
	default:
		return "N/A";
	}
}

static constexpr size_t CODE_BUFFER_SIZE = 32768;

ShaderManagerD3D11::ShaderManagerD3D11(Draw::DrawContext *draw, ID3D11Device *device, ID3D11DeviceContext *context, D3D_FEATURE_LEVEL featureLevel)
	: ShaderManagerCommon(draw), device_(device), context_(context), featureLevel_(featureLevel) {
	codeBuffer_ = new char[CODE_BUFFER_SIZE];
	memset(&ub_base, 0, sizeof(ub_base));
	memset(&ub_lights, 0, sizeof(ub_lights));
	memset(&ub_bones, 0, sizeof(ub_bones));

	static_assert(sizeof(ub_base) <= 512, "ub_base grew too big");
	static_assert(sizeof(ub_lights) <= 512, "ub_lights grew too big");
	static_assert(sizeof(ub_bones) <= 384, "ub_bones grew too big");

	D3D11_BUFFER_DESC desc{sizeof(ub_base), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
	ASSERT_SUCCESS(device_->CreateBuffer(&desc, nullptr, &push_base));
	desc.ByteWidth = sizeof(ub_lights);
	ASSERT_SUCCESS(device_->CreateBuffer(&desc, nullptr, &push_lights));
	desc.ByteWidth = sizeof(ub_bones);
	ASSERT_SUCCESS(device_->CreateBuffer(&desc, nullptr, &push_bones));
}

ShaderManagerD3D11::~ShaderManagerD3D11() {
	push_base->Release();
	push_lights->Release();
	push_bones->Release();
	ClearShaders();
	delete[] codeBuffer_;
}

void ShaderManagerD3D11::Clear() {
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

void ShaderManagerD3D11::ClearShaders() {
	Clear();
	DirtyLastShader();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);
}

void ShaderManagerD3D11::DirtyLastShader() {
	lastFSID_.set_invalid();
	lastVSID_.set_invalid();
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
	gstate_c.Dirty(DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE);
}

uint64_t ShaderManagerD3D11::UpdateUniforms(bool useBufferedRendering) {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty != 0) {
		D3D11_MAPPED_SUBRESOURCE map;
		if (dirty & DIRTY_BASE_UNIFORMS) {
			BaseUpdateUniforms(&ub_base, dirty, true, useBufferedRendering);
			context_->Map(push_base, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
			memcpy(map.pData, &ub_base, sizeof(ub_base));
			context_->Unmap(push_base, 0);
		}
		if (dirty & DIRTY_LIGHT_UNIFORMS) {
			LightUpdateUniforms(&ub_lights, dirty);
			context_->Map(push_lights, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
			memcpy(map.pData, &ub_lights, sizeof(ub_lights));
			context_->Unmap(push_lights, 0);
		}
		if (dirty & DIRTY_BONE_UNIFORMS) {
			BoneUpdateUniforms(&ub_bones, dirty);
			context_->Map(push_bones, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
			memcpy(map.pData, &ub_bones, sizeof(ub_bones));
			context_->Unmap(push_bones, 0);
		}
	}
	gstate_c.CleanUniforms();
	return dirty;
}

void ShaderManagerD3D11::BindUniforms() {
	ID3D11Buffer *vs_cbs[3] = { push_base, push_lights, push_bones };
	ID3D11Buffer *ps_cbs[1] = { push_base };
	context_->VSSetConstantBuffers(0, 3, vs_cbs);
	context_->PSSetConstantBuffers(0, 1, ps_cbs);
}

void ShaderManagerD3D11::GetShaders(int prim, VertexDecoder *decoder, D3D11VertexShader **vshader, D3D11FragmentShader **fshader, const ComputedPipelineState &pipelineState, bool useHWTransform, bool useHWTessellation, bool weightsAsFloat, bool useSkinInDecode) {
	VShaderID VSID;
	FShaderID FSID;

	if (gstate_c.IsDirty(DIRTY_VERTEXSHADER_STATE)) {
		gstate_c.Clean(DIRTY_VERTEXSHADER_STATE);
		ComputeVertexShaderID(&VSID, decoder, useHWTransform, useHWTessellation, weightsAsFloat, useSkinInDecode);
	} else {
		VSID = lastVSID_;
	}

	if (gstate_c.IsDirty(DIRTY_FRAGMENTSHADER_STATE)) {
		gstate_c.Clean(DIRTY_FRAGMENTSHADER_STATE);
		ComputeFragmentShaderID(&FSID, pipelineState, draw_->GetBugs());
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
	D3D11VertexShader *vs;
	if (vsIter == vsCache_.end()) {
		// Vertex shader not in cache. Let's compile it.
		std::string genErrorString;
		uint32_t attrMask;
		uint64_t uniformMask;
		VertexShaderFlags flags;
		GenerateVertexShader(VSID, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &attrMask, &uniformMask, &flags, &genErrorString);
		_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "VS length error: %d", (int)strlen(codeBuffer_));
		vs = new D3D11VertexShader(device_, featureLevel_, VSID, codeBuffer_, useHWTransform);
		vsCache_[VSID] = vs;
	} else {
		vs = vsIter->second;
	}
	lastVSID_ = VSID;

	FSCache::iterator fsIter = fsCache_.find(FSID);
	D3D11FragmentShader *fs;
	if (fsIter == fsCache_.end()) {
		// Fragment shader not in cache. Let's compile it.
		std::string genErrorString;
		uint64_t uniformMask;
		FragmentShaderFlags flags;
		GenerateFragmentShader(FSID, codeBuffer_, draw_->GetShaderLanguageDesc(), draw_->GetBugs(), &uniformMask, &flags, &genErrorString);
		_assert_msg_(strlen(codeBuffer_) < CODE_BUFFER_SIZE, "FS length error: %d", (int)strlen(codeBuffer_));
		fs = new D3D11FragmentShader(device_, featureLevel_, FSID, codeBuffer_, useHWTransform);
		fsCache_[FSID] = fs;
	} else {
		fs = fsIter->second;
	}

	lastFSID_ = FSID;

	lastVShader_ = vs;
	lastFShader_ = fs;

	*vshader = vs;
	*fshader = fs;
}

std::vector<std::string> ShaderManagerD3D11::DebugGetShaderIDs(DebugShaderType type) {
	std::string id;
	std::vector<std::string> ids;
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		for (auto iter : vsCache_) {
			iter.first.ToString(&id);
			ids.push_back(id);
		}
		break;
	}
	case SHADER_TYPE_FRAGMENT:
	{
		for (auto iter : fsCache_) {
			iter.first.ToString(&id);
			ids.push_back(id);
		}
		break;
	}
	default:
		break;
	}
	return ids;
}

std::string ShaderManagerD3D11::DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) {
	ShaderID shaderId;
	shaderId.FromString(id);
	switch (type) {
	case SHADER_TYPE_VERTEX:
	{
		auto iter = vsCache_.find(VShaderID(shaderId));
		if (iter == vsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}

	case SHADER_TYPE_FRAGMENT:
	{
		auto iter = fsCache_.find(FShaderID(shaderId));
		if (iter == fsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}
	default:
		return "N/A";
	}
}
