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

#include <vector>

#include "base/basictypes.h"
#include "Common/Hashmaps.h"
#include "thin3d/GLRenderManager.h"
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
	LinkedShader(GLRenderManager *render, VShaderID VSID, Shader *vs, FShaderID FSID, Shader *fs, bool useHWTransform, bool preloading = false);
	~LinkedShader();

	void use(const ShaderID &VSID);
	void UpdateUniforms(u32 vertType, const ShaderID &VSID);

	GLRenderManager *render_;
	Shader *vs_;
	// Set to false if the VS failed, happens on Mali-400 a lot for complex shaders.
	bool useHWTransform_;

	GLRProgram *program;
	uint64_t availableUniforms;
	uint64_t dirtyUniforms = 0;

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

	// Shader depal
	int u_pal;  // the texture
	int u_depal;  // the params

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
	int u_lightangle_spotCoef[4]; // spotlight cone angle (cosine) (x), spotlight dropoff (y)
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
	Shader(GLRenderManager *render, const char *code, const std::string &desc, uint32_t glShaderType, bool useHWTransform, uint32_t attrMask, uint64_t uniformMask);
	~Shader();
	GLRShader *shader;

	bool Failed() const { return failed_; }
	bool UseHWTransform() const { return useHWTransform_; }  // only relevant for vtx shaders

	std::string GetShaderString(DebugShaderStringType type, ShaderID id) const;

	uint32_t GetAttrMask() const { return attrMask_; }
	uint64_t GetUniformMask() const { return uniformMask_; }

private:
	GLRenderManager *render_;
	std::string source_;
	bool failed_;
	bool useHWTransform_;
	bool isFragment_;
	uint32_t attrMask_; // only used in vertex shaders
	uint64_t uniformMask_;
};

class ShaderManagerGLES : public ShaderManagerCommon {
public:
	ShaderManagerGLES(Draw::DrawContext *draw);
	~ShaderManagerGLES();

	void ClearCache(bool deleteThem);  // TODO: deleteThem currently not respected

	// This is the old ApplyShader split into two parts, because of annoying information dependencies.
	// If you call ApplyVertexShader, you MUST call ApplyFragmentShader soon afterwards.
	Shader *ApplyVertexShader(int prim, u32 vertType, VShaderID *VSID);
	LinkedShader *ApplyFragmentShader(VShaderID VSID, Shader *vs, u32 vertType, int prim);

	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

	void DirtyShader();
	void DirtyLastShader() override;  // disables vertex arrays

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }
	int GetNumPrograms() const { return (int)linkedShaderCache_.size(); }

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

	void Load(const std::string &filename);
	bool ContinuePrecompile(float sliceTime = 1.0f / 60.0f);
	void Save(const std::string &filename);

private:
	void Clear();
	Shader *CompileFragmentShader(FShaderID id);
	Shader *CompileVertexShader(VShaderID id);

	struct LinkedShaderCacheEntry {
		LinkedShaderCacheEntry(Shader *vs_, Shader *fs_, LinkedShader *ls_)
			: vs(vs_), fs(fs_), ls(ls_) { }

		Shader *vs;
		Shader *fs;
		LinkedShader *ls;
	};
	typedef std::vector<LinkedShaderCacheEntry> LinkedShaderCache;

	GLRenderManager *render_;
	LinkedShaderCache linkedShaderCache_;

	bool lastVShaderSame_;

	FShaderID lastFSID_;
	VShaderID lastVSID_;

	LinkedShader *lastShader_;
	u64 shaderSwitchDirtyUniforms_;
	char *codeBuffer_;

	typedef DenseHashMap<FShaderID, Shader *, nullptr> FSCache;
	FSCache fsCache_;

	typedef DenseHashMap<VShaderID, Shader *, nullptr> VSCache;
	VSCache vsCache_;

	bool diskCacheDirty_;
	struct {
		std::vector<VShaderID> vert;
		std::vector<FShaderID> frag;
		std::vector<std::pair<VShaderID, FShaderID>> link;

		size_t vertPos = 0;
		size_t fragPos = 0;
		size_t linkPos = 0;
		double start;

		void Clear() {
			vert.clear();
			frag.clear();
			link.clear();
			vertPos = 0;
			fragPos = 0;
			linkPos = 0;
		}

		bool Done() {
			return vertPos >= vert.size() && fragPos >= frag.size() && linkPos >= link.size();
		}
	} diskCachePending_;
};
