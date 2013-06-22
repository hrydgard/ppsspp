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
#include "VertexShaderGenerator.h"
#include "FragmentShaderGenerator.h"

class Shader;

class LinkedShader
{
public:
	LinkedShader(Shader *vs, Shader *fs, bool useHWTransform);
	~LinkedShader();

	void use();
	void stop();
	void updateUniforms();

	// Set to false if the VS failed, happens on Mali-400 a lot for complex shaders.
	bool useHWTransform_;

	uint32_t program;
	u32 dirtyUniforms;

	// Pre-fetched attrs and uniforms
	int a_position;
	int a_color0;
	int a_color1;
	int a_texcoord;
	int a_normal;
	int a_weight0123;
	int a_weight4567;

	int u_tex;
	int u_proj;
	int u_proj_through;
	int u_texenv;
	int u_view;
	int u_texmtx;
	int u_world;
#ifdef USE_BONE_ARRAY
	int u_bone;  // array, size is numBones
#else
	int u_bone[8];
#endif
	int numBones;
	
	// Fragment processing inputs
	int u_alphacolorref;
	int u_colormask;
	int u_fogcolor;
	int u_fogcoef;

	// Texturing
	int u_uvscaleoffset;

	// Lighting
	int u_ambient;
	int u_matambientalpha;
	int u_matdiffuse;
	int u_matspecular;
	int u_matemissive;
	int u_lightpos[4];
	int u_lightdir[4];
	int u_lightatt[4];  // attenuation
	int u_lightangle[4]; // spotlight cone angle (cosine)
	int u_lightspotCoef[4]; // spotlight dropoff
	int u_lightdiffuse[4];  // each light consist of vec4[3]
	int u_lightspecular[4];  // attenuation
	int u_lightambient[4];  // attenuation
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
	DIRTY_COLORMASK	 = (1 << 7),
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

class Shader {
public:
	Shader(const char *code, uint32_t shaderType, bool useHWTransform);
	~Shader();
	uint32_t shader;
	const std::string &source() const { return source_; }

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }

private:
	std::string source_;
	bool failed_;
	bool useHWTransform_;
};


class ShaderManager
{
public:
	ShaderManager();
	~ShaderManager();

	void ClearCache(bool deleteThem);  // TODO: deleteThem currently not respected
	LinkedShader *ApplyShader(int prim);
	void DirtyShader();
	void DirtyUniform(u32 what);
	void EndFrame();  // disables vertex arrays

	int NumVertexShaders() const { return (int)vsCache.size(); }
	int NumFragmentShaders() const { return (int)fsCache.size(); }
	int NumPrograms() const { return (int)linkedShaderCache.size(); }

private:
	void Clear();

	typedef std::map<std::pair<Shader *, Shader *>, LinkedShader *> LinkedShaderCache;

	LinkedShaderCache linkedShaderCache;
	FragmentShaderID lastFSID;
	VertexShaderID lastVSID;

	LinkedShader *lastShader;
	u32 globalDirty;
	u32 shaderSwitchDirty;
	char *codeBuffer_;

	typedef std::map<FragmentShaderID, Shader *> FSCache;
	FSCache fsCache;

	typedef std::map<VertexShaderID, Shader *> VSCache;
	VSCache vsCache;
};
