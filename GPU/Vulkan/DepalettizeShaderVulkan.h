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

#pragma once

#include <map>

#include "Common/CommonTypes.h"
#include "GPU/ge_constants.h"

class DepalShaderVulkan {
public:
	/*
	GLuint program;
	GLuint fragShader;
	GLint a_position;
	GLint a_texcoord0;
	*/
};

class DepalTextureVulkan {
public:
	int  texture;
	int lastFrame;
};

class VulkanTexture;

// Caches both shaders and palette textures.
// Could even avoid bothering with palette texture and just use uniform data...
class DepalShaderCacheVulkan {
public:
	DepalShaderCacheVulkan();
	~DepalShaderCacheVulkan();

	// This also uploads the palette and binds the correct texture.
	DepalShaderVulkan *GetDepalettizeShader(GEPaletteFormat clutFormat, GEBufferFormat pixelFormat);
	VulkanTexture *GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut);
	void Clear();
	void Decimate();

private:
	u32 GenerateShaderID(GEPaletteFormat clutFormat, GEBufferFormat pixelFormat);
	bool CreateVertexShader();

	// GLuint vertexShader_;
	std::map<u32, DepalShaderVulkan *> cache_;
	std::map<u32, DepalTextureVulkan *> texCache_;
};

