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

struct Shader;

struct LinkedShader
{
	LinkedShader(Shader *vs, Shader *fs);
	~LinkedShader();

	void use();

	uint32_t program;

	u32 dirtyUniforms;

	// Pre-fetched attrs and uniforms
	int a_position;
	int a_color0;
	int a_color1;
	int a_texcoord;
	// int a_blendWeight0123;
	// int a_blendWeight4567;

	int u_tex;
	int u_proj;
	int u_proj_through;
	int u_texenv;

	// Fragment processing inputs
	int u_alpharef;
	int u_fogcolor;
	int u_fogcoef;
};

enum
{
	DIRTY_PROJMATRIX = (1 << 0),
	DIRTY_PROJTHROUGHMATRIX = (1 << 1),
	DIRTY_FOGCOLOR	 = (1 << 2),
	DIRTY_FOGCOEF    = (1 << 3),
	DIRTY_TEXENV		 = (1 << 4),
	DIRTY_ALPHAREF	 = (1 << 5),

	DIRTY_ALL = (1 << 6) - 1
};

// Real public interface

struct Shader
{
	Shader(const char *code, uint32_t shaderType);
	uint32_t shader;
private:
	std::string source_;
};


class ShaderManager
{
public:
	ShaderManager() : globalDirty(0xFFFFFFFF) {}

	void ClearCache(bool deleteThem);  // TODO: deleteThem currently not respected
	LinkedShader *ApplyShader();
	void DirtyShader();
	void DirtyUniform(u32 what);

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

	typedef std::map<FragmentShaderID, Shader *> FSCache;
	FSCache fsCache;

	typedef std::map<VertexShaderID, Shader *> VSCache;
	VSCache vsCache;
};
