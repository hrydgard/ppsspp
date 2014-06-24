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

#include <set>
#include <algorithm>

#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"
#include "gfx_es2/fbo.h"

#include "base/timeutil.h"
#include "math/lin/matrix4x4.h"

#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/HLE/sceDisplay.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#include "GPU/Common/PostShader.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/TransformPipeline.h"
#include "GPU/GLES/ShaderManager.h"

#include "UI/OnScreenDisplay.h"

#if defined(USING_GLES2)
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER GL_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER GL_FRAMEBUFFER
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 GL_RGBA
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 GL_DEPTH_COMPONENT24_OES
#endif
#ifndef GL_DEPTH24_STENCIL8_OES
#define GL_DEPTH24_STENCIL8_OES 0x88F0
#endif
#endif

extern int g_iNumVideos;

static const char tex_fs[] =
#ifdef USING_GLES2
	"precision mediump float;\n"
#endif
	"uniform sampler2D sampler0;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"  gl_FragColor = texture2D(sampler0, v_texcoord0);\n"
	"}\n";

static const char basic_vs[] =
	"attribute vec4 a_position;\n"
	"attribute vec2 a_texcoord0;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"  v_texcoord0 = a_texcoord0;\n"
	"  gl_Position = a_position;\n"
	"}\n";

static const char color_fs[] =
#ifdef USING_GLES2
	"precision mediump float;\n"
#endif
	"uniform vec4 u_color;\n"
	"void main() {\n"
	"  gl_FragColor.rgba = u_color;\n"
	"}\n";

static const char color_vs[] =
	"attribute vec4 a_position;\n"
	"void main() {\n"
	"  gl_Position = a_position;\n"
	"}\n";

// Aggressively delete unused FBO:s to save gpu memory.
enum {
	FBO_OLD_AGE = 5,
};

bool FramebufferManager::MaskedEqual(u32 addr1, u32 addr2) {
	return (addr1 & 0x03FFFFFF) == (addr2 & 0x03FFFFFF);
}

inline u16 RGBA8888toRGB565(u32 px) {
	return ((px >> 3) & 0x001F) | ((px >> 5) & 0x07E0) | ((px >> 8) & 0xF800);
}

inline u16 RGBA8888toRGBA4444(u32 px) {
	return ((px >> 4) & 0x000F) | ((px >> 8) & 0x00F0) | ((px >> 12) & 0x0F00) | ((px >> 16) & 0xF000);
}

inline u16 BGRA8888toRGB565(u32 px) {
	return ((px >> 19) & 0x001F) | ((px >> 5) & 0x07E0) | ((px << 8) & 0xF800);
}

inline u16 BGRA8888toRGBA4444(u32 px) {
	return ((px >> 20) & 0x000F) | ((px >> 8) & 0x00F0) | ((px << 4) & 0x0F00) | ((px >> 16) & 0xF000);
}

void ConvertFromRGBA8888(u8 *dst, const u8 *src, u32 stride, u32 width, u32 height, GEBufferFormat format);

void CenterRect(float *x, float *y, float *w, float *h,
                float origW, float origH, float frameW, float frameH) {
	float outW;
	float outH;

	if (g_Config.bStretchToDisplay) {
		outW = frameW;
		outH = frameH;
	} else {
		// Add special case for 1080p displays, cutting off the bottom and top 1-pixel rows from the original 480x272.
		// This will be what 99.9% of users want.
		if (origW == 480 && origH == 272 && frameW == 1920 && frameH == 1080) {
			*x = 0;
			*y = -4;
			*w = 1920;
			*h = 1088;
			return;
		}

		float origRatio = origW / origH;
		float frameRatio = frameW / frameH;
		if (origRatio > frameRatio) {
			// Image is wider than frame. Center vertically.
			outW = frameW;
			outH = frameW / origRatio;
			// Stretch a little bit
			if (g_Config.bPartialStretch)
				outH = (frameH + outH) / 2.0f; // (408 + 720) / 2 = 564
		}
		else {
			// Image is taller than frame. Center horizontally.
			outW = frameH * origRatio;
			outH = frameH;
		}
	}

	if (g_Config.bSmallDisplay) {
		outW /= 2.0f;
		outH /= 2.0f;
	}

	*x = (frameW - outW) / 2.0f;
	*y = (frameH - outH) / 2.0f;
	*w = outW;
	*h = outH;
}

void FramebufferManager::ClearBuffer() {
	glstate.scissorTest.disable();
	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glstate.stencilFunc.set(GL_ALWAYS, 0, 0);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearStencil(0);
#ifdef USING_GLES2
	glClearDepthf(0.0f);
#else
	glClearDepth(0.0);
#endif
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void FramebufferManager::ClearDepthBuffer() {
	glstate.scissorTest.disable();
	glstate.depthWrite.set(GL_TRUE);
#ifdef USING_GLES2
	glClearDepthf(0.0f);
#else
	glClearDepth(0.0);
#endif
	glClear(GL_DEPTH_BUFFER_BIT);
}

void FramebufferManager::DisableState() {
	glstate.blend.disable();
	glstate.cullFace.disable();
	glstate.depthTest.disable();
	glstate.scissorTest.disable();
	glstate.stencilTest.disable();
#if !defined(USING_GLES2)
	glstate.colorLogicOp.disable();
#endif
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void FramebufferManager::SetNumExtraFBOs(int num) {
	for (size_t i = 0; i < extraFBOs_.size(); i++) {
		fbo_destroy(extraFBOs_[i]);
	}
	extraFBOs_.clear();
	for (int i = 0; i < num; i++) {
		// No depth/stencil for post processing
		FBO *fbo = fbo_create(PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight, 1, false, FBO_8888);
		extraFBOs_.push_back(fbo);

		// The new FBO is still bound after creation.
		ClearBuffer();
	}

	currentRenderVfb_ = 0;
	fbo_unbind();
}

void FramebufferManager::CompileDraw2DProgram() {
	if (!draw2dprogram_) {
		std::string errorString;
		draw2dprogram_ = glsl_create_source(basic_vs, tex_fs, &errorString);
		if (!draw2dprogram_) {
			ERROR_LOG_REPORT(G3D, "Failed to compile draw2dprogram! This shouldn't happen.\n%s", errorString.c_str());
		} else {
			glsl_bind(draw2dprogram_);
			glUniform1i(draw2dprogram_->sampler0, 0);
		}

		plainColorProgram_ = glsl_create_source(color_vs, color_fs, &errorString);
		if (!plainColorProgram_) {
			ERROR_LOG_REPORT(G3D, "Failed to compile plainColorProgram! This shouldn't happen.\n%s", errorString.c_str());
		} else {
			glsl_bind(plainColorProgram_);
			plainColorLoc_ = glsl_uniform_loc(plainColorProgram_, "u_color");
		}

		SetNumExtraFBOs(0);
		const ShaderInfo *shaderInfo = 0;
		if (g_Config.sPostShaderName != "Off") {
			shaderInfo = GetPostShaderInfo(g_Config.sPostShaderName);
		}

		if (shaderInfo) {
			postShaderAtOutputResolution_ = shaderInfo->outputResolution;
			postShaderProgram_ = glsl_create(shaderInfo->vertexShaderFile.c_str(), shaderInfo->fragmentShaderFile.c_str(), &errorString);
			if (!postShaderProgram_) {
				// DO NOT turn this into a report, as it will pollute our logs with all kinds of
				// user shader experiments.
				ERROR_LOG(G3D, "Failed to build post-processing program from %s and %s!\n%s", shaderInfo->vertexShaderFile.c_str(), shaderInfo->fragmentShaderFile.c_str(), errorString.c_str());
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
					if (errorString[i] == '\n') {
						firstLine = errorString.substr(start, i - start);
						if (blacklistedLines.find(firstLine) == blacklistedLines.end()) {
							break;
						}
						start = i + 1;
						firstLine.clear();
					}
				}
				if (!firstLine.empty()) {
					osm.Show("Post-shader error: " + firstLine + "...", 10.0f, 0xFF3090FF);
				} else {
					osm.Show("Post-shader error, see log for details", 10.0f, 0xFF3090FF);
				}
				usePostShader_ = false;
			} else {
				glsl_bind(postShaderProgram_);
				glUniform1i(postShaderProgram_->sampler0, 0);
				SetNumExtraFBOs(1);
				float u_delta = 1.0f / PSP_CoreParameter().renderWidth;
				float v_delta = 1.0f / PSP_CoreParameter().renderHeight;
				float u_pixel_delta = u_delta;
				float v_pixel_delta = v_delta;
				if (postShaderAtOutputResolution_) {
					float x, y, w, h;
					CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight);
					u_pixel_delta = 1.0f / w;
					v_pixel_delta = 1.0f / h;
				}

				int deltaLoc = glsl_uniform_loc(postShaderProgram_, "u_texelDelta");
				if (deltaLoc != -1)
					glUniform2f(deltaLoc, u_delta, v_delta);
				int pixelDeltaLoc = glsl_uniform_loc(postShaderProgram_, "u_pixelDelta");
				if (pixelDeltaLoc != -1)
					glUniform2f(pixelDeltaLoc, u_pixel_delta, v_pixel_delta);
				timeLoc_ = glsl_uniform_loc(postShaderProgram_, "u_time");
				if (timeLoc_ != -1)
					glUniform4f(timeLoc_, 0.0f, 0.0f, 0.0f, 0.0f);

				usePostShader_ = true;
			}
		} else {
			postShaderProgram_ = 0;
			usePostShader_ = false;
		}

		glsl_unbind();
	}
}

void FramebufferManager::DestroyDraw2DProgram() {
	if (draw2dprogram_) {
		glsl_destroy(draw2dprogram_);
		draw2dprogram_ = 0;
	}
	if (plainColorProgram_) {
		glsl_destroy(plainColorProgram_);
		plainColorProgram_ = 0;
	}
	if (postShaderProgram_) {
		glsl_destroy(postShaderProgram_);
		postShaderProgram_ = 0;
	}
}

FramebufferManager::FramebufferManager() :
	displayFramebufPtr_(0),
	displayStride_(0),
	displayFormat_(GE_FORMAT_565),
	displayFramebuf_(0),
	prevDisplayFramebuf_(0),
	prevPrevDisplayFramebuf_(0),
	frameLastFramebufUsed(0),
	currentRenderVfb_(0),
	drawPixelsTex_(0),
	drawPixelsTexFormat_(GE_FORMAT_INVALID),
	convBuf_(0),
	draw2dprogram_(0),
	postShaderProgram_(0),
	stencilUploadProgram_(0),
	plainColorLoc_(-1),
	timeLoc_(-1),
	textureCache_(0),
	shaderManager_(0),
	usePostShader_(false),
	postShaderAtOutputResolution_(false),
	resized_(false),
	gameUsesSequentialCopies_(false),
	framebufRangeEnd_(0)
#ifndef USING_GLES2
	,
	pixelBufObj_(0),
	currentPBO_(0)
#endif
{
}

void FramebufferManager::Init() {
	CompileDraw2DProgram();

	const std::string gameId = g_paramSFO.GetValueString("DISC_ID");
	// This applies a hack to Dangan Ronpa, its demo, and its sequel.
	// The game draws solid colors to a small framebuffer, and then reads this directly in VRAM.
	// We force this framebuffer to 1x and force download it automatically.
	hackForce04154000Download_ = gameId == "NPJH50631" || gameId == "NPJH50372" || gameId == "NPJH90164" || gameId == "NPJH50515";

	// And an initial clear. We don't clear per frame as the games are supposed to handle that
	// by themselves.
	ClearBuffer();

	SetLineWidth();
	BeginFrame();
}

FramebufferManager::~FramebufferManager() {
	if (drawPixelsTex_)
		glDeleteTextures(1, &drawPixelsTex_);
	if (draw2dprogram_) {
		glsl_destroy(draw2dprogram_);
	}
	if (stencilUploadProgram_) {
		glsl_destroy(stencilUploadProgram_);
	}
	SetNumExtraFBOs(0);

	for (auto it = tempFBOs_.begin(), end = tempFBOs_.end(); it != end; ++it) {
		fbo_destroy(it->second.fbo);
	}

#ifndef USING_GLES2
	delete [] pixelBufObj_;
#endif
	delete [] convBuf_;
}

void FramebufferManager::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	if (drawPixelsTex_ && (drawPixelsTexFormat_ != srcPixelFormat || drawPixelsTexW_ != width || drawPixelsTexH_ != height)) {
		glDeleteTextures(1, &drawPixelsTex_);
		drawPixelsTex_ = 0;
	}

	if (!drawPixelsTex_) {
		drawPixelsTex_ = textureCache_->AllocTextureName();
		drawPixelsTexW_ = width;
		drawPixelsTexH_ = height;

		// Initialize backbuffer texture for DrawPixels
		glBindTexture(GL_TEXTURE_2D, drawPixelsTex_);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		drawPixelsTexFormat_ = srcPixelFormat;
	} else {
		glBindTexture(GL_TEXTURE_2D, drawPixelsTex_);
	}

	// TODO: We can just change the texture format and flip some bits around instead of this.
	// Could share code with the texture cache perhaps.
	bool useConvBuf = false;
	if (srcPixelFormat != GE_FORMAT_8888 || srcStride != width) {
		useConvBuf = true;
		u32 neededSize = width * height * 4;
		if (!convBuf_ || convBufSize_ < neededSize) {
			delete [] convBuf_;
			convBuf_ = new u8[neededSize];
			convBufSize_ = neededSize;
		}
		for (int y = 0; y < height; y++) {
			switch (srcPixelFormat) {
			case GE_FORMAT_565:
				{
					const u16 *src = (const u16 *)srcPixels + srcStride * y;
					u8 *dst = convBuf_ + 4 * width * y;
					for (int x = 0; x < width; x++)
					{
						u16 col = src[x];
						dst[x * 4] = Convert5To8((col) & 0x1f);
						dst[x * 4 + 1] = Convert6To8((col >> 5) & 0x3f);
						dst[x * 4 + 2] = Convert5To8((col >> 11) & 0x1f);
						dst[x * 4 + 3] = 255;
					}
				}
				break;

			case GE_FORMAT_5551:
				{
					const u16 *src = (const u16 *)srcPixels + srcStride * y;
					u8 *dst = convBuf_ + 4 * width * y;
					for (int x = 0; x < width; x++)
					{
						u16 col = src[x];
						dst[x * 4] = Convert5To8((col) & 0x1f);
						dst[x * 4 + 1] = Convert5To8((col >> 5) & 0x1f);
						dst[x * 4 + 2] = Convert5To8((col >> 10) & 0x1f);
						dst[x * 4 + 3] = (col >> 15) ? 255 : 0;
					}
				}
				break;

			case GE_FORMAT_4444:
				{
					const u16 *src = (const u16 *)srcPixels + srcStride * y;
					u8 *dst = convBuf_ + 4 * width * y;
					for (int x = 0; x < width; x++)
					{
						u16 col = src[x];
						dst[x * 4] = Convert4To8((col >> 8) & 0xf);
						dst[x * 4 + 1] = Convert4To8((col >> 4) & 0xf);
						dst[x * 4 + 2] = Convert4To8(col & 0xf);
						dst[x * 4 + 3] = Convert4To8(col >> 12);
					}
				}
				break;

			case GE_FORMAT_8888:
				{
					const u8 *src = srcPixels + srcStride * 4 * y;
					u8 *dst = convBuf_ + 4 * width * y;
					memcpy(dst, src, 4 * width);
				}
				break;

			case GE_FORMAT_INVALID:
				_dbg_assert_msg_(G3D, false, "Invalid pixelFormat passed to DrawPixels().");
				break;
			}
		}
	}
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, useConvBuf ? convBuf_ : srcPixels);
}

void FramebufferManager::DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, width, height);
	DisableState();
	DrawActiveTexture(0, dstX, dstY, width, height, vfb->bufferWidth, vfb->bufferHeight, false, 0.0f, 0.0f, 1.0f, 1.0f);
}

void FramebufferManager::DrawFramebuffer(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) {
	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, 512, 272);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_Config.iTexFiltering == NEAREST ? GL_NEAREST : GL_LINEAR);

	DisableState();

	// This might draw directly at the backbuffer (if so, applyPostShader is set) so if there's a post shader, we need to apply it here.
	// Should try to unify this path with the regular path somehow, but this simple solution works for most of the post shaders 
	// (it always runs at output resolution so FXAA may look odd).
	float x, y, w, h;
	CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight);
	if (applyPostShader && usePostShader_ && useBufferedRendering_) {
		DrawActiveTexture(0, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, 0.0f, 0.0f, 480.0f / 512.0f, 1.0f, postShaderProgram_);
	} else {
		DrawActiveTexture(0, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, 0.0f, 0.0f, 480.0f / 512.0f);
	}
}

void FramebufferManager::DrawPlainColor(u32 color) {
	// Cannot take advantage of scissor + clear here - this has to be a regular draw so that
	// stencil can be used and abused, as that's what we're gonna use this for.
	static const float pos[12] = {
		-1,-1,-1,
		1,-1,-1,
		1,1,-1,
		-1,1,-1
	};
	static const GLubyte indices[4] = {0,1,3,2};

	GLSLProgram *program = 0;
	if (!draw2dprogram_) {
		CompileDraw2DProgram();
	}
	program = plainColorProgram_;

	const float col[4] = {
		((color & 0xFF)) / 255.0f,
		((color & 0xFF00) >> 8) / 255.0f,
		((color & 0xFF0000) >> 16) / 255.0f,
		((color & 0xFF000000) >> 24) / 255.0f,
	};

	shaderManager_->DirtyLastShader();

	glsl_bind(program);
	glUniform4fv(plainColorLoc_, 1, col);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(program->a_position);
	glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);
	glDisableVertexAttribArray(program->a_position);

	glsl_unbind();
}

// x, y, w, h are relative coordinates against destW/destH, which is not very intuitive.
void FramebufferManager::DrawActiveTexture(GLuint texture, float x, float y, float w, float h, float destW, float destH, bool flip, float u0, float v0, float u1, float v1, GLSLProgram *program) {
	if (flip) {
		// We're flipping, so 0 is downward.  Reverse everything from 1.0f.
		v0 = 1.0f - v0;
		v1 = 1.0f - v1;
	}
	const float texCoords[8] = {u0,v0, u1,v0, u1,v1, u0,v1};
	static const GLushort indices[4] = {0,1,3,2};

	if (texture) {
		// We know the texture, we can do a DrawTexture shortcut on nvidia.
#if defined(ANDROID)
		// Don't remember why I disabled this - no win?
		if (false && gl_extensions.NV_draw_texture && !program) {
			// Fast path for Tegra. TODO: Make this path work on desktop nvidia, seems GLEW doesn't have a clue.
			// Actually, on Desktop we should just use glBlitFramebuffer - although we take a texture here
			// so that's a little gnarly, will have to modify all callers.
			glDrawTextureNV(texture, 0,
				x, y, w, h, 0.0f,
				u0, v1, u1, v0);
			return;
		}
#endif

		glBindTexture(GL_TEXTURE_2D, texture);
	}

	float pos[12] = {
		x,y,0,
		x+w,y,0,
		x+w,y+h,0,
		x,y+h,0
	};

	float invDestW = 1.0f / (destW * 0.5f);
	float invDestH = 1.0f / (destH * 0.5f);
	for (int i = 0; i < 4; i++) {
		pos[i * 3] = pos[i * 3] * invDestW - 1.0f;
		pos[i * 3 + 1] = -(pos[i * 3 + 1] * invDestH - 1.0f);
	}

	if (!program) {
		if (!draw2dprogram_) {
			CompileDraw2DProgram();
		}

		program = draw2dprogram_;
	}

	// Always use linear filtering when stretching a buffer to the screen. Might want to make this
	// an option in the future.
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	shaderManager_->DirtyLastShader();  // dirty lastShader_

	glsl_bind(program);
	if (program == postShaderProgram_ && timeLoc_ != -1) {
		int flipCount = __DisplayGetFlipCount();
		int vCount = __DisplayGetVCount();
		float time[4] = {time_now(), (vCount % 60) * 1.0f/60.0f, (float)vCount, (float)(flipCount % 60)};
		glUniform4fv(timeLoc_, 1, time);
	}
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(program->a_position);
	glEnableVertexAttribArray(program->a_texcoord0);
	glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glVertexAttribPointer(program->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, indices);
	glDisableVertexAttribArray(program->a_position);
	glDisableVertexAttribArray(program->a_texcoord0);

	glsl_unbind();
}


VirtualFramebuffer *FramebufferManager::GetVFBAt(u32 addr) {
	VirtualFramebuffer *match = NULL;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *v = vfbs_[i];
		if (MaskedEqual(v->fb_address, addr)) {
			// Could check w too but whatever
			if (match == NULL || match->last_frame_render < v->last_frame_render) {
				match = v;
			}
		}
	}
	if (match != NULL) {
		return match;
	}

	DEBUG_LOG(SCEGE, "Finding no FBO matching address %08x", addr);
	return 0;
}

// Heuristics to figure out the size of FBO to create.
void FramebufferManager::EstimateDrawingSize(int &drawing_width, int &drawing_height) {
	static const int MAX_FRAMEBUF_HEIGHT = 512;
	const int viewport_width = (int) gstate.getViewportX1();
	const int viewport_height = (int) gstate.getViewportY1();
	const int region_width = gstate.getRegionX2() + 1;
	const int region_height = gstate.getRegionY2() + 1;
	const int scissor_width = gstate.getScissorX2() + 1;
	const int scissor_height = gstate.getScissorY2() + 1;
	const int fb_stride = std::max(gstate.FrameBufStride(), 4);

	// Games don't always set any of these.  Take the greatest parameter that looks valid based on stride.
	if (viewport_width > 4 && viewport_width <= fb_stride) {
		drawing_width = viewport_width;
		drawing_height = viewport_height;
		// Some games specify a viewport with 0.5, but don't have VRAM for 273.  480x272 is the buffer size.
		if (viewport_width == 481 && region_width == 480 && viewport_height == 273 && region_height == 272) {
			drawing_width = 480;
			drawing_height = 272;
		}
		// Sometimes region is set larger than the VRAM for the framebuffer.
		if (region_width <= fb_stride && region_width > drawing_width && region_height <= MAX_FRAMEBUF_HEIGHT) {
			drawing_width = region_width;
			drawing_height = std::max(drawing_height, region_height);
		}
		// Scissor is often set to a subsection of the framebuffer, so we pay the least attention to it.
		if (scissor_width <= fb_stride && scissor_width > drawing_width && scissor_height <= MAX_FRAMEBUF_HEIGHT) {
			drawing_width = scissor_width;
			drawing_height = std::max(drawing_height, scissor_height);
		}
	} else {
		// If viewport wasn't valid, let's just take the greatest anything regardless of stride.
		drawing_width = std::min(std::max(region_width, scissor_width), fb_stride);
		drawing_height = std::max(region_height, scissor_height);
	}

	// Assume no buffer is > 512 tall, it couldn't be textured or displayed fully if so.
	if (drawing_height >= MAX_FRAMEBUF_HEIGHT) {
		if (region_height < MAX_FRAMEBUF_HEIGHT) {
			drawing_height = region_height;
		} else if (scissor_height < MAX_FRAMEBUF_HEIGHT) {
			drawing_height = scissor_height;
		}
	}

	if (viewport_width != region_width) {
		// The majority of the time, these are equal.  If not, let's check what we know.
		const u32 fb_address = gstate.getFrameBufAddress();
		u32 nearest_address = 0xFFFFFFFF;
		for (size_t i = 0; i < vfbs_.size(); ++i) {
			const u32 other_address = vfbs_[i]->fb_address | 0x44000000;
			if (other_address > fb_address && other_address < nearest_address) {
				nearest_address = other_address;
			}
		}

		// Unless the game is using overlapping buffers, the next buffer should be far enough away.
		// This catches some cases where we can know this.
		// Hmm.  The problem is that we could only catch it for the first of two buffers...
		const u32 bpp = gstate.FrameBufFormat() == GE_FORMAT_8888 ? 4 : 2;
		int avail_height = (nearest_address - fb_address) / (fb_stride * bpp);
		if (avail_height < drawing_height && avail_height == region_height) {
			drawing_width = std::min(region_width, fb_stride);
			drawing_height = avail_height;
		}

		// Some games draw buffers interleaved, with a high stride/region/scissor but default viewport.
		if (fb_stride == 1024 && region_width == 1024 && scissor_width == 1024) {
			drawing_width = 1024;
		}
	}

	DEBUG_LOG(G3D, "Est: %08x V: %ix%i, R: %ix%i, S: %ix%i, STR: %i, THR:%i, Z:%08x = %ix%i", gstate.getFrameBufAddress(), viewport_width,viewport_height, region_width, region_height, scissor_width, scissor_height, fb_stride, gstate.isModeThrough(), gstate.isDepthWriteEnabled() ? gstate.getDepthBufAddress() : 0, drawing_width, drawing_height);
}

void FramebufferManager::DestroyFramebuf(VirtualFramebuffer *v) {
	textureCache_->NotifyFramebuffer(v->fb_address, v, NOTIFY_FB_DESTROYED);
	if (v->fbo) {
		fbo_destroy(v->fbo);
		v->fbo = 0;
	}

	// Wipe some pointers
	if (currentRenderVfb_ == v)
		currentRenderVfb_ = 0;
	if (displayFramebuf_ == v)
		displayFramebuf_ = 0;
	if (prevDisplayFramebuf_ == v)
		prevDisplayFramebuf_ = 0;
	if (prevPrevDisplayFramebuf_ == v)
		prevPrevDisplayFramebuf_ = 0;

	delete v;
}

void FramebufferManager::RebindFramebuffer() {
	if (currentRenderVfb_ && currentRenderVfb_->fbo) {
		fbo_bind_as_render_target(currentRenderVfb_->fbo);
	}
}

void FramebufferManager::ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force) {
	float renderWidthFactor = (float)vfb->renderWidth / (float)vfb->bufferWidth;
	float renderHeightFactor = (float)vfb->renderHeight / (float)vfb->bufferHeight;
	VirtualFramebuffer old = *vfb;

	if (force) {
		vfb->bufferWidth = w;
		vfb->bufferHeight = h;
	} else {
		if (vfb->bufferWidth >= w && vfb->bufferHeight >= h) {
			return;
		}

		// In case it gets thin and wide, don't resize down either side.
		vfb->bufferWidth = std::max(vfb->bufferWidth, w);
		vfb->bufferHeight = std::max(vfb->bufferHeight, h);
	}

	vfb->renderWidth = vfb->bufferWidth * renderWidthFactor;
	vfb->renderHeight = vfb->bufferHeight * renderHeightFactor;

	if (g_Config.bTrueColor) {
		vfb->colorDepth = FBO_8888;
	} else {
		switch (vfb->format) {
		case GE_FORMAT_4444:
			vfb->colorDepth = FBO_4444;
			break;
		case GE_FORMAT_5551:
			vfb->colorDepth = FBO_5551;
			break;
		case GE_FORMAT_565:
			vfb->colorDepth = FBO_565;
			break;
		case GE_FORMAT_8888:
		default:
			vfb->colorDepth = FBO_8888;
			break;
		}
	}

	textureCache_->ForgetLastTexture();
	fbo_unbind();

	if (!useBufferedRendering_) {
		if (vfb->fbo) {
			fbo_destroy(vfb->fbo);
			vfb->fbo = 0;
		}
		return;
	}

	vfb->fbo = fbo_create(vfb->renderWidth, vfb->renderHeight, 1, true, vfb->colorDepth);
	if (old.fbo) {
		INFO_LOG(SCEGE, "Resizing FBO for %08x : %i x %i x %i", vfb->fb_address, w, h, vfb->format);
		if (vfb->fbo) {
			ClearBuffer();
			BlitFramebuffer_(vfb, 0, 0, &old, 0, 0, std::min(vfb->bufferWidth, vfb->width), std::min(vfb->height, vfb->bufferHeight), 0);
		}
		fbo_destroy(old.fbo);
		if (vfb->fbo) {
			fbo_bind_as_render_target(vfb->fbo);
		}
	}

	if (!vfb->fbo) {
		ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", vfb->renderWidth, vfb->renderHeight);
	}
}

void FramebufferManager::DoSetRenderFrameBuffer() {
	/*
	if (useBufferedRendering_ && currentRenderVfb_) {
		// Hack is enabled, and there was a previous framebuffer.
		// Before we switch, let's do a series of trickery to copy one bit of stencil to
		// destination alpha. Or actually, this is just a bunch of hackery attempts on Wipeout.
		// Ignore for now.
		glstate.depthTest.disable();
		glstate.colorMask.set(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
		glstate.stencilTest.enable();
		glstate.stencilOp.set(GL_KEEP, GL_KEEP, GL_KEEP);  // don't modify stencil§
		glstate.stencilFunc.set(GL_GEQUAL, 0xFE, 0xFF);
		DrawPlainColor(0x00000000);
		//glstate.stencilFunc.set(GL_LESS, 0x80, 0xFF);
		//DrawPlainColor(0xFF000000);
		glstate.stencilTest.disable();
		glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

		glstate.depthTest.disable();
		glstate.colorMask.set(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
		DrawPlainColor(0x00000000);
		shaderManager_->DirtyLastShader();  // dirty lastShader_
	}
	*/


	gstate_c.framebufChanged = false;

	// Get parameters
	const u32 fb_address = gstate.getFrameBufRawAddress();
	const int fb_stride = gstate.FrameBufStride();

	const u32 z_address = gstate.getDepthBufRawAddress();
	const int z_stride = gstate.DepthBufStride();

	GEBufferFormat fmt = gstate.FrameBufFormat();

	// As there are no clear "framebuffer width" and "framebuffer height" registers,
	// we need to infer the size of the current framebuffer somehow.
	int drawing_width, drawing_height;
	EstimateDrawingSize(drawing_width, drawing_height);

	gstate_c.cutRTOffsetX = 0;
	bool vfbFormatChanged = false;

	// Find a matching framebuffer
	VirtualFramebuffer *vfb = 0;
	size_t i;
	for (i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *v = vfbs_[i];
		if (v->fb_address == fb_address) {
			vfb = v;
			// Update fb stride in case it changed
			if (vfb->fb_stride != fb_stride || vfb->format != fmt) {
				vfb->fb_stride = fb_stride;
				vfb->format = fmt;
				vfbFormatChanged = true;
			}
			// In throughmode, a higher height could be used.  Let's avoid shrinking the buffer.
			if (gstate.isModeThrough() && (int)vfb->width < fb_stride) {
				vfb->width = std::max((int)vfb->width, drawing_width);
				vfb->height = std::max((int)vfb->height, drawing_height);
			} else {
				vfb->width = drawing_width;
				vfb->height = drawing_height;
			}
			break;
		} else if (v->fb_address < fb_address && v->fb_address + v->fb_stride * 4 > fb_address) {
			// Possibly a render-to-offset.
			const u32 bpp = v->format == GE_FORMAT_8888 ? 4 : 2;
			const int x_offset = (fb_address - v->fb_address) / bpp;
			if (v->format == fmt && v->fb_stride == fb_stride && x_offset < fb_stride && v->height >= drawing_height) {
				WARN_LOG_REPORT_ONCE(renderoffset, HLE, "Rendering to framebuffer offset: %08x +%dx%d", v->fb_address, x_offset, 0);
				vfb = v;
				gstate_c.cutRTOffsetX = x_offset;
				vfb->width = std::max((int)vfb->width, x_offset + drawing_width);
				// To prevent the newSize code from being confused.
				drawing_width += x_offset;
				break;
			}
		}
	}

	if (vfb) {
		if ((drawing_width != vfb->bufferWidth || drawing_height != vfb->bufferHeight)) {
			// Even if it's not newly wrong, if this is larger we need to resize up.
			if (vfb->width > vfb->bufferWidth || vfb->height > vfb->bufferHeight) {
				ResizeFramebufFBO(vfb, vfb->width, vfb->height);
			} else if (vfb->newWidth != drawing_width || vfb->newHeight != drawing_height) {
				// If it's newly wrong, or changing every frame, just keep track.
				vfb->newWidth = drawing_width;
				vfb->newHeight = drawing_height;
				vfb->lastFrameNewSize = gpuStats.numFlips;
			} else if (vfb->lastFrameNewSize + FBO_OLD_AGE < gpuStats.numFlips) {
				// Okay, it's changed for a while (and stayed that way.)  Let's start over.
				// But only if we really need to, to avoid blinking.
				bool needsRecreate = vfb->bufferWidth > fb_stride;
				needsRecreate = needsRecreate || vfb->newWidth > vfb->bufferWidth || vfb->newWidth * 2 < vfb->bufferWidth;
				needsRecreate = needsRecreate || vfb->newHeight > vfb->newHeight || vfb->newHeight * 2 < vfb->newHeight;
				if (needsRecreate) {
					ResizeFramebufFBO(vfb, vfb->width, vfb->height, true);
				}
			}
		} else {
			// It's not different, let's keep track of that too.
			vfb->lastFrameNewSize = gpuStats.numFlips;
		}
	}

	float renderWidthFactor = (float)PSP_CoreParameter().renderWidth / 480.0f;
	float renderHeightFactor = (float)PSP_CoreParameter().renderHeight / 272.0f;

	if (hackForce04154000Download_ && fb_address == 0x00154000) {
		renderWidthFactor = 1.0;
		renderHeightFactor = 1.0;
	}

	// None found? Create one.
	if (!vfb) {
		vfb = new VirtualFramebuffer();
		vfb->fbo = 0;
		vfb->fb_address = fb_address;
		vfb->fb_stride = fb_stride;
		vfb->z_address = z_address;
		vfb->z_stride = z_stride;
		vfb->width = drawing_width;
		vfb->height = drawing_height;
		vfb->newWidth = drawing_width;
		vfb->newHeight = drawing_height;
		vfb->lastFrameNewSize = gpuStats.numFlips;
		vfb->renderWidth = (u16)(drawing_width * renderWidthFactor);
		vfb->renderHeight = (u16)(drawing_height * renderHeightFactor);
		vfb->bufferWidth = drawing_width;
		vfb->bufferHeight = drawing_height;
		vfb->format = fmt;
		vfb->usageFlags = FB_USAGE_RENDERTARGET;
		SetColorUpdated(vfb);
		vfb->depthUpdated = false;

		ResizeFramebufFBO(vfb, drawing_width, drawing_height, true);

		if (!useBufferedRendering_) {
			fbo_unbind();
			// Let's ignore rendering to targets that have not (yet) been displayed.
			gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
		}

		INFO_LOG(SCEGE, "Creating FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);

		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_CREATED);

		vfb->last_frame_render = gpuStats.numFlips;
		vfb->last_frame_used = 0;
		vfb->last_frame_attached = 0;
		frameLastFramebufUsed = gpuStats.numFlips;
		vfbs_.push_back(vfb);
		glEnable(GL_DITHER);  // why?
		currentRenderVfb_ = vfb;

		u32 byteSize = FramebufferByteSize(vfb);
		u32 fb_address_mem = (fb_address & 0x3FFFFFFF) | 0x04000000;
		if (Memory::IsVRAMAddress(fb_address_mem) && fb_address_mem + byteSize > framebufRangeEnd_) {
			framebufRangeEnd_ = fb_address_mem + byteSize;
		}

		// Some AMD drivers crash if we don't clear the buffer first?
		ClearBuffer();
		if (useBufferedRendering_ && !updateVRAM_) {
			gpu->PerformMemoryUpload(fb_address_mem, byteSize);
			gpu->PerformStencilUpload(fb_address_mem, byteSize);
			// TODO: Is it worth trying to upload the depth buffer?
		}

		// Let's check for depth buffer overlap.  Might be interesting.
		bool sharingReported = false;
		for (size_t i = 0, end = vfbs_.size(); i < end; ++i) {
			if (vfbs_[i]->z_stride != 0 && fb_address == vfbs_[i]->z_address) {
				// If it's clearing it, most likely it just needs more video memory.
				// Technically it could write something interesting and the other might not clear, but that's not likely.
				if (!gstate.isModeClear() || !gstate.isClearModeColorMask() || !gstate.isClearModeAlphaMask()) {
					WARN_LOG_REPORT(SCEGE, "FBO created from existing depthbuffer as color, %08x/%08x and %08x/%08x", fb_address, z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
				}
			} else if (z_stride != 0 && z_address == vfbs_[i]->fb_address) {
				// If it's clearing it, then it's probably just the reverse of the above case.
				if (!gstate.isModeClear() || !gstate.isClearModeDepthMask()) {
					WARN_LOG_REPORT(SCEGE, "FBO using existing buffer as depthbuffer, %08x/%08x and %08x/%08x", fb_address, z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
				}
			} else if (vfbs_[i]->z_stride != 0 && z_address == vfbs_[i]->z_address && fb_address != vfbs_[i]->fb_address && !sharingReported) {
				// This happens a lot, but virtually always it's cleared.
				// It's possible the other might not clear, but when every game is reported it's not useful.
				if (!gstate.isModeClear() || !gstate.isClearModeDepthMask()) {
					WARN_LOG_REPORT(SCEGE, "FBO reusing depthbuffer, %08x/%08x and %08x/%08x", fb_address, z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
					sharingReported = true;
				}
			}
		}

	// We already have it!
	} else if (vfb != currentRenderVfb_) {
	
		if (ShouldDownloadFramebuffer(vfb) && !vfb->memoryUpdated) {
			ReadFramebufferToMemory(vfb, true, 0, 0, vfb->width, vfb->height);
		}
		// Use it as a render target.
		DEBUG_LOG(SCEGE, "Switching render target to FBO for %08x: %i x %i x %i ", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		vfb->usageFlags |= FB_USAGE_RENDERTARGET;
		textureCache_->ForgetLastTexture();
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;

		if (useBufferedRendering_) {
			if (vfb->fbo) {
				fbo_bind_as_render_target(vfb->fbo);
			} else {
				// wtf? This should only happen very briefly when toggling bBufferedRendering
				fbo_unbind();
			}
		} else {
			if (vfb->fbo) {
				// wtf? This should only happen very briefly when toggling bBufferedRendering
				textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_DESTROYED);
				fbo_destroy(vfb->fbo);
				vfb->fbo = 0;
			}
			fbo_unbind();

			// Let's ignore rendering to targets that have not (yet) been displayed.
			if (vfb->usageFlags & FB_USAGE_DISPLAYED_FRAMEBUFFER) {
				gstate_c.skipDrawReason &= ~SKIPDRAW_NON_DISPLAYED_FB;
			} else {
				gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
			}
		}
		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);

#ifdef USING_GLES2
		// Some tiled mobile GPUs benefit IMMENSELY from clearing an FBO before rendering
		// to it. This broke stuff before, so now it only clears on the first use of an
		// FBO in a frame. This means that some games won't be able to avoid the on-some-GPUs
		// performance-crushing framebuffer reloads from RAM, but we'll have to live with that.
		if (vfb->last_frame_render != gpuStats.numFlips) {
			ClearBuffer();
		}
#endif

		// Copy depth pixel value from the read framebuffer to the draw framebuffer
		if (currentRenderVfb_) {
			BlitFramebufferDepth(currentRenderVfb_, vfb);
		}
		currentRenderVfb_ = vfb;
	} else {
		if (vfbFormatChanged) {
			textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);
		}

		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;
	}

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
		gstate_c.curRTWidth = vfb->width;
		gstate_c.curRTHeight = vfb->height;
	}
	gstate_c.curRTRenderWidth = vfb->renderWidth;
	gstate_c.curRTRenderHeight = vfb->renderHeight;
}

void FramebufferManager::SetLineWidth() {
#ifndef USING_GLES2
	if (g_Config.iInternalResolution == 0) {
		glLineWidth(std::max(1, (int)(PSP_CoreParameter().renderWidth / 480)));
		glPointSize(std::max(1.0f, (float)(PSP_CoreParameter().renderWidth / 480.f)));
	} else {
		glLineWidth(g_Config.iInternalResolution);
		glPointSize((float)g_Config.iInternalResolution);
	}
#endif
}

void FramebufferManager::BlitFramebufferDepth(VirtualFramebuffer *sourceframebuffer, VirtualFramebuffer *targetframebuffer) {
	if (!sourceframebuffer->fbo || !targetframebuffer->fbo || !useBufferedRendering_) {
		return;
	}

	// If depth wasn't updated, then we're at least "two degrees" away from the data.
	// This is an optimization: it probably doesn't need to be copied in this case.
	if (!sourceframebuffer->depthUpdated) {
		return;
	}

	if (sourceframebuffer->z_address == targetframebuffer->z_address &&
		sourceframebuffer->z_stride != 0 &&
		targetframebuffer->z_stride != 0 &&
		sourceframebuffer->renderWidth == targetframebuffer->renderWidth &&
		sourceframebuffer->renderHeight == targetframebuffer->renderHeight) {

#ifndef USING_GLES2
		if (gl_extensions.FBO_ARB) {
			bool useNV = false;
#else
		if (gl_extensions.GLES3 || gl_extensions.NV_framebuffer_blit) {
			bool useNV = !gl_extensions.GLES3;
#endif

			// Let's only do this if not clearing.
			if (!gstate.isModeClear() || !gstate.isClearModeDepthMask()) {
				fbo_bind_for_read(sourceframebuffer->fbo);
				glDisable(GL_SCISSOR_TEST);

#if defined(USING_GLES2) && defined(ANDROID)  // We only support this extension on Android, it's not even available on PC.
				if (useNV) {
					glBlitFramebufferNV(0, 0, sourceframebuffer->renderWidth, sourceframebuffer->renderHeight, 0, 0, targetframebuffer->renderWidth, targetframebuffer->renderHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				} else 
#endif // defined(USING_GLES2) && defined(ANDROID)
					glBlitFramebuffer(0, 0, sourceframebuffer->renderWidth, sourceframebuffer->renderHeight, 0, 0, targetframebuffer->renderWidth, targetframebuffer->renderHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				// If we set targetframebuffer->depthUpdated here, our optimization above would be pointless.

				glstate.scissorTest.restore();
			}
		}
	}
}

FBO *FramebufferManager::GetTempFBO(u16 w, u16 h, FBOColorDepth depth) {
	u32 key = ((u64)depth << 32) | (w << 16) | h;
	auto it = tempFBOs_.find(key);
	if (it != tempFBOs_.end()) {
		it->second.last_frame_used = gpuStats.numFlips;
		return it->second.fbo;
	}

	FBO *fbo = fbo_create(w, h, 1, false, depth);
	if (!fbo)
		return fbo;
	ClearBuffer();
	const TempFBO info = {fbo, gpuStats.numFlips};
	tempFBOs_[key] = info;
	return fbo;
}

void FramebufferManager::BindFramebufferColor(VirtualFramebuffer *framebuffer, bool skipCopy) {
	if (framebuffer == NULL) {
		framebuffer = currentRenderVfb_;
	}

	if (!framebuffer->fbo || !useBufferedRendering_) {
		glBindTexture(GL_TEXTURE_2D, 0);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	if (GPUStepping::IsStepping()) {
		skipCopy = true;
	}
	if (!skipCopy && currentRenderVfb_ && framebuffer->fb_address == gstate.getFrameBufRawAddress()) {
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		FBO *renderCopy = GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, framebuffer->colorDepth);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;
			BlitFramebuffer_(&copyInfo, 0, 0, framebuffer, 0, 0, framebuffer->width, framebuffer->height, 0, false);

			RebindFramebuffer();
			fbo_bind_color_as_texture(renderCopy, 0);
		} else {
			fbo_bind_color_as_texture(framebuffer->fbo, 0);
		}
	} else {
		fbo_bind_color_as_texture(framebuffer->fbo, 0);
	}
}

void FramebufferManager::CopyDisplayToOutput() {
	fbo_unbind();
	glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
	currentRenderVfb_ = 0;

	u32 offsetX = 0;
	u32 offsetY = 0;

	VirtualFramebuffer *vfb = GetVFBAt(displayFramebufPtr_);
	if (!vfb) {
		// Let's search for a framebuf within this range.
		const u32 addr = (displayFramebufPtr_ & 0x03FFFFFF) | 0x04000000;
		for (size_t i = 0; i < vfbs_.size(); ++i) {
			VirtualFramebuffer *v = vfbs_[i];
			const u32 v_addr = (v->fb_address & 0x03FFFFFF) | 0x04000000;
			const u32 v_size = FramebufferByteSize(v);
			if (addr >= v_addr && addr < v_addr + v_size) {
				const u32 dstBpp = v->format == GE_FORMAT_8888 ? 4 : 2;
				const u32 v_offsetX = ((addr - v_addr) / dstBpp) % v->fb_stride;
				const u32 v_offsetY = ((addr - v_addr) / dstBpp) / v->fb_stride;
				// We have enough space there for the display, right?
				if (v_offsetX + 480 > (u32)v->fb_stride || v->bufferHeight < v_offsetY + 272) {
					continue;
				}
				// Check for the closest one.
				if (offsetY == 0 || offsetY > v_offsetY) {
					offsetX = v_offsetX;
					offsetY = v_offsetY;
					vfb = v;
				}
			}
		}

		if (vfb) {
			// Okay, we found one above.
			INFO_LOG_REPORT_ONCE(displayoffset, HLE, "Rendering from framebuf with offset %08x -> %08x+%dx%d", addr, vfb->fb_address, offsetX, offsetY);
		}
	}

	if (vfb && vfb->format != displayFormat_) {
		if (vfb->last_frame_render + FBO_OLD_AGE < gpuStats.numFlips) {
			// The game probably switched formats on us.
			vfb->format = displayFormat_;
		} else {
			vfb = 0;
		}
	}

	if (!vfb) {
		if (Memory::IsValidAddress(displayFramebufPtr_)) {
			// The game is displaying something directly from RAM. In GTA, it's decoded video.

			// First check that it's not a known RAM copy of a VRAM framebuffer though, as in MotoGP
			for (auto iter = knownFramebufferRAMCopies_.begin(); iter != knownFramebufferRAMCopies_.end(); ++iter) {
				if (iter->second == displayFramebufPtr_) {
					vfb = GetVFBAt(iter->first);
				}
			}

			if (!vfb) {
				// Just a pointer to plain memory to draw. We should create a framebuffer, then draw to it.
				DrawFramebuffer(Memory::GetPointer(displayFramebufPtr_), displayFormat_, displayStride_, true);
				return;
			}
		} else {
			DEBUG_LOG(SCEGE, "Found no FBO to display! displayFBPtr = %08x", displayFramebufPtr_);
			// No framebuffer to display! Clear to black.
			ClearBuffer();
			return;
		}
	}

	vfb->usageFlags |= FB_USAGE_DISPLAYED_FRAMEBUFFER;
	vfb->dirtyAfterDisplay = false;
	vfb->reallyDirtyAfterDisplay = false;

	if (prevDisplayFramebuf_ != displayFramebuf_) {
		prevPrevDisplayFramebuf_ = prevDisplayFramebuf_;
	}
	if (displayFramebuf_ != vfb) {
		prevDisplayFramebuf_ = displayFramebuf_;
	}
	displayFramebuf_ = vfb;

	if (resized_) {
		ClearBuffer();
		DestroyDraw2DProgram();
		SetLineWidth();
	}

	if (vfb->fbo) {
		DEBUG_LOG(SCEGE, "Displaying FBO %08x", vfb->fb_address);
		DisableState();

		GLuint colorTexture = fbo_get_color_texture(vfb->fbo);

		// Output coordinates
		float x, y, w, h;
		CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight);

		// TODO ES3: Use glInvalidateFramebuffer to discard depth/stencil data at the end of frame.
		// and to discard extraFBOs_ after using them.

		const float u0 = offsetX / (float)vfb->bufferWidth;
		const float v0 = offsetY / (float)vfb->bufferHeight;
		const float u1 = (480.0f + offsetX) / (float)vfb->bufferWidth;
		const float v1 = (272.0f + offsetY) / (float)vfb->bufferHeight;

		if (!usePostShader_) {
			glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
			// These are in the output display coordinates
			DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1);
		} else if (usePostShader_ && extraFBOs_.size() == 1 && !postShaderAtOutputResolution_) {
			// An additional pass, post-processing shader to the extra FBO.
			fbo_bind_as_render_target(extraFBOs_[0]);
			int fbo_w, fbo_h;
			fbo_get_dimensions(extraFBOs_[0], &fbo_w, &fbo_h);
			glstate.viewport.set(0, 0, fbo_w, fbo_h);
			DrawActiveTexture(colorTexture, 0, 0, fbo_w, fbo_h, fbo_w, fbo_h, true, 0.0f, 0.0f, 1.0f, 1.0f, postShaderProgram_);

			fbo_unbind();

			// Use the extra FBO, with applied post-processing shader, as a texture.
			// fbo_bind_color_as_texture(extraFBOs_[0], 0);
			if (extraFBOs_.size() == 0) {
				ERROR_LOG(G3D, "WTF?");
				return;
			}
			colorTexture = fbo_get_color_texture(extraFBOs_[0]);
			glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
			// These are in the output display coordinates
			DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1);
		} else {
			// Use post-shader, but run shader at output resolution.
			glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
			// These are in the output display coordinates
			DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1, postShaderProgram_);
		}

		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

inline bool FramebufferManager::ShouldDownloadFramebuffer(const VirtualFramebuffer *vfb) const {
	return updateVRAM_ || (hackForce04154000Download_ && vfb->fb_address == 0x00154000);
}

void FramebufferManager::ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) {

#ifndef USING_GLES2
	if (sync) {
		PackFramebufferAsync_(NULL); // flush async just in case when we go for synchronous update
	}
#endif

	if (vfb) {

		// We'll pseudo-blit framebuffers here to get a resized and flipped version of vfb.
		// For now we'll keep these on the same struct as the ones that can get displayed
		// (and blatantly copy work already done above while at it).
		VirtualFramebuffer *nvfb = 0;

		// We maintain a separate vector of framebuffer objects for blitting.
		for (size_t i = 0; i < bvfbs_.size(); ++i) {
			VirtualFramebuffer *v = bvfbs_[i];
			if (v->fb_address == vfb->fb_address && v->format == vfb->format) {
				if (v->bufferWidth == vfb->bufferWidth && v->bufferHeight == vfb->bufferHeight) {
					nvfb = v;
					v->fb_stride = vfb->fb_stride;
					v->width = vfb->width;
					v->height = vfb->height;
					break;
				}
			}
		}

		// Create a new fbo if none was found for the size
		if (!nvfb) {
			nvfb = new VirtualFramebuffer();
			nvfb->fbo = 0;
			nvfb->fb_address = vfb->fb_address;
			nvfb->fb_stride = vfb->fb_stride;
			nvfb->z_address = vfb->z_address;
			nvfb->z_stride = vfb->z_stride;
			nvfb->width = vfb->width;
			nvfb->height = vfb->height;
			nvfb->renderWidth = vfb->bufferWidth;
			nvfb->renderHeight = vfb->bufferHeight;
			nvfb->bufferWidth = vfb->bufferWidth;
			nvfb->bufferHeight = vfb->bufferHeight;
			nvfb->format = vfb->format;
			nvfb->usageFlags = FB_USAGE_RENDERTARGET;
			nvfb->dirtyAfterDisplay = true;

			// When updating VRAM, it need to be exact format.
			switch (vfb->format) {
				case GE_FORMAT_4444:
					nvfb->colorDepth = FBO_4444;
					break;
				case GE_FORMAT_5551:
					nvfb->colorDepth = FBO_5551;
					break;
				case GE_FORMAT_565:
					nvfb->colorDepth = FBO_565;
					break;
				case GE_FORMAT_8888:
				default:
					nvfb->colorDepth = FBO_8888;
					break;
			}

			nvfb->fbo = fbo_create(nvfb->width, nvfb->height, 1, false, nvfb->colorDepth);
			if (!(nvfb->fbo)) {
				ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
				return;
			}

			nvfb->last_frame_render = gpuStats.numFlips;
			bvfbs_.push_back(nvfb);
			ClearBuffer();
			glEnable(GL_DITHER);
		} else {
			nvfb->usageFlags |= FB_USAGE_RENDERTARGET;
			textureCache_->ForgetLastTexture();
			nvfb->last_frame_render = gpuStats.numFlips;
			nvfb->dirtyAfterDisplay = true;

#ifdef USING_GLES2
			if (nvfb->fbo) {
				fbo_bind_as_render_target(nvfb->fbo);
			}

			// Some tiled mobile GPUs benefit IMMENSELY from clearing an FBO before rendering
			// to it. This broke stuff before, so now it only clears on the first use of an
			// FBO in a frame. This means that some games won't be able to avoid the on-some-GPUs
			// performance-crushing framebuffer reloads from RAM, but we'll have to live with that.
			if (nvfb->last_frame_render != gpuStats.numFlips)	{
				ClearBuffer();
			}
#endif
		}

		if (gameUsesSequentialCopies_) {
			// Ignore the x/y/etc., read the entire thing.
			x = 0;
			y = 0;
			w = vfb->width;
			h = vfb->height;
		}
		if (x == 0 && y == 0 && w == vfb->width && h == vfb->height) {
			vfb->memoryUpdated = true;
		} else {
			const static int FREQUENT_SEQUENTIAL_COPIES = 3;
			static int frameLastCopy = 0;
			static u32 bufferLastCopy = 0;
			static int copiesThisFrame = 0;
			if (frameLastCopy != gpuStats.numFlips || bufferLastCopy != vfb->fb_address) {
				frameLastCopy = gpuStats.numFlips;
				bufferLastCopy = vfb->fb_address;
				copiesThisFrame = 0;
			}
			if (++copiesThisFrame > FREQUENT_SEQUENTIAL_COPIES) {
				gameUsesSequentialCopies_ = true;
			}
		}
		BlitFramebuffer_(nvfb, x, y, vfb, x, y, w, h, 0, true);

		// PackFramebufferSync_() - Synchronous pixel data transfer using glReadPixels
		// PackFramebufferAsync_() - Asynchronous pixel data transfer using glReadPixels with PBOs

#ifdef USING_GLES2
		PackFramebufferSync_(nvfb, x, y, w, h);
#else
		if (gl_extensions.PBO_ARB && gl_extensions.OES_texture_npot) {
			if (!sync) {
				PackFramebufferAsync_(nvfb);
			} else {
				PackFramebufferSync_(nvfb, x, y, w, h);
			}
		}
#endif

		RebindFramebuffer();
	}
}

// TODO: If dimensions are the same, we can use glCopyImageSubData.
void FramebufferManager::BlitFramebuffer_(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, bool flip) {
	if (!dst->fbo) {
		ERROR_LOG_REPORT_ONCE(dstfbozero, SCEGE, "BlitFramebuffer_: dst->fbo == 0");
		fbo_unbind();
		return;
	}

	if (!src->fbo) {
		ERROR_LOG_REPORT_ONCE(srcfbozero, SCEGE, "BlitFramebuffer_: src->fbo == 0");
		fbo_unbind();
		return;
	}

	fbo_bind_as_render_target(dst->fbo);
	glDisable(GL_SCISSOR_TEST);

	bool useBlit = false;
	bool useNV = false;

#ifndef USING_GLES2
	if (gl_extensions.FBO_ARB) {
		useNV = false;
		useBlit = true;
	}
#else
	if (gl_extensions.GLES3 || gl_extensions.NV_framebuffer_blit) {
		useNV = !gl_extensions.GLES3;
		useBlit = true;
	}
#endif

	float srcXFactor = useBlit ? (float)src->renderWidth / (float)src->bufferWidth : 1.0f;
	float srcYFactor = useBlit ? (float)src->renderHeight / (float)src->bufferHeight : 1.0f;
	const int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
	if (srcBpp != bpp && bpp != 0) {
		srcXFactor = (srcXFactor * bpp) / srcBpp;
	}
	int srcX1 = srcX * srcXFactor;
	int srcX2 = (srcX + w) * srcXFactor;
	int srcY2 = src->renderHeight - (h + srcY) * srcYFactor;
	int srcY1 = srcY2 + h * srcYFactor;

	float dstXFactor = useBlit ? (float)dst->renderWidth / (float)dst->bufferWidth : 1.0f;
	float dstYFactor = useBlit ? (float)dst->renderHeight / (float)dst->bufferHeight : 1.0f;
	const int dstBpp = dst->format == GE_FORMAT_8888 ? 4 : 2;
	if (dstBpp != bpp && bpp != 0) {
		dstXFactor = (dstXFactor * bpp) / dstBpp;
	}
	int dstX1 = dstX * dstXFactor;
	int dstX2 = (dstX + w) * dstXFactor;
	int dstY2 = dst->renderHeight - (h + dstY) * dstYFactor;
	int dstY1 = dstY2 + h * dstYFactor;

	if (useBlit) {
		if (flip) {
			dstY1 = dst->renderHeight - dstY1;
			dstY2 = dst->renderHeight - dstY2;
		}

		fbo_bind_for_read(src->fbo);
		if (!useNV) {
			glBlitFramebuffer(srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		} else {
#if defined(USING_GLES2) && defined(ANDROID)  // We only support this extension on Android, it's not even available on PC.
			glBlitFramebufferNV(srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2, GL_COLOR_BUFFER_BIT, GL_NEAREST);
#endif // defined(USING_GLES2) && defined(ANDROID)
		}

	} else {
		fbo_bind_color_as_texture(src->fbo, 0);

		// Make sure our 2D drawing program is ready. Compiles only if not already compiled.
		CompileDraw2DProgram();

		glViewport(0, 0, dst->renderWidth, dst->renderHeight);
		DisableState();

		// The first four coordinates are relative to the 6th and 7th arguments of DrawActiveTexture.
		// Should maybe revamp that interface.
		float srcW = src->bufferWidth;
		float srcH = src->bufferHeight;
		DrawActiveTexture(0, dstX1, dstY, w * dstXFactor, h, dst->bufferWidth, dst->bufferHeight, !flip, srcX1 / srcW, srcY / srcH, srcX2 / srcW, (srcY + h) / srcH, draw2dprogram_);
		glBindTexture(GL_TEXTURE_2D, 0);
		textureCache_->ForgetLastTexture();
	}

	glstate.scissorTest.restore();
	glstate.viewport.restore();
	fbo_unbind();
}

// TODO: SSE/NEON
// Could also make C fake-simd for 64-bit, two 8888 pixels fit in a register :)
void ConvertFromRGBA8888(u8 *dst, const u8 *src, u32 stride, u32 width, u32 height, GEBufferFormat format) {
	// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
	const u32 *src32 = (const u32 *)src;

	if (format == GE_FORMAT_8888) {
		u32 *dst32 = (u32 *)dst;
		if (src == dst) {
			return;
		} else if (UseBGRA8888()) {
			for (u32 y = 0; y < height; ++y) {
				ConvertBGRA8888ToRGBA8888(dst32, src32, width);
				src32 += stride;
				dst32 += stride;
			}
		} else {
			// Here let's assume they don't intersect
			for (u32 y = 0; y < height; ++y) {
				memcpy(dst32, src32, width * 4);
				src32 += stride;
				dst32 += stride;
			}
		}
	} else {
		// But here it shouldn't matter if they do intersect
		int size = height * stride;
		u16 *dst16 = (u16 *)dst;
		switch (format) {
			case GE_FORMAT_565: // BGR 565
				if (UseBGRA8888()) {
					for (u32 y = 0; y < height; ++y) {
						for (u32 x = 0; x < width; ++x) {
							dst16[x] = BGRA8888toRGB565(src32[x]);
						}
						src32 += stride;
						dst16 += stride;
					}
				} else {
					for (u32 y = 0; y < height; ++y) {
						for (u32 x = 0; x < width; ++x) {
							dst16[x] = RGBA8888toRGB565(src32[x]);
						}
						src32 += stride;
						dst16 += stride;
					}
				}
				break;
			case GE_FORMAT_5551: // ABGR 1555
				if (UseBGRA8888()) {
					for (u32 y = 0; y < height; ++y) {
						ConvertBGRA8888ToRGBA5551(dst16, src32, width);
						src32 += stride;
						dst16 += stride;
					}
				} else {
					for (u32 y = 0; y < height; ++y) {
						ConvertRGBA8888ToRGBA5551(dst16, src32, width);
						src32 += stride;
						dst16 += stride;
					}
				}
				break;
			case GE_FORMAT_4444: // ABGR 4444
				if (UseBGRA8888()) {
					for (u32 y = 0; y < height; ++y) {
						for (u32 x = 0; x < width; ++x) {
							dst16[x] = BGRA8888toRGBA4444(src32[x]);
						}
						src32 += stride;
						dst16 += stride;
					}
				} else {
					for (u32 y = 0; y < height; ++y) {
						for (u32 x = 0; x < width; ++x) {
							dst16[x] = RGBA8888toRGBA4444(src32[x]);
						}
						src32 += stride;
						dst16 += stride;
					}
				}
				break;
			case GE_FORMAT_8888:
			case GE_FORMAT_INVALID:
				// Not possible.
				break;
		}
	}
}

#ifndef USING_GLES2

// TODO: Make more generic.
static void LogReadPixelsError(GLenum error) {
	switch (error) {
	case GL_NO_ERROR:
		break;
	case GL_INVALID_ENUM:
		ERROR_LOG(SCEGE, "glReadPixels: GL_INVALID_ENUM");
		break;
	case GL_INVALID_VALUE:
		ERROR_LOG(SCEGE, "glReadPixels: GL_INVALID_VALUE");
		break;
	case GL_INVALID_OPERATION:
		ERROR_LOG(SCEGE, "glReadPixels: GL_INVALID_OPERATION");
		break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		ERROR_LOG(SCEGE, "glReadPixels: GL_INVALID_FRAMEBUFFER_OPERATION");
		break;
	case GL_OUT_OF_MEMORY:
		ERROR_LOG(SCEGE, "glReadPixels: GL_OUT_OF_MEMORY");
		break;
	case GL_STACK_UNDERFLOW:
		ERROR_LOG(SCEGE, "glReadPixels: GL_STACK_UNDERFLOW");
		break;
	case GL_STACK_OVERFLOW:
		ERROR_LOG(SCEGE, "glReadPixels: GL_STACK_OVERFLOW");
		break;
	}
}

void FramebufferManager::PackFramebufferAsync_(VirtualFramebuffer *vfb) {
	const int MAX_PBO = 2;
	GLubyte *packed = 0;
	bool unbind = false;
	u8 nextPBO = (currentPBO_ + 1) % MAX_PBO;
	bool useCPU = g_Config.iRenderingMode == FB_READFBOMEMORY_CPU;

	// We'll prepare two PBOs to switch between readying and reading
	if (!pixelBufObj_) {
		GLuint pbos[MAX_PBO];
		glGenBuffers(MAX_PBO, pbos);

		pixelBufObj_ = new AsyncPBO[MAX_PBO];
		for (int i = 0; i < MAX_PBO; i++) {
			pixelBufObj_[i].handle = pbos[i];
			pixelBufObj_[i].maxSize = 0;
			pixelBufObj_[i].reading = false;
		}
	}

	// Receive previously requested data from a PBO
	AsyncPBO &pbo = pixelBufObj_[nextPBO];
	if (pbo.reading) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo.handle);
		packed = (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);

		if (packed) {
			DEBUG_LOG(SCEGE, "Reading PBO to memory , bufSize = %u, packed = %p, fb_address = %08x, stride = %u, pbo = %u",
			pbo.size, packed, pbo.fb_address, pbo.stride, nextPBO);

			if (useCPU || (UseBGRA8888() && pbo.format == GE_FORMAT_8888)) {
				u8 *dst = Memory::GetPointer(pbo.fb_address);
				ConvertFromRGBA8888(dst, packed, pbo.stride, pbo.stride, pbo.height, pbo.format);
			} else {
				// We don't need to convert, GPU already did (or should have)
				Memory::Memcpy(pbo.fb_address, packed, pbo.size);
			}

			pbo.reading = false;
		}

		glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		unbind = true;
	}

	// Order packing/readback of the framebuffer
	if (vfb) {
		int pixelType, pixelSize, pixelFormat, align;

		bool reverseOrder = (gl_extensions.gpuVendor == GPU_VENDOR_NVIDIA) || (gl_extensions.gpuVendor == GPU_VENDOR_AMD);
		switch (vfb->format) {
			// GL_UNSIGNED_INT_8_8_8_8 returns A B G R (little-endian, tested in Nvidia card/x86 PC)
			// GL_UNSIGNED_BYTE returns R G B A in consecutive bytes ("big-endian"/not treated as 32-bit value)
			// We want R G B A, so we use *_REV for 16-bit formats and GL_UNSIGNED_BYTE for 32-bit
			case GE_FORMAT_4444: // 16 bit RGBA
				pixelType = (reverseOrder ? GL_UNSIGNED_SHORT_4_4_4_4_REV : GL_UNSIGNED_SHORT_4_4_4_4);
				pixelFormat = GL_RGBA;
				pixelSize = 2;
				align = 8;
				break;
			case GE_FORMAT_5551: // 16 bit RGBA
				pixelType = (reverseOrder ? GL_UNSIGNED_SHORT_1_5_5_5_REV : GL_UNSIGNED_SHORT_5_5_5_1);
				pixelFormat = GL_RGBA;
				pixelSize = 2;
				align = 8;
				break;
			case GE_FORMAT_565: // 16 bit RGB
				pixelType = (reverseOrder ? GL_UNSIGNED_SHORT_5_6_5_REV : GL_UNSIGNED_SHORT_5_6_5);
				pixelFormat = GL_RGB;
				pixelSize = 2;
				align = 8;
				break;
			case GE_FORMAT_8888: // 32 bit RGBA
			default:
				pixelType = GL_UNSIGNED_BYTE;
				pixelFormat = UseBGRA8888() ? GL_BGRA_EXT : GL_RGBA;
				pixelSize = 4;
				align = 4;
				break;
		}

		// If using the CPU, we need 4 bytes per pixel always.
		u32 bufSize = vfb->fb_stride * vfb->height * (useCPU ? 4 : pixelSize);
		u32 fb_address = (0x04000000) | vfb->fb_address;

		if (vfb->fbo) {
			fbo_bind_for_read(vfb->fbo);
		} else {
			ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferAsync_: vfb->fbo == 0");
			fbo_unbind();
			if (gl_extensions.FBO_ARB) {
				glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			}
			return;
		}

		if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			ERROR_LOG(SCEGE, "Incomplete source framebuffer, aborting read");
			fbo_unbind();
			if (gl_extensions.FBO_ARB) {
				glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
			}
			return;
		}

		glBindBuffer(GL_PIXEL_PACK_BUFFER, pixelBufObj_[currentPBO_].handle);

		if (pixelBufObj_[currentPBO_].maxSize < bufSize) {
			// We reserve a buffer big enough to fit all those pixels
			glBufferData(GL_PIXEL_PACK_BUFFER, bufSize, NULL, GL_DYNAMIC_READ);
			pixelBufObj_[currentPBO_].maxSize = bufSize;
		}

		if (useCPU) {
			// If converting pixel formats on the CPU we'll always request RGBA8888
			glPixelStorei(GL_PACK_ALIGNMENT, 4);
			glReadPixels(0, 0, vfb->fb_stride, vfb->height, UseBGRA8888() ? GL_BGRA_EXT : GL_RGBA, GL_UNSIGNED_BYTE, 0);
		} else {
			// Otherwise we'll directly request the format we need and let the GPU sort it out
			glPixelStorei(GL_PACK_ALIGNMENT, align);
			glReadPixels(0, 0, vfb->fb_stride, vfb->height, pixelFormat, pixelType, 0);
		}

		// LogReadPixelsError(glGetError());

		fbo_unbind();
		if (gl_extensions.FBO_ARB) {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		}

		unbind = true;

		pixelBufObj_[currentPBO_].fb_address = fb_address;
		pixelBufObj_[currentPBO_].size = bufSize;
		pixelBufObj_[currentPBO_].stride = vfb->fb_stride;
		pixelBufObj_[currentPBO_].height = vfb->height;
		pixelBufObj_[currentPBO_].format = vfb->format;
		pixelBufObj_[currentPBO_].reading = true;
	}

	currentPBO_ = nextPBO;

	if (unbind) {
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}
}

#endif

void FramebufferManager::PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (vfb->fbo) {
		fbo_bind_for_read(vfb->fbo);
	} else {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferSync_: vfb->fbo == 0");
		fbo_unbind();
		if (gl_extensions.FBO_ARB) {
			glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
		}
		return;
	}

	// Pixel size always 4 here because we always request RGBA8888
	size_t bufSize = vfb->fb_stride * vfb->height * 4;
	u32 fb_address = (0x04000000) | vfb->fb_address;

	GLubyte *packed = 0;

	bool convert = vfb->format != GE_FORMAT_8888 || UseBGRA8888();
	const int dstBpp = vfb->format == GE_FORMAT_8888 ? 4 : 2;

	if (!convert) {
		packed = (GLubyte *)Memory::GetPointer(fb_address);
	} else { // End result may be 16-bit but we are reading 32-bit, so there may not be enough space at fb_address
		u32 neededSize = (u32)bufSize * sizeof(GLubyte);
		if (!convBuf_ || convBufSize_ < neededSize) {
			delete [] convBuf_;
			convBuf_ = new u8[neededSize];
			convBufSize_ = neededSize;
		}
		packed = convBuf_;
	}

	if (packed) {
		DEBUG_LOG(SCEGE, "Reading framebuffer to mem, bufSize = %u, packed = %p, fb_address = %08x", 
			(u32)bufSize, packed, fb_address);

		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		GLenum glfmt = GL_RGBA;
		if (UseBGRA8888()) {
			glfmt = GL_BGRA_EXT;
		}

		int byteOffset = y * vfb->fb_stride * 4;
		glReadPixels(0, y, vfb->fb_stride, h, glfmt, GL_UNSIGNED_BYTE, packed + byteOffset);
		// LogReadPixelsError(glGetError());

		if (convert) {
			int dstByteOffset = y * vfb->fb_stride * dstBpp;
			ConvertFromRGBA8888(Memory::GetPointer(fb_address + dstByteOffset), packed + byteOffset, vfb->fb_stride, vfb->width, h, vfb->format);
		}
	}

	fbo_unbind();
}

void FramebufferManager::EndFrame() {
	if (resized_) {
		DestroyAllFBOs();
		glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		int zoom = g_Config.iInternalResolution;
		if (zoom == 0) // auto mode
			zoom = (PSP_CoreParameter().pixelWidth + 479) / 480;

		PSP_CoreParameter().renderWidth = 480 * zoom;
		PSP_CoreParameter().renderHeight = 272 * zoom;
		resized_ = false;
	}

#ifndef USING_GLES2
	// We flush to memory last requested framebuffer, if any.
	// Only do this in the read-framebuffer modes.
	if (updateVRAM_)
		PackFramebufferAsync_(NULL);
#endif
}

void FramebufferManager::DeviceLost() {
	DestroyAllFBOs();
	DestroyDraw2DProgram();
	resized_ = false;
}

void FramebufferManager::BeginFrame() {
	DecimateFBOs();
	currentRenderVfb_ = 0;
	useBufferedRendering_ = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	updateVRAM_ = !(g_Config.iRenderingMode == FB_NON_BUFFERED_MODE || g_Config.iRenderingMode == FB_BUFFERED_MODE);
}

void FramebufferManager::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	displayFramebufPtr_ = framebuf;
	displayStride_ = stride;
	displayFormat_ = format;
}

std::vector<FramebufferInfo> FramebufferManager::GetFramebufferList() {
	std::vector<FramebufferInfo> list;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];

		FramebufferInfo info;
		info.fb_address = vfb->fb_address;
		info.z_address = vfb->z_address;
		info.format = vfb->format;
		info.width = vfb->width;
		info.height = vfb->height;
		info.fbo = vfb->fbo;
		list.push_back(info);
	}

	return list;
}

void FramebufferManager::DecimateFBOs() {
	fbo_unbind();
	currentRenderVfb_ = 0;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		int age = frameLastFramebufUsed - std::max(vfb->last_frame_render, vfb->last_frame_used);

		if (ShouldDownloadFramebuffer(vfb) && age == 0 && !vfb->memoryUpdated) {
#ifdef USING_GLES2
			bool sync = true;
#else
			bool sync = false;
#endif
			ReadFramebufferToMemory(vfb, sync, 0, 0, vfb->width, vfb->height);
		}

		if (vfb == displayFramebuf_ || vfb == prevDisplayFramebuf_ || vfb == prevPrevDisplayFramebuf_) {
			continue;
		}

		if (age > FBO_OLD_AGE) {
			INFO_LOG(SCEGE, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age)
			DestroyFramebuf(vfb);
			vfbs_.erase(vfbs_.begin() + i--);
		}
	}

	for (auto it = tempFBOs_.begin(); it != tempFBOs_.end(); ) {
		int age = frameLastFramebufUsed - it->second.last_frame_used;
		if (age > FBO_OLD_AGE) {
			fbo_destroy(it->second.fbo);
			tempFBOs_.erase(it++);
		} else {
			++it;
		}
	}

	// Do the same for ReadFramebuffersToMemory's VFBs
	for (size_t i = 0; i < bvfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = bvfbs_[i];
		int age = frameLastFramebufUsed - vfb->last_frame_render;
		if (age > FBO_OLD_AGE) {
			INFO_LOG(SCEGE, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age)
			DestroyFramebuf(vfb);
			bvfbs_.erase(bvfbs_.begin() + i--);
		}
	}
}

void FramebufferManager::DestroyAllFBOs() {
	fbo_unbind();
	currentRenderVfb_ = 0;
	displayFramebuf_ = 0;
	prevDisplayFramebuf_ = 0;
	prevPrevDisplayFramebuf_ = 0;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		INFO_LOG(SCEGE, "Destroying FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		DestroyFramebuf(vfb);
	}
	vfbs_.clear();
}

void FramebufferManager::UpdateFromMemory(u32 addr, int size, bool safe) {
	addr &= ~0x40000000;
	// TODO: Could go through all FBOs, but probably not important?
	// TODO: Could also check for inner changes, but video is most important.
	bool isDisplayBuf = addr == DisplayFramebufAddr() || addr == PrevDisplayFramebufAddr();
	if (isDisplayBuf || safe) {
		// TODO: Deleting the FBO is a heavy hammer solution, so let's only do it if it'd help.
		if (!Memory::IsValidAddress(displayFramebufPtr_))
			return;

		bool needUnbind = false;
		for (size_t i = 0; i < vfbs_.size(); ++i) {
			VirtualFramebuffer *vfb = vfbs_[i];
			if (MaskedEqual(vfb->fb_address, addr)) {
				FlushBeforeCopy();
				fbo_unbind();

				// TODO: This without the fbo_unbind() above would be better than destroying the FBO.
				// However, it doesn't seem to work for Star Ocean, at least
				if (useBufferedRendering_ && vfb->fbo) {
					DisableState();
					fbo_bind_as_render_target(vfb->fbo);
					glstate.viewport.set(0, 0, vfb->renderWidth, vfb->renderHeight);
					needUnbind = true;
					GEBufferFormat fmt = vfb->format;
					if (vfb->last_frame_render + 1 < gpuStats.numFlips && isDisplayBuf) {
						// If we're not rendering to it, format may be wrong.  Use displayFormat_ instead.
						fmt = displayFormat_;
					}
					DrawPixels(vfb, 0, 0, Memory::GetPointer(addr | 0x04000000), fmt, vfb->fb_stride, vfb->width, vfb->height);
					SetColorUpdated(vfb);
				} else {
					INFO_LOG(SCEGE, "Invalidating FBO for %08x (%i x %i x %i)", vfb->fb_address, vfb->width, vfb->height, vfb->format)
					DestroyFramebuf(vfb);
					vfbs_.erase(vfbs_.begin() + i--);
				}
			}
		}

		if (needUnbind) {
			fbo_unbind();
		}
		RebindFramebuffer();
	}
}

bool FramebufferManager::NotifyFramebufferCopy(u32 src, u32 dst, int size, bool isMemset) {
	if (updateVRAM_ || size == 0) {
		return false;
	}

	dst &= 0x3FFFFFFF;
	src &= 0x3FFFFFFF;

	VirtualFramebuffer *dstBuffer = 0;
	VirtualFramebuffer *srcBuffer = 0;
	u32 dstY = (u32)-1;
	u32 dstH = 0;
	u32 srcY = (u32)-1;
	u32 srcH = 0;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		const u32 vfb_address = (0x04000000 | vfb->fb_address) & 0x3FFFFFFF;
		const u32 vfb_size = FramebufferByteSize(vfb);
		const u32 vfb_bpp = vfb->format == GE_FORMAT_8888 ? 4 : 2;
		const u32 vfb_byteStride = vfb->fb_stride * vfb_bpp;
		const int vfb_byteWidth = vfb->width * vfb_bpp;

		if (dst >= vfb_address && (dst + size <= vfb_address + vfb_size || dst == vfb_address)) {
			const u32 offset = dst - vfb_address;
			const u32 yOffset = offset / vfb_byteStride;
			if ((offset % vfb_byteStride) == 0 && (size == vfb_byteWidth || (size % vfb_byteStride) == 0) && yOffset < dstY) {
				dstBuffer = vfb;
				dstY = yOffset;
				dstH = size == vfb_byteWidth ? 1 : std::min((u32)size / vfb_byteStride, (u32)vfb->height);
			}
		}

		if (src >= vfb_address && (src + size <= vfb_address + vfb_size || src == vfb_address)) {
			const u32 offset = src - vfb_address;
			const u32 yOffset = offset / vfb_byteStride;
			if ((offset % vfb_byteStride) == 0 && (size == vfb_byteWidth || (size % vfb_byteStride) == 0) && yOffset < srcY) {
				srcBuffer = vfb;
				srcY = yOffset;
				srcH = size == vfb_byteWidth ? 1 : std::min((u32)size / vfb_byteStride, (u32)vfb->height);
			}
		}
	}

	if (srcBuffer && srcY == 0 && srcH == srcBuffer->height && !dstBuffer) {
		// MotoGP workaround - it copies a framebuffer to memory and then displays it.
		// TODO: It's rare anyway, but the game could modify the RAM and then we'd display the wrong thing.
		// Unfortunately, that would force 1x render resolution.
		if (Memory::IsRAMAddress(dst)) {
			knownFramebufferRAMCopies_.insert(std::pair<u32, u32>(src, dst));
		}
	}

	if (!useBufferedRendering_) {
		// If we're copying into a recently used display buf, it's probably destined for the screen.
		if (srcBuffer || (dstBuffer != displayFramebuf_ && dstBuffer != prevDisplayFramebuf_)) {
			return false;
		}
	}

	if (dstBuffer && srcBuffer && !isMemset) {
		if (srcBuffer == dstBuffer) {
			WARN_LOG_REPORT_ONCE(dstsrccpy, G3D, "Intra-buffer memcpy (not supported) %08x -> %08x", src, dst);
		} else {
			WARN_LOG_REPORT_ONCE(dstnotsrccpy, G3D, "Inter-buffer memcpy %08x -> %08x", src, dst);
			// Just do the blit!
			if (g_Config.bBlockTransferGPU) {
				BlitFramebuffer_(dstBuffer, 0, dstY, srcBuffer, 0, srcY, srcBuffer->width, srcH, 0);
				SetColorUpdated(dstBuffer);
			}
		}
		return false;
	} else if (dstBuffer) {
		WARN_LOG_REPORT_ONCE(btucpy, G3D, "Memcpy fbo upload %08x -> %08x", src, dst);
		if (g_Config.bBlockTransferGPU) {
			FlushBeforeCopy();
			const u8 *srcBase = Memory::GetPointerUnchecked(src);
			if (useBufferedRendering_ && dstBuffer->fbo) {
				fbo_bind_as_render_target(dstBuffer->fbo);
			}
			glViewport(0, 0, dstBuffer->renderWidth, dstBuffer->renderHeight);
			DrawPixels(dstBuffer, 0, dstY, srcBase, dstBuffer->format, dstBuffer->fb_stride, dstBuffer->width, dstH);
			SetColorUpdated(dstBuffer);
			if (useBufferedRendering_) {
				RebindFramebuffer();
			} else {
				fbo_unbind();
			}
			glstate.viewport.restore();
			textureCache_->ForgetLastTexture();
			// This is a memcpy, let's still copy just in case.
			return false;
		}
		return false;
	} else if (srcBuffer) {
		WARN_LOG_REPORT_ONCE(btdcpy, G3D, "Memcpy fbo download %08x -> %08x", src, dst);
		FlushBeforeCopy();
		if (g_Config.bBlockTransferGPU && !srcBuffer->memoryUpdated && srcH > 0) {
			ReadFramebufferToMemory(srcBuffer, true, 0, 0, srcBuffer->width, srcH);
		}
		return false;
	} else {
		return false;
	}
}

u32 FramebufferManager::FramebufferByteSize(const VirtualFramebuffer *vfb) const {
	return vfb->fb_stride * vfb->height * (vfb->format == GE_FORMAT_8888 ? 4 : 2);
}

void FramebufferManager::FindTransferFramebuffers(VirtualFramebuffer *&dstBuffer, VirtualFramebuffer *&srcBuffer, u32 dstBasePtr, int dstStride, int &dstX, int &dstY, u32 srcBasePtr, int srcStride, int &srcX, int &srcY, int &srcWidth, int &srcHeight, int &dstWidth, int &dstHeight, int bpp) const {
	u32 dstYOffset = -1;
	u32 dstXOffset = -1;
	u32 srcYOffset = -1;
	u32 srcXOffset = -1;
	int width = srcWidth;
	int height = srcHeight;

	dstBasePtr &= 0x3FFFFFFF;
	srcBasePtr &= 0x3FFFFFFF;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		const u32 vfb_address = (0x04000000 | vfb->fb_address) & 0x3FFFFFFF;
		const u32 vfb_size = FramebufferByteSize(vfb);
		const u32 vfb_bpp = vfb->format == GE_FORMAT_8888 ? 4 : 2;
		const u32 vfb_byteStride = vfb->fb_stride * vfb_bpp;
		const u32 vfb_byteWidth = vfb->width * vfb_bpp;

		// These heuristics are a bit annoying.
		// The goal is to avoid using GPU block transfers for things that ought to be memory.
		// Maybe we should even check for textures at these places instead?

		if (vfb_address <= dstBasePtr && dstBasePtr < vfb_address + vfb_size) {
			const u32 byteOffset = dstBasePtr - vfb_address;
			const u32 byteStride = dstStride * bpp;
			const u32 yOffset = byteOffset / byteStride;
			// Some games use mismatching bitdepths.  But make sure the stride matches.
			// If it doesn't, generally this means we detected the framebuffer with too large a height.
			bool match = yOffset < dstYOffset;
			if (match && vfb_byteStride != byteStride) {
				// Grand Knights History copies with a mismatching stride but a full line at a time.
				// Makes it hard to detect the wrong transfers in e.g. God of War.
				if (width != dstStride || (byteStride * height != vfb_byteStride && byteStride * height != vfb_byteWidth)) {
					match = false;
				} else {
					dstWidth = byteStride * height / vfb_bpp;
					dstHeight = 1;
				}
			} else if (match) {
				dstWidth = width;
				dstHeight = height;
			}
			if (match) {
				dstYOffset = yOffset;
				dstXOffset = (byteOffset / bpp) % dstStride;
				dstBuffer = vfb;
			}
		}
		if (vfb_address <= srcBasePtr && srcBasePtr < vfb_address + vfb_size) {
			const u32 byteOffset = srcBasePtr - vfb_address;
			const u32 byteStride = srcStride * bpp;
			const u32 yOffset = byteOffset / byteStride;
			bool match = yOffset < srcYOffset;
			if (match && vfb_byteStride != byteStride) {
				if (width != srcStride || (byteStride * height != vfb_byteStride && byteStride * height != vfb_byteWidth)) {
					match = false;
				} else {
					srcWidth = byteStride * height / vfb_bpp;
					srcHeight = 1;
				}
			} else if (match) {
				srcWidth = width;
				srcHeight = height;
			}
			if (match) {
				srcYOffset = yOffset;
				srcXOffset = (byteOffset / bpp) % srcStride;
				srcBuffer = vfb;
			}
		}
	}

	if (dstYOffset != (u32)-1) {
		dstY += dstYOffset;
		dstX += dstXOffset;
	}
	if (srcYOffset != (u32)-1) {
		srcY += srcYOffset;
		srcX += srcXOffset;
	}
}

void FramebufferManager::FlushBeforeCopy() {
	// Flush anything not yet drawn before blitting, downloading, or uploading.
	// This might be a stalled list, or unflushed before a block transfer, etc.
	SetRenderFrameBuffer();
	transformDraw_->Flush();
}

bool FramebufferManager::NotifyBlockTransferBefore(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int width, int height, int bpp) {
	if (!useBufferedRendering_ || updateVRAM_) {
		return false;
	}

	// Skip checking if there's no framebuffers in that area.
	if (!MayIntersectFramebuffer(srcBasePtr) && !MayIntersectFramebuffer(dstBasePtr)) {
		return false;
	}

	VirtualFramebuffer *dstBuffer = 0;
	VirtualFramebuffer *srcBuffer = 0;
	int srcWidth = width;
	int srcHeight = height;
	int dstWidth = width;
	int dstHeight = height;
	FindTransferFramebuffers(dstBuffer, srcBuffer, dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, srcWidth, srcHeight, dstWidth, dstHeight, bpp);

	if (dstBuffer && srcBuffer) {
		if (srcBuffer == dstBuffer) {
			if (srcX != dstX || srcY != dstY) {
				WARN_LOG_ONCE(dstsrc, G3D, "Intra-buffer block transfer %08x -> %08x", srcBasePtr, dstBasePtr);
				if (g_Config.bBlockTransferGPU) {
					FlushBeforeCopy();
					FBO *tempFBO = GetTempFBO(dstBuffer->renderWidth, dstBuffer->renderHeight, dstBuffer->colorDepth);
					VirtualFramebuffer tempBuffer = *dstBuffer;
					tempBuffer.fbo = tempFBO;
					BlitFramebuffer_(&tempBuffer, srcX, srcY, dstBuffer, srcX, srcY, dstWidth, dstHeight, bpp);
					BlitFramebuffer_(dstBuffer, dstX, dstY, &tempBuffer, srcX, srcY, dstWidth, dstHeight, bpp);
					RebindFramebuffer();
					SetColorUpdated(dstBuffer);
					return true;
				}
			} else {
				// Ignore, nothing to do.  Tales of Phantasia X does this by accident.
				if (g_Config.bBlockTransferGPU) {
					return true;
				}
			}
		} else {
			WARN_LOG_ONCE(dstnotsrc, G3D, "Inter-buffer block transfer %08x -> %08x", srcBasePtr, dstBasePtr);
			// Just do the blit!
			if (g_Config.bBlockTransferGPU) {
				FlushBeforeCopy();
				BlitFramebuffer_(dstBuffer, dstX, dstY, srcBuffer, srcX, srcY, dstWidth, dstHeight, bpp);
				RebindFramebuffer();
				SetColorUpdated(dstBuffer);
				return true;  // No need to actually do the memory copy behind, probably.
			}
		}
		return false;
	} else if (dstBuffer) {
		// Here we should just draw the pixels into the buffer.  Copy first.
		return false;
	} else if (srcBuffer) {
		WARN_LOG_ONCE(btd, G3D, "Block transfer download %08x -> %08x", srcBasePtr, dstBasePtr);
		FlushBeforeCopy();
		if (g_Config.bBlockTransferGPU && !srcBuffer->memoryUpdated) {
			int srcBpp = srcBuffer->format == GE_FORMAT_8888 ? 4 : 2;
			float srcXFactor = (float)bpp / srcBpp;
			ReadFramebufferToMemory(srcBuffer, true, srcX * srcXFactor, srcY, srcWidth * srcXFactor, srcHeight);
		}
		return false;  // Let the bit copy happen
	} else {
		return false;
	}
}

void FramebufferManager::NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int width, int height, int bpp) {
	// A few games use this INSTEAD of actually drawing the video image to the screen, they just blast it to
	// the backbuffer. Detect this and have the framebuffermanager draw the pixels.

	u32 backBuffer = PrevDisplayFramebufAddr();
	u32 displayBuffer = DisplayFramebufAddr();

	// TODO: Is this not handled by upload?  Should we check !dstBuffer to avoid a double copy?
	if (((backBuffer != 0 && dstBasePtr == backBuffer) ||
		(displayBuffer != 0 && dstBasePtr == displayBuffer)) &&
		dstStride == 512 && height == 272 && !useBufferedRendering_) {
		FlushBeforeCopy();
		DrawFramebuffer(Memory::GetPointerUnchecked(dstBasePtr), displayFormat_, 512, false);
	}

	if (MayIntersectFramebuffer(srcBasePtr) || MayIntersectFramebuffer(dstBasePtr)) {
		VirtualFramebuffer *dstBuffer = 0;
		VirtualFramebuffer *srcBuffer = 0;
		int srcWidth = width;
		int srcHeight = height;
		int dstWidth = width;
		int dstHeight = height;
		FindTransferFramebuffers(dstBuffer, srcBuffer, dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, srcWidth, srcHeight, dstWidth, dstHeight, bpp);

		if (!useBufferedRendering_ && currentRenderVfb_ != dstBuffer) {
			return;
		}

		if (dstBuffer && !srcBuffer) {
			WARN_LOG_ONCE(btu, G3D, "Block transfer upload %08x -> %08x", srcBasePtr, dstBasePtr);
			if (g_Config.bBlockTransferGPU) {
				FlushBeforeCopy();
				const u8 *srcBase = Memory::GetPointerUnchecked(srcBasePtr) + (srcX + srcY * srcStride) * bpp;
				if (useBufferedRendering_ && dstBuffer->fbo) {
					fbo_bind_as_render_target(dstBuffer->fbo);
				}
				int dstBpp = dstBuffer->format == GE_FORMAT_8888 ? 4 : 2;
				float dstXFactor = (float)bpp / dstBpp;
				glViewport(0, 0, dstBuffer->renderWidth, dstBuffer->renderHeight);
				DrawPixels(dstBuffer, dstX * dstXFactor, dstY, srcBase, dstBuffer->format, srcStride * dstXFactor, dstWidth * dstXFactor, dstHeight);
				SetColorUpdated(dstBuffer);
				if (useBufferedRendering_) {
					RebindFramebuffer();
				} else {
					fbo_unbind();
				}
				glstate.viewport.restore();
				textureCache_->ForgetLastTexture();
			}
		}
	}
}

void FramebufferManager::Resized() {
	resized_ = true;
}

bool FramebufferManager::GetCurrentFramebuffer(GPUDebugBuffer &buffer) {
	u32 fb_address = gstate.getFrameBufRawAddress();
	int fb_stride = gstate.FrameBufStride();

	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(fb_address | 0x04000000), fb_stride, 512, gstate.FrameBufFormat());
		return true;
	}

	buffer.Allocate(vfb->renderWidth, vfb->renderHeight, GE_FORMAT_8888, true, true);
	if (vfb->fbo)
		fbo_bind_for_read(vfb->fbo);
#ifndef USING_GLES2
	glReadBuffer(GL_COLOR_ATTACHMENT0);
#endif
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadPixels(0, 0, vfb->renderWidth, vfb->renderHeight, GL_RGBA, GL_UNSIGNED_BYTE, buffer.GetData());

	return true;
}

bool FramebufferManager::GetCurrentDepthbuffer(GPUDebugBuffer &buffer) {
	u32 fb_address = gstate.getFrameBufRawAddress();
	int fb_stride = gstate.FrameBufStride();

	u32 z_address = gstate.getDepthBufRawAddress();
	int z_stride = gstate.DepthBufStride();

	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		// TODO: Is the value 16-bit?  It seems to be.
		buffer = GPUDebugBuffer(Memory::GetPointer(z_address | 0x04000000), z_stride, 512, GPU_DBG_FORMAT_16BIT);
		return true;
	}

#ifndef USING_GLES2
	buffer.Allocate(vfb->renderWidth, vfb->renderHeight, GPU_DBG_FORMAT_16BIT, true);
	if (vfb->fbo)
		fbo_bind_for_read(vfb->fbo);
	glReadBuffer(GL_DEPTH_ATTACHMENT);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glReadPixels(0, 0, vfb->renderWidth, vfb->renderHeight, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, buffer.GetData());

	return true;
#else
	return false;
#endif
}

bool FramebufferManager::GetCurrentStencilbuffer(GPUDebugBuffer &buffer) {
	u32 fb_address = gstate.getFrameBufRawAddress();
	int fb_stride = gstate.FrameBufStride();

	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		// TODO: Actually get the stencil.
		buffer = GPUDebugBuffer(Memory::GetPointer(fb_address | 0x04000000), fb_stride, 512, GPU_DBG_FORMAT_8888);
		return true;
	}

#ifndef USING_GLES2
	buffer.Allocate(vfb->renderWidth, vfb->renderHeight, GPU_DBG_FORMAT_8BIT, true);
	if (vfb->fbo)
		fbo_bind_for_read(vfb->fbo);
	glReadBuffer(GL_STENCIL_ATTACHMENT);
	glPixelStorei(GL_PACK_ALIGNMENT, 2);
	glReadPixels(0, 0, vfb->renderWidth, vfb->renderHeight, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, buffer.GetData());

	return true;
#else
	return false;
#endif
}
