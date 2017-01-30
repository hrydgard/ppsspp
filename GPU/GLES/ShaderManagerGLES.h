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
#include "Globals.h"
#include <map>

#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/GLES/VertexShaderGeneratorGLES.h"
#include "GPU/GLES/FragmentShaderGeneratorGLES.h"

class Shader;

// Pre-fetched attrs and uniforms
enum {
	ATTR_POSITION = 0,
	ATTR_TEXCOORD = 1,
	ATTR_NORMAL = 2,
	ATTR_W1 = 3,
	ATTR_W2 = 4,
	ATTR_COLOR0 = 5,
	ATTR_COLOR1 = 6,

	ATTR_COUNT,
};

class LinkedShader {
public:
	LinkedShader(ShaderID VSID, Shader *vs, ShaderID FSID, Shader *fs, bool useHWTransform);
	~LinkedShader();

	void use(const ShaderID &VSID, LinkedShader *previous);
	void stop();
	void UpdateUniforms(u32 vertType, const ShaderID &VSID);

	Shader *vs_;
	// Set to false if the VS failed, happens on Mali-400 a lot for complex shaders.
	bool useHWTransform_;

	uint32_t program;
	uint64_t availableUniforms;
	uint64_t dirtyUniforms;

	// Present attributes in the shader.
	int attrMask;  // 1 << ATTR_ ... or-ed together.

	int u_stencilReplaceValue;
	int u_tex;
	int u_proj;
	int u_proj_through;
	int u_texenv;
	int u_view;
	int u_texmtx;
	int u_world;
	int u_depthRange;   // x,y = viewport xscale/xcenter. z,w=clipping minz/maxz (?)

#ifdef USE_BONE_ARRAY
	int u_bone;  // array, size is numBones
#else
	int u_bone[8];
#endif
	int numBones;

	// Shader blending.
	int u_fbotex;
	int u_blendFixA;
	int u_blendFixB;
	int u_fbotexSize;

	// Fragment processing inputs
	int u_alphacolorref;
	int u_alphacolormask;
	int u_testtex;
	int u_fogcolor;
	int u_fogcoef;

	// Texturing
	int u_uvscaleoffset;
	int u_texclamp;
	int u_texclampoff;

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

	int u_tess_pos_tex;
	int u_tess_tex_tex;
	int u_tess_col_tex;
	int u_spline_count_u;
	int u_spline_count_v;
	int u_spline_type_u;
	int u_spline_type_v;
};

// Real public interface

class Shader {
public:
	Shader(const char *code, uint32_t glShaderType, bool useHWTransform);
	~Shader();
	uint32_t shader;

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; } // only relevant for vtx shaders

	std::string GetShaderString(DebugShaderStringType type, ShaderID id) const;

private:
	std::string source_;
	bool failed_;
	bool useHWTransform_;
	bool isFragment_;
};

class ShaderManagerGLES : public ShaderManagerCommon {
public:
	ShaderManagerGLES();
	~ShaderManagerGLES();

	void ClearCache(bool deleteThem);  // TODO: deleteThem currently not respected

	// This is the old ApplyShader split into two parts, because of annoying information dependencies.
	// If you call ApplyVertexShader, you MUST call ApplyFragmentShader soon afterwards.
	Shader *ApplyVertexShader(int prim, u32 vertType, ShaderID *VSID);
	LinkedShader *ApplyFragmentShader(ShaderID VSID, Shader *vs, u32 vertType, int prim);

	void DirtyShader();
	void DirtyLastShader() override;  // disables vertex arrays

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }
	int GetNumPrograms() const { return (int)linkedShaderCache_.size(); }

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

	void LoadAndPrecompile(const std::string &filename);
	void Save(const std::string &filename);

private:
	void Clear();
	Shader *CompileFragmentShader(ShaderID id);
	Shader *CompileVertexShader(ShaderID id);

	struct LinkedShaderCacheEntry {
		LinkedShaderCacheEntry(Shader *vs_, Shader *fs_, LinkedShader *ls_)
			: vs(vs_), fs(fs_), ls(ls_) { }

		Shader *vs;
		Shader *fs;
		LinkedShader *ls;
	};
	typedef std::vector<LinkedShaderCacheEntry> LinkedShaderCache;

	LinkedShaderCache linkedShaderCache_;

	bool lastVShaderSame_;

	ShaderID lastFSID_;
	ShaderID lastVSID_;

	LinkedShader *lastShader_;
	u64 shaderSwitchDirtyUniforms_;
	char *codeBuffer_;

	typedef std::map<ShaderID, Shader *> FSCache;
	FSCache fsCache_;

	typedef std::map<ShaderID, Shader *> VSCache;
	VSCache vsCache_;

	bool diskCacheDirty_;
};
