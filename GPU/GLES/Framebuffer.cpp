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

#include "Common/ColorConv.h"
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
#include "GPU/Common/FramebufferCommon.h"
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

void ConvertFromRGBA8888(u8 *dst, const u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format);

void FramebufferManager::ClearBuffer() {
	glstate.scissorTest.disable();
	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glstate.stencilFunc.set(GL_ALWAYS, 0, 0);
	glstate.stencilMask.set(0xFF);
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
	glstate.stencilMask.set(0xFF);
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

		// The new FBO is still bound after creation, but let's bind it anyway.
		fbo_bind_as_render_target(fbo);
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
					CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, ROTATION_LOCKED_HORIZONTAL);
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
	gameUsesSequentialCopies_(false)
#ifndef USING_GLES2
	,
	pixelBufObj_(0),
	currentPBO_(0)
#endif
{
}

void FramebufferManager::Init() {
	FramebufferManagerCommon::Init();
	CompileDraw2DProgram();
	SetLineWidth();
}

FramebufferManager::~FramebufferManager() {
	if (drawPixelsTex_)
		glDeleteTextures(1, &drawPixelsTex_);
	DestroyDraw2DProgram();
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
					ConvertRGBA565ToRGBA8888((u32 *)dst, src, width);
				}
				break;

			case GE_FORMAT_5551:
				{
					const u16 *src = (const u16 *)srcPixels + srcStride * y;
					u8 *dst = convBuf_ + 4 * width * y;
					ConvertRGBA5551ToRGBA8888((u32 *)dst, src, width);
				}
				break;

			case GE_FORMAT_4444:
				{
					const u16 *src = (const u16 *)srcPixels + srcStride * y;
					u8 *dst = convBuf_ + 4 * width * y;
					ConvertRGBA4444ToRGBA8888((u32 *)dst, src, width);
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
	if (useBufferedRendering_ && vfb->fbo) {
		fbo_bind_as_render_target(vfb->fbo);
	}
	glViewport(0, 0, vfb->renderWidth, vfb->renderHeight);
	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, width, height);
	DisableState();
	DrawActiveTexture(0, dstX, dstY, width, height, vfb->bufferWidth, vfb->bufferHeight, false, 0.0f, 0.0f, 1.0f, 1.0f);
	textureCache_->ForgetLastTexture();
}

void FramebufferManager::DrawFramebuffer(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) {
	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, 512, 272);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_Config.iTexFiltering == NEAREST ? GL_NEAREST : GL_LINEAR);

	DisableState();

	struct CardboardSettings cardboardSettings;
	GetCardboardSettings(&cardboardSettings);

	// This might draw directly at the backbuffer (if so, applyPostShader is set) so if there's a post shader, we need to apply it here.
	// Should try to unify this path with the regular path somehow, but this simple solution works for most of the post shaders 
	// (it always runs at output resolution so FXAA may look odd).
	float x, y, w, h;
	int uvRotation = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
	CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, uvRotation);
	if (cardboardSettings.enabled) {
		// Left Eye Image
		glstate.viewport.set(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
		if (applyPostShader && usePostShader_ && useBufferedRendering_) {
			DrawActiveTexture(0, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, 0.0f, 0.0f, 480.0f / 512.0f, 1.0f, postShaderProgram_);
		} else {
			DrawActiveTexture(0, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, 0.0f, 0.0f, 480.0f / 512.0f, 1.0f);
		}

		// Right Eye Image
		glstate.viewport.set(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
		if (applyPostShader && usePostShader_ && useBufferedRendering_) {
			DrawActiveTexture(0, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, 0.0f, 0.0f, 480.0f / 512.0f, 1.0f, postShaderProgram_);
		} else {
			DrawActiveTexture(0, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, 0.0f, 0.0f, 480.0f / 512.0f);
		}
	} else {
		// Fullscreen Image
		glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		if (applyPostShader && usePostShader_ && useBufferedRendering_) {
			DrawActiveTexture(0, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, 0.0f, 0.0f, 480.0f / 512.0f, 1.0f, postShaderProgram_, uvRotation);
		} else {
			DrawActiveTexture(0, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, false, 0.0f, 0.0f, 480.0f / 512.0f, 1.0f, NULL, uvRotation);
		}
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
	glstate.arrayBuffer.unbind();
	glstate.elementArrayBuffer.unbind();
	glEnableVertexAttribArray(program->a_position);
	glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);
	glDisableVertexAttribArray(program->a_position);

	glsl_unbind();
}

// x, y, w, h are relative coordinates against destW/destH, which is not very intuitive.
void FramebufferManager::DrawActiveTexture(GLuint texture, float x, float y, float w, float h, float destW, float destH, bool flip, float u0, float v0, float u1, float v1, GLSLProgram *program, int uvRotation) {
	if (flip) {
		// We're flipping, so 0 is downward.  Reverse everything from 1.0f.
		v0 = 1.0f - v0;
		v1 = 1.0f - v1;
	}

	float texCoords[8] = {
		u0,v0,
		u1,v0,
		u1,v1,
		u0,v1
	};

	static const GLushort indices[4] = {0,1,3,2};

	if (uvRotation != ROTATION_LOCKED_HORIZONTAL) {
		float temp[8];
		int rotation = 0;
		switch (uvRotation) {
		case ROTATION_LOCKED_HORIZONTAL180: rotation = 4; break;
		case ROTATION_LOCKED_VERTICAL: rotation = 2; break;
		case ROTATION_LOCKED_VERTICAL180: rotation = 6; break;
		}
		for (int i = 0; i < 8; i++) {
			temp[i] = texCoords[(i + rotation) & 7];
		}
		memcpy(texCoords, temp, sizeof(temp));
	}

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

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_Config.iBufFilter == SCALE_NEAREST ? GL_NEAREST : GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_Config.iBufFilter == SCALE_NEAREST ? GL_NEAREST : GL_LINEAR);

	shaderManager_->DirtyLastShader();  // dirty lastShader_

	glsl_bind(program);
	if (program == postShaderProgram_ && timeLoc_ != -1) {
		int flipCount = __DisplayGetFlipCount();
		int vCount = __DisplayGetVCount();
		float time[4] = {time_now(), (vCount % 60) * 1.0f/60.0f, (float)vCount, (float)(flipCount % 60)};
		glUniform4fv(timeLoc_, 1, time);
	}
	glstate.arrayBuffer.unbind();
	glstate.elementArrayBuffer.unbind();
	glEnableVertexAttribArray(program->a_position);
	glEnableVertexAttribArray(program->a_texcoord0);
	glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glVertexAttribPointer(program->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, indices);
	glDisableVertexAttribArray(program->a_position);
	glDisableVertexAttribArray(program->a_texcoord0);

	glsl_unbind();
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
	} else {
		fbo_unbind();
	}
	if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE)
		glstate.viewport.restore();
}

void FramebufferManager::ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force) {
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

	SetRenderSize(vfb);

	bool trueColor = g_Config.bTrueColor;
	if (hackForce04154000Download_ && vfb->fb_address == 0x00154000) {
		trueColor = true;
	}

	if (trueColor) {
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

	vfb->fbo = fbo_create(vfb->renderWidth, vfb->renderHeight, 1, true, (FBOColorDepth)vfb->colorDepth);
	if (old.fbo) {
		INFO_LOG(SCEGE, "Resizing FBO for %08x : %i x %i x %i", vfb->fb_address, w, h, vfb->format);
		if (vfb->fbo) {
			fbo_bind_as_render_target(vfb->fbo);
			ClearBuffer();
			if (!g_Config.bDisableSlowFramebufEffects) {
				BlitFramebuffer(vfb, 0, 0, &old, 0, 0, std::min(vfb->bufferWidth, vfb->width), std::min(vfb->height, vfb->bufferHeight), 0);
			}
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

void FramebufferManager::NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) {
	if (!useBufferedRendering_) {
		fbo_unbind();
		// Let's ignore rendering to targets that have not (yet) been displayed.
		gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
	}

	textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_CREATED);

	// Some AMD drivers crash if we don't clear the buffer first?
	glDisable(GL_DITHER);  // why?
	ClearBuffer();

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
	}
}

void FramebufferManager::NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb) {
	if (ShouldDownloadFramebuffer(vfb) && !vfb->memoryUpdated) {
		ReadFramebufferToMemory(vfb, true, 0, 0, vfb->width, vfb->height);
	}
	textureCache_->ForgetLastTexture();

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
	if (prevVfb && !g_Config.bDisableSlowFramebufEffects) {
		BlitFramebufferDepth(prevVfb, vfb);
	}
	if (vfb->drawnFormat != vfb->format) {
		// TODO: Might ultimately combine this with the resize step in DoSetRenderFrameBuffer().
		ReformatFramebufferFrom(vfb, vfb->drawnFormat);
	}

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
	}
}

void FramebufferManager::NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) {
	if (vfbFormatChanged) {
		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);
		if (vfb->drawnFormat != vfb->format) {
			ReformatFramebufferFrom(vfb, vfb->drawnFormat);
		}
	}

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
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

void FramebufferManager::ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) {
	if (!useBufferedRendering_ || !vfb->fbo) {
		return;
	}

	fbo_bind_as_render_target(vfb->fbo);

	// Technically, we should at this point re-interpret the bytes of the old format to the new.
	// That might get tricky, and could cause unnecessary slowness in some games.
	// For now, we just clear alpha/stencil from 565, which fixes shadow issues in Kingdom Hearts.
	// (it uses 565 to write zeros to the buffer, than 4444 to actually render the shadow.)
	//
	// The best way to do this may ultimately be to create a new FBO (combine with any resize?)
	// and blit with a shader to that, then replace the FBO on vfb.  Stencil would still be complex
	// to exactly reproduce in 4444 and 8888 formats.

	if (old == GE_FORMAT_565) {
		glstate.scissorTest.disable();
		glstate.depthWrite.set(GL_FALSE);
		glstate.colorMask.set(false, false, false, true);
		glstate.stencilFunc.set(GL_ALWAYS, 0, 0);
		glstate.stencilMask.set(0xFF);
		glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
		glClearStencil(0);
		glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}

	RebindFramebuffer();
}

void FramebufferManager::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) {
	if (!src->fbo || !dst->fbo || !useBufferedRendering_) {
		return;
	}

	// If depth wasn't updated, then we're at least "two degrees" away from the data.
	// This is an optimization: it probably doesn't need to be copied in this case.
	if (!src->depthUpdated) {
		return;
	}

	if (src->z_address == dst->z_address &&
		src->z_stride != 0 && dst->z_stride != 0 &&
		src->renderWidth == dst->renderWidth &&
		src->renderHeight == dst->renderHeight) {

#ifndef USING_GLES2
		if (gl_extensions.FBO_ARB) {
			bool useNV = false;
#else
		if (gl_extensions.GLES3 || gl_extensions.NV_framebuffer_blit) {
			bool useNV = !gl_extensions.GLES3;
#endif

			// Let's only do this if not clearing.
			if (!gstate.isModeClear() || !gstate.isClearModeDepthMask()) {
				fbo_bind_for_read(src->fbo);
				glDisable(GL_SCISSOR_TEST);

#if defined(USING_GLES2) && defined(ANDROID)  // We only support this extension on Android, it's not even available on PC.
				if (useNV) {
					glBlitFramebufferNV(0, 0, src->renderWidth, src->renderHeight, 0, 0, dst->renderWidth, dst->renderHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				} else 
#endif // defined(USING_GLES2) && defined(ANDROID)
					glBlitFramebuffer(0, 0, src->renderWidth, src->renderHeight, 0, 0, dst->renderWidth, dst->renderHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
				// If we set dst->depthUpdated here, our optimization above would be pointless.

				glstate.scissorTest.restore();
			}
		}
	}
}

FBO *FramebufferManager::GetTempFBO(u16 w, u16 h, FBOColorDepth depth) {
	u64 key = ((u64)depth << 32) | ((u32)w << 16) | h;
	auto it = tempFBOs_.find(key);
	if (it != tempFBOs_.end()) {
		it->second.last_frame_used = gpuStats.numFlips;
		return it->second.fbo;
	}

	textureCache_->ForgetLastTexture();
	FBO *fbo = fbo_create(w, h, 1, false, depth);
	if (!fbo)
		return fbo;
	fbo_bind_as_render_target(fbo);
	ClearBuffer();
	const TempFBO info = {fbo, gpuStats.numFlips};
	tempFBOs_[key] = info;
	return fbo;
}

void FramebufferManager::BindFramebufferColor(int stage, VirtualFramebuffer *framebuffer, bool skipCopy) {
	if (framebuffer == NULL) {
		framebuffer = currentRenderVfb_;
	}

	if (stage != GL_TEXTURE0) {
		glActiveTexture(stage);
	}

	if (!framebuffer->fbo || !useBufferedRendering_) {
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	if (GPUStepping::IsStepping() || g_Config.bDisableSlowFramebufEffects) {
		skipCopy = true;
	}
	if (!skipCopy && currentRenderVfb_ && framebuffer->fb_address == gstate.getFrameBufRawAddress()) {
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		FBO *renderCopy = GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, (FBOColorDepth)framebuffer->colorDepth);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;
			BlitFramebuffer(&copyInfo, 0, 0, framebuffer, 0, 0, framebuffer->drawnWidth, framebuffer->drawnHeight, 0, false);

			RebindFramebuffer();
			fbo_bind_color_as_texture(renderCopy, 0);
		} else {
			fbo_bind_color_as_texture(framebuffer->fbo, 0);
		}
	} else {
		fbo_bind_color_as_texture(framebuffer->fbo, 0);
	}

	if (stage != GL_TEXTURE0) {
		glActiveTexture(stage);
	}
}

struct CardboardSettings * FramebufferManager::GetCardboardSettings(struct CardboardSettings * cardboardSettings) {
	if (cardboardSettings) {
		// Calculate Cardboard Settings
		float cardboardScreenScale = g_Config.iCardboardScreenSize / 100.0f;
		float cardboardScreenWidth = PSP_CoreParameter().pixelWidth / 2.0f * cardboardScreenScale;
		float cardboardScreenHeight = PSP_CoreParameter().pixelHeight / 2.0f * cardboardScreenScale;
		float cardboardMaxXShift = (PSP_CoreParameter().pixelWidth / 2.0f - cardboardScreenWidth) / 2.0f;
		float cardboardUserXShift = g_Config.iCardboardXShift / 100.0f * cardboardMaxXShift;
		float cardboardLeftEyeX = cardboardMaxXShift + cardboardUserXShift;
		float cardboardRightEyeX = PSP_CoreParameter().pixelWidth / 2.0f + cardboardMaxXShift - cardboardUserXShift;
		float cardboardMaxYShift = PSP_CoreParameter().pixelHeight / 2.0f - cardboardScreenHeight / 2.0f;
		float cardboardUserYShift = g_Config.iCardboardYShift / 100.0f * cardboardMaxYShift;
		float cardboardScreenY = cardboardMaxYShift + cardboardUserYShift;

		// Copy current Settings into Structure
		cardboardSettings->enabled = g_Config.bEnableCardboard;
		cardboardSettings->leftEyeXPosition = cardboardLeftEyeX;
		cardboardSettings->rightEyeXPosition = cardboardRightEyeX;
		cardboardSettings->screenYPosition = cardboardScreenY;
		cardboardSettings->screenWidth = cardboardScreenWidth;
		cardboardSettings->screenHeight = cardboardScreenHeight;
	}

	return cardboardSettings;
}

void FramebufferManager::CopyDisplayToOutput() {
	fbo_unbind();
	glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
	currentRenderVfb_ = 0;

	u32 offsetX = 0;
	u32 offsetY = 0;

	struct CardboardSettings cardboardSettings;
	GetCardboardSettings(&cardboardSettings);

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
	vfb->last_frame_displayed = gpuStats.numFlips;
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

		int uvRotation = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;

		// Output coordinates
		float x, y, w, h;
		CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, uvRotation);

		// TODO ES3: Use glInvalidateFramebuffer to discard depth/stencil data at the end of frame.

		const float u0 = offsetX / (float)vfb->bufferWidth;
		const float v0 = offsetY / (float)vfb->bufferHeight;
		const float u1 = (480.0f + offsetX) / (float)vfb->bufferWidth;
		const float v1 = (272.0f + offsetY) / (float)vfb->bufferHeight;

		if (!usePostShader_) {
			if (cardboardSettings.enabled) {
				// Left Eye Image
				glstate.viewport.set(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1);

				// Right Eye Image
				glstate.viewport.set(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1);
			} else {
				// Fullscreen Image
				glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1, NULL, uvRotation);
			}
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

			if (g_Config.bEnableCardboard) {
				// Left Eye Image
				glstate.viewport.set(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1);

				// Right Eye Image
				glstate.viewport.set(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1);
			} else {
				// Fullscreen Image
				glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1, NULL, uvRotation);
			}

			if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
				fbo_bind_as_render_target(extraFBOs_[0]);
				GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
				glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, attachments);
			}
		} else {
			if (g_Config.bEnableCardboard) {
				// Left Eye Image
				glstate.viewport.set(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1);

				// Right Eye Image
				glstate.viewport.set(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1);
			} else {
				// Fullscreen Image
				glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, true, u0, v0, u1, v1, postShaderProgram_, uvRotation);
			}
		}

		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

inline bool FramebufferManager::ShouldDownloadUsingCPU(const VirtualFramebuffer *vfb) const {
#ifndef USING_GLES2
	bool useCPU = g_Config.iRenderingMode == FB_READFBOMEMORY_CPU;
	// We might get here if hackForce04154000Download_ is hit.
	// Some cards or drivers seem to always dither when downloading a framebuffer to 16-bit.
	// This causes glitches in games that expect the exact values.
	// It has not been experienced on NVIDIA cards, so those are left using the GPU (which is faster.)
	if (g_Config.iRenderingMode == FB_BUFFERED_MODE && gl_extensions.gpuVendor != GPU_VENDOR_NVIDIA) {
		useCPU = true;
	}
	return useCPU;
#else
	return  true;
#endif
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
			nvfb->drawnWidth = vfb->drawnWidth;
			nvfb->drawnHeight = vfb->drawnHeight;
			nvfb->drawnFormat = vfb->format;
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
			if (ShouldDownloadUsingCPU(vfb)) {
				nvfb->colorDepth = vfb->colorDepth;
			}

			textureCache_->ForgetLastTexture();
			nvfb->fbo = fbo_create(nvfb->width, nvfb->height, 1, false, (FBOColorDepth)nvfb->colorDepth);
			if (!(nvfb->fbo)) {
				ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
				delete nvfb;
				return;
			}

			nvfb->last_frame_render = gpuStats.numFlips;
			bvfbs_.push_back(nvfb);
			fbo_bind_as_render_target(nvfb->fbo);
			ClearBuffer();
			glDisable(GL_DITHER);
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
		BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0, true);

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
void FramebufferManager::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, bool flip) {
	if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
		// This can happen if they recently switched from non-buffered.
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

		fbo_unbind_read();
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
		glstate.viewport.restore();
	}

	glstate.scissorTest.restore();
}

// TODO: SSE/NEON
// Could also make C fake-simd for 64-bit, two 8888 pixels fit in a register :)
void ConvertFromRGBA8888(u8 *dst, const u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format) {
	// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
	const u32 *src32 = (const u32 *)src;

	if (format == GE_FORMAT_8888) {
		u32 *dst32 = (u32 *)dst;
		if (src == dst) {
			return;
		} else if (UseBGRA8888()) {
			for (u32 y = 0; y < height; ++y) {
				ConvertBGRA8888ToRGBA8888(dst32, src32, width);
				src32 += srcStride;
				dst32 += dstStride;
			}
		} else {
			// Here let's assume they don't intersect
			for (u32 y = 0; y < height; ++y) {
				memcpy(dst32, src32, width * 4);
				src32 += srcStride;
				dst32 += dstStride;
			}
		}
	} else {
		// But here it shouldn't matter if they do intersect
		u16 *dst16 = (u16 *)dst;
		switch (format) {
			case GE_FORMAT_565: // BGR 565
				if (UseBGRA8888()) {
					for (u32 y = 0; y < height; ++y) {
						ConvertBGRA8888ToRGB565(dst16, src32, width);
						src32 += srcStride;
						dst16 += dstStride;
					}
				} else {
					for (u32 y = 0; y < height; ++y) {
						ConvertRGBA8888ToRGB565(dst16, src32, width);
						src32 += srcStride;
						dst16 += dstStride;
					}
				}
				break;
			case GE_FORMAT_5551: // ABGR 1555
				if (UseBGRA8888()) {
					for (u32 y = 0; y < height; ++y) {
						ConvertBGRA8888ToRGBA5551(dst16, src32, width);
						src32 += srcStride;
						dst16 += dstStride;
					}
				} else {
					for (u32 y = 0; y < height; ++y) {
						ConvertRGBA8888ToRGBA5551(dst16, src32, width);
						src32 += srcStride;
						dst16 += dstStride;
					}
				}
				break;
			case GE_FORMAT_4444: // ABGR 4444
				if (UseBGRA8888()) {
					for (u32 y = 0; y < height; ++y) {
						ConvertBGRA8888ToRGBA4444(dst16, src32, width);
						src32 += srcStride;
						dst16 += dstStride;
					}
				} else {
					for (u32 y = 0; y < height; ++y) {
						ConvertRGBA8888ToRGBA4444(dst16, src32, width);
						src32 += srcStride;
						dst16 += dstStride;
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
	const u8 nextPBO = (currentPBO_ + 1) % MAX_PBO;
	const bool useCPU = ShouldDownloadUsingCPU(vfb);

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
				ConvertFromRGBA8888(dst, packed, pbo.stride, pbo.stride, pbo.stride, pbo.height, pbo.format);
			} else {
				// We don't need to convert, GPU already did (or should have)
				Memory::MemcpyUnchecked(pbo.fb_address, packed, pbo.size);
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
				align = 2;
				break;
			case GE_FORMAT_5551: // 16 bit RGBA
				pixelType = (reverseOrder ? GL_UNSIGNED_SHORT_1_5_5_5_REV : GL_UNSIGNED_SHORT_5_5_5_1);
				pixelFormat = GL_RGBA;
				pixelSize = 2;
				align = 2;
				break;
			case GE_FORMAT_565: // 16 bit RGB
				pixelType = (reverseOrder ? GL_UNSIGNED_SHORT_5_6_5_REV : GL_UNSIGNED_SHORT_5_6_5);
				pixelFormat = GL_RGB;
				pixelSize = 2;
				align = 2;
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
			fbo_unbind_read();
			return;
		}

		GLenum fbStatus;
#ifndef USING_GLES2
		if (!gl_extensions.FBO_ARB) {
			fbStatus = glCheckFramebufferStatusEXT(GL_READ_FRAMEBUFFER);
		} else {
			fbStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
		}
#else
		fbStatus = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
#endif

		if (fbStatus != GL_FRAMEBUFFER_COMPLETE) {
			ERROR_LOG(SCEGE, "Incomplete source framebuffer, aborting read");
			fbo_unbind_read();
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

		fbo_unbind_read();
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
		fbo_unbind_read();
		return;
	}

	// Pixel size always 4 here because we always request RGBA8888
	size_t bufSize = vfb->fb_stride * std::max(vfb->height, (u16)h) * 4;
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
			ConvertFromRGBA8888(Memory::GetPointer(fb_address + dstByteOffset), packed + byteOffset, vfb->fb_stride, vfb->fb_stride, vfb->width, h, vfb->format);
		}
	}

	if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
#ifdef USING_GLES2
		// GLES3 doesn't support using GL_READ_FRAMEBUFFER here.
		fbo_bind_as_render_target(vfb->fbo);
		const GLenum target = GL_FRAMEBUFFER;
#else
		const GLenum target = GL_READ_FRAMEBUFFER;
#endif
		GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
		glInvalidateFramebuffer(target, 3, attachments);
	}

	fbo_unbind_read();
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

	// Let's explicitly invalidate any temp FBOs used during this frame.
	if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
		for (auto temp : tempFBOs_) {
			if (temp.second.last_frame_used < gpuStats.numFlips) {
				continue;
			}

			fbo_bind_as_render_target(temp.second.fbo);
			GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_STENCIL_ATTACHMENT, GL_DEPTH_ATTACHMENT };
			glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, attachments);
		}
		fbo_unbind();
	}
}

void FramebufferManager::DeviceLost() {
	DestroyAllFBOs();
	DestroyDraw2DProgram();
	resized_ = false;
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
		int age = frameLastFramebufUsed_ - std::max(vfb->last_frame_render, vfb->last_frame_used);

		if (ShouldDownloadFramebuffer(vfb) && age == 0 && !vfb->memoryUpdated) {
#ifdef USING_GLES2
			bool sync = true;
#else
			bool sync = false;
#endif
			ReadFramebufferToMemory(vfb, sync, 0, 0, vfb->width, vfb->height);
		}

		// Let's also "decimate" the usageFlags.
		UpdateFramebufUsage(vfb);

		if (vfb != displayFramebuf_ && vfb != prevDisplayFramebuf_ && vfb != prevPrevDisplayFramebuf_) {
			if (age > FBO_OLD_AGE) {
				INFO_LOG(SCEGE, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age);
				DestroyFramebuf(vfb);
				vfbs_.erase(vfbs_.begin() + i--);
			}
		}
	}

	for (auto it = tempFBOs_.begin(); it != tempFBOs_.end(); ) {
		int age = frameLastFramebufUsed_ - it->second.last_frame_used;
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
		int age = frameLastFramebufUsed_ - vfb->last_frame_render;
		if (age > FBO_OLD_AGE) {
			INFO_LOG(SCEGE, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age);
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

	for (size_t i = 0; i < bvfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = bvfbs_[i];
		DestroyFramebuf(vfb);
	}
	bvfbs_.clear();

	for (auto it = tempFBOs_.begin(), end = tempFBOs_.end(); it != end; ++it) {
		fbo_destroy(it->second.fbo);
	}
	tempFBOs_.clear();

	fbo_unbind();
	DisableState();
}

void FramebufferManager::FlushBeforeCopy() {
	// Flush anything not yet drawn before blitting, downloading, or uploading.
	// This might be a stalled list, or unflushed before a block transfer, etc.
	SetRenderFrameBuffer();
	transformDraw_->Flush();
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

bool FramebufferManager::GetDisplayFramebuffer(GPUDebugBuffer &buffer) {
	fbo_unbind_read();
	buffer.Allocate(PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, GPU_DBG_FORMAT_888_RGB, true);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, GL_RGB, GL_UNSIGNED_BYTE, buffer.GetData());
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
