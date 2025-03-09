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

#include <algorithm>
#include <cmath>
#include <set>
#include <cstdint>
#include "Common/GPU/thin3d.h"

#include "Common/System/Display.h"
#include "Common/System/System.h"
#include "Common/System/OSD.h"
#include "Common/File/VFS/VFS.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/Math/geom2d.h"
#include "Common/Log.h"
#include "Common/TimeUtil.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/System.h"
#include "Core/HW/Display.h"
#include "GPU/Common/PostShader.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/GPUState.h"
#include "Common/GPU/ShaderTranslation.h"

struct Vertex {
	float x, y, z;
	float u, v;
	uint32_t rgba;
};

static bool g_overrideScreenBounds;
static Bounds g_screenBounds;

void SetOverrideScreenFrame(const Bounds *bounds) {
	g_overrideScreenBounds = bounds != nullptr;
	if (bounds) {
		g_screenBounds = *bounds;
	}
}

FRect GetScreenFrame(float pixelWidth, float pixelHeight) {
	FRect rc = FRect{
		0.0f,
		0.0f,
		pixelWidth,
		pixelHeight,
	};

	bool applyInset = !g_Config.bIgnoreScreenInsets;

	if (applyInset) {
		// Remove the DPI scale to get back to pixels.
		float left = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_LEFT) / g_display.dpi_scale;
		float right = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_RIGHT) / g_display.dpi_scale;
		float top = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_TOP) / g_display.dpi_scale;
		float bottom = System_GetPropertyFloat(SYSPROP_DISPLAY_SAFE_INSET_BOTTOM) / g_display.dpi_scale;

		// Adjust left edge to compensate for cutouts (notches) if any.
		rc.x += left;
		rc.w -= (left + right);
		rc.y += top;
		rc.h -= (top + bottom);
	}

	if (g_overrideScreenBounds) {
		// Set rectangle to match central node. Here we ignore bIgnoreScreenInsets.
		rc.x = g_screenBounds.x;
		rc.y = g_screenBounds.y;
		rc.w = g_screenBounds.w;
		rc.h = g_screenBounds.h;
	}

	return rc;
}

void CalculateDisplayOutputRect(FRect *rc, float origW, float origH, const FRect &frame, int rotation) {
	float outW;
	float outH;

	bool rotated = rotation == ROTATION_LOCKED_VERTICAL || rotation == ROTATION_LOCKED_VERTICAL180;

	bool stretch = g_Config.bDisplayStretch && !g_Config.bDisplayIntegerScale;

	float offsetX = g_Config.fDisplayOffsetX;
	float offsetY = g_Config.fDisplayOffsetY;

	float scale = g_Config.fDisplayScale;
	float aspectRatioAdjust = g_Config.fDisplayAspectRatio;

	float origRatio = !rotated ? origW / origH : origH / origW;
	float frameRatio = frame.w / frame.h;

	if (stretch) {
		// Automatically set aspect ratio to match the display, IF the rotation matches the output display ratio! Otherwise, just
		// sets standard aspect ratio because actually stretching will just look silly.
		bool globalRotated = g_display.rotation == DisplayRotation::ROTATE_90 || g_display.rotation == DisplayRotation::ROTATE_270;
		if (rotated == (g_display.dp_yres > g_display.dp_xres)) {
			origRatio = frameRatio;
		} else {
			origRatio *= aspectRatioAdjust;
		}
	} else {
		origRatio *= aspectRatioAdjust;
	}

	float scaledWidth = frame.w * scale;
	float scaledHeight = frame.h * scale;

	if (origRatio > frameRatio) {
		// Image is wider than frame. Center vertically.
		outW = scaledWidth;
		outH = scaledWidth / origRatio;
	} else {
		// Image is taller than frame. Center horizontally.
		outW = scaledHeight * origRatio;
		outH = scaledHeight;
	}

	// Ye olde 1080p hack: If everything is setup to exactly cover the screen (defaults), and the screen display aspect ratio is 16:9,
	// cut off one line from the top and bottom.
	if (scale == 1.0f && aspectRatioAdjust == 1.0f && offsetX == 0.5f && offsetY == 0.5f && !g_Config.bDisplayIntegerScale && g_Config.bDisplayCropTo16x9) {
		if (fabsf(frame.w / frame.h - 16.0f / 9.0f) < 0.0001f) {
			outW *= 272.0f / 270.0f;
			outH *= 272.0f / 270.0f;
		}
	}

	if (g_Config.bDisplayIntegerScale) {
		float wDim = 480.0f;
		if (rotated) {
			wDim = 272.0f;
		}

		int zoom = g_Config.iInternalResolution;
		if (zoom == 0) {
			// Auto (1:1) mode, not super meaningful with integer scaling, but let's do something that makes
			// some sense. use the longest dimension, just to have something. round down.
			if (!g_Config.IsPortrait()) {
				zoom = (PSP_CoreParameter().pixelWidth) / 480;
			} else {
				zoom = (PSP_CoreParameter().pixelHeight) / 480;
			}
		}
		// If integer scaling, limit ourselves to even multiples of the rendered resolution,
		// to make sure all the pixels are square.
		wDim *= zoom;
		outW = std::max(1.0f, floorf(outW / wDim)) * wDim;
		outH = outW / origRatio;
	}

	if (IsVREnabled()) {
		rc->x = 0;
		rc->y = 0;
		rc->w = floorf(frame.w);
		rc->h = floorf(frame.h);
		outW = frame.w;
		outH = frame.h;
	} else {
		rc->x = floorf(frame.x + frame.w * offsetX - outW * 0.5f);
		rc->y = floorf(frame.y + frame.h * offsetY - outH * 0.5f);
		rc->w = floorf(outW);
		rc->h = floorf(outH);
	}
}

PresentationCommon::PresentationCommon(Draw::DrawContext *draw) : draw_(draw) {
	CreateDeviceObjects();
}

PresentationCommon::~PresentationCommon() {
	DestroyDeviceObjects();
}

void PresentationCommon::GetCardboardSettings(CardboardSettings *cardboardSettings) const {
	if (!g_Config.bEnableCardboardVR) {
		cardboardSettings->enabled = false;
		return;
	}

	// Calculate Cardboard Settings
	float cardboardScreenScale = g_Config.iCardboardScreenSize / 100.0f;
	float cardboardScreenWidth = pixelWidth_ / 2.0f * cardboardScreenScale;
	float cardboardScreenHeight = pixelHeight_ * cardboardScreenScale;
	float cardboardMaxXShift = (pixelWidth_ / 2.0f - cardboardScreenWidth) / 2.0f;
	float cardboardUserXShift = g_Config.iCardboardXShift / 100.0f * cardboardMaxXShift;
	float cardboardLeftEyeX = cardboardMaxXShift + cardboardUserXShift;
	float cardboardRightEyeX = pixelWidth_ / 2.0f + cardboardMaxXShift - cardboardUserXShift;
	float cardboardMaxYShift = pixelHeight_ / 2.0f - cardboardScreenHeight / 2.0f;
	float cardboardUserYShift = g_Config.iCardboardYShift / 100.0f * cardboardMaxYShift;
	float cardboardScreenY = cardboardMaxYShift + cardboardUserYShift;

	cardboardSettings->enabled = true;
	cardboardSettings->leftEyeXPosition = cardboardLeftEyeX;
	cardboardSettings->rightEyeXPosition = cardboardRightEyeX;
	cardboardSettings->screenYPosition = cardboardScreenY;
	cardboardSettings->screenWidth = cardboardScreenWidth;
	cardboardSettings->screenHeight = cardboardScreenHeight;
}

static float GetShaderSettingValue(const ShaderInfo *shaderInfo, int i, const char *nameSuffix) {
	std::string key = shaderInfo->section + nameSuffix;
	auto it = g_Config.mPostShaderSetting.find(key);
	if (it != g_Config.mPostShaderSetting.end())
		return it->second;
	return shaderInfo->settings[i].value;
}

void PresentationCommon::CalculatePostShaderUniforms(int bufferWidth, int bufferHeight, int targetWidth, int targetHeight, const ShaderInfo *shaderInfo, PostShaderUniforms *uniforms) const {
	float u_delta = 1.0f / bufferWidth;
	float v_delta = 1.0f / bufferHeight;
	float u_pixel_delta = 1.0f / targetWidth;
	float v_pixel_delta = 1.0f / targetHeight;
	int flipCount = __DisplayGetFlipCount();
	int vCount = __DisplayGetVCount();
	float time[4] = { (float)time_now_d(), (vCount % 60) * 1.0f / 60.0f, (float)vCount, (float)(flipCount % 60) };

	uniforms->texelDelta[0] = u_delta;
	uniforms->texelDelta[1] = v_delta;
	uniforms->pixelDelta[0] = u_pixel_delta;
	uniforms->pixelDelta[1] = v_pixel_delta;
	memcpy(uniforms->time, time, 4 * sizeof(float));
	uniforms->timeDelta[0] = time[0] - previousUniforms_.time[0];
	uniforms->timeDelta[1] = (time[2] - previousUniforms_.time[2]) * (1.0f / 60.0f);
	uniforms->timeDelta[2] = time[2] - previousUniforms_.time[2];
	uniforms->timeDelta[3] = time[3] != previousUniforms_.time[3] ? 1.0f : 0.0f;
	uniforms->video = hasVideo_ ? 1.0f : 0.0f;

	// The shader translator tacks this onto our shaders, if we don't set it they render garbage.
	uniforms->gl_HalfPixel[0] = u_pixel_delta * 0.5f;
	uniforms->gl_HalfPixel[1] = v_pixel_delta * 0.5f;

	uniforms->setting[0] = GetShaderSettingValue(shaderInfo, 0, "SettingCurrentValue1");
	uniforms->setting[1] = GetShaderSettingValue(shaderInfo, 1, "SettingCurrentValue2");
	uniforms->setting[2] = GetShaderSettingValue(shaderInfo, 2, "SettingCurrentValue3");
	uniforms->setting[3] = GetShaderSettingValue(shaderInfo, 3, "SettingCurrentValue4");
}

static std::string ReadShaderSrc(const Path &filename) {
	size_t sz = 0;
	char *data = (char *)g_VFS.ReadFile(filename.c_str(), &sz);
	if (!data) {
		return "";
	}

	std::string src(data, sz);
	delete[] data;
	return src;
}

// Note: called on resize and settings changes.
// Also takes care of making sure the appropriate stereo shader is compiled.
bool PresentationCommon::UpdatePostShader() {
	DestroyStereoShader();

	if (gstate_c.Use(GPU_USE_SIMPLE_STEREO_PERSPECTIVE)) {
		const ShaderInfo *stereoShaderInfo = GetPostShaderInfo(g_Config.sStereoToMonoShader);
		if (stereoShaderInfo) {
			bool result = CompilePostShader(stereoShaderInfo, &stereoPipeline_);
			if (result) {
				stereoShaderInfo_ = new ShaderInfo(*stereoShaderInfo);
			}
		} else {
			WARN_LOG(Log::G3D, "Failed to get info about stereo shader '%s'", g_Config.sStereoToMonoShader.c_str());
		}
	}

	std::vector<const ShaderInfo *> shaderInfo;
	if (!g_Config.vPostShaderNames.empty()) {
		ReloadAllPostShaderInfo(draw_);
		shaderInfo = GetFullPostShadersChain(g_Config.vPostShaderNames);
	}

	DestroyPostShader();
	if (shaderInfo.empty()) {
		usePostShader_ = false;
		return false;
	}

	bool usePreviousFrame = false;
	bool usePreviousAtOutputResolution = false;
	for (size_t i = 0; i < shaderInfo.size(); ++i) {
		const ShaderInfo *next = i + 1 < shaderInfo.size() ? shaderInfo[i + 1] : nullptr;
		Draw::Pipeline *postPipeline = nullptr;
		if (!BuildPostShader(shaderInfo[i], next, &postPipeline)) {
			DestroyPostShader();
			return false;
		}
		_dbg_assert_(postPipeline);
		postShaderPipelines_.push_back(postPipeline);
		postShaderInfo_.push_back(*shaderInfo[i]);
		if (shaderInfo[i]->usePreviousFrame) {
			usePreviousFrame = true;
			usePreviousAtOutputResolution = shaderInfo[i]->outputResolution;
		}
	}

	if (usePreviousFrame) {
		int w = usePreviousAtOutputResolution ? pixelWidth_ : renderWidth_;
		int h = usePreviousAtOutputResolution ? pixelHeight_ : renderHeight_;

		_dbg_assert_(w > 0 && h > 0);

		static constexpr int FRAMES = 2;
		previousFramebuffers_.resize(FRAMES);
		previousIndex_ = 0;

		for (int i = 0; i < FRAMES; ++i) {
			previousFramebuffers_[i] = draw_->CreateFramebuffer({ w, h, 1, 1, 0, false, "inter_presentation" });
			if (!previousFramebuffers_[i]) {
				DestroyPostShader();
				return false;
			}
		}
	}

	usePostShader_ = true;
	return true;
}

bool PresentationCommon::CompilePostShader(const ShaderInfo *shaderInfo, Draw::Pipeline **outPipeline) const {
	_assert_(shaderInfo);

	std::string vsSourceGLSL = ReadShaderSrc(shaderInfo->vertexShaderFile);
	std::string fsSourceGLSL = ReadShaderSrc(shaderInfo->fragmentShaderFile);
	if (vsSourceGLSL.empty() || fsSourceGLSL.empty()) {
		return false;
	}

	std::string vsError;
	std::string fsError;

	// All post shaders are written in GLSL 1.0 so that's what we pass in here as a "from" language.
	Draw::ShaderModule *vs = CompileShaderModule(ShaderStage::Vertex, GLSL_1xx, vsSourceGLSL, &vsError);
	Draw::ShaderModule *fs = CompileShaderModule(ShaderStage::Fragment, GLSL_1xx, fsSourceGLSL, &fsError);

	// Don't worry, CompileShaderModule makes sure they get freed if one succeeded.
	if (!fs || !vs) {
		std::string errorString = vsError + "\n" + fsError;
		// DO NOT turn this into an ERROR_LOG_REPORT, as it will pollute our logs with all kinds of
		// user shader experiments.
		ERROR_LOG(Log::FrameBuf, "Failed to build post-processing program from %s and %s!\n%s", shaderInfo->vertexShaderFile.c_str(), shaderInfo->fragmentShaderFile.c_str(), errorString.c_str());
		ShowPostShaderError(errorString);
		return false;
	}

	UniformBufferDesc postShaderDesc{ sizeof(PostShaderUniforms), {
		{ "gl_HalfPixel", 0, -1, UniformType::FLOAT4, offsetof(PostShaderUniforms, gl_HalfPixel) },
		{ "u_texelDelta", 1, 1, UniformType::FLOAT2, offsetof(PostShaderUniforms, texelDelta) },
		{ "u_pixelDelta", 2, 2, UniformType::FLOAT2, offsetof(PostShaderUniforms, pixelDelta) },
		{ "u_time", 3, 3, UniformType::FLOAT4, offsetof(PostShaderUniforms, time) },
		{ "u_timeDelta", 4, 4, UniformType::FLOAT4, offsetof(PostShaderUniforms, timeDelta) },
		{ "u_setting", 5, 5, UniformType::FLOAT4, offsetof(PostShaderUniforms, setting) },
		{ "u_video", 6, 6, UniformType::FLOAT1, offsetof(PostShaderUniforms, video) },
	} };

	Draw::Pipeline *pipeline = CreatePipeline({ vs, fs }, true, &postShaderDesc);

	fs->Release();
	vs->Release();

	if (!pipeline)
		return false;

	*outPipeline = pipeline;
	return true;
}

bool PresentationCommon::BuildPostShader(const ShaderInfo * shaderInfo, const ShaderInfo * next, Draw::Pipeline **outPipeline) {
	if (!CompilePostShader(shaderInfo, outPipeline)) {
		return false;
	}

	if (!shaderInfo->outputResolution || next) {
		int nextWidth = renderWidth_;
		int nextHeight = renderHeight_;

		// When chaining, we use the previous resolution as a base, rather than the render resolution.
		if (!postShaderFramebuffers_.empty())
			draw_->GetFramebufferDimensions(postShaderFramebuffers_.back(), &nextWidth, &nextHeight);

		if (next && next->isUpscalingFilter) {
			// Force 1x for this shader, so the next can upscale.
			const bool isPortrait = g_Config.IsPortrait();
			nextWidth = isPortrait ? 272 : 480;
			nextHeight = isPortrait ? 480 : 272;
		} else if (next && next->SSAAFilterLevel >= 2) {
			// Increase the resolution this shader outputs for the next to SSAA.
			nextWidth *= next->SSAAFilterLevel;
			nextHeight *= next->SSAAFilterLevel;
		} else if (shaderInfo->outputResolution) {
			// If the current shader uses output res (not next), we will use output res for it.
			FRect rc;
			FRect frame = GetScreenFrame((float)pixelWidth_, (float)pixelHeight_);
			CalculateDisplayOutputRect(&rc, 480.0f, 272.0f, frame, g_Config.iInternalScreenRotation);
			nextWidth = (int)rc.w;
			nextHeight = (int)rc.h;
		}

		if (!AllocateFramebuffer(nextWidth, nextHeight)) {
			(*outPipeline)->Release();
			*outPipeline = nullptr;
			return false;
		}
	}

	return true;
}

bool PresentationCommon::AllocateFramebuffer(int w, int h) {
	using namespace Draw;

	// First, let's try to find a framebuffer of the right size that is NOT the most recent.
	Framebuffer *last = postShaderFramebuffers_.empty() ? nullptr : postShaderFramebuffers_.back();
	for (const auto &prev : postShaderFBOUsage_) {
		if (prev.w == w && prev.h == h && prev.fbo != last) {
			// Great, this one's perfect.  Ref it for when we release.
			prev.fbo->AddRef();
			postShaderFramebuffers_.push_back(prev.fbo);
			return true;
		}
	}

	// No depth/stencil for post processing
	Draw::Framebuffer *fbo = draw_->CreateFramebuffer({ w, h, 1, 1, 0, false, "presentation" });
	if (!fbo) {
		return false;
	}

	postShaderFBOUsage_.push_back({ fbo, w, h });
	postShaderFramebuffers_.push_back(fbo);
	return true;
}

void PresentationCommon::ShowPostShaderError(const std::string &errorString) {
	// let's show the first line of the error string as an OSM.
	std::set<std::string> blacklistedLines;
	// These aren't useful to show, skip to the first interesting line.
	blacklistedLines.insert("Fragment shader failed to compile with the following errors:");
	blacklistedLines.insert("Vertex shader failed to compile with the following errors:");
	blacklistedLines.insert("Compile failed.");
	blacklistedLines.insert("");

	std::string firstLine;
	size_t start = 0;
	for (size_t i = 0; i < errorString.size(); i++) {
		if (errorString[i] == '\n' && i == start) {
			start = i + 1;
		} else if (errorString[i] == '\n') {
			firstLine = errorString.substr(start, i - start);
			if (blacklistedLines.find(firstLine) == blacklistedLines.end()) {
				break;
			}
			start = i + 1;
			firstLine.clear();
		}
	}
	if (!firstLine.empty()) {
		g_OSD.Show(OSDType::MESSAGE_ERROR_DUMP, "Post-shader error: " + firstLine + "...:\n" + errorString, 10.0f);
	} else {
		g_OSD.Show(OSDType::MESSAGE_ERROR, "Post-shader error, see log for details", 10.0f);
	}
}

void PresentationCommon::DeviceLost() {
	DestroyDeviceObjects();
	draw_ = nullptr;
}

void PresentationCommon::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	CreateDeviceObjects();
}

Draw::Pipeline *PresentationCommon::CreatePipeline(std::vector<Draw::ShaderModule *> shaders, bool postShader, const UniformBufferDesc *uniformDesc) const {
	using namespace Draw;

	Semantic pos = SEM_POSITION;
	Semantic tc = SEM_TEXCOORD0;
	// Shader translation marks these both as "TEXCOORDs" on HLSL...
	if (postShader && lang_ == HLSL_D3D11) {
		pos = SEM_TEXCOORD0;
		tc = SEM_TEXCOORD1;
	}

	// TODO: Maybe get rid of color0.
	InputLayoutDesc inputDesc = {
		sizeof(Vertex),
		{
			{ pos, DataFormat::R32G32B32_FLOAT, 0 },
			{ tc, DataFormat::R32G32_FLOAT, 12 },
			{ SEM_COLOR0, DataFormat::R8G8B8A8_UNORM, 20 },
		},
	};

	InputLayout *inputLayout = draw_->CreateInputLayout(inputDesc);
	DepthStencilState *depth = draw_->CreateDepthStencilState({ false, false, Comparison::LESS });
	BlendState *blendstateOff = draw_->CreateBlendState({ false, 0xF });
	RasterState *rasterNoCull = draw_->CreateRasterState({});

	PipelineDesc pipelineDesc{ Primitive::TRIANGLE_STRIP, shaders, inputLayout, depth, blendstateOff, rasterNoCull, uniformDesc };
	Pipeline *pipeline = draw_->CreateGraphicsPipeline(pipelineDesc, "presentation");

	inputLayout->Release();
	depth->Release();
	blendstateOff->Release();
	rasterNoCull->Release();

	return pipeline;
}

void PresentationCommon::CreateDeviceObjects() {
	using namespace Draw;
	_assert_(vdata_ == nullptr);

	// TODO: Could probably just switch to DrawUP, it's supported well by all backends now.
	vdata_ = draw_->CreateBuffer(sizeof(Vertex) * 12, BufferUsageFlag::DYNAMIC | BufferUsageFlag::VERTEXDATA);

	samplerNearest_ = draw_->CreateSamplerState({ TextureFilter::NEAREST, TextureFilter::NEAREST, TextureFilter::NEAREST, 0.0f, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE });
	samplerLinear_ = draw_->CreateSamplerState({ TextureFilter::LINEAR, TextureFilter::LINEAR, TextureFilter::LINEAR, 0.0f, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE, TextureAddressMode::CLAMP_TO_EDGE });

	texColor_ = CreatePipeline({ draw_->GetVshaderPreset(VS_TEXTURE_COLOR_2D), draw_->GetFshaderPreset(FS_TEXTURE_COLOR_2D) }, false, &vsTexColBufDesc);
	texColorRBSwizzle_ = CreatePipeline({ draw_->GetVshaderPreset(VS_TEXTURE_COLOR_2D), draw_->GetFshaderPreset(FS_TEXTURE_COLOR_2D_RB_SWIZZLE) }, false, &vsTexColBufDesc);

	if (restorePostShader_)
		UpdatePostShader();
	restorePostShader_ = false;
}

template <typename T>
static void DoRelease(T *&obj) {
	if (obj)
		obj->Release();
	obj = nullptr;
}

template <typename T>
static void DoReleaseVector(std::vector<T *> &list) {
	for (auto &obj : list)
		obj->Release();
	list.clear();
}

void PresentationCommon::DestroyDeviceObjects() {
	DoRelease(texColor_);
	DoRelease(texColorRBSwizzle_);
	DoRelease(samplerNearest_);
	DoRelease(samplerLinear_);
	DoRelease(vdata_);
	DoRelease(srcTexture_);
	DoRelease(srcFramebuffer_);

	restorePostShader_ = usePostShader_;
	DestroyPostShader();
	DestroyStereoShader();
}

void PresentationCommon::DestroyPostShader() {
	usePostShader_ = false;

	DoReleaseVector(postShaderPipelines_);
	DoReleaseVector(postShaderFramebuffers_);
	DoReleaseVector(previousFramebuffers_);
	postShaderInfo_.clear();
	postShaderFBOUsage_.clear();
}

void PresentationCommon::DestroyStereoShader() {
	DoRelease(stereoPipeline_);
	delete stereoShaderInfo_;
	stereoShaderInfo_ = nullptr;
}

Draw::ShaderModule *PresentationCommon::CompileShaderModule(ShaderStage stage, ShaderLanguage lang, const std::string &src, std::string *errorString) const {
	std::string translated = src;
	if (lang != lang_) {
		// Gonna have to upconvert the shader.
		if (!TranslateShader(&translated, lang_, draw_->GetShaderLanguageDesc(), nullptr, src, lang, stage, errorString)) {
			ERROR_LOG(Log::FrameBuf, "Failed to translate post-shader. Error string: '%s'\nSource code:\n%s\n", errorString->c_str(), src.c_str());
			return nullptr;
		}
	}
	Draw::ShaderModule *shader = draw_->CreateShaderModule(stage, lang_, (const uint8_t *)translated.c_str(), translated.size(), "postshader");
	return shader;
}

void PresentationCommon::SourceTexture(Draw::Texture *texture, int bufferWidth, int bufferHeight) {
	// AddRef before release and assign in case it's the same.
	texture->AddRef();

	DoRelease(srcTexture_);
	DoRelease(srcFramebuffer_);

	srcTexture_ = texture;
	srcWidth_ = bufferWidth;
	srcHeight_ = bufferHeight;
}

void PresentationCommon::SourceFramebuffer(Draw::Framebuffer *fb, int bufferWidth, int bufferHeight) {
	fb->AddRef();

	DoRelease(srcTexture_);
	DoRelease(srcFramebuffer_);

	srcFramebuffer_ = fb;
	srcWidth_ = bufferWidth;
	srcHeight_ = bufferHeight;
}

// Return value is if stereo binding succeeded.
bool PresentationCommon::BindSource(int binding, bool bindStereo) {
	if (srcTexture_) {
		draw_->BindTexture(binding, srcTexture_);
		return false;
	} else if (srcFramebuffer_) {
		if (bindStereo) {
			if (srcFramebuffer_->Layers() > 1) {
				draw_->BindFramebufferAsTexture(srcFramebuffer_, binding, Draw::Aspect::COLOR_BIT, Draw::ALL_LAYERS);
				return true;
			} else {
				// Single layer. This might be from a post shader and those don't yet support stereo.
				draw_->BindFramebufferAsTexture(srcFramebuffer_, binding, Draw::Aspect::COLOR_BIT, 0);
				return false;
			}
		} else {
			draw_->BindFramebufferAsTexture(srcFramebuffer_, binding, Draw::Aspect::COLOR_BIT, 0);
			return false;
		}
	} else {
		_assert_(false);
		return false;
	}
}

void PresentationCommon::UpdateUniforms(bool hasVideo) {
	hasVideo_ = hasVideo;
}

void PresentationCommon::CopyToOutput(OutputFlags flags, int uvRotation, float u0, float v0, float u1, float v1) {
	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);

	// TODO: If shader objects have been created by now, we might have received errors.
	// GLES can have the shader fail later, shader->failed / shader->error.
	// This should auto-disable usePostShader_ and call ShowPostShaderError().

	bool useNearest = flags & OutputFlags::NEAREST;
	bool useStereo = gstate_c.Use(GPU_USE_SIMPLE_STEREO_PERSPECTIVE) && stereoPipeline_ != nullptr;  // TODO: Also check that the backend has support for it.

	const bool usePostShader = usePostShader_ && !useStereo && !(flags & OutputFlags::RB_SWIZZLE);
	const bool isFinalAtOutputResolution = usePostShader && postShaderFramebuffers_.size() < postShaderPipelines_.size();
	Draw::Framebuffer *postShaderOutput = nullptr;
	int lastWidth = srcWidth_;
	int lastHeight = srcHeight_;

	int pixelWidth = pixelWidth_;
	int pixelHeight = pixelHeight_;

	// These are the output coordinates.
	FRect frame = GetScreenFrame((float)pixelWidth, (float)pixelHeight);
	// Note: In cardboard mode, we halve the width here to compensate
	// for splitting the window in half, while still reusing normal centering.
	if (g_Config.bEnableCardboardVR) {
		frame.w /= 2.0;
		pixelWidth /= 2;
	}
	FRect rc;
	CalculateDisplayOutputRect(&rc, 480.0f, 272.0f, frame, uvRotation);

	// To make buffer updates easier, we use one array of verts.
	int postVertsOffset = (int)sizeof(Vertex) * 4;

	float finalU0 = u0, finalU1 = u1, finalV0 = v0, finalV1 = v1;

	if (usePostShader && !(isFinalAtOutputResolution && postShaderPipelines_.size() == 1)) {
		// The final blit will thus use the full texture.
		finalU0 = 0.0f;
		finalV0 = 0.0f;
		finalU1 = 1.0f;
		finalV1 = 1.0f;
	}

	// Our vertex buffer is split into three parts, with four vertices each:
	// 0-3: The final blit vertices (needs to handle cropping the input ONLY if post-processing is not enabled)
	// 4-7: Post-processing, other passes
	// 8-11: Post-processing, first pass (needs to handle cropping the input image, if wrong dimensions)
	Vertex verts[12] = {
		{ rc.x, rc.y, 0, finalU0, finalV0, 0xFFFFFFFF }, // TL
		{ rc.x + rc.w, rc.y, 0, finalU1, finalV0, 0xFFFFFFFF }, // TR
		{ rc.x, rc.y + rc.h, 0, finalU0, finalV1, 0xFFFFFFFF }, // BL
		{ rc.x + rc.w, rc.y + rc.h, 0, finalU1, finalV1, 0xFFFFFFFF }, // BR
	};

	// Rescale X, Y to normalized coordinate system.
	float invDestW = 2.0f / pixelWidth;
	float invDestH = 2.0f / pixelHeight;
	for (int i = 0; i < 4; i++) {
		verts[i].x = verts[i].x * invDestW - 1.0f;
		verts[i].y = verts[i].y * invDestH - 1.0f;
	}

	if (uvRotation != ROTATION_LOCKED_HORIZONTAL) {
		struct {
			float u;
			float v;
		} temp[4];
		int rotation = 0;
		// Vertical and Vertical180 needed swapping after we changed the coordinate system.
		switch (uvRotation) {
		case ROTATION_LOCKED_HORIZONTAL180: rotation = 2; break;
		case ROTATION_LOCKED_VERTICAL: rotation = 3; break;
		case ROTATION_LOCKED_VERTICAL180: rotation = 1; break;
		}

		// If we flipped, we rotate the other way.
		if ((flags & OutputFlags::BACKBUFFER_FLIPPED) || (flags & OutputFlags::POSITION_FLIPPED)) {
			if ((rotation & 1) != 0)
				rotation ^= 2;
		}

		static int rotLookup[4] = { 0, 1, 3, 2 };

		for (int i = 0; i < 4; i++) {
			int otherI = rotLookup[(rotLookup[i] + rotation) & 3];
			temp[i].u = verts[otherI].u;
			temp[i].v = verts[otherI].v;
		}
		for (int i = 0; i < 4; i++) {
			verts[i].u = temp[i].u;
			verts[i].v = temp[i].v;
		}
	}

	if (isFinalAtOutputResolution || useStereo) {
		// In this mode, we ignore the g_display_rot_matrix.  Apply manually.
		if (g_display.rotation != DisplayRotation::ROTATE_0) {
			for (int i = 0; i < 4; i++) {
				Lin::Vec3 v(verts[i].x, verts[i].y, verts[i].z);
				// Backwards notation, should fix that...
				v = v * g_display.rot_matrix;
				verts[i].x = v.x;
				verts[i].y = v.y;
			}
		}
	}

	if (flags & OutputFlags::PILLARBOX) {
		for (int i = 0; i < 4; i++) {
			// Looks about right.
			verts[i].x *= 0.75f;
		}
	}

	// Finally, we compensate the y vertex positions for the backbuffer for any flipping.
	if ((flags & OutputFlags::POSITION_FLIPPED) || (flags & OutputFlags::BACKBUFFER_FLIPPED)) {
		for (int i = 0; i < 4; i++) {
			verts[i].y = -verts[i].y;
		}
	}

	// Grab the previous framebuffer early so we can change previousIndex_ when we want.
	Draw::Framebuffer *previousFramebuffer = previousFramebuffers_.empty() ? nullptr : previousFramebuffers_[previousIndex_];

	PostShaderUniforms uniforms;
	const auto performShaderPass = [&](const ShaderInfo *shaderInfo, Draw::Framebuffer *postShaderFramebuffer, Draw::Pipeline *postShaderPipeline, int vertsOffset) {
		if (postShaderOutput) {
			draw_->BindFramebufferAsTexture(postShaderOutput, 0, Draw::Aspect::COLOR_BIT, 0);
		} else {
			BindSource(0, false);
		}
		BindSource(1, false);
		if (shaderInfo->usePreviousFrame)
			draw_->BindFramebufferAsTexture(previousFramebuffer, 2, Draw::Aspect::COLOR_BIT, 0);

		int nextWidth, nextHeight;
		draw_->GetFramebufferDimensions(postShaderFramebuffer, &nextWidth, &nextHeight);
		Draw::Viewport viewport{ 0, 0, (float)nextWidth, (float)nextHeight, 0.0f, 1.0f };
		draw_->SetViewport(viewport);
		draw_->SetScissorRect(0, 0, nextWidth, nextHeight);

		CalculatePostShaderUniforms(lastWidth, lastHeight, nextWidth, nextHeight, shaderInfo, &uniforms);

		draw_->BindPipeline(postShaderPipeline);
		draw_->UpdateDynamicUniformBuffer(&uniforms, sizeof(uniforms));

		Draw::SamplerState *sampler = useNearest || shaderInfo->isUpscalingFilter ? samplerNearest_ : samplerLinear_;
		draw_->BindSamplerStates(0, 1, &sampler);
		draw_->BindSamplerStates(1, 1, &sampler);
		if (shaderInfo->usePreviousFrame)
			draw_->BindSamplerStates(2, 1, &sampler);

		draw_->BindVertexBuffer(vdata_, vertsOffset);
		draw_->Draw(4, 0);

		postShaderOutput = postShaderFramebuffer;
		lastWidth = nextWidth;
		lastHeight = nextHeight;
	};

	if (usePostShader) {
		// When we render to temp framebuffers during post, we switch position, not UV.
		// The flipping here is only because D3D has a clip coordinate system that doesn't match their screen coordinate system.
		// The flipping here is only because D3D has a clip coordinate system that doesn't match their screen coordinate system.
		bool flipped = flags & OutputFlags::POSITION_FLIPPED;
		float y0 = flipped ? 1.0f : -1.0f;
		float y1 = flipped ? -1.0f : 1.0f;
		verts[4] = { -1.0f, y0, 0.0f, 0.0f, 0.0f, 0xFFFFFFFF }; // TL
		verts[5] = {  1.0f, y0, 0.0f, 1.0f, 0.0f, 0xFFFFFFFF }; // TR
		verts[6] = { -1.0f, y1, 0.0f, 0.0f, 1.0f, 0xFFFFFFFF }; // BL
		verts[7] = {  1.0f, y1, 0.0f, 1.0f, 1.0f, 0xFFFFFFFF }; // BR

		// Now, adjust for the desired input rectangle.
		verts[8]  = { -1.0f, y0, 0.0f, u0, v0, 0xFFFFFFFF }; // TL
		verts[9]  = {  1.0f, y0, 0.0f, u1, v0, 0xFFFFFFFF }; // TR
		verts[10] = { -1.0f, y1, 0.0f, u0, v1, 0xFFFFFFFF }; // BL
		verts[11] = {  1.0f, y1, 0.0f, u1, v1, 0xFFFFFFFF }; // BR

		draw_->UpdateBuffer(vdata_, (const uint8_t *)verts, 0, sizeof(verts), Draw::UPDATE_DISCARD);

		for (size_t i = 0; i < postShaderFramebuffers_.size(); ++i) {
			Draw::Pipeline *postShaderPipeline = postShaderPipelines_[i];
			const ShaderInfo *shaderInfo = &postShaderInfo_[i];
			Draw::Framebuffer *postShaderFramebuffer = postShaderFramebuffers_[i];
			if (!isFinalAtOutputResolution && i == postShaderFramebuffers_.size() - 1 && !previousFramebuffers_.empty()) {
				// This is the last pass and we're going direct to the backbuffer after this.
				// Redirect output to a separate framebuffer to keep the previous frame.
				previousIndex_++;
				if (previousIndex_ >= (int)previousFramebuffers_.size())
					previousIndex_ = 0;
				postShaderFramebuffer = previousFramebuffers_[previousIndex_];
			}

			draw_->BindFramebufferAsRenderTarget(postShaderFramebuffer, { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "PostShader");

			// Pick vertices 8-11 for the first pass.
			int vertOffset = i == 0 ? (int)sizeof(Vertex) * 8 : (int)sizeof(Vertex) * 4;
			performShaderPass(shaderInfo, postShaderFramebuffer, postShaderPipeline, vertOffset);
		}

		if (isFinalAtOutputResolution && postShaderInfo_.back().isUpscalingFilter)
			useNearest = true;
	} else {
		// Only need to update the first four verts, the rest are unused.
		draw_->UpdateBuffer(vdata_, (const uint8_t *)verts, 0, postVertsOffset, Draw::UPDATE_DISCARD);
	}

	// If we need to save the previous frame, we have to save any final pass in a framebuffer.
	if (isFinalAtOutputResolution && !previousFramebuffers_.empty()) {
		Draw::Pipeline *postShaderPipeline = postShaderPipelines_.back();
		const ShaderInfo *shaderInfo = &postShaderInfo_.back();

		// Pick the next to render to.
		previousIndex_++;
		if (previousIndex_ >= (int)previousFramebuffers_.size())
			previousIndex_ = 0;
		Draw::Framebuffer *postShaderFramebuffer = previousFramebuffers_[previousIndex_];

		draw_->BindFramebufferAsRenderTarget(postShaderFramebuffer, { Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "InterFrameBlit");
		performShaderPass(shaderInfo, postShaderFramebuffer, postShaderPipeline, postVertsOffset);
	}

	draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "FinalBlit");
	draw_->SetScissorRect(0, 0, pixelWidth_, pixelHeight_);

	Draw::Pipeline *pipeline = (flags & OutputFlags::RB_SWIZZLE) ? texColorRBSwizzle_ : texColor_;

	if (useStereo) {
		draw_->BindPipeline(stereoPipeline_);
		if (!BindSource(0, true)) {
			// Fall back
			draw_->BindPipeline(texColor_);
			useStereo = false;  // Otherwise we end up uploading the wrong uniforms
		}
	} else {
		if (isFinalAtOutputResolution && previousFramebuffers_.empty()) {
			pipeline = postShaderPipelines_.back();
		}

		draw_->BindPipeline(pipeline);
		if (postShaderOutput) {
			draw_->BindFramebufferAsTexture(postShaderOutput, 0, Draw::Aspect::COLOR_BIT, 0);
		} else {
			BindSource(0, false);
		}
	}
	BindSource(1, false);

	if (isFinalAtOutputResolution && previousFramebuffers_.empty()) {
		CalculatePostShaderUniforms(lastWidth, lastHeight, (int)rc.w, (int)rc.h, &postShaderInfo_.back(), &uniforms);
		draw_->UpdateDynamicUniformBuffer(&uniforms, sizeof(uniforms));
	} else if (useStereo) {
		CalculatePostShaderUniforms(lastWidth, lastHeight, (int)rc.w, (int)rc.h, stereoShaderInfo_, &uniforms);
		draw_->UpdateDynamicUniformBuffer(&uniforms, sizeof(uniforms));
	} else {
		Draw::VsTexColUB ub{};
		memcpy(ub.WorldViewProj, g_display.rot_matrix.m, sizeof(float) * 16);
		draw_->UpdateDynamicUniformBuffer(&ub, sizeof(ub));
	}

	draw_->BindVertexBuffer(vdata_, 0);

	Draw::SamplerState *sampler = useNearest ? samplerNearest_ : samplerLinear_;
	draw_->BindSamplerStates(0, 1, &sampler);
	draw_->BindSamplerStates(1, 1, &sampler);

	auto setViewport = [&](float x, float y, float w, float h) {
		Draw::Viewport viewport{ x, y, w, h, 0.0f, 1.0f };
		draw_->SetViewport(viewport);
	};

	CardboardSettings cardboardSettings;
	GetCardboardSettings(&cardboardSettings);
	if (cardboardSettings.enabled) {
		// TODO: This could actually support stereo now, with an appropriate shader.

		// This is what the left eye sees.
		setViewport(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
		draw_->Draw(4, 0);

		// And this is the right eye, unless they're a pirate.
		setViewport(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
		draw_->Draw(4, 0);
	} else {
		setViewport(0.0f, 0.0f, (float)pixelWidth_, (float)pixelHeight_);
		draw_->Draw(4, 0);
	}

	DoRelease(srcFramebuffer_);
	DoRelease(srcTexture_);

	// Unbinds all textures and samplers too, needed since sometimes a MakePixelTexture is deleted etc.
	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);

	previousUniforms_ = uniforms;
	presentedThisFrame_ = true;
}

void PresentationCommon::CalculateRenderResolution(int *width, int *height, int *scaleFactor, bool *upscaling, bool *ssaa) const {
	// Check if postprocessing shader is doing upscaling as it requires native resolution
	std::vector<const ShaderInfo *> shaderInfo;
	if (!g_Config.vPostShaderNames.empty()) {
		ReloadAllPostShaderInfo(draw_);
		RemoveUnknownPostShaders(&g_Config.vPostShaderNames);
		FixPostShaderOrder(&g_Config.vPostShaderNames);
		shaderInfo = GetFullPostShadersChain(g_Config.vPostShaderNames);
	}

	bool firstIsUpscalingFilter = shaderInfo.empty() ? false : shaderInfo.front()->isUpscalingFilter;
	int firstSSAAFilterLevel = shaderInfo.empty() ? 0 : shaderInfo.front()->SSAAFilterLevel;

	// In auto mode (zoom == 0), round up to an integer zoom factor for the render size.
	int zoom = g_Config.iInternalResolution;
	if (zoom == 0 || firstSSAAFilterLevel >= 2) {
		// auto mode, use the longest dimension
		if (!g_Config.IsPortrait()) {
			zoom = (PSP_CoreParameter().pixelWidth + 479) / 480;
		} else {
			zoom = (PSP_CoreParameter().pixelHeight + 479) / 480;
		}
		if (firstSSAAFilterLevel >= 2)
			zoom *= firstSSAAFilterLevel;
	}
	if (zoom <= 1 || firstIsUpscalingFilter)
		zoom = 1;

	if (upscaling) {
		*upscaling = firstIsUpscalingFilter;
		for (auto &info : shaderInfo) {
			*upscaling = *upscaling || info->isUpscalingFilter;
		}
	}
	if (ssaa) {
		*ssaa = firstSSAAFilterLevel >= 2;
		for (auto &info : shaderInfo) {
			*ssaa = *ssaa || info->SSAAFilterLevel >= 2;
		}
	}

	if (IsVREnabled()) {
		*width = 480 * zoom;
		*height = 480 * zoom;
	} else {
		// Note: We previously checked g_Config.IsPortrait (internal rotation) here but that was wrong -
		// we still render at 480x272 * zoom.
		*width = 480 * zoom;
		*height = 272 * zoom;
	}

	*scaleFactor = zoom;
}
