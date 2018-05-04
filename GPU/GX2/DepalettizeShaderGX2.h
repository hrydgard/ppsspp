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
#include <wiiu/gx2.h>
#include <wiiu/os/memory.h>

#include "Common/CommonTypes.h"
#include "GPU/ge_constants.h"
#include "Common/GPU/thin3d.h"
#include "GPU/Common/DepalettizeShaderCommon.h"
#include "GPU/GX2/GX2Shaders.h"

// Caches both shaders and palette textures.
class DepalShaderCacheGX2 : public DepalShaderCacheCommon {
public:
	DepalShaderCacheGX2(Draw::DrawContext *draw);
	~DepalShaderCacheGX2();

	// This also uploads the palette and binds the correct texture.
	GX2PixelShader *GetDepalettizePixelShader(u32 clutMode, GEBufferFormat pixelFormat);
	GX2VertexShader *GetDepalettizeVertexShader() { return &defVShaderGX2; }
	GX2FetchShader *GetFetchShader() { return &fetchShader_; }
	GX2Texture *GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32_le *rawClut, bool expandTo32bit);
	void Clear();
	void Decimate();
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

private:
	struct DepalShaderGX2 : public GX2PixelShader {
		DepalShaderGX2(GEBufferFormat pixelFormat);
		~DepalShaderGX2() { MEM2_free(program); }
	};

	struct DepalTextureGX2 : public GX2Texture {
		DepalTextureGX2() : GX2Texture({}) {}
		~DepalTextureGX2() { MEM2_free(surface.image); }
		int lastFrame;
	};

	GX2ContextState *context_;
	GX2FetchShader fetchShader_ = {};

	std::map<u32, DepalShaderGX2 *> cache_;
	std::map<u32, DepalTextureGX2 *> texCache_;
};
