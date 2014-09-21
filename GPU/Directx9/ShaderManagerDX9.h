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

#include "base/basictypes.h"
#include "../../Globals.h"
#include <map>
#include "GPU/Directx9/VertexShaderGeneratorDX9.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"
#include "thin3d/d3dx9_loader.h"
#include "math/lin/matrix4x4.h"

namespace DX9 {

class PSShader;
class VSShader;

void ConvertProjMatrixToD3D(Matrix4x4 & in);

// Pretty much full. Will need more bits for more fine grained dirty tracking for lights.
enum {
	DIRTY_PROJMATRIX = (1 << 0),
	DIRTY_PROJTHROUGHMATRIX = (1 << 1),
	DIRTY_FOGCOLOR = (1 << 2),
	DIRTY_FOGCOEF = (1 << 3),
	DIRTY_TEXENV = (1 << 4),
	DIRTY_ALPHACOLORREF = (1 << 5),
	DIRTY_STENCILREPLACEVALUE = (1 << 6),

	DIRTY_ALPHACOLORMASK = (1 << 7),
	DIRTY_LIGHT0 = (1 << 8),
	DIRTY_LIGHT1 = (1 << 9),
	DIRTY_LIGHT2 = (1 << 10),
	DIRTY_LIGHT3 = (1 << 11),

	DIRTY_MATDIFFUSE = (1 << 12),
	DIRTY_MATSPECULAR = (1 << 13),
	DIRTY_MATEMISSIVE = (1 << 14),
	DIRTY_AMBIENT = (1 << 15),
	DIRTY_MATAMBIENTALPHA = (1 << 16),
	DIRTY_SHADERBLEND = (1 << 17),  // Used only for in-shader blending.
	DIRTY_UVSCALEOFFSET = (1 << 18),  // this will be dirtied ALL THE TIME... maybe we'll need to do "last value with this shader compares"
	DIRTY_TEXCLAMP = (1 << 19),

	DIRTY_WORLDMATRIX = (1 << 21),
	DIRTY_VIEWMATRIX = (1 << 22),  // Maybe we'll fold this into projmatrix eventually
	DIRTY_TEXMATRIX = (1 << 23),
	DIRTY_BONEMATRIX0 = (1 << 24),
	DIRTY_BONEMATRIX1 = (1 << 25),
	DIRTY_BONEMATRIX2 = (1 << 26),
	DIRTY_BONEMATRIX3 = (1 << 27),
	DIRTY_BONEMATRIX4 = (1 << 28),
	DIRTY_BONEMATRIX5 = (1 << 29),
	DIRTY_BONEMATRIX6 = (1 << 30),
	DIRTY_BONEMATRIX7 = (1 << 31),

	DIRTY_ALL = 0xFFFFFFFF
};

// Real public interface

class PSShader {
public:
	PSShader(const char *code, bool useHWTransform);
	~PSShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }
	
	LPDIRECT3DPIXELSHADER9 shader;

protected:	
	std::string source_;
	bool failed_;
	bool useHWTransform_;
};

class VSShader {
public:
	VSShader(const char *code, int vertType, bool useHWTransform);
	~VSShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }
	
	LPDIRECT3DVERTEXSHADER9 shader;

protected:	
	std::string source_;
	bool failed_;
	bool useHWTransform_;
};

class ShaderManagerDX9 {
public:
	ShaderManagerDX9();
	~ShaderManagerDX9();

	void ClearCache(bool deleteThem);  // TODO: deleteThem currently not respected
	VSShader *ApplyShader(int prim, u32 vertType);
	void DirtyShader();
	void DirtyUniform(u32 what) {
		globalDirty_ |= what;
	}
	void DirtyLastShader();

	int NumVertexShaders() const { return (int)vsCache_.size(); }
	int NumFragmentShaders() const { return (int)fsCache_.size(); }

private:
	void PSUpdateUniforms(int dirtyUniforms);
	void VSUpdateUniforms(int dirtyUniforms);
	void PSSetColorUniform3Alpha255(int creg, u32 color, u8 alpha);
	void PSSetColorUniform3(int creg, u32 color);
	void PSSetFloat(int creg, float value);
	void PSSetFloatArray(int creg, const float *value, int count);

	void VSSetMatrix4x3(int creg, const float *m4x3);
	void VSSetMatrix4x3_3(int creg, const float *m4x3);
	void VSSetColorUniform3(int creg, u32 color);
	void VSSetColorUniform3ExtraFloat(int creg, u32 color, float extra);
	void VSSetColorUniform3Alpha(int creg, u32 color, u8 alpha);
	void VSSetMatrix(int creg, const float* pMatrix);
	void VSSetFloat(int creg, float value);
	void VSSetFloatArray(int creg, const float *value, int count);
	void VSSetFloat24Uniform3(int creg, const u32 data[3]);

	void Clear();

	FragmentShaderIDDX9 lastFSID_;
	VertexShaderIDDX9 lastVSID_;

	u32 globalDirty_;
	char *codeBuffer_;

	VSShader *lastVShader_;
	PSShader *lastPShader_;

	typedef std::map<FragmentShaderIDDX9, PSShader *> FSCache;
	FSCache fsCache_;

	typedef std::map<VertexShaderIDDX9, VSShader *> VSCache;
	VSCache vsCache_;
};

};
