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

namespace DX9 {

class PSShader;
class VSShader;

class LinkedShaderDX9
{
protected:		
	// Helper
	D3DXHANDLE GetConstantByName(LPCSTR pName);
	void SetMatrix4x3(D3DXHANDLE uniform, const float *m4x3);
	void SetColorUniform3(D3DXHANDLE uniform, u32 color);
	void SetColorUniform3ExtraFloat(D3DXHANDLE uniform, u32 color, float extra);
	void SetColorUniform3Alpha(D3DXHANDLE uniform, u32 color, u8 alpha);
	void SetColorUniform3Alpha255(D3DXHANDLE uniform, u32 color, u8 alpha);
	void SetMatrix(D3DXHANDLE uniform, const float* pMatrix);
	void SetFloatArray(D3DXHANDLE uniform, const float* pArray, int len);
	void SetFloat(D3DXHANDLE uniform, float value);
	void SetFloat24Uniform3(D3DXHANDLE uniform, const u32 data[3]);

public:
	LinkedShaderDX9(VSShader *vs, PSShader *fs, u32 vertType, bool useHWTransform);
	~LinkedShaderDX9();

	void use();
	void stop();
	void updateUniforms();

	// Set to false if the VS failed, happens on Mali-400 a lot for complex shaders.
	bool useHWTransform_;

	VSShader *m_vs;
	PSShader *m_fs;

	u32 dirtyUniforms;

	// Pre-fetched attrs and uniforms
	D3DXHANDLE a_position;
	D3DXHANDLE a_color0;
	D3DXHANDLE a_color1;
	D3DXHANDLE a_texcoord;
	D3DXHANDLE a_normal;
	D3DXHANDLE a_weight0123;
	D3DXHANDLE a_weight4567;

	D3DXHANDLE u_tex;
	D3DXHANDLE u_proj;
	D3DXHANDLE u_proj_through;
	D3DXHANDLE u_texenv;
	D3DXHANDLE u_view;
	D3DXHANDLE u_texmtx;
	D3DXHANDLE u_world;
#ifdef USE_BONE_ARRAY
	D3DXHANDLE u_bone;  // array, size is numBones
#else
	D3DXHANDLE u_bone[8];
#endif
	int numBones;
	
	// Fragment processing inputs
	D3DXHANDLE u_alphacolorref;
	D3DXHANDLE u_alphacolormask;
	D3DXHANDLE u_fogcolor;
	D3DXHANDLE u_fogcoef;

	// Texturing
	D3DXHANDLE u_uvscaleoffset;

	// Lighting
	D3DXHANDLE u_ambient;
	D3DXHANDLE u_matambientalpha;
	D3DXHANDLE u_matdiffuse;
	D3DXHANDLE u_matspecular;
	D3DXHANDLE u_matemissive;
	D3DXHANDLE u_lightpos[4];
	D3DXHANDLE u_lightdir[4];
	D3DXHANDLE u_lightatt[4];  // attenuation
	D3DXHANDLE u_lightangle[4]; // spotlight cone angle (cosine)
	D3DXHANDLE u_lightspotCoef[4]; // spotlight dropoff
	D3DXHANDLE u_lightdiffuse[4];  // each light consist of vec4[3]
	D3DXHANDLE u_lightspecular[4];  // attenuation
	D3DXHANDLE u_lightambient[4];  // attenuation
};

// Will reach 32 bits soon :P
enum
{
	DIRTY_PROJMATRIX = (1 << 0),
	DIRTY_PROJTHROUGHMATRIX = (1 << 1),
	DIRTY_FOGCOLOR	 = (1 << 2),
	DIRTY_FOGCOEF    = (1 << 3),
	DIRTY_TEXENV		 = (1 << 4),
	DIRTY_ALPHACOLORREF	 = (1 << 5),
	DIRTY_COLORREF	 = (1 << 6),
	DIRTY_ALPHACOLORMASK	 = (1 << 7),
	DIRTY_LIGHT0 = (1 << 8),
	DIRTY_LIGHT1 = (1 << 9),
	DIRTY_LIGHT2 = (1 << 10),
	DIRTY_LIGHT3 = (1 << 11),

	DIRTY_MATDIFFUSE = (1 << 12),
	DIRTY_MATSPECULAR = (1 << 13),
	DIRTY_MATEMISSIVE = (1 << 14),
	DIRTY_AMBIENT = (1 << 15),
	DIRTY_MATAMBIENTALPHA = (1 << 16),
	DIRTY_MATERIAL = (1 << 17),  // let's set all 4 together (emissive ambient diffuse specular). We hide specular coef in specular.a
	DIRTY_UVSCALEOFFSET = (1 << 18),  // this will be dirtied ALL THE TIME... maybe we'll need to do "last value with this shader compares"

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
	LPD3DXCONSTANTTABLE constant;
protected:	
	std::string source_;
	bool failed_;
	bool useHWTransform_;
};

class VSShader {
public:
	VSShader(const char *code, bool useHWTransform);
	~VSShader();

	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }
	
	LPDIRECT3DVERTEXSHADER9 shader;
	LPD3DXCONSTANTTABLE constant;
protected:	
	std::string source_;
	bool failed_;
	bool useHWTransform_;
};

class ShaderManagerDX9
{
public:
	ShaderManagerDX9();
	~ShaderManagerDX9();

	void ClearCache(bool deleteThem);  // TODO: deleteThem currently not respected
	LinkedShaderDX9 *ApplyShader(int prim, u32 vertType);
	void DirtyShader();
	void DirtyUniform(u32 what) {
		globalDirty_ |= what;
	}
	void EndFrame();  // disables vertex arrays

	int NumVertexShaders() const { return (int)vsCache_.size(); }
	int NumFragmentShaders() const { return (int)fsCache_.size(); }
	int NumPrograms() const { return (int)linkedShaderCache_.size(); }

private:
	void Clear();

	struct LinkedShaderCacheEntry {
		LinkedShaderCacheEntry(VSShader *vs_, PSShader *fs_, LinkedShaderDX9 *ls_)
			: vs(vs_), fs(fs_), ls(ls_) { }

		VSShader *vs;
		PSShader *fs;
		LinkedShaderDX9 *ls;

	};
	typedef std::vector<LinkedShaderCacheEntry> LinkedShaderCache;

	LinkedShaderCache linkedShaderCache_;
	FragmentShaderIDDX9 lastFSID_;
	VertexShaderIDDX9 lastVSID_;

	LinkedShaderDX9 *lastShader_;
	u32 globalDirty_;
	u32 shaderSwitchDirty_;
	char *codeBuffer_;

	typedef std::map<FragmentShaderIDDX9, PSShader *> FSCache;
	FSCache fsCache_;

	typedef std::map<VertexShaderIDDX9, VSShader *> VSCache;
	VSCache vsCache_;

};

};
