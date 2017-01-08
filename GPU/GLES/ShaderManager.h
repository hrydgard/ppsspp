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
#include "GPU/GLES/VertexShaderGenerator.h"
#include "GPU/GLES/FragmentShaderGenerator.h"

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
	u64 availableUniforms;
	u64 dirtyUniforms;

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
};

enum {
	_DIRTY_PROJMATRIX = 0,
	_DIRTY_PROJTHROUGHMATRIX = 1,
	_DIRTY_FOGCOLOR = 2,
	_DIRTY_FOGCOEF = 3,
	_DIRTY_TEXENV = 4,
	_DIRTY_ALPHACOLORREF = 5,

	// 1 << 6 is free! Wait, not anymore...
	_DIRTY_STENCILREPLACEVALUE = 6,

	_DIRTY_ALPHACOLORMASK = 7,
	_DIRTY_LIGHT0 = 8,
	_DIRTY_LIGHT1 = 9,
	_DIRTY_LIGHT2 = 10,
	_DIRTY_LIGHT3 = 11,

	_DIRTY_MATDIFFUSE = 12,
	_DIRTY_MATSPECULAR = 13,
	_DIRTY_MATEMISSIVE = 14,
	_DIRTY_AMBIENT = 15,
	_DIRTY_MATAMBIENTALPHA = 16,

	_DIRTY_SHADERBLEND = 17,  // Used only for in-shader blending.

	_DIRTY_UVSCALEOFFSET = 18,  // this will be dirtied ALL THE TIME... maybe we'll need to do "last value with this shader compares"

	// Texclamp is fairly rare so let's share it's bit with DIRTY_DEPTHRANGE.
	_DIRTY_TEXCLAMP = 19,
	_DIRTY_DEPTHRANGE = 19,

	_DIRTY_WORLDMATRIX = 21,
	_DIRTY_VIEWMATRIX = 22,  // Maybe we'll fold this into projmatrix eventually
	_DIRTY_TEXMATRIX = 23,
	_DIRTY_BONEMATRIX0 = 24,
	_DIRTY_BONEMATRIX1 = 25,
	_DIRTY_BONEMATRIX2 = 26,
	_DIRTY_BONEMATRIX3 = 27,
	_DIRTY_BONEMATRIX4 = 28,
	_DIRTY_BONEMATRIX5 = 29,
	_DIRTY_BONEMATRIX6 = 30,
	_DIRTY_BONEMATRIX7 = 31,

	_DIRTY_BEZIERCOUNTU = 32,  // Used only for hardware tessellation
	_DIRTY_SPLINECOUNTU = 33,  // Used only for hardware tessellation
	_DIRTY_SPLINECOUNTV = 34,  // Used only for hardware tessellation
	_DIRTY_SPLINETYPEU = 35,  // Used only for hardware tessellation
	_DIRTY_SPLINETYPEV = 36,  // Used only for hardware tessellation

	__END_OF_LINE__DIRTY = 64,

	DIRTY_ALL = -1
};

#define FLAG_BIT64(x) (1ULL << x)

#define DIRTY_PROJMATRIX FLAG_BIT64(_DIRTY_PROJMATRIX)
#define DIRTY_PROJTHROUGHMATRIX FLAG_BIT64(_DIRTY_PROJTHROUGHMATRIX)
#define DIRTY_FOGCOLOR FLAG_BIT64(_DIRTY_FOGCOLOR)
#define DIRTY_FOGCOEF FLAG_BIT64(_DIRTY_FOGCOEF)
#define DIRTY_TEXENV FLAG_BIT64(_DIRTY_TEXENV)
#define DIRTY_ALPHACOLORREF FLAG_BIT64(_DIRTY_ALPHACOLORREF)
#define DIRTY_STENCILREPLACEVALUE FLAG_BIT64(_DIRTY_STENCILREPLACEVALUE)
#define DIRTY_ALPHACOLORMASK FLAG_BIT64(_DIRTY_ALPHACOLORMASK)
#define DIRTY_LIGHT0 FLAG_BIT64(_DIRTY_LIGHT0)
#define DIRTY_LIGHT1 FLAG_BIT64(_DIRTY_LIGHT1)
#define DIRTY_LIGHT2 FLAG_BIT64(_DIRTY_LIGHT2)
#define DIRTY_LIGHT3 FLAG_BIT64(_DIRTY_LIGHT3)
#define DIRTY_MATDIFFUSE FLAG_BIT64(_DIRTY_MATDIFFUSE)
#define DIRTY_MATSPECULAR FLAG_BIT64(_DIRTY_MATSPECULAR)
#define DIRTY_MATEMISSIVE FLAG_BIT64(_DIRTY_MATEMISSIVE)
#define DIRTY_AMBIENT FLAG_BIT64(_DIRTY_AMBIENT)
#define DIRTY_MATAMBIENTALPHA FLAG_BIT64(_DIRTY_MATAMBIENTALPHA)
#define DIRTY_SHADERBLEND FLAG_BIT64(_DIRTY_SHADERBLEND)
#define DIRTY_UVSCALEOFFSET FLAG_BIT64(_DIRTY_UVSCALEOFFSET)
#define DIRTY_TEXCLAMP FLAG_BIT64(_DIRTY_TEXCLAMP)
#define DIRTY_DEPTHRANGE FLAG_BIT64(_DIRTY_DEPTHRANGE)
#define DIRTY_WORLDMATRIX FLAG_BIT64(_DIRTY_WORLDMATRIX)
#define DIRTY_VIEWMATRIX FLAG_BIT64(_DIRTY_VIEWMATRIX)
#define DIRTY_TEXMATRIX FLAG_BIT64(_DIRTY_TEXMATRIX)
#define DIRTY_BONEMATRIX0 FLAG_BIT64(_DIRTY_BONEMATRIX0)
#define DIRTY_BONEMATRIX1 FLAG_BIT64(_DIRTY_BONEMATRIX1)
#define DIRTY_BONEMATRIX2 FLAG_BIT64(_DIRTY_BONEMATRIX2)
#define DIRTY_BONEMATRIX3 FLAG_BIT64(_DIRTY_BONEMATRIX3)
#define DIRTY_BONEMATRIX4 FLAG_BIT64(_DIRTY_BONEMATRIX4)
#define DIRTY_BONEMATRIX5 FLAG_BIT64(_DIRTY_BONEMATRIX5)
#define DIRTY_BONEMATRIX6 FLAG_BIT64(_DIRTY_BONEMATRIX6)
#define DIRTY_BONEMATRIX7 FLAG_BIT64(_DIRTY_BONEMATRIX7)
#define DIRTY_BEZIERCOUNTU FLAG_BIT64(_DIRTY_BEZIERCOUNTU)
#define DIRTY_SPLINECOUNTU FLAG_BIT64(_DIRTY_SPLINECOUNTU)
#define DIRTY_SPLINECOUNTV FLAG_BIT64(_DIRTY_SPLINECOUNTV)
#define DIRTY_SPLINETYPEU FLAG_BIT64(_DIRTY_SPLINETYPEU)
#define DIRTY_SPLINETYPEV FLAG_BIT64(_DIRTY_SPLINETYPEV)

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

class ShaderManager {
public:
	ShaderManager();
	~ShaderManager();

	void ClearCache(bool deleteThem);  // TODO: deleteThem currently not respected

	// This is the old ApplyShader split into two parts, because of annoying information dependencies.
	// If you call ApplyVertexShader, you MUST call ApplyFragmentShader soon afterwards.
	Shader *ApplyVertexShader(int prim, u32 vertType, ShaderID *VSID);
	LinkedShader *ApplyFragmentShader(ShaderID VSID, Shader *vs, u32 vertType, int prim);

	void DirtyShader();
	void DirtyUniform(u64 what) {
		globalDirty_ |= what;
	}
	void DirtyLastShader();  // disables vertex arrays

	int NumVertexShaders() const { return (int)vsCache_.size(); }
	int NumFragmentShaders() const { return (int)fsCache_.size(); }
	int NumPrograms() const { return (int)linkedShaderCache_.size(); }

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
	u64 globalDirty_;
	u64 shaderSwitchDirty_;
	char *codeBuffer_;

	typedef std::map<ShaderID, Shader *> FSCache;
	FSCache fsCache_;

	typedef std::map<ShaderID, Shader *> VSCache;
	VSCache vsCache_;

	bool diskCacheDirty_;
};
