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

#include "Common/Common.h"
#include "Common/GPU/Shader.h"

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
	float timeDelta[4];
	float setting[4];
	float video; float pad[3];
	float vr;
	// Used on Direct3D9.
	float gl_HalfPixel[4];
};

struct FRect {
	float x;
	float y;
	float w;
	float h;
};

struct Bounds;  // from geom2d
struct DisplayLayoutConfig;

FRect GetScreenFrame(bool ignoreInsets, float pixelWidth, float pixelHeight);
void SetOverrideScreenFrame(const Bounds *bounds);
struct DisplayLayoutConfig;
void CalculateDisplayOutputRect(const DisplayLayoutConfig &config, FRect *rc, float origW, float origH, const FRect &frame, int rotation);

namespace Draw {
class Buffer;
class DrawContext;
class Framebuffer;
class Pipeline;
class SamplerState;
class ShaderModule;
class Texture;
}

struct ShaderInfo;
class TextureCacheCommon;

enum class OutputFlags {
	DEFAULT = 0,
	LINEAR = 0x0000,
	NEAREST = 0x0001,
	RB_SWIZZLE = 0x0002,
	BACKBUFFER_FLIPPED = 0x0004,  // Viewport/scissor coordinates are y-flipped.
	POSITION_FLIPPED = 0x0008,    // Vertex position in the shader is y-flipped relative to the screen.
	PILLARBOX = 0x0010,           // Squeeze the image horizontally. Used for the DarkStalkers hack.
};
ENUM_CLASS_BITOPS(OutputFlags);

class PresentationCommon {
public:
	PresentationCommon(Draw::DrawContext *draw);
	~PresentationCommon();

	void UpdateDisplaySize(int w, int h) {
		pixelWidth_ = w;
		pixelHeight_ = h;
	}

	// NOTE: Should be un-rotated width/height.
	void UpdateRenderSize(int rw, int rh) {
		renderWidth_ = rw;
		renderHeight_ = rh;
	}
	void SetLanguage(ShaderLanguage lang) {
		lang_ = lang;
	}

	bool HasPostShader() {
		return usePostShader_;
	}

	bool UpdatePostShader(const DisplayLayoutConfig &config);

	void BeginFrame(const DisplayLayoutConfig &config) {
		if (restorePostShader_) {
			UpdatePostShader(config);
			restorePostShader_ = false;
		}
		presentedThisFrame_ = false;
	}
	bool PresentedThisFrame() const {
		return presentedThisFrame_;
	}
	void NotifyPresent() {
		// Something else did the present, skipping PresentationCommon.
		// If you haven't called BindFramebufferAsRenderTarget, you must not set this.
		presentedThisFrame_ = true;
	}

	void DeviceLost();
	void DeviceRestore(Draw::DrawContext *draw);

	void UpdateUniforms(bool hasVideo);

	// One of these must be called every frame.
	void SourceBlank();
	void SourceTexture(Draw::Texture *texture, int bufferWidth, int bufferHeight);
	void SourceFramebuffer(Draw::Framebuffer *fb, int bufferWidth, int bufferHeight);

	void RunPostshaderPasses(const DisplayLayoutConfig &config, OutputFlags flags, int uvRotation, float u0, float v0, float u1, float v1);
	void CopyToOutput(const DisplayLayoutConfig &config);

	void CalculateRenderResolution(const DisplayLayoutConfig &config, int *width, int *height, int *scaleFactor, bool *upscaling, bool *ssaa) const;

protected:
	void CreateDeviceObjects();
	void DestroyDeviceObjects();

	void DestroyPostShader();
	void DestroyStereoShader();

	static void ShowPostShaderError(const std::string &errorString);

	Draw::ShaderModule *CompileShaderModule(ShaderStage stage, ShaderLanguage lang, const std::string &src, std::string *errorString) const;
	Draw::Pipeline *CreatePipeline(std::vector<Draw::ShaderModule *> shaders, bool postShader, const UniformBufferDesc *uniformDesc) const;
	bool CompilePostShader(const ShaderInfo *shaderInfo, Draw::Pipeline **outPipeline) const;
	bool BuildPostShader(const DisplayLayoutConfig &config, const ShaderInfo *shaderInfo, const ShaderInfo *next, Draw::Pipeline **outPipeline);
	bool AllocateFramebuffer(int w, int h);

	bool BindSource(int binding, bool bindStereo);

	void GetCardboardSettings(const DisplayLayoutConfig &config, CardboardSettings *cardboardSettings) const;
	void CalculatePostShaderUniforms(int bufferWidth, int bufferHeight, int targetWidth, int targetHeight, const ShaderInfo *shaderInfo, PostShaderUniforms *uniforms) const;

	Draw::DrawContext *draw_;
	Draw::Pipeline *texColor_ = nullptr;
	Draw::Pipeline *texColorRBSwizzle_ = nullptr;
	Draw::SamplerState *samplerNearest_ = nullptr;
	Draw::SamplerState *samplerLinear_ = nullptr;
	Draw::Buffer *vdata_ = nullptr;

	std::vector<Draw::Pipeline *> postShaderPipelines_;
	std::vector<Draw::Framebuffer *> postShaderFramebuffers_;
	std::vector<ShaderInfo> postShaderInfo_;
	std::vector<Draw::Framebuffer *> previousFramebuffers_;
	
	Draw::Pipeline *stereoPipeline_ = nullptr;
	ShaderInfo *stereoShaderInfo_ = nullptr;

	int previousIndex_ = 0;
	PostShaderUniforms previousUniforms_{};

	Draw::Texture *srcTexture_ = nullptr;
	Draw::Framebuffer *srcFramebuffer_ = nullptr;
	int srcWidth_ = 0;
	int srcHeight_ = 0;
	bool hasVideo_ = false;

	int pixelWidth_ = 0;
	int pixelHeight_ = 0;
	int renderWidth_ = 0;
	int renderHeight_ = 0;

	bool usePostShader_ = false;
	bool restorePostShader_ = false;
	bool presentedThisFrame_ = false;
	ShaderLanguage lang_;

	struct PrevFBO {
		Draw::Framebuffer *fbo;
		int w;
		int h;
	};
	std::vector<PrevFBO> postShaderFBOUsage_;

	// Carry over info between RunPostShaderPasses and CopyToOutput.
	Draw::Framebuffer *postShaderOutput_ = nullptr;
	FRect rc_;
	OutputFlags outputFlags_ = OutputFlags::DEFAULT;
};
