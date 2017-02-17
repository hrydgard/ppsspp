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

#include <d3d11.h>
#include <d3dcompiler.h>

#include <map>

#include "base/logging.h"
#include "math/lin/matrix4x4.h"
#include "math/math_util.h"
#include "math/dataconv.h"
#include "util/text/utf8.h"
#include "thin3d/d3d11_loader.h"
#include "Common/Common.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/FragmentShaderGeneratorD3D11.h"
#include "GPU/D3D11/VertexShaderGeneratorD3D11.h"
#include "GPU/D3D11/D3D11Util.h"

D3D11FragmentShader::D3D11FragmentShader(ID3D11Device *device, ShaderID id, const char *code, bool useHWTransform)
	: device_(device), id_(id), failed_(false), useHWTransform_(useHWTransform), module_(0) {
	source_ = code;

	module_ = CreatePixelShaderD3D11(device, code, strlen(code));
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

D3D11VertexShader::D3D11VertexShader(ID3D11Device *device, ShaderID id, const char *code, int vertType, bool useHWTransform, bool usesLighting)
	: device_(device), id_(id), failed_(false), useHWTransform_(useHWTransform), module_(nullptr), usesLighting_(usesLighting) {
	source_ = code;

	module_ = CreateVertexShaderD3D11(device, code, strlen(code), &bytecode_);
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

ShaderManagerD3D11::ShaderManagerD3D11(ID3D11Device *device, ID3D11DeviceContext *context)
	: device_(device), context_(context), lastVShader_(nullptr), lastFShader_(nullptr) {
	codeBuffer_ = new char[16384];
	memset(&ub_base, 0, sizeof(ub_base));
	memset(&ub_lights, 0, sizeof(ub_lights));
	memset(&ub_bones, 0, sizeof(ub_bones));

	ILOG("sizeof(ub_base): %d", (int)sizeof(ub_base));
	ILOG("sizeof(ub_lights): %d", (int)sizeof(ub_lights));
	ILOG("sizeof(ub_bones): %d", (int)sizeof(ub_bones));

	D3D11_BUFFER_DESC desc{sizeof(ub_base), D3D11_USAGE_DYNAMIC, D3D11_BIND_CONSTANT_BUFFER, D3D11_CPU_ACCESS_WRITE };
	device_->CreateBuffer(&desc, nullptr, &push_base);
	desc.ByteWidth = sizeof(ub_lights);
	device_->CreateBuffer(&desc, nullptr, &push_lights);
	desc.ByteWidth = sizeof(ub_bones);
	device_->CreateBuffer(&desc, nullptr, &push_bones);
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
	lastFSID_.clear();
	lastVSID_.clear();
}

void ShaderManagerD3D11::ClearShaders() {
	Clear();
	DirtyShader();
	gstate_c.Dirty(DIRTY_ALL_UNIFORMS);
}

void ShaderManagerD3D11::DirtyShader() {
	// Forget the last shader ID
	lastFSID_.clear();
	lastVSID_.clear();
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
}

void ShaderManagerD3D11::DirtyLastShader() { // disables vertex arrays
	lastVShader_ = nullptr;
	lastFShader_ = nullptr;
}

uint64_t ShaderManagerD3D11::UpdateUniforms() {
	uint64_t dirty = gstate_c.GetDirtyUniforms();
	if (dirty != 0) {
		D3D11_MAPPED_SUBRESOURCE map;
		if (dirty & DIRTY_BASE_UNIFORMS) {
			BaseUpdateUniforms(&ub_base, dirty, true);
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

void ShaderManagerD3D11::GetShaders(int prim, u32 vertType, D3D11VertexShader **vshader, D3D11FragmentShader **fshader, bool useHWTransform) {
	ShaderID VSID;
	ShaderID FSID;
	ComputeVertexShaderID(&VSID, vertType, useHWTransform);
	ComputeFragmentShaderID(&FSID);

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
		bool usesLighting;
		GenerateVertexShaderD3D11(VSID, codeBuffer_, &usesLighting);
		vs = new D3D11VertexShader(device_, VSID, codeBuffer_, vertType, useHWTransform, usesLighting);
		vsCache_[VSID] = vs;
	} else {
		vs = vsIter->second;
	}
	lastVSID_ = VSID;

	FSCache::iterator fsIter = fsCache_.find(FSID);
	D3D11FragmentShader *fs;
	if (fsIter == fsCache_.end()) {
		// Fragment shader not in cache. Let's compile it.
		GenerateFragmentShaderD3D11(FSID, codeBuffer_);
		fs = new D3D11FragmentShader(device_, FSID, codeBuffer_, useHWTransform);
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
		auto iter = vsCache_.find(shaderId);
		if (iter == vsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}

	case SHADER_TYPE_FRAGMENT:
	{
		auto iter = fsCache_.find(shaderId);
		if (iter == fsCache_.end()) {
			return "";
		}
		return iter->second->GetShaderString(stringType);
	}
	default:
		return "N/A";
	}
}
