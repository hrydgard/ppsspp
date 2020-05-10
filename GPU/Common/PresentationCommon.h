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

struct CardboardSettings {
	bool enabled;
	float leftEyeXPosition;
	float rightEyeXPosition;
	float screenYPosition;
	float screenWidth;
	float screenHeight;
};

struct PostShaderUniforms {
	float texelDelta[2]; float pixelDelta[2];
	float time[4];
	float video;
};

void CenterDisplayOutputRect(float *x, float *y, float *w, float *h, float origW, float origH, float frameW, float frameH, int rotation);

namespace Draw {
class Buffer;
class DrawContext;
class Framebuffer;
class Pipeline;
class SamplerState;
class Texture;
}

struct ShaderInfo;
class TextureCacheCommon;

enum class OutputFlags {
	LINEAR = 0x0000,
	NEAREST = 0x0001,
	RB_SWIZZLE = 0x0002,
	BACKBUFFER_FLIPPED = 0x0004,
};

inline OutputFlags operator | (const OutputFlags &lhs, const OutputFlags &rhs) {
	return OutputFlags((int)lhs | (int)rhs);
}
inline OutputFlags operator |= (OutputFlags &lhs, const OutputFlags &rhs) {
	lhs = lhs | rhs;
	return lhs;
}
inline bool operator & (const OutputFlags &lhs, const OutputFlags &rhs) {
	return ((int)lhs & (int)rhs) != 0;
}

class PresentationCommon {
public:
	PresentationCommon(Draw::DrawContext *draw);
	~PresentationCommon();

	void UpdateSize(int w, int h) {
		pixelWidth_ = w;
		pixelHeight_ = h;
	}
	void UpdateShaderInfo(const ShaderInfo *shaderInfo);

	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

	void GetCardboardSettings(CardboardSettings *cardboardSettings);
	void CalculatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight, bool hasVideo, PostShaderUniforms *uniforms);

	// TODO: Cleanup
	void SourceTexture(Draw::Texture *texture);
	void SourceFramebuffer(Draw::Framebuffer *fb);
	void CopyToOutput(OutputFlags flags, int uvRotation, float u0, float v0, float u1, float v1);

protected:
	void CreateDeviceObjects();
	void DestroyDeviceObjects();

	Draw::DrawContext *draw_;
	Draw::Pipeline *texColor_ = nullptr;
	Draw::Pipeline *texColorRBSwizzle_ = nullptr;
	Draw::SamplerState *samplerNearest_ = nullptr;
	Draw::SamplerState *samplerLinear_ = nullptr;
	Draw::Buffer *vdata_ = nullptr;
	Draw::Buffer *idata_ = nullptr;

	Draw::Texture *srcTexture_ = nullptr;
	Draw::Framebuffer *srcFramebuffer_ = nullptr;

	int pixelWidth_ = 0;
	int pixelHeight_ = 0;
	bool postShaderAtOutputResolution_ = false;
};
