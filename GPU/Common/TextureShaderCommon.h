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
#include <vector>
#include <string>

#include "Common/CommonTypes.h"
#include "Common/GPU/thin3d.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/Draw2D.h"
#include "GPU/Common/ShaderCommon.h"


class ClutTexture {
public:
	enum { MAX_RAMPS = 3 };
	Draw::Texture *texture;
	int lastFrame;
	int rampLengths[MAX_RAMPS];
	int rampStarts[MAX_RAMPS];
};

// For CLUT depal shaders, and other pre-bind texture shaders.
// Caches both shaders and palette textures.
class TextureShaderCache {
public:
	TextureShaderCache(Draw::DrawContext *draw, Draw2D *draw2D);
	~TextureShaderCache();

	Draw2DPipeline *GetDepalettizeShader(uint32_t clutMode, GETextureFormat texFormat, GEBufferFormat pixelFormat, bool smoothedDepal, u32 depthUpperBits);
	ClutTexture GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, const u32 *rawClut);

	Draw::SamplerState *GetSampler(bool linearFilter);

	void Clear();
	void Decimate();
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(const std::string &id, DebugShaderType type, DebugShaderStringType stringType);

	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

private:
	Draw::DrawContext *draw_;
	Draw::SamplerState *nearestSampler_ = nullptr;
	Draw::SamplerState *linearSampler_ = nullptr;
	Draw2D *draw2D_;

	std::map<u64, Draw2DPipeline *> depalCache_;
	std::map<u32, ClutTexture *> texCache_;
};
