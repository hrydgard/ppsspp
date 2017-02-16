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

#include <d3d11.h>

#include "base/basictypes.h"
#include "Globals.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
// #include "GPU/DX9/VertexShaderGeneratorD3D11.h"
// #include "GPU/DX9/FragmentShaderGeneratorD3D11.h"
#include "math/lin/matrix4x4.h"
#include "GPU/Common/ShaderUniforms.h"

class D3D11Context;
class D3D11PushBuffer;

class D3D11FragmentShader {
public:
	D3D11FragmentShader(ID3D11Device *device, ShaderID id, const char *code, bool useHWTransform);
	~D3D11FragmentShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }

	std::string GetShaderString(DebugShaderStringType type) const;
	ID3D11PixelShader *GetShader() const { return module_; }

protected:
	ID3D11PixelShader *module_;

	ID3D11Device *device_;
	std::string source_;
	bool failed_;
	bool useHWTransform_;
	ShaderID id_;
};

class D3D11VertexShader {
public:
	D3D11VertexShader(ID3D11Device *device, ShaderID id, const char *code, int vertType, bool useHWTransform, bool usesLighting);
	~D3D11VertexShader();

	const std::string &source() const { return source_; }
	const std::vector<uint8_t> &bytecode() const { return bytecode_; }
	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }
	bool HasBones() const {
		return id_.Bit(VS_BIT_ENABLE_BONES);
	}
	bool HasLights() const {
		return usesLighting_;
	}

	std::string GetShaderString(DebugShaderStringType type) const;
	ID3D11VertexShader *GetShader() const { return module_; }

protected:
	ID3D11VertexShader *module_;

	ID3D11Device *device_;
	std::string source_;
	std::vector<uint8_t> bytecode_;

	bool failed_;
	bool useHWTransform_;
	bool usesLighting_;
	ShaderID id_;
};

class D3D11PushBuffer;

class ShaderManagerD3D11 : public ShaderManagerCommon {
public:
	ShaderManagerD3D11(ID3D11Device *device, ID3D11DeviceContext *context);
	~ShaderManagerD3D11();

	void GetShaders(int prim, u32 vertType, D3D11VertexShader **vshader, D3D11FragmentShader **fshader, bool useHWTransform);
	void ClearShaders();
	void DirtyShader();
	void DirtyLastShader() override;

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

	uint64_t UpdateUniforms();
	void BindUniforms();

	// TODO: Avoid copying these buffers if same as last draw, can still point to it assuming we're still in the same pushbuffer.
	// Applies dirty changes and copies the buffer.
	bool IsBaseDirty() { return true; }
	bool IsLightDirty() { return true; }
	bool IsBoneDirty() { return true; }

	/*
	uint32_t PushBaseBuffer(D3D11PushBuffer *dest, VkBuffer *buf);
	uint32_t PushLightBuffer(D3D11PushBuffer *dest, VkBuffer *buf);
	uint32_t PushBoneBuffer(D3D11PushBuffer *dest, VkBuffer *buf);
	*/

private:
	void Clear();

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;

	typedef std::map<ShaderID, D3D11FragmentShader *> FSCache;
	FSCache fsCache_;

	typedef std::map<ShaderID, D3D11VertexShader *> VSCache;
	VSCache vsCache_;

	char *codeBuffer_;

	// Uniform block scratchpad. These (the relevant ones) are copied to the current pushbuffer at draw time.
	UB_VS_FS_Base ub_base;
	UB_VS_Lights ub_lights;
	UB_VS_Bones ub_bones;

	// Not actual pushbuffers, requires D3D11.1, let's try to live without that first.
	ID3D11Buffer *push_base;
	ID3D11Buffer *push_lights;
	ID3D11Buffer *push_bones;

	D3D11FragmentShader *lastFShader_;
	D3D11VertexShader *lastVShader_;

	ShaderID lastFSID_;
	ShaderID lastVSID_;
};