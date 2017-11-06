// Copyright (c) 2017- PPSSPP Project.

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
#include <cstdint>
#include <d3d11.h>

#include "Common/CommonTypes.h"
#include "GPU/ge_constants.h"
#include "thin3d/thin3d.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

class DepalShaderD3D11 {
public:
	ID3D11PixelShader *pixelShader;
	std::string code;
};

class DepalTextureD3D11 {
public:
	~DepalTextureD3D11() {
		if (texture)
			texture->Release();
		if (view)
			view->Release();
	}
	ID3D11Texture2D *texture;
	ID3D11ShaderResourceView *view;
	int lastFrame;
};

// Caches both shaders and palette textures.
class DepalShaderCacheD3D11 : public DepalShaderCacheCommon {
public:
	DepalShaderCacheD3D11(Draw::DrawContext *draw);
	~DepalShaderCacheD3D11();

	// This also uploads the palette and binds the correct texture.
	ID3D11PixelShader *GetDepalettizePixelShader(uint32_t clutMode, GEBufferFormat pixelFormat);
	ID3D11VertexShader *GetDepalettizeVertexShader() { return vertexShader_; }
	ID3D11InputLayout *GetInputLayout() { return inputLayout_; }
	ID3D11ShaderResourceView *GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut, bool expandTo32bit);
	void Clear();
	void Decimate();
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

private:
	ID3D11Device *device_;
	ID3D11DeviceContext *context_;
	D3D_FEATURE_LEVEL featureLevel_;
	ID3D11VertexShader *vertexShader_ = nullptr;
	ID3D11InputLayout *inputLayout_ = nullptr;

	std::map<u32, DepalShaderD3D11 *> cache_;
	std::map<u32, DepalTextureD3D11 *> texCache_;
};
