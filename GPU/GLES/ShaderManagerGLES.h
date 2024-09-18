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

#include "Common/Data/Collections/Hashmaps.h"
#include "Common/GPU/OpenGL/GLRenderManager.h"
#include "Common/File/Path.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexShaderGenerator.h"
#include "GPU/Common/FragmentShaderGenerator.h"

class DrawEngineGLES;
class Shader;
struct ShaderLanguageDesc;

namespace File {
class IOFile;
}

class LinkedShader {
public:
	LinkedShader(GLRenderManager *render, VShaderID VSID, Shader *vs, FShaderID FSID, Shader *fs, bool useHWTransform, bool preloading = false);
	~LinkedShader();

	void use(const ShaderID &VSID) const;
	void UpdateUniforms(const ShaderID &VSID, bool useBufferedRendering, const ShaderLanguageDesc &shaderLanguage);
	void Delete();

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
	int u_proj_lens;
	int u_proj_through;
	int u_texenv;
	int u_view;
	int u_texmtx;
	int u_world;
	int u_depthRange;   // x,y = viewport xscale/xcenter. z,w=clipping minz/maxz (?)
	int u_cullRangeMin;
	int u_cullRangeMax;
	int u_rotation;
	int u_mipBias;
	int u_scaleX;
	int u_scaleY;

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
	int u_depal_mask_shift_off_fmt;  // the params

	// Fragment processing inputs
	int u_alphacolorref;
	int u_alphacolormask;
	int u_colorWriteMask;
	int u_testtex;
	int u_fogcolor;
	int u_fogcoef;

	// Texturing
	int u_uvscaleoffset;
	int u_texclamp;
	int u_texclampoff;
	int u_texNoAlphaMul;

	// Lighting
	int u_lightControl;
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

	// Spline Tessellation
	int u_tess_points; // Control Points
	int u_tess_weights_u;
	int u_tess_weights_v;
	int u_spline_counts;
};

// Real public interface

struct ShaderDescGLES {
	uint32_t glShaderType;
	uint32_t attrMask;
	uint64_t uniformMask;
	bool useHWTransform;
};

class Shader {
public:
	Shader(GLRenderManager *render, const char *code, const std::string &desc, const ShaderDescGLES &params);
	~Shader();
	GLRShader *shader;

	bool UseHWTransform() const { return useHWTransform_; }  // only relevant for vtx shaders

	std::string GetShaderString(DebugShaderStringType type, ShaderID id) const;

	uint32_t GetAttrMask() const { return attrMask_; }
	uint64_t GetUniformMask() const { return uniformMask_; }

private:
	GLRenderManager *render_;
	std::string source_;
	bool useHWTransform_;
	bool isFragment_;
	uint32_t attrMask_; // only used in vertex shaders
	uint64_t uniformMask_;
};

class ShaderManagerGLES : public ShaderManagerCommon {
public:
	ShaderManagerGLES(Draw::DrawContext *draw);
	~ShaderManagerGLES();

	void ClearShaders() override;

	// This is the old ApplyShader split into two parts, because of annoying information dependencies.
	// If you call ApplyVertexShader, you MUST call ApplyFragmentShader soon afterwards.
	Shader *ApplyVertexShader(bool useHWTransform, bool useHWTessellation, VertexDecoder *vertexDecoder, bool weightsAsFloat, bool useSkinInDecode, VShaderID *VSID);
	LinkedShader *ApplyFragmentShader(VShaderID VSID, Shader *vs, const ComputedPipelineState &pipelineState, bool useBufferedRendering);

	void DeviceLost() override;
	void DeviceRestore(Draw::DrawContext *draw) override;

	void DirtyLastShader() override;

	int GetNumVertexShaders() const { return (int)vsCache_.size(); }
	int GetNumFragmentShaders() const { return (int)fsCache_.size(); }
	int GetNumPrograms() const { return (int)linkedShaderCache_.size(); }

	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type) override;
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType) override;

	static bool LoadCacheFlags(File::IOFile &f, DrawEngineGLES *drawEngine);
	bool LoadCache(File::IOFile &f);
	void SaveCache(const Path &filename, DrawEngineGLES *drawEngine);

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

	bool lastVShaderSame_ = false;

	FShaderID lastFSID_;
	VShaderID lastVSID_;

	LinkedShader *lastShader_ = nullptr;
	u64 shaderSwitchDirtyUniforms_ = 0;
	char *codeBuffer_;

	typedef DenseHashMap<FShaderID, Shader *> FSCache;
	FSCache fsCache_;

	typedef DenseHashMap<VShaderID, Shader *> VSCache;
	VSCache vsCache_;
};
