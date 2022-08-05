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
#include <vector>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/GPU/Shader.h"
#include "Common/GPU/thin3d.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

class DepalShader {
public:
	Draw::ShaderModule *fragShader;
	Draw::Pipeline *pipeline;
	std::string code;
};

class DepalTexture {
public:
	Draw::Texture *texture;
	int lastFrame;
};

// Caches both shaders and palette textures.
class DepalShaderCache : public DepalShaderCacheCommon {
public:
	DepalShaderCache(Draw::DrawContext *draw);
	~DepalShaderCache();

	// This also uploads the palette and binds the correct texture.
	DepalShader *GetDepalettizeShader(uint32_t clutMode, GEBufferFormat pixelFormat);
	Draw::Texture *GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut);

	Draw::SamplerState *GetSampler();

	void Clear();
	void Decimate();
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

	// Exposed for testing.
	static bool GenerateVertexShader(char *buffer, const ShaderLanguageDesc &lang);

private:
	Draw::DrawContext *draw_;
	Draw::ShaderModule *vertexShader_ = nullptr;
	Draw::SamplerState *nearestSampler_ = nullptr;

	std::map<u32, DepalShader *> cache_;
	std::map<u32, DepalTexture *> texCache_;
};

