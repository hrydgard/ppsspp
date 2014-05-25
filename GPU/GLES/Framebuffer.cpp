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
#include "Core/HLE/sceDisplay.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#include "GPU/Common/PostShader.h"
#include "GPU/Common/TextureDecoder.h"
#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/TextureCache.h"
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
	"  gl_FragColor.rgb = texture2D(sampler0, v_texcoord0).rgb;\n"
	"  gl_FragColor.a = 1.0;\n"
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

static bool MaskedEqual(u32 addr1, u32 addr2) {
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

void ConvertFromRGBA8888(u8 *dst, const u8 *src, u32 stride, u32 height, GEBufferFormat format);

void CenterRect(float *x, float *y, float *w, float *h,
                float origW, float origH, float frameW, float frameH) {
	float outW;
	float outH;

	if (g_Config.bStretchToDisplay) {
		outW = frameW;
		outH = frameH;
	} else {
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

static void ClearBuffer() {
	glstate.scissorTest.disable();
	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glstate.stencilFunc.set(GL_ALWAYS, 0xFF, 0xFF);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClearStencil(0xFF);
#ifdef USING_GLES2
	glClearDepthf(1.0f);
#else
	glClearDepth(1.0);
#endif
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

static void DisableState() {
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
	}
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
	plainColorLoc_(-1),
	timeLoc_(-1),
	textureCache_(0),
	shaderManager_(0),
	usePostShader_(false),
	postShaderAtOutputResolution_(false),
	resized_(false)
#ifndef USING_GLES2
	,
	pixelBufObj_(0),
	currentPBO_(0)
#endif
{
	CompileDraw2DProgram();

	// And an initial clear. We don't clear per frame as the games are supposed to handle that
	// by themselves.
	ClearBuffer();

	useBufferedRendering_ = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	updateVRAM_ = !(g_Config.iRenderingMode == FB_NON_BUFFERED_MODE || g_Config.iRenderingMode == FB_BUFFERED_MODE);
	
	SetLineWidth();
}

FramebufferManager::~FramebufferManager() {
	if (drawPixelsTex_)
		glDeleteTextures(1, &drawPixelsTex_);
	if (draw2dprogram_) {
		glsl_destroy(draw2dprogram_);
	}
	SetNumExtraFBOs(0);

	for (auto it = renderCopies_.begin(), end = renderCopies_.end(); it != end; ++it) {
		fbo_destroy(it->second);
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
		glGenTextures(1, &drawPixelsTex_);
		drawPixelsTexW_ = width;
		drawPixelsTexH_ = height;

		// Initialize backbuffer texture for DrawPixels
		glBindTexture(GL_TEXTURE_2D, drawPixelsTex_);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

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
						dst[x * 4] = ((col) & 0x1f) << 3;
						dst[x * 4 + 1] = ((col >> 5) & 0x3f) << 2;
						dst[x * 4 + 2] = ((col >> 11) & 0x1f) << 3;
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
						dst[x * 4] = ((col) & 0x1f) << 3;
						dst[x * 4 + 1] = ((col >> 5) & 0x1f) << 3;
						dst[x * 4 + 2] = ((col >> 10) & 0x1f) << 3;
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
						dst[x * 4] = ((col >> 8) & 0xf) << 4;
						dst[x * 4 + 1] = ((col >> 4) & 0xf) << 4;
						dst[x * 4 + 2] = (col & 0xf) << 4;
						dst[x * 4 + 3] = (col >> 12) << 4;
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
	DrawActiveTexture(0, dstX, dstY, width, height, vfb->width, vfb->height, false, 0.0f, 0.0f, 1.0f, 1.0f);
}

void FramebufferManager::DrawFramebuffer(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) {
	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, 512, 272);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	DisableState();

	// This might draw directly at the backbuffer (if so, applyPostShader is set) so if there's a post shader, we need to apply it here.
	// Should try to unify this path with the regular path somehow, but this simple solution works for most of the post shaders 
	// (it always runs at output resolution so FXAA may look odd).
	float x, y, w, h;
	CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight);
	if (applyPostShader && usePostShader_ && g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) {
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
#if !defined(__SYMBIAN32__) && !defined(MEEGO_EDITION_HARMATTAN) && !defined(IOS) && !defined(BLACKBERRY) && !defined(MAEMO)
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

	shaderManager_->DirtyLastShader();  // dirty lastShader_
}

VirtualFramebuffer *FramebufferManager::GetVFBAt(u32 addr) {
	VirtualFramebuffer *match = NULL;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *v = vfbs_[i];
		if (MaskedEqual(v->fb_address, addr) && v->format == displayFormat_ && v->width >= 480) {
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
static void EstimateDrawingSize(int &drawing_width, int &drawing_height) {
	int default_width = 480;
	int default_height = 272;
	int viewport_width = (int) gstate.getViewportX1();
	int viewport_height = (int) gstate.getViewportY1();
	int region_width = gstate.getRegionX2() + 1;
	int region_height = gstate.getRegionY2() + 1;
	int scissor_width = gstate.getScissorX2() + 1;
	int scissor_height = gstate.getScissorY2() + 1;
	int fb_stride = gstate.FrameBufStride();

	DEBUG_LOG(SCEGE,"viewport : %ix%i, region : %ix%i , scissor: %ix%i, stride: %i, %i", viewport_width,viewport_height, region_width, region_height, scissor_width, scissor_height, fb_stride, gstate.isModeThrough());

	// Viewport may return 0x0 for example FF Type-0 / God of War and we set it to 480x272
	if (viewport_width <= 1 && viewport_height <=1) {
		viewport_width = default_width;
		viewport_height = default_height;
	}

	if (fb_stride > 0 && fb_stride <= 512) {
		if (fb_stride == viewport_width) {
			drawing_width = viewport_width;
			drawing_height = viewport_height;
		} else {
			drawing_width = scissor_width;
			drawing_height = scissor_height;
		}
	} else {
		drawing_width = default_width;
		drawing_height = default_height;
	}

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

void FramebufferManager::DoSetRenderFrameBuffer() {
	/*
	if (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE && currentRenderVfb_) {
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
	u32 fb_address = gstate.getFrameBufRawAddress();
	int fb_stride = gstate.FrameBufStride();

	u32 z_address = gstate.getDepthBufRawAddress();
	int z_stride = gstate.DepthBufStride();

	// Yeah this is not completely right. but it'll do for now.
	//int drawing_width = ((gstate.region2) & 0x3FF) + 1;
	//int drawing_height = ((gstate.region2 >> 10) & 0x3FF) + 1;

	GEBufferFormat fmt = gstate.FrameBufFormat();

	// As there are no clear "framebuffer width" and "framebuffer height" registers,
	// we need to infer the size of the current framebuffer somehow.
	int drawing_width, drawing_height;
	EstimateDrawingSize(drawing_width, drawing_height);

	int buffer_width = drawing_width;
	int buffer_height = drawing_height;

	// Find a matching framebuffer
	VirtualFramebuffer *vfb = 0;
	size_t i;
	for (i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *v = vfbs_[i];
		if (MaskedEqual(v->fb_address, fb_address)) {
			vfb = v;
			// Update fb stride in case it changed
			vfb->fb_stride = fb_stride;
			if (v->width < drawing_width && v->height < drawing_height) {
				v->width = drawing_width;
				v->height = drawing_height;
			}
			if (v->format != fmt) {
				v->width = drawing_width;
				v->height = drawing_height;
				v->format = fmt;
			}
			break;
		}
	}

	if (vfb) {
		if ((drawing_width != vfb->bufferWidth || drawing_height != vfb->bufferHeight)) {
			// If it's newly wrong, or changing every frame, just keep track.
			if (vfb->newWidth != drawing_width || vfb->newHeight != drawing_height) {
				vfb->newWidth = drawing_width;
				vfb->newHeight = drawing_height;
				vfb->lastFrameNewSize = gpuStats.numFlips;
			} else if (vfb->lastFrameNewSize + FBO_OLD_AGE < gpuStats.numFlips) {
				// Okay, it's changed for a while (and stayed that way.)  Let's start over.
				DestroyFramebuf(vfb);
				vfbs_.erase(vfbs_.begin() + i);
				vfb = NULL;
			}
		} else {
			// It's not different, let's keep track of that too.
			vfb->lastFrameNewSize = gpuStats.numFlips;
		}
	}

	float renderWidthFactor = (float)PSP_CoreParameter().renderWidth / 480.0f;
	float renderHeightFactor = (float)PSP_CoreParameter().renderHeight / 272.0f;

	// None found? Create one.
	if (!vfb) {
		gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
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
		vfb->bufferWidth = buffer_width;
		vfb->bufferHeight = buffer_height;
		vfb->format = fmt;
		vfb->usageFlags = FB_USAGE_RENDERTARGET;
		vfb->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;
		vfb->memoryUpdated = false;
		vfb->depthUpdated = false;

		if (g_Config.bTrueColor) {
			vfb->colorDepth = FBO_8888;
		} else { 
			switch (fmt) {
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

		if (useBufferedRendering_) {
			vfb->fbo = fbo_create(vfb->renderWidth, vfb->renderHeight, 1, true, vfb->colorDepth);
			if (vfb->fbo) {
				fbo_bind_as_render_target(vfb->fbo);
			} else {
				ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", vfb->renderWidth, vfb->renderHeight);
			}
		} else {
			fbo_unbind();
			// Let's ignore rendering to targets that have not (yet) been displayed.
			gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
		}

		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_CREATED);

		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed = gpuStats.numFlips;
		vfbs_.push_back(vfb);
		ClearBuffer();
		glEnable(GL_DITHER);  // why?
		currentRenderVfb_ = vfb;

		INFO_LOG(SCEGE, "Creating FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);

		// Let's check for depth buffer overlap.  Might be interesting.
		bool sharingReported = false;
		for (size_t i = 0, end = vfbs_.size(); i < end; ++i) {
			if (MaskedEqual(fb_address, vfbs_[i]->z_address)) {
				// If it's clearing it, most likely it just needs more video memory.
				// Technically it could write something interesting and the other might not clear, but that's not likely.
				if (!gstate.isModeClear() || !gstate.isClearModeColorMask() || !gstate.isClearModeAlphaMask()) {
					WARN_LOG_REPORT(SCEGE, "FBO created from existing depthbuffer as color, %08x/%08x and %08x/%08x", fb_address, z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
				}
			} else if (MaskedEqual(z_address, vfbs_[i]->fb_address)) {
				// If it's clearing it, then it's probably just the reverse of the above case.
				if (!gstate.isModeClear() || !gstate.isClearModeDepthMask()) {
					WARN_LOG_REPORT(SCEGE, "FBO using existing buffer as depthbuffer, %08x/%08x and %08x/%08x", fb_address, z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
				}
			} else if (MaskedEqual(z_address, vfbs_[i]->z_address) && fb_address != vfbs_[i]->fb_address && !sharingReported) {
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
	
		if (updateVRAM_ && !vfb->memoryUpdated) {
			ReadFramebufferToMemory(vfb, true, 0, 0, vfb->width, vfb->height);
		}
		// Use it as a render target.
		DEBUG_LOG(SCEGE, "Switching render target to FBO for %08x: %i x %i x %i ", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		vfb->usageFlags |= FB_USAGE_RENDERTARGET;
		gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;
		vfb->memoryUpdated = false;

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
		if (vfb->last_frame_render != gpuStats.numFlips)	{
			ClearBuffer();
		}
#endif

		// Copy depth pixel value from the read framebuffer to the draw framebuffer
		BindFramebufferDepth(currentRenderVfb_,vfb);
		currentRenderVfb_ = vfb;
	} else {
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

void FramebufferManager::BindFramebufferDepth(VirtualFramebuffer *sourceframebuffer, VirtualFramebuffer *targetframebuffer) {
	if (!sourceframebuffer || !targetframebuffer->fbo || !useBufferedRendering_) {
		return;
	}

	// If depth wasn't updated, then we're at least "two degrees" away from the data.
	// This is an optimization: it probably doesn't need to be copied in this case.
	if (!sourceframebuffer->depthUpdated) {
		return;
	}

	if (MaskedEqual(sourceframebuffer->z_address, targetframebuffer->z_address) && 
		sourceframebuffer->renderWidth == targetframebuffer->renderWidth &&
		sourceframebuffer->renderHeight == targetframebuffer->renderHeight) {
		
#ifndef USING_GLES2
		if (gl_extensions.FBO_ARB) {
			bool useNV = false;
#else
		if (gl_extensions.GLES3 || gl_extensions.NV_framebuffer_blit) {
			bool useNV = !gl_extensions.GLES3;
#endif

#ifdef MAY_HAVE_GLES3
			// Let's only do this if not clearing.
			if (!gstate.isModeClear() || !gstate.isClearModeDepthMask()) {
				fbo_bind_for_read(sourceframebuffer->fbo);

#if defined(USING_GLES2) && (defined(ANDROID) || defined(BLACKBERRY))  // We only support this extension on Android, it's not even available on PC.
				if (useNV) {
					glBlitFramebufferNV(0, 0, sourceframebuffer->renderWidth, sourceframebuffer->renderHeight, 0, 0, targetframebuffer->renderWidth, targetframebuffer->renderHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				} else 
#endif // defined(USING_GLES2) && (defined(ANDROID) || defined(BLACKBERRY))
					glBlitFramebuffer(0, 0, sourceframebuffer->renderWidth, sourceframebuffer->renderHeight, 0, 0, targetframebuffer->renderWidth, targetframebuffer->renderHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				// If we set targetframebuffer->depthUpdated here, our optimization above would be pointless.
			}
#endif
		}
	}
}

void FramebufferManager::BindFramebufferColor(VirtualFramebuffer *framebuffer) {
	if (!framebuffer->fbo || !useBufferedRendering_) {
		glBindTexture(GL_TEXTURE_2D, 0);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	if (currentRenderVfb_ && MaskedEqual(framebuffer->fb_address, gstate.getFrameBufRawAddress())) {
#ifndef USING_GLES2
		if (gl_extensions.FBO_ARB) {
			bool useNV = false;
#else
		if (gl_extensions.GLES3 || gl_extensions.NV_framebuffer_blit) {
			bool useNV = !gl_extensions.GLES3;
#endif
#ifdef MAY_HAVE_GLES3

			// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
			FBO *renderCopy = NULL;
			std::pair<int, int> copySize = std::make_pair((int)framebuffer->renderWidth, (int)framebuffer->renderHeight);
			for (auto it = renderCopies_.begin(), end = renderCopies_.end(); it != end; ++it) {
				if (it->first == copySize) {
					renderCopy = it->second;
					break;
				}
			}
			if (!renderCopy) {
				renderCopy = fbo_create(framebuffer->renderWidth, framebuffer->renderHeight, 1, true, framebuffer->colorDepth);
				renderCopies_[copySize] = renderCopy;
			}

			fbo_bind_as_render_target(renderCopy);
			glViewport(0, 0, framebuffer->renderWidth, framebuffer->renderHeight);
			fbo_bind_for_read(framebuffer->fbo);
			
#if defined(USING_GLES2) && (defined(ANDROID) || defined(BLACKBERRY))  // We only support this extension on Android, it's not even available on PC.
			if (useNV) {
				glBlitFramebufferNV(0, 0, framebuffer->renderWidth, framebuffer->renderHeight, 0, 0, framebuffer->renderWidth, framebuffer->renderHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			} else 
#endif // defined(USING_GLES2) && (defined(ANDROID) || defined(BLACKBERRY))
				glBlitFramebuffer(0, 0, framebuffer->renderWidth, framebuffer->renderHeight, 0, 0, framebuffer->renderWidth, framebuffer->renderHeight, GL_COLOR_BUFFER_BIT, GL_NEAREST);
			
			fbo_bind_as_render_target(currentRenderVfb_->fbo);
			fbo_bind_color_as_texture(renderCopy, 0);
#endif
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

	VirtualFramebuffer *vfb = GetVFBAt(displayFramebufPtr_);
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

		if (!usePostShader_) {
			glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
			// These are in the output display coordinates
			DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, 0.0f, 0.0f, 480.0f / (float)vfb->width, 272.0f / (float)vfb->height);
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
			DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, 0.0f, 0.0f, 480.0f / (float)vfb->width, 272.0f / (float)vfb->height);
		} else {
			// Use post-shader, but run shader at output resolution.
			glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
			// These are in the output display coordinates
			DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, 0.0f, 0.0f, 480.0f / (float)vfb->width, 272.0f / (float)vfb->height, postShaderProgram_);
		}

		glBindTexture(GL_TEXTURE_2D, 0);
	}
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
			if (MaskedEqual(v->fb_address, vfb->fb_address) && v->format == vfb->format) {
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
			nvfb->renderWidth = vfb->width;
			nvfb->renderHeight = vfb->height;
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

			nvfb->fbo = fbo_create(nvfb->width, nvfb->height, 1, true, nvfb->colorDepth);
			if (!(nvfb->fbo)) {
				ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
				return;
			}

			nvfb->last_frame_render = gpuStats.numFlips;
			bvfbs_.push_back(nvfb);
			fbo_bind_as_render_target(nvfb->fbo);
			ClearBuffer();
			glEnable(GL_DITHER);
		} else {
			nvfb->usageFlags |= FB_USAGE_RENDERTARGET;
			gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
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

		vfb->memoryUpdated = true;
		BlitFramebuffer_(nvfb, x, y, vfb, x, y, w, h, 0, true);

		// PackFramebufferSync_() - Synchronous pixel data transfer using glReadPixels
		// PackFramebufferAsync_() - Asynchronous pixel data transfer using glReadPixels with PBOs

#ifdef USING_GLES2
		PackFramebufferSync_(nvfb);
#else
		if (gl_extensions.PBO_ARB && gl_extensions.OES_texture_npot) {
			if (!sync) {
				PackFramebufferAsync_(nvfb);
			} else {
				PackFramebufferSync_(nvfb);
			}
		}
#endif

		// Null out currentRenderVfb_ so it gets set again properly.
		currentRenderVfb_ = NULL;
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


#ifndef USING_GLES2
	if (gl_extensions.FBO_ARB) {
		bool useNV = false;
#else
	if (gl_extensions.GLES3 || gl_extensions.NV_framebuffer_blit) {
		bool useNV = !gl_extensions.GLES3;
#endif

		float srcXFactor = (float)src->renderWidth / (float)src->bufferWidth;
		float srcYFactor = (float)src->renderHeight / (float)src->bufferHeight;
		int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
		if (srcBpp != bpp && bpp != 0) {
			srcXFactor = (srcXFactor * bpp) / srcBpp;
		}
		int srcX1 = srcX * srcXFactor;
		int srcX2 = (srcX + w) * srcXFactor;
		int srcY2 = src->renderHeight - (h + srcY) * srcYFactor;
		int srcY1 = srcY2 + h * srcYFactor;

		float dstXFactor = (float)dst->renderWidth / (float)dst->bufferWidth;
		float dstYFactor = (float)dst->renderHeight / (float)dst->bufferHeight;
		int dstBpp = dst->format == GE_FORMAT_8888 ? 4 : 2;
		if (dstBpp != bpp && bpp != 0) {
			dstXFactor = (dstXFactor * bpp) / dstBpp;
		}
		int dstX1 = dstX * dstXFactor;
		int dstX2 = (dstX + w) * dstXFactor;
		int dstY2 = dst->renderHeight - (h + dstY) * dstYFactor;
		int dstY1 = dstY2 + h * dstYFactor;

		if (flip) {
			std::swap(dstY1, dstY2);
		}

#ifdef MAY_HAVE_GLES3
		fbo_bind_for_read(src->fbo);
		if (!useNV) {
			glBlitFramebuffer(srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
#if defined(USING_GLES2) && (defined(ANDROID) || defined(BLACKBERRY))  // We only support this extension on Android, it's not even available on PC.
		else if (gl_extensions.NV_framebuffer_blit) {
			glBlitFramebufferNV(srcX1, srcY1, srcX2, srcY2, dstX1, dstY1, dstX2, dstY2, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		}
#endif // defined(USING_GLES2) && (defined(ANDROID) || defined(BLACKBERRY))

#endif // MAY_HAVE_GLES3

	} else {
		fbo_bind_color_as_texture(src->fbo, 0);

		// Make sure our 2D drawing program is ready. Compiles only if not already compiled.
		CompileDraw2DProgram();

		glstate.viewport.set(0, 0, dst->width, dst->height);
		DisableState();

		// The first four coordinates are relative to the 6th and 7th arguments of DrawActiveTexture.
		// Should maybe revamp that interface.
		float srcW = src->width;
		float srcH = src->height;
		DrawActiveTexture(0, dstX, dstY, w, h, dst->width, dst->height, false, srcX / srcW, srcY / srcH, (srcX + w) / srcW, (srcY + h) / srcH, draw2dprogram_);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	fbo_unbind();
}

static inline bool UseBGRA8888() {
	// TODO: Other platforms?  May depend on vendor which is faster?
#ifdef _WIN32
	return gl_extensions.EXT_bgra;
#endif
	return false;
}

// TODO: SSE/NEON
// Could also make C fake-simd for 64-bit, two 8888 pixels fit in a register :)
void ConvertFromRGBA8888(u8 *dst, const u8 *src, u32 stride, u32 height, GEBufferFormat format) {
	if (format == GE_FORMAT_8888) {
		if (src == dst) {
			return;
		} else if (UseBGRA8888()) {
			u32 numPixels = height * stride;
			ConvertBGRA8888ToRGBA8888((u32 *)dst, (const u32 *)src, numPixels);
		} else {
			// Here let's assume they don't intersect
			memcpy(dst, src, stride * height * 4);
		}
	} else {
		// But here it shouldn't matter if they do intersect
		int size = height * stride;
		const u32 *src32 = (const u32 *)src;
		u16 *dst16 = (u16 *)dst;
		switch (format) {
			case GE_FORMAT_565: // BGR 565
				if (UseBGRA8888()) {
					for (int i = 0; i < size; i++) {
						dst16[i] = BGRA8888toRGB565(src32[i]);
					}
				} else {
					for (int i = 0; i < size; i++) {
						dst16[i] = RGBA8888toRGB565(src32[i]);
					}
				}
				break;
			case GE_FORMAT_5551: // ABGR 1555
				if (UseBGRA8888()) {
					ConvertBGRA8888ToRGBA5551(dst16, src32, size);
				} else {
					ConvertRGBA8888ToRGBA5551(dst16, src32, size);
				}
				break;
			case GE_FORMAT_4444: // ABGR 4444
				if (UseBGRA8888()) {
					for (int i = 0; i < size; i++) {
						dst16[i] = BGRA8888toRGBA4444(src32[i]);
					}
				} else {
					for (int i = 0; i < size; i++) {
						dst16[i] = RGBA8888toRGBA4444(src32[i]);
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
				ConvertFromRGBA8888(dst, packed, pbo.stride, pbo.height, pbo.format);
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

void FramebufferManager::PackFramebufferSync_(VirtualFramebuffer *vfb) {
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

	if (!convert) {
		packed = (GLubyte *)Memory::GetPointer(fb_address);
	} else { // End result may be 16-bit but we are reading 32-bit, so there may not be enough space at fb_address
		packed = (GLubyte *)malloc(bufSize * sizeof(GLubyte));
	}

	if (packed) {
		DEBUG_LOG(SCEGE, "Reading framebuffer to mem, bufSize = %u, packed = %p, fb_address = %08x", 
			(u32)bufSize, packed, fb_address);

		glPixelStorei(GL_PACK_ALIGNMENT, 4);
		GLenum glfmt = GL_RGBA;
#if defined(MAY_HAVE_GLES3)
		if (UseBGRA8888()) {
			glfmt = GL_BGRA_EXT;
		}
#endif
		glReadPixels(0, 0, vfb->fb_stride, vfb->height, glfmt, GL_UNSIGNED_BYTE, packed);
		// LogReadPixelsError(glGetError());

		if (convert) {
			ConvertFromRGBA8888(Memory::GetPointer(fb_address), packed, vfb->fb_stride, vfb->height, vfb->format);
			free(packed);
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
	if (g_Config.iRenderingMode == FB_READFBOMEMORY_CPU || g_Config.iRenderingMode == FB_READFBOMEMORY_GPU)
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

		if (updateVRAM_ && age == 0 && !vfb->memoryUpdated) {
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
	if (addr == DisplayFramebufAddr() || addr == PrevDisplayFramebufAddr() || safe) {
		// TODO: Deleting the FBO is a heavy hammer solution, so let's only do it if it'd help.
		if (!Memory::IsValidAddress(displayFramebufPtr_))
			return;

		bool needUnbind = false;
		for (size_t i = 0; i < vfbs_.size(); ++i) {
			VirtualFramebuffer *vfb = vfbs_[i];
			if (MaskedEqual(vfb->fb_address, addr)) {
				fbo_unbind();
				currentRenderVfb_ = 0;

				vfb->dirtyAfterDisplay = true;
				vfb->reallyDirtyAfterDisplay = true;
				// TODO: This without the fbo_unbind() above would be better than destroying the FBO.
				// However, it doesn't seem to work for Star Ocean, at least
				if (useBufferedRendering_ && vfb->fbo) {
					DisableState();
					fbo_bind_as_render_target(vfb->fbo);

					int w = vfb->bufferWidth;
					int h = vfb->bufferHeight;
					// Often, the framebuffer size is incorrect.  But here we have the size.  Bit of a hack.
					if (vfb->fb_stride == 512 && (size == 512 * 272 * 4 || size == 512 * 272 * 2)) {
						// Looks like a standard 480x272 sized framebuffer/video/etc.
						w = 480;
						h = 272;
					}
					// Scale by the render resolution factor.
					w = (w * vfb->renderWidth) / vfb->bufferWidth;
					h = (h * vfb->renderHeight) / vfb->bufferHeight;
					glstate.viewport.set(0, vfb->renderHeight - h, w, h);
					needUnbind = true;
					DrawPixels(vfb, 0, 0, Memory::GetPointer(addr | 0x04000000), vfb->format, vfb->fb_stride, vfb->width, vfb->height);
				} else {
					INFO_LOG(SCEGE, "Invalidating FBO for %08x (%i x %i x %i)", vfb->fb_address, vfb->width, vfb->height, vfb->format)
					DestroyFramebuf(vfb);
					vfbs_.erase(vfbs_.begin() + i--);
				}
			}
		}

		if (needUnbind)
			fbo_unbind();
	}
}

void FramebufferManager::NotifyFramebufferCopy(u32 src, u32 dst, int size) {
	// MotoGP workaround
	for (size_t i = 0; i < vfbs_.size(); i++) {
		int bpp = vfbs_[i]->format == GE_FORMAT_8888 ? 4 : 2;
		int fsize = vfbs_[i]->fb_stride * vfbs_[i]->height * (vfbs_[i]->format == GE_FORMAT_8888 ? 4 : 2);
		if ((vfbs_[i]->fb_address | 0x04000000) == src && size == fsize) {
			// A framebuffer matched!
			knownFramebufferRAMCopies_.insert(std::pair<u32, u32>(src, dst));
		}
	}

	VirtualFramebuffer *dstBuffer = 0;
	VirtualFramebuffer *srcBuffer = 0;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		if (MaskedEqual(vfb->fb_address, dst)) {
			dstBuffer = vfb;
		}
		if (MaskedEqual(vfb->fb_address, src)) {
			srcBuffer = vfb;
		}
	}

	// TODO: Do ReadFramebufferToMemory etc where applicable.
	// This will slow down MotoGP but make the hack above unnecessary.
	if (dstBuffer && srcBuffer) {
		if (srcBuffer == dstBuffer) {
			WARN_LOG_REPORT_ONCE(dstsrccpy, G3D, "Intra-buffer memcpy (not supported) %08x -> %08x", src, dst);
		} else {
			WARN_LOG_ONCE(dstnotsrccpy, G3D, "Inter-buffer memcpy %08x -> %08x", src, dst);
			// Just do the blit!
			// TODO: Possibly take bpp into account somehow if games are doing really crazy things?
			// if (g_Config.bBlockTransferGPU) {
			//   BlitFramebuffer_(dstBuffer, 0, 0, srcBuffer, 0, 0, srcBuffer->width, srcBuffer->height, 0);
			// }
		}
	} else if (dstBuffer) {
		WARN_LOG_REPORT_ONCE(btucpy, G3D, "Memcpy fbo upload (not supported) %08x -> %08x", src, dst);
		// Here we should just draw the pixels into the buffer.
		// if (g_Config.bBlockTransferGPU) {
		// }
	} else if (srcBuffer && g_Config.iRenderingMode == FB_BUFFERED_MODE) {
		WARN_LOG_ONCE(btdcpy, G3D, "Memcpy fbo download %08x -> %08x", src, dst);
		// if (g_Config.bBlockTransferGPU) {
		//   ReadFramebufferToMemory(srcBuffer, true, 0, 0, srcBuffer->width, srcBuffer->height);
		// }
	}
}

bool FramebufferManager::NotifyBlockTransfer(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int width, int height, int bpp) {
	if (!(g_Config.iRenderingMode == FB_BUFFERED_MODE)) {
		return false;
	}
	
	if (Memory::IsRAMAddress(srcBasePtr) && Memory::IsVRAMAddress(dstBasePtr)) {
		// TODO: This causes glitches in Tactics Ogre if we don't implement both ways (which will probably be slow...)
		// The main thing this helps is videos, which will have a matching stride, and zero x/y.
		if (dstStride == srcStride && dstY == 0 && dstX == 0 && srcX == 0 && srcY == 0) {
			UpdateFromMemory(dstBasePtr, (dstY + height) * dstStride * bpp, true);
		}
	}

	// A few games use this INSTEAD of actually drawing the video image to the screen, they just blast it to
	// the backbuffer. Detect this and have the framebuffermanager draw the pixels.

	u32 backBuffer = PrevDisplayFramebufAddr();
	u32 displayBuffer = DisplayFramebufAddr();

	if (((backBuffer != 0 && dstBasePtr == backBuffer) ||
		(displayBuffer != 0 && dstBasePtr == displayBuffer)) &&
		dstStride == 512 && height == 272) {
		// TODO: Use displayFormat_ instead of GE_FORMAT_8888?
		DrawFramebuffer(Memory::GetPointerUnchecked(dstBasePtr), GE_FORMAT_8888, 512, false);
	}

	VirtualFramebuffer *dstBuffer = 0;
	VirtualFramebuffer *srcBuffer = 0;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		const u32 vfb_address = 0x04000000 | vfb->fb_address;
		const u32 vfb_size = vfb->fb_stride * vfb->height * (vfb->format == GE_FORMAT_8888 ? 4 : 2);
		if (vfb_address <= dstBasePtr && dstBasePtr < vfb_address + vfb_size) {
			dstY += (dstBasePtr - vfb_address) / (dstStride * bpp);
			dstBuffer = vfb;
		}
		if (vfb_address <= srcBasePtr && srcBasePtr < vfb_address + vfb_size) {
			srcY += (srcBasePtr - vfb_address) / (srcStride * bpp);
			srcBuffer = vfb;
		}
	}

	if (dstBuffer && srcBuffer) {
		if (srcBuffer == dstBuffer) {
			WARN_LOG_REPORT_ONCE(dstsrc, G3D, "Intra-buffer block transfer (not supported) %08x -> %08x", srcBasePtr, dstBasePtr);
		} else {
			WARN_LOG_ONCE(dstnotsrc, G3D, "Inter-buffer block transfer %08x -> %08x", srcBasePtr, dstBasePtr);
			// Just do the blit!
			// TODO: Possibly take bpp into account somehow if games are doing really crazy things?
			if (g_Config.bBlockTransferGPU) {
				BlitFramebuffer_(dstBuffer, dstX, dstY, srcBuffer, srcX, srcY, width, height, bpp);
				return true;  // No need to actually do the memory copy behind, probably.
			}
		}
		return false;
	} else if (dstBuffer) {
		WARN_LOG_REPORT_ONCE(btu, G3D, "Block transfer upload (not supported) %08x -> %08x", srcBasePtr, dstBasePtr);
		if (g_Config.bBlockTransferGPU) {
			u8 *srcBase = Memory::GetPointerUnchecked(srcBasePtr) + (srcX + srcY * srcStride) * bpp;
			DrawPixels(dstBuffer, dstX, dstY, srcBase, dstBuffer->format, srcStride * bpp, width, height);
		}
		// Here we should just draw the pixels into the buffer.
		return false;
	} else if (srcBuffer && g_Config.iRenderingMode == FB_BUFFERED_MODE) {
		WARN_LOG_ONCE(btd, G3D, "Block transfer download %08x -> %08x", srcBasePtr, dstBasePtr);
		if (g_Config.bBlockTransferGPU) {
			ReadFramebufferToMemory(srcBuffer, true, srcX, srcY, width, height);
		}
		return false;  // Let the bit copy happen
	} else {
		return false;
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
