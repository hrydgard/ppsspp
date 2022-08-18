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
#include "Common/GPU/Shader.h"
#include "Common/GPU/thin3d.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/Draw2D.h"
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
class DepalShaderCache {
public:
	DepalShaderCache(Draw::DrawContext *draw);
	~DepalShaderCache();

	// This also uploads the palette and binds the correct texture.
	DepalShader *GetDepalettizeShader(uint32_t clutMode, GETextureFormat texFormat, GEBufferFormat pixelFormat);
	Draw::Texture *GetClutTexture(GEPaletteFormat clutFormat, const u32 clutHash, u32 *rawClut);

	Draw::SamplerState *GetSampler();

	void Clear();
	void Decimate();
	std::vector<std::string> DebugGetShaderIDs(DebugShaderType type);
	std::string DebugGetShaderString(std::string id, DebugShaderType type, DebugShaderStringType stringType);

	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

private:
	static uint32_t GenerateShaderID(uint32_t clutMode, GETextureFormat texFormat, GEBufferFormat pixelFormat) {
		return (clutMode & 0xFFFFFF) | (pixelFormat << 24) | (texFormat << 28);
	}

	static uint32_t GetClutID(GEPaletteFormat clutFormat, uint32_t clutHash) {
		// Simplistic.
		return clutHash ^ (uint32_t)clutFormat;
	}

	Draw::DrawContext *draw_;
	Draw::ShaderModule *vertexShader_ = nullptr;
	Draw::SamplerState *nearestSampler_ = nullptr;

	std::map<u32, DepalShader *> cache_;
	std::map<u32, DepalTexture *> texCache_;
};

// TODO: Merge with DepalShaderCache?
class TextureShaderApplier {
public:
	struct Pos {
		float x;
		float y;
	};
	struct UV {
		float u;
		float v;
	};

	TextureShaderApplier(Draw::DrawContext *draw, DepalShader *shader, float bufferW, float bufferH, int renderW, int renderH)
		: draw_(draw), shader_(shader), bufferW_(bufferW), bufferH_(bufferH), renderW_(renderW), renderH_(renderH) {
		static const Pos pos[4] = {
			{-1, -1 },
			{ 1, -1 },
			{-1,  1 },
			{ 1,  1 },
		};
		memcpy(pos_, pos, sizeof(pos_));

		static const UV uv[4] = {
			{ 0, 0 },
			{ 1, 0 },
			{ 0, 1 },
			{ 1, 1 },
		};
		memcpy(uv_, uv, sizeof(uv_));
	}

	void ApplyBounds(const KnownVertexBounds &bounds, u32 uoff, u32 voff) {
		// If min is not < max, then we don't have values (wasn't set during decode.)
		if (bounds.minV < bounds.maxV) {
			const float invWidth = 1.0f / bufferW_;
			const float invHeight = 1.0f / bufferH_;
			// Inverse of half = double.
			const float invHalfWidth = invWidth * 2.0f;
			const float invHalfHeight = invHeight * 2.0f;

			const int u1 = bounds.minU + uoff;
			const int v1 = bounds.minV + voff;
			const int u2 = bounds.maxU + uoff;
			const int v2 = bounds.maxV + voff;

			const float left = u1 * invHalfWidth - 1.0f;
			const float right = u2 * invHalfWidth - 1.0f;
			const float top = v1 * invHalfHeight - 1.0f;
			const float bottom = v2 * invHalfHeight - 1.0f;
			// Points are: BL, BR, TR, TL.
			pos_[0] = Pos{ left, bottom };
			pos_[1] = Pos{ right, bottom };
			pos_[2] = Pos{ left, top };
			pos_[3] = Pos{ right, top };

			// And also the UVs, same order.
			const float uvleft = u1 * invWidth;
			const float uvright = u2 * invWidth;
			const float uvtop = v1 * invHeight;
			const float uvbottom = v2 * invHeight;
			uv_[0] = UV{ uvleft, uvbottom };
			uv_[1] = UV{ uvright, uvbottom };
			uv_[2] = UV{ uvleft, uvtop };
			uv_[3] = UV{ uvright, uvtop };

			// We need to reapply the texture next time since we cropped UV.
			gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
		}
	}

	void Use() {
		draw_->BindPipeline(shader_->pipeline);
		struct SimpleVertex {
			float pos[2];
			float uv[2];
		};
		for (int i = 0; i < 4; i++) {
			memcpy(&verts_[i].x, &pos_[i], sizeof(Pos));
			memcpy(&verts_[i].u, &uv_[i], sizeof(UV));
		}
	}

	void Shade() {
		Draw::Viewport vp{ 0.0f, 0.0f, (float)renderW_, (float)renderH_, 0.0f, 1.0f };
		draw_->SetViewports(1, &vp);
		draw_->SetScissorRect(0, 0, renderW_, renderH_);
		draw_->DrawUP((const uint8_t *)verts_, 4);
	}

protected:
	Draw::DrawContext *draw_;
	DepalShader *shader_;
	Pos pos_[4];
	UV uv_[4];
	Draw2DVertex verts_[4];
	float bufferW_;
	float bufferH_;
	int renderW_;
	int renderH_;
};
