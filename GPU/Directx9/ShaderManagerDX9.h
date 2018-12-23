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

#include <map>
#include <cstdint>

#include "base/basictypes.h"
#include "GPU/Directx9/VertexShaderGeneratorDX9.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "thin3d/d3dx9_loader.h"
#include "math/lin/matrix4x4.h"

namespace DX9 {

class PSShader;
class VSShader;

// Real public interface

class PSShader {
public:
	PSShader(LPDIRECT3DDEVICE9 device, FShaderID id, const char *code);
	~PSShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }

	std::string GetShaderString(DebugShaderStringType type) const;

	LPDIRECT3DPIXELSHADER9 shader;

protected:	
	std::string source_;
	bool failed_;
	FShaderID id_;
};

class VSShader {
public:
	VSShader(LPDIRECT3DDEVICE9 device, VShaderID id, const char *code, bool useHWTransform);
	~VSShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }

	std::string GetShaderString(DebugShaderStringType type) const;

	LPDIRECT3DVERTEXSHADER9 shader;

protected:	
	std::string source_;
	bool failed_;
	bool useHWTransform_;
	VShaderID id_;
};

class ShaderManagerDX9 : public ShaderManagerCommon {
public:
	ShaderManagerDX9(Draw::DrawContext *draw, LPDIRECT3DDEVICE9 device);
	~ShaderManagerDX9();

	void ClearCache(bool deleteThem);  // TODO: deleteThem currently not respected
	VSShader *ApplyShader(int prim, u32 vertType);
	void DirtyShader();
	void DirtyLastShader() override;

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

private:
	void PSUpdateUniforms(u64 dirtyUniforms);
	void VSUpdateUniforms(u64 dirtyUniforms);
	inline void PSSetColorUniform3Alpha255(int creg, u32 color, u8 alpha);
	inline void PSSetColorUniform3(int creg, u32 color);
	inline void PSSetFloat(int creg, float value);
	inline void PSSetFloatArray(int creg, const float *value, int count);

	void VSSetMatrix4x3(int creg, const float *m4x3);
	void VSSetMatrix4x3_3(int creg, const float *m4x3);
	inline void VSSetColorUniform3(int creg, u32 color);
	inline void VSSetColorUniform3ExtraFloat(int creg, u32 color, float extra);
	inline void VSSetColorUniform3Alpha(int creg, u32 color, u8 alpha);
	void VSSetMatrix(int creg, const float* pMatrix);
	void VSSetFloat(int creg, float value);
	void VSSetFloatArray(int creg, const float *value, int count);
	void VSSetFloat24Uniform3(int creg, const u32 data[3]);
	void VSSetFloatUniform4(int creg, float data[4]);

	void Clear();

	LPDIRECT3DDEVICE9 device_;

	FShaderID lastFSID_;
	VShaderID lastVSID_;

	char *codeBuffer_;

	VSShader *lastVShader_;
	PSShader *lastPShader_;

	typedef std::map<FShaderID, PSShader *> FSCache;
	FSCache fsCache_;

	typedef std::map<VShaderID, VSShader *> VSCache;
	VSCache vsCache_;
};

};
