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
#include "gfx_es2/gl_state.h"
#include "GPU/ge_constants.h"

class DepalShader {
public:
	GLuint program;
	GLuint fragShader;
};

class DepalTexture {
public:
	GLuint texture;
	int lastFrame;
};

// Caches both shaders and palette textures.
class DepalShaderCache {
public:
	DepalShaderCache();
	~DepalShaderCache();

	// This also uploads the palette and binds the correct texture.
	GLuint GetDepalettizeShader(GEBufferFormat pixelFormat);
	GLuint GetClutTexture(const u32 clutHash, u32 *rawClut);
	void Clear();
	void Decimate();

private:
	u32 GenerateShaderID(GEBufferFormat pixelFormat);

	bool useGL3_;
	GLuint vertexShader_;
	std::map<u32, DepalShader *> cache_;
	std::map<u32, DepalTexture *> texCache_;
};

