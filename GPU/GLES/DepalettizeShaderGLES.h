// Copyright (c) 2014- PPSSPP Project.

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

#include <map>

#include "Common/CommonTypes.h"
#include "gfx/gl_common.h"
#include "thin3d/thin3d.h"
#include "thin3d/GLRenderManager.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

class DepalShader {
public:
	GLRProgram *program;
	GLRShader *fragShader;
	GLint a_position;
	GLint a_texcoord0;
	GLint u_tex;
	GLint u_pal;
	std::string code;
};

class DepalTexture {
public:
	GLRTexture *texture;
	int lastFrame;
};

// Caches both shaders and palette textures.
class DepalShaderCacheGLES : public DepalShaderCacheCommon {
public:
	DepalShaderCacheGLES(Draw::DrawContext *draw);
	~DepalShaderCacheGLES();

	// This also uploads the palette and binds the correct texture.
	DepalShader *GetDepalettizeShader(uint32_t clutMode, GEBufferFormat pixelFormat);
	GLRTexture *GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut);
	void Clear();
	void Decimate();
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

private:
	bool CreateVertexShader();

	GLRenderManager *render_;
	bool useGL3_;
	bool vertexShaderFailed_;
	GLRShader *vertexShader_;
	std::map<u32, DepalShader *> cache_;
	std::map<u32, DepalTexture *> texCache_;
};

