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
#include "GPU/ge_constants.h"

#include "GPU/Directx9/helper/global.h"

namespace DX9 {

class DepalShaderDX9 {
public:
	LPDIRECT3DPIXELSHADER9 pixelShader;
};

class DepalTextureDX9 {
public:
	LPDIRECT3DTEXTURE9 texture;
	int lastFrame;
};

// Caches both shaders and palette textures.
class DepalShaderCacheDX9 {
public:
	DepalShaderCacheDX9();
	~DepalShaderCacheDX9();

	// This also uploads the palette and binds the correct texture.
	LPDIRECT3DPIXELSHADER9 GetDepalettizePixelShader(GEBufferFormat pixelFormat);
	LPDIRECT3DVERTEXSHADER9 GetDepalettizeVertexShader() { return vertexShader_; }
	LPDIRECT3DTEXTURE9 GetClutTexture(const u32 clutHash, u32 *rawClut);
	void Clear();
	void Decimate();

private:
	u32 GenerateShaderID(GEBufferFormat pixelFormat);

	LPDIRECT3DVERTEXSHADER9 vertexShader_;
	std::map<u32, DepalShaderDX9 *> cache_;
	std::map<u32, DepalTextureDX9 *> texCache_;
};

}  // namespace