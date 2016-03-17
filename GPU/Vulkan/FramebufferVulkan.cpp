// Copyright (c) 2015- PPSSPP Project.

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

#include "profiler/profiler.h"

#include "base/timeutil.h"
#include "math/lin/matrix4x4.h"

#include "Common/Vulkan/VulkanContext.h"
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

#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"
#include "Common/Vulkan/VulkanImage.h"
#include "GPU/Vulkan/FramebufferVulkan.h"
#include "GPU/Vulkan/DrawEngineVulkan.h"
#include "GPU/Vulkan/TextureCacheVulkan.h"
#include "GPU/Vulkan/ShaderManagerVulkan.h"
#include "GPU/Vulkan/VulkanUtil.h"

#include "UI/OnScreenDisplay.h"

extern int g_iNumVideos;


static const char tex_fs[] =
"layout (binding = 0) uniform sampler2D sampler0;\n"
"layout (location = 0) in vec2 v_texcoord0;\n"
"void main() {\n"
"  gl_FragColor = texture2D(sampler0, v_texcoord0);\n"
"}\n";

static const char basic_vs[] =
"layout (location = 0) in vec4 a_position;\n"
"layout (location = 1) in attribute vec2 a_texcoord0;\n"
"layout (location = 0) out vec2 v_texcoord0;\n"
"void main() {\n"
"  v_texcoord0 = a_texcoord0;\n"
"  gl_Position = a_position;\n"
"}\n";

void ConvertFromRGBA8888_Vulkan(u8 *dst, const u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format);

void FramebufferManagerVulkan::ClearBuffer(bool keepState) {
	// keepState is irrelevant.
	if (!currentRenderVfb_) {
		return;
	}
	VkClearAttachment clear[2];
	memset(clear, 0, sizeof(clear));
	clear[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	clear[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	VkClearRect rc;
	rc.baseArrayLayer = 0;
	rc.layerCount = 1;
	rc.rect.offset.x = 0;
	rc.rect.offset.y = 0;
	rc.rect.extent.width = currentRenderVfb_->bufferWidth;
	rc.rect.extent.height = currentRenderVfb_->bufferHeight;
	vkCmdClearAttachments(curCmd_, 2, clear, 1, &rc);
}

void FramebufferManagerVulkan::DisableState() {
}

void FramebufferManagerVulkan::SetNumExtraFBOs(int num) {
	/*
	for (size_t i = 0; i < extraFBOs_.size(); i++) {
		fbo_destroy(extraFBOs_[i]);
	}
	extraFBOs_.clear();
	for (int i = 0; i < num; i++) {
		// No depth/stencil for post processing
		FBO *fbo_vk = fbo_create(renderWidth_, renderHeight_, 1, false, FBO_8888);
		extraFBOs_.push_back(fbo_vk);

		// The new FBO is still bound after creation, but let's bind it anyway.
		fbo_bind_as_render_target(fbo_vk);
		ClearBuffer();
	}

	currentRenderVfb_ = 0;
	fbo_unbind();
	*/
}

void FramebufferManagerVulkan::CompileDraw2DProgram() {
	/*
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
				deltaLoc_ = glsl_uniform_loc(postShaderProgram_, "u_texelDelta");
				pixelDeltaLoc_ = glsl_uniform_loc(postShaderProgram_, "u_pixelDelta");
				timeLoc_ = glsl_uniform_loc(postShaderProgram_, "u_time");
				usePostShader_ = true;
			}
		} else {
			postShaderProgram_ = nullptr;
			usePostShader_ = false;
		}

		glsl_unbind();
	}
	*/
}

void FramebufferManagerVulkan::UpdatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight) {
	float u_delta = 1.0f / renderWidth;
	float v_delta = 1.0f / renderHeight;
	float u_pixel_delta = u_delta;
	float v_pixel_delta = v_delta;
	if (postShaderAtOutputResolution_) {
		float x, y, w, h;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, ROTATION_LOCKED_HORIZONTAL);
		u_pixel_delta = (1.0f / w) * (480.0f / bufferWidth);
		v_pixel_delta = (1.0f / h) * (272.0f / bufferHeight);
	}

	postUniforms_.texelDelta[0] = u_delta;
	postUniforms_.texelDelta[1] = v_delta;
	postUniforms_.pixelDelta[0] = u_pixel_delta;
	postUniforms_.pixelDelta[1] = v_pixel_delta;
	int flipCount = __DisplayGetFlipCount();
	int vCount = __DisplayGetVCount();
	float time[4] = { time_now(), (vCount % 60) * 1.0f / 60.0f, (float)vCount, (float)(flipCount % 60) };
	memcpy(postUniforms_.time, time, 4 * sizeof(float));
}

void FramebufferManagerVulkan::DestroyDraw2DProgram() {
	/*
	if (draw2dprogram_) {
		glsl_destroy(draw2dprogram_);
		draw2dprogram_ = nullptr;
	}
	if (postShaderProgram_) {
		glsl_destroy(postShaderProgram_);
		postShaderProgram_ = nullptr;
	}
	*/
}

FramebufferManagerVulkan::FramebufferManagerVulkan(VulkanContext *vulkan) :
	vulkan_(vulkan),
	drawPixelsTex_(0),
	drawPixelsTexFormat_(GE_FORMAT_INVALID),
	convBuf_(nullptr),
	textureCache_(nullptr),
	shaderManager_(nullptr),
	resized_(false),
	pixelBufObj_(nullptr),
	currentPBO_(0) {
}

FramebufferManagerVulkan::~FramebufferManagerVulkan() {
	/*
	if (drawPixelsTex_)
	glDeleteTextures(1, &drawPixelsTex_);
	DestroyDraw2DProgram();
	if (stencilUploadProgram_) {
	glsl_destroy(stencilUploadProgram_);
	}
	SetNumExtraFBOs(0);

	for (auto it = tempFBOs_.begin(), end = tempFBOs_.end(); it != end; ++it) {
	fbo_destroy(it->second.fbo_vk);
	}

	delete[] pixelBufObj_;
	delete[] convBuf_;
	*/
}

void FramebufferManagerVulkan::Init() {
	FramebufferManagerCommon::Init();
	// Workaround for upscaling shaders where we force x1 resolution without saving it
	resized_ = true;
	CompileDraw2DProgram();
}

void FramebufferManagerVulkan::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	/*
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
			delete[] convBuf_;
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
	*/
}

void FramebufferManagerVulkan::DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	/*
	float v0 = 0.0f, v1 = 1.0f;
	if (useBufferedRendering_ && vfb && vfb->fbo_vk) {
		fbo_bind_as_render_target(vfb->fbo_vk);
		glViewport(0, 0, vfb->renderWidth, vfb->renderHeight);
	} else {
		// We are drawing to the back buffer so need to flip.
		v0 = 1.0f;
		v1 = 0.0f;
		float x, y, w, h;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, ROTATION_LOCKED_HORIZONTAL);
		glViewport(x, y, w, h);
	}

	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, width, height);

	DrawActiveTexture(0, dstX, dstY, width, height, vfb->bufferWidth, vfb->bufferHeight, 0.0f, v0, 1.0f, v1, nullptr, ROTATION_LOCKED_HORIZONTAL);
	textureCache_->ForgetLastTexture();
	*/
}

void FramebufferManagerVulkan::DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) {
	/*
	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, 512, 272);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_Config.iTexFiltering == TEX_FILTER_NEAREST ? GL_NEAREST : GL_LINEAR);

	struct CardboardSettings cardboardSettings;
	GetCardboardSettings(&cardboardSettings);

	// This might draw directly at the backbuffer (if so, applyPostShader is set) so if there's a post shader, we need to apply it here.
	// Should try to unify this path with the regular path somehow, but this simple solution works for most of the post shaders 
	// (it always runs at output resolution so FXAA may look odd).
	float x, y, w, h;
	int uvRotation = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
	CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, uvRotation);
	if (applyPostShader) {
		// Make sure we've compiled the shader.
		if (!postShaderProgram_) {
			CompileDraw2DProgram();
		}
		// Might've changed if the shader was just changed to Off.
		if (usePostShader_) {
			glsl_bind(postShaderProgram_);
			UpdatePostShaderUniforms(480, 272, renderWidth_, renderHeight_);
		}
	}
	float u0 = 0.0f, u1 = 480.0f / 512.0f;
	float v0 = 0.0f, v1 = 1.0f;

	// We are drawing directly to the back buffer.
	std::swap(v0, v1);

	if (cardboardSettings.enabled) {
		// Left Eye Image
		glstate.viewport.set(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
		if (applyPostShader && usePostShader_ && useBufferedRendering_) {
			DrawActiveTexture(0, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, postShaderProgram_, ROTATION_LOCKED_HORIZONTAL);
		} else {
			DrawActiveTexture(0, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, ROTATION_LOCKED_HORIZONTAL);
		}

		// Right Eye Image
		glstate.viewport.set(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
		if (applyPostShader && usePostShader_ && useBufferedRendering_) {
			DrawActiveTexture(0, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, postShaderProgram_, ROTATION_LOCKED_HORIZONTAL);
		} else {
			DrawActiveTexture(0, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, ROTATION_LOCKED_HORIZONTAL);
		}
	} else {
		// Fullscreen Image
		glstate.viewport.set(0, 0, pixelWidth_, pixelHeight_);
		if (applyPostShader && usePostShader_ && useBufferedRendering_) {
			DrawActiveTexture(0, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, postShaderProgram_, uvRotation);
		} else {
			DrawActiveTexture(0, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, uvRotation);
		}
	}
	*/
}

// x, y, w, h are relative coordinates against destW/destH, which is not very intuitive.
void FramebufferManagerVulkan::DrawActiveTexture(VulkanTexture *texture, float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, VkPipeline pipeline, int uvRotation) {
	/*
	float texCoords[8] = {
		u0,v0,
		u1,v0,
		u1,v1,
		u0,v1,
	};

	static const GLubyte indices[4] = { 0,1,3,2 };

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
		// Previously had NVDrawTexture fallback here but wasn't worth it.
		glBindTexture(GL_TEXTURE_2D, texture);
	}

	float pos[12] = {
		x,y,0,
		x + w,y,0,
		x + w,y + h,0,
		x,y + h,0
	};

	float invDestW = 1.0f / (destW * 0.5f);
	float invDestH = 1.0f / (destH * 0.5f);
	for (int i = 0; i < 4; i++) {
		pos[i * 3] = pos[i * 3] * invDestW - 1.0f;
		pos[i * 3 + 1] = pos[i * 3 + 1] * invDestH - 1.0f;
	}

	if (!program) {
		if (!draw2dprogram_) {
			CompileDraw2DProgram();
		}

		program = draw2dprogram_;
	}

	// Upscaling postshaders doesn't look well with linear
	if (postShaderIsUpscalingFilter_) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, g_Config.iBufFilter == SCALE_NEAREST ? GL_NEAREST : GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, g_Config.iBufFilter == SCALE_NEAREST ? GL_NEAREST : GL_LINEAR);
	}

	if (program != postShaderProgram_) {
		shaderManager_->DirtyLastShader();  // dirty lastShader_
		glsl_bind(program);
	}

	glEnableVertexAttribArray(program->a_position);
	glEnableVertexAttribArray(program->a_texcoord0);
	if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
		transformDraw_->BindBuffer(pos, sizeof(pos), texCoords, sizeof(texCoords));
		transformDraw_->BindElementBuffer(indices, sizeof(indices));
		glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, 12, 0);
		glVertexAttribPointer(program->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, (void *)sizeof(pos));
		glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, 0);
	} else {
		glstate.arrayBuffer.unbind();
		glstate.elementArrayBuffer.unbind();
		glVertexAttribPointer(program->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
		glVertexAttribPointer(program->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);
		glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, indices);
	}
	glDisableVertexAttribArray(program->a_position);
	glDisableVertexAttribArray(program->a_texcoord0);

	glsl_unbind();
	*/
}

void FramebufferManagerVulkan::DestroyFramebuf(VirtualFramebuffer *v) {
	textureCache_->NotifyFramebuffer(v->fb_address, v, NOTIFY_FB_DESTROYED);
	if (v->fbo_vk) {
		delete v->fbo_vk;
		v->fbo_vk = 0;
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

void FramebufferManagerVulkan::RebindFramebuffer() {
	/*
	if (currentRenderVfb_ && currentRenderVfb_->fbo_vk) {
		ImagcurrentRenderVfb_->fbo_vk->GetColorImageView();
	} else {
		fbo_unbind();
	}
	if (g_Config.iRenderingMode == FB_NON_BUFFERED_MODE)
		glstate.viewport.restore();
		*/
}

void FramebufferManagerVulkan::ResizeFramebufFBO(VirtualFramebuffer *vfb, u16 w, u16 h, bool force) {
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
		vfb->colorDepth = VK_FBO_8888;
	} else {
		switch (vfb->format) {
		case GE_FORMAT_4444:
			vfb->colorDepth = VK_FBO_4444;
			break;
		case GE_FORMAT_5551:
			vfb->colorDepth = VK_FBO_5551;
			break;
		case GE_FORMAT_565:
			vfb->colorDepth = VK_FBO_565;
			break;
		case GE_FORMAT_8888:
		default:
			vfb->colorDepth = VK_FBO_8888;
			break;
		}
	}

	textureCache_->ForgetLastTexture();

	if (!useBufferedRendering_) {
		if (vfb->fbo_vk) {
			delete vfb->fbo_vk;
			vfb->fbo_vk = 0;
		}
		return;
	}

	vfb->fbo_vk = new VulkanFBO();
	// bo_create(vfb->renderWidth, vfb->renderHeight, 1, true, (FBOColorDepth)vfb->colorDepth);
	if (old.fbo_vk) {
		INFO_LOG(SCEGE, "Resizing FBO for %08x : %i x %i x %i", vfb->fb_address, w, h, vfb->format);
		if (vfb->fbo_vk) {
			/// fbo_bind_as_render_target(vfb->fbo_vk);
			ClearBuffer();
			if (!g_Config.bDisableSlowFramebufEffects) {
				BlitFramebuffer(vfb, 0, 0, &old, 0, 0, std::min(vfb->bufferWidth, vfb->width), std::min(vfb->height, vfb->bufferHeight), 0);
			}
		}
		delete old.fbo_vk;
		if (vfb->fbo_vk) {
			// fbo_bind_as_render_target(vfb->fbo_vk);
		}
	}

	if (!vfb->fbo_vk) {
		ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", vfb->renderWidth, vfb->renderHeight);
	}
}

void FramebufferManagerVulkan::NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) {
	if (!useBufferedRendering_) {
		// Let's ignore rendering to targets that have not (yet) been displayed.
		gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
	}

	textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_CREATED);

	// Some AMD drivers crash if we don't clear the buffer first?
	/*
	glDisable(GL_DITHER);  // why?
	ClearBuffer();
	*/

	// ugly...
	if ((gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) && shaderManager_) {
		shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
	}
}

void FramebufferManagerVulkan::NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth) {
	if (ShouldDownloadFramebuffer(vfb) && !vfb->memoryUpdated) {
		ReadFramebufferToMemory(vfb, true, 0, 0, vfb->width, vfb->height);
	}
	textureCache_->ForgetLastTexture();

	if (useBufferedRendering_) {
		if (vfb->fbo_vk) {
			// vfb->fbo_vk->GetColorImageView();
		}
	} else {
		if (vfb->fbo_vk) {
			// wtf? This should only happen very briefly when toggling bBufferedRendering
			textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_DESTROYED);
			delete vfb->fbo_vk;
			vfb->fbo_vk = nullptr;
		}

		// Let's ignore rendering to targets that have not (yet) been displayed.
		if (vfb->usageFlags & FB_USAGE_DISPLAYED_FRAMEBUFFER) {
			gstate_c.skipDrawReason &= ~SKIPDRAW_NON_DISPLAYED_FB;
		} else {
			gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
		}
	}
	textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);

	// Copy depth pixel value from the read framebuffer to the draw framebuffer
	if (prevVfb && !g_Config.bDisableSlowFramebufEffects) {
		if (!prevVfb->fbo_vk || !vfb->fbo_vk || !useBufferedRendering_ || !prevVfb->depthUpdated || isClearingDepth) {
			// If depth wasn't updated, then we're at least "two degrees" away from the data.
			// This is an optimization: it probably doesn't need to be copied in this case.
		} else {
			BlitFramebufferDepth(prevVfb, vfb);
		}
	}
	if (vfb->drawnFormat != vfb->format) {
		// TODO: Might ultimately combine this with the resize step in DoSetRenderFrameBuffer().
		ReformatFramebufferFrom(vfb, vfb->drawnFormat);
	}

	// ugly...
	if ((gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) && shaderManager_) {
		shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
	}
}

void FramebufferManagerVulkan::NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) {
	if (vfbFormatChanged) {
		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);
		if (vfb->drawnFormat != vfb->format) {
			ReformatFramebufferFrom(vfb, vfb->drawnFormat);
		}
	}

	// ugly...
	if ((gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) && shaderManager_) {
		shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);
	}
}

bool FramebufferManagerVulkan::NotifyStencilUpload(u32 addr, int size, bool skipZero) {
	// In Vulkan we should be able to simply copy the stencil data directly to a stencil buffer without
	// messing about with bitplane textures and the like.
	return false;
}


int FramebufferManagerVulkan::GetLineWidth() {
	if (g_Config.iInternalResolution == 0) {
		return std::max(1, (int)(renderWidth_ / 480));
	} else {
		return g_Config.iInternalResolution;
	}
}

void FramebufferManagerVulkan::ReformatFramebufferFrom(VirtualFramebuffer *vfb, GEBufferFormat old) {
	if (!useBufferedRendering_ || !vfb->fbo_vk) {
		return;
	}
	/*
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
	*/
}

void FramebufferManagerVulkan::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) {
	/*
	if (src->z_address == dst->z_address &&
		src->z_stride != 0 && dst->z_stride != 0 &&
		src->renderWidth == dst->renderWidth &&
		src->renderHeight == dst->renderHeight) {

		if (gstate_c.Supports(GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT | GPU_SUPPORTS_NV_FRAMEBUFFER_BLIT)) {
			// Only use NV if ARB isn't supported.
			bool useNV = !gstate_c.Supports(GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT);

			// Let's only do this if not clearing depth.
			fbo_bind_for_read(src->fbo);
			glstate.scissorTest.force(false);

			if (useNV) {
#if defined(USING_GLES2) && defined(ANDROID)  // We only support this extension on Android, it's not even available on PC.
				glBlitFramebufferNV(0, 0, src->renderWidth, src->renderHeight, 0, 0, dst->renderWidth, dst->renderHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
#endif // defined(USING_GLES2) && defined(ANDROID)
			} else {
				glBlitFramebuffer(0, 0, src->renderWidth, src->renderHeight, 0, 0, dst->renderWidth, dst->renderHeight, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
			}
			// If we set dst->depthUpdated here, our optimization above would be pointless.

			glstate.scissorTest.restore();
		}
	}*/
}

VulkanFBO *FramebufferManagerVulkan::GetTempFBO(u16 w, u16 h, VulkanFBOColorDepth depth) {
	u64 key = ((u64)depth << 32) | ((u32)w << 16) | h;
	auto it = tempFBOs_.find(key);
	if (it != tempFBOs_.end()) {
		it->second.last_frame_used = gpuStats.numFlips;
		return it->second.fbo_vk;
	}

	textureCache_->ForgetLastTexture();
	// FBO *fbo_vk = fbo_create(w, h, 1, false, depth);
	VulkanFBO *fbo_vk = new VulkanFBO();
	if (!fbo_vk)
		return nullptr;
	//	 fbo_bind_as_render_target(fbo_vk);
	// fbo_vk->GetColorImageView()
	// ClearBuffer(true);
	const TempFBO info = { fbo_vk, gpuStats.numFlips };
	tempFBOs_[key] = info;
	return fbo_vk;
}

void FramebufferManagerVulkan::BindFramebufferColor(int stage, u32 fbRawAddress, VirtualFramebuffer *framebuffer, int flags) {
	if (framebuffer == NULL) {
		framebuffer = currentRenderVfb_;
	}

	/*
	if (stage != GL_TEXTURE0) {
		glActiveTexture(stage);
	}

	if (!framebuffer->fbo_vk || !useBufferedRendering_) {
		glBindTexture(GL_TEXTURE_2D, 0);
		glActiveTexture(GL_TEXTURE0);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	bool skipCopy = (flags & BINDFBCOLOR_MAY_COPY) == 0;
	if (GPUStepping::IsStepping() || g_Config.bDisableSlowFramebufEffects) {
		skipCopy = true;
	}
	if (!skipCopy && currentRenderVfb_ && framebuffer->fb_address == fbRawAddress) {
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		fbo_vk *renderCopy = GetTempFBO(framebuffer->renderWidth, framebuffer->renderHeight, (FBOColorDepth)framebuffer->colorDepth);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo_vk = renderCopy;

			int x = 0;
			int y = 0;
			int w = framebuffer->drawnWidth;
			int h = framebuffer->drawnHeight;

			// If max is not > min, we probably could not detect it.  Skip.
			// See the vertex decoder, where this is updated.
			if ((flags & BINDFBCOLOR_MAY_COPY_WITH_UV) == BINDFBCOLOR_MAY_COPY_WITH_UV && gstate_c.vertBounds.maxU > gstate_c.vertBounds.minU) {
				x = gstate_c.vertBounds.minU;
				y = gstate_c.vertBounds.minV;
				w = gstate_c.vertBounds.maxU - x;
				h = gstate_c.vertBounds.maxV - y;

				// If we bound a framebuffer, apply the byte offset as pixels to the copy too.
				if (flags & BINDFBCOLOR_APPLY_TEX_OFFSET) {
					x += gstate_c.curTextureXOffset;
					y += gstate_c.curTextureYOffset;
				}
			}

			BlitFramebuffer(&copyInfo, x, y, framebuffer, x, y, w, h, 0);

			fbo_bind_color_as_texture(renderCopy, 0);
		} else {
			fbo_bind_color_as_texture(framebuffer->fbo_vk, 0);
		}
	} else {
		fbo_bind_color_as_texture(framebuffer->fbo_vk, 0);
	}

	if (stage != GL_TEXTURE0) {
		glActiveTexture(stage);
	}
	*/
}

struct CardboardSettings * FramebufferManagerVulkan::GetCardboardSettings(struct CardboardSettings * cardboardSettings) {
	if (cardboardSettings) {
		// Calculate Cardboard Settings
		float cardboardScreenScale = g_Config.iCardboardScreenSize / 100.0f;
		float cardboardScreenWidth = pixelWidth_ / 2.0f * cardboardScreenScale;
		float cardboardScreenHeight = pixelHeight_ / 2.0f * cardboardScreenScale;
		float cardboardMaxXShift = (pixelWidth_ / 2.0f - cardboardScreenWidth) / 2.0f;
		float cardboardUserXShift = g_Config.iCardboardXShift / 100.0f * cardboardMaxXShift;
		float cardboardLeftEyeX = cardboardMaxXShift + cardboardUserXShift;
		float cardboardRightEyeX = pixelWidth_ / 2.0f + cardboardMaxXShift - cardboardUserXShift;
		float cardboardMaxYShift = pixelHeight_ / 2.0f - cardboardScreenHeight / 2.0f;
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

void FramebufferManagerVulkan::CopyDisplayToOutput() {
	// This is where we should collect all the renderpasses from this frame,
	// sort them in order according to texturing dependencies, and enqueue
	// them on the Vulkan context.
	
	// Then, we will simply perform a blit of the currently displayed framebuffer to the backbuffer.
	// If there's no extra graphics to draw like framerate counters or controls,
	// then in theory, we can even avoid starting up a render pass at all for the backbuffer (!). not sure if that
	// is worth the needed refactoring trouble though.

	// fbo_unbind();
	// glstate.viewport.set(0, 0, pixelWidth_, pixelHeight_);

	currentRenderVfb_ = 0;

	if (useBufferedRendering_) {
		// TODO: Clear here. Although it will be done through the surface pass instead..
	}

	if (displayFramebufPtr_ == 0) {
		DEBUG_LOG(SCEGE, "Display disabled, displaying only black");
		// No framebuffer to display! Clear to black.
		ClearBuffer();
		return;
	}

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
				DrawFramebufferToOutput(Memory::GetPointer(displayFramebufPtr_), displayFormat_, displayStride_, true);
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

	if (vfb->fbo_vk) {
		DEBUG_LOG(SCEGE, "Displaying FBO %08x", vfb->fb_address);

		// We should not be in a renderpass here so can just copy.

		/*
		GLuint colorTexture = fbo_get_color_texture(vfb->fbo_vk);

		int uvRotation = (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE) ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;

		// Output coordinates
		float x, y, w, h;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, uvRotation);

		// TODO ES3: Use glInvalidateFramebuffer to discard depth/stencil data at the end of frame.

		float u0 = offsetX / (float)vfb->bufferWidth;
		float v0 = offsetY / (float)vfb->bufferHeight;
		float u1 = (480.0f + offsetX) / (float)vfb->bufferWidth;
		float v1 = (272.0f + offsetY) / (float)vfb->bufferHeight;

		if (!usePostShader_) {
			// We are doing the DrawActiveTexture call directly to the backbuffer here. Hence, we must
			// flip V.
			std::swap(v0, v1);
			if (cardboardSettings.enabled) {
				// Left Eye Image
				glstate.viewport.set(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, ROTATION_LOCKED_HORIZONTAL);

				// Right Eye Image
				glstate.viewport.set(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, ROTATION_LOCKED_HORIZONTAL);
			} else {
				// Fullscreen Image
				glstate.viewport.set(0, 0, pixelWidth_, pixelHeight_);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, uvRotation);
			}
		} else if (usePostShader_ && extraFBOs_.size() == 1 && !postShaderAtOutputResolution_) {
			// An additional pass, post-processing shader to the extra FBO.
			fbo_bind_as_render_target(extraFBOs_[0]);
			int fbo_w, fbo_h;
			fbo_get_dimensions(extraFBOs_[0], &fbo_w, &fbo_h);
			glstate.viewport.set(0, 0, fbo_w, fbo_h);
			shaderManager_->DirtyLastShader();  // dirty lastShader_
			glsl_bind(postShaderProgram_);
			UpdatePostShaderUniforms(vfb->bufferWidth, vfb->bufferHeight, renderWidth_, renderHeight_);
			DrawActiveTexture(colorTexture, 0, 0, fbo_w, fbo_h, fbo_w, fbo_h, 0.0f, 0.0f, 1.0f, 1.0f, postShaderProgram_, ROTATION_LOCKED_HORIZONTAL);

			fbo_unbind();

			// Use the extra FBO, with applied post-processing shader, as a texture.
			// fbo_bind_color_as_texture(extraFBOs_[0], 0);
			if (extraFBOs_.size() == 0) {
				ERROR_LOG(G3D, "WTF?");
				return;
			}
			colorTexture = fbo_get_color_texture(extraFBOs_[0]);

			// We are doing the DrawActiveTexture call directly to the backbuffer after here. Hence, we must
			// flip V.
			std::swap(v0, v1);

			if (g_Config.bEnableCardboard) {
				// Left Eye Image
				glstate.viewport.set(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, ROTATION_LOCKED_HORIZONTAL);

				// Right Eye Image
				glstate.viewport.set(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, ROTATION_LOCKED_HORIZONTAL);
			} else {
				// Fullscreen Image
				glstate.viewport.set(0, 0, pixelWidth_, pixelHeight_);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, uvRotation);
			}

			if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
				fbo_bind_as_render_target(extraFBOs_[0]);
				GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
				glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, attachments);
			}
		} else {
			// We are doing the DrawActiveTexture call directly to the backbuffer here. Hence, we must
			// flip V.
			std::swap(v0, v1);

			shaderManager_->DirtyLastShader();  // dirty lastShader_
			glsl_bind(postShaderProgram_);
			UpdatePostShaderUniforms(vfb->bufferWidth, vfb->bufferHeight, vfb->renderWidth, vfb->renderHeight);
			if (g_Config.bEnableCardboard) {
				// Left Eye Image
				glstate.viewport.set(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, ROTATION_LOCKED_HORIZONTAL);

				// Right Eye Image
				glstate.viewport.set(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, nullptr, ROTATION_LOCKED_HORIZONTAL);
			} else {
				// Fullscreen Image
				glstate.viewport.set(0, 0, pixelWidth_, pixelHeight_);
				DrawActiveTexture(colorTexture, x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, postShaderProgram_, uvRotation);
			}
		}

		glBindTexture(GL_TEXTURE_2D, 0);
		*/
	}
}

void FramebufferManagerVulkan::ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) {
	PROFILE_THIS_SCOPE("gpu-readback");
	if (sync) {
		// flush async just in case when we go for synchronous update
		// Doesn't actually pack when sent a null argument.
		PackFramebufferAsync_(nullptr);
	}

	if (vfb) {
		// We'll pseudo-blit framebuffers here to get a resized version of vfb.
		VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
		OptimizeDownloadRange(vfb, x, y, w, h);
		BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0);

		// PackFramebufferSync_() - Synchronous pixel data transfer using glReadPixels
		// PackFramebufferAsync_() - Asynchronous pixel data transfer using glReadPixels with PBOs

		// TODO: Can we fall back to sync without these?
		if (!sync) {
			PackFramebufferAsync_(nvfb);
		} else {
			PackFramebufferSync_(nvfb, x, y, w, h);
		}

		textureCache_->ForgetLastTexture();
		RebindFramebuffer();
	}
}

void FramebufferManagerVulkan::DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) {
	PROFILE_THIS_SCOPE("gpu-readback");
	// Flush async just in case.
	PackFramebufferAsync_(nullptr);

	VirtualFramebuffer *vfb = GetVFBAt(fb_address);
	if (vfb && vfb->fb_stride != 0) {
		const u32 bpp = vfb->drawnFormat == GE_FORMAT_8888 ? 4 : 2;
		int x = 0;
		int y = 0;
		int pixels = loadBytes / bpp;
		// The height will be 1 for each stride or part thereof.
		int w = std::min(pixels % vfb->fb_stride, (int)vfb->width);
		int h = std::min((pixels + vfb->fb_stride - 1) / vfb->fb_stride, (int)vfb->height);

		// No need to download if we already have it.
		if (!vfb->memoryUpdated && vfb->clutUpdatedBytes < loadBytes) {
			// We intentionally don't call OptimizeDownloadRange() here - we don't want to over download.
			// CLUT framebuffers are often incorrectly estimated in size.
			if (x == 0 && y == 0 && w == vfb->width && h == vfb->height) {
				vfb->memoryUpdated = true;
			}
			vfb->clutUpdatedBytes = loadBytes;

			// We'll pseudo-blit framebuffers here to get a resized version of vfb.
			VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
			BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0);

			PackFramebufferSync_(nvfb, x, y, w, h);

			textureCache_->ForgetLastTexture();
			RebindFramebuffer();
		}
	}
}

bool FramebufferManagerVulkan::CreateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	// When updating VRAM, it need to be exact format.
	if (!gstate_c.Supports(GPU_PREFER_CPU_DOWNLOAD)) {
		switch (nvfb->format) {
		case GE_FORMAT_4444:
			nvfb->colorDepth = VK_FBO_4444;
			break;
		case GE_FORMAT_5551:
			nvfb->colorDepth = VK_FBO_5551;
			break;
		case GE_FORMAT_565:
			nvfb->colorDepth = VK_FBO_565;
			break;
		case GE_FORMAT_8888:
		default:
			nvfb->colorDepth = VK_FBO_8888;
			break;
		}
	}

	/*
	nvfb->fbo = fbo_create(nvfb->width, nvfb->height, 1, false, (FBOColorDepth)nvfb->colorDepth);
	if (!(nvfb->fbo)) {
		ERROR_LOG(SCEGE, "Error creating FBO! %i x %i", nvfb->renderWidth, nvfb->renderHeight);
		return false;
	}

	fbo_bind_as_render_target(nvfb->fbo);
	ClearBuffer();
	glDisable(GL_DITHER);
	*/
	return true;
}

void FramebufferManagerVulkan::UpdateDownloadTempBuffer(VirtualFramebuffer *nvfb) {
	_assert_msg_(G3D, nvfb->fbo, "Expecting a valid nvfb in UpdateDownloadTempBuffer");

	// Discard the previous contents of this buffer where possible.
	/*
	if (gl_extensions.GLES3 && glInvalidateFramebuffer != nullptr) {
		fbo_bind_as_render_target(nvfb->fbo);
		GLenum attachments[3] = { GL_COLOR_ATTACHMENT0, GL_STENCIL_ATTACHMENT, GL_DEPTH_ATTACHMENT };
		glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, attachments);
	} else if (gl_extensions.IsGLES) {
		fbo_bind_as_render_target(nvfb->fbo);
		ClearBuffer();
	}
	*/
}

void FramebufferManagerVulkan::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp) {
	/*
	if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
		// This can happen if they recently switched from non-buffered.
		fbo_unbind();
		return;
	}

	bool useBlit = gstate_c.Supports(GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT | GPU_SUPPORTS_NV_FRAMEBUFFER_BLIT);
	bool useNV = useBlit && !gstate_c.Supports(GPU_SUPPORTS_ARB_FRAMEBUFFER_BLIT);

	float srcXFactor = useBlit ? (float)src->renderWidth / (float)src->bufferWidth : 1.0f;
	float srcYFactor = useBlit ? (float)src->renderHeight / (float)src->bufferHeight : 1.0f;
	const int srcBpp = src->format == GE_FORMAT_8888 ? 4 : 2;
	if (srcBpp != bpp && bpp != 0) {
		srcXFactor = (srcXFactor * bpp) / srcBpp;
	}
	int srcX1 = srcX * srcXFactor;
	int srcX2 = (srcX + w) * srcXFactor;
	int srcY1 = srcY * srcYFactor;
	int srcY2 = (srcY + h) * srcYFactor;

	float dstXFactor = useBlit ? (float)dst->renderWidth / (float)dst->bufferWidth : 1.0f;
	float dstYFactor = useBlit ? (float)dst->renderHeight / (float)dst->bufferHeight : 1.0f;
	const int dstBpp = dst->format == GE_FORMAT_8888 ? 4 : 2;
	if (dstBpp != bpp && bpp != 0) {
		dstXFactor = (dstXFactor * bpp) / dstBpp;
	}
	int dstX1 = dstX * dstXFactor;
	int dstX2 = (dstX + w) * dstXFactor;
	int dstY1 = dstY * dstYFactor;
	int dstY2 = (dstY + h) * dstYFactor;

	if (src == dst && srcX == dstX && srcY == dstY) {
		// Let's just skip a copy where the destination is equal to the source.
		WARN_LOG_REPORT_ONCE(blitSame, G3D, "Skipped blit with equal dst and src");
		return;
	}

	if (gstate_c.Supports(GPU_SUPPORTS_ANY_COPY_IMAGE)) {
		// glBlitFramebuffer can clip, but glCopyImageSubData is more restricted.
		// In case the src goes outside, we just skip the optimization in that case.
		const bool sameSize = dstX2 - dstX1 == srcX2 - srcX1 && dstY2 - dstY1 == srcY2 - srcY1;
		const bool sameDepth = dst->colorDepth == src->colorDepth;
		const bool srcInsideBounds = srcX2 <= src->renderWidth && srcY2 <= src->renderHeight;
		const bool dstInsideBounds = dstX2 <= dst->renderWidth && dstY2 <= dst->renderHeight;
		const bool xOverlap = src == dst && srcX2 > dstX1 && srcX1 < dstX2;
		const bool yOverlap = src == dst && srcY2 > dstY1 && srcY1 < dstY2;
		if (sameSize && sameDepth && srcInsideBounds && dstInsideBounds && !(xOverlap && yOverlap)) {
#if defined(USING_GLES2)
#ifndef IOS
			glCopyImageSubDataOES(
				fbo_get_color_texture(src->fbo), GL_TEXTURE_2D, 0, srcX1, srcY1, 0,
				fbo_get_color_texture(dst->fbo), GL_TEXTURE_2D, 0, dstX1, dstY1, 0,
				dstX2 - dstX1, dstY2 - dstY1, 1);
			return;
#endif
#else
			if (gl_extensions.ARB_copy_image) {
				glCopyImageSubData(
					fbo_get_color_texture(src->fbo), GL_TEXTURE_2D, 0, srcX1, srcY1, 0,
					fbo_get_color_texture(dst->fbo), GL_TEXTURE_2D, 0, dstX1, dstY1, 0,
					dstX2 - dstX1, dstY2 - dstY1, 1);
				return;
			} else if (gl_extensions.NV_copy_image) {
				// Older, pre GL 4.x NVIDIA cards.
				glCopyImageSubDataNV(
					fbo_get_color_texture(src->fbo), GL_TEXTURE_2D, 0, srcX1, srcY1, 0,
					fbo_get_color_texture(dst->fbo), GL_TEXTURE_2D, 0, dstX1, dstY1, 0,
					dstX2 - dstX1, dstY2 - dstY1, 1);
				return;
			}
#endif
		}
	}

	fbo_bind_as_render_target(dst->fbo);
	glstate.scissorTest.force(false);

	if (useBlit) {
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

		glstate.viewport.force(0, 0, dst->renderWidth, dst->renderHeight);
		glstate.blend.force(false);
		glstate.cullFace.force(false);
		glstate.depthTest.force(false);
		glstate.stencilTest.force(false);
#if !defined(USING_GLES2)
		glstate.colorLogicOp.force(false);
#endif
		glstate.colorMask.force(true, true, true, true);
		glstate.stencilMask.force(0xFF);

		// The first four coordinates are relative to the 6th and 7th arguments of DrawActiveTexture.
		// Should maybe revamp that interface.
		float srcW = src->bufferWidth;
		float srcH = src->bufferHeight;
		DrawActiveTexture(0, dstX1, dstY1, w * dstXFactor, h, dst->bufferWidth, dst->bufferHeight, srcX1 / srcW, srcY1 / srcH, srcX2 / srcW, srcY2 / srcH, draw2dprogram_, ROTATION_LOCKED_HORIZONTAL);
		glBindTexture(GL_TEXTURE_2D, 0);
		textureCache_->ForgetLastTexture();
		glstate.viewport.restore();
		glstate.blend.restore();
		glstate.cullFace.restore();
		glstate.depthTest.restore();
		glstate.stencilTest.restore();
#if !defined(USING_GLES2)
		glstate.colorLogicOp.restore();
#endif
		glstate.colorMask.restore();
		glstate.stencilMask.restore();
	}

	glstate.scissorTest.restore();
	*/
}

// TODO: SSE/NEON
// Could also make C fake-simd for 64-bit, two 8888 pixels fit in a register :)
void ConvertFromRGBA8888_Vulkan(u8 *dst, const u8 *src, u32 dstStride, u32 srcStride, u32 width, u32 height, GEBufferFormat format) {
	// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
	const u32 *src32 = (const u32 *)src;

	if (format == GE_FORMAT_8888) {
		u32 *dst32 = (u32 *)dst;
		if (src == dst) {
			return;
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
			for (u32 y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGB565(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_5551: // ABGR 1555
			for (u32 y = 0; y < height; ++y) {
				ConvertBGRA8888ToRGBA5551(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_4444: // ABGR 4444
			for (u32 y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGBA4444(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case GE_FORMAT_8888:
		case GE_FORMAT_INVALID:
			// Not possible.
			break;
		}
	}
}

#ifdef DEBUG_READ_PIXELS
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
#ifndef USING_GLES2
	case GL_STACK_UNDERFLOW:
		ERROR_LOG(SCEGE, "glReadPixels: GL_STACK_UNDERFLOW");
		break;
	case GL_STACK_OVERFLOW:
		ERROR_LOG(SCEGE, "glReadPixels: GL_STACK_OVERFLOW");
		break;
#endif
	default:
		ERROR_LOG(SCEGE, "glReadPixels: %08x", error);
		break;
	}
}
#endif

void FramebufferManagerVulkan::PackFramebufferAsync_(VirtualFramebuffer *vfb) {
	/*
	const int MAX_PBO = 2;
	GLubyte *packed = 0;
	bool unbind = false;
	const u8 nextPBO = (currentPBO_ + 1) % MAX_PBO;
	const bool useCPU = gstate_c.Supports(GPU_PREFER_CPU_DOWNLOAD);

	// We'll prepare two PBOs to switch between readying and reading
	if (!pixelBufObj_) {
		if (!vfb) {
			// This call is just to flush the buffers.  We don't have any yet,
			// so there's nothing to do.
			return;
		}

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
#ifdef USING_GLES2
		// Not on desktop GL 2.x...
		packed = (GLubyte *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, pbo.size, GL_MAP_READ_BIT);
#else
		packed = (GLubyte *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
#endif

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

		bool reverseOrder = gstate_c.Supports(GPU_PREFER_REVERSE_COLOR_ORDER);
		switch (vfb->format) {
			// GL_UNSIGNED_INT_8_8_8_8 returns A B G R (little-endian, tested in Nvidia card/x86 PC)
			// GL_UNSIGNED_BYTE returns R G B A in consecutive bytes ("big-endian"/not treated as 32-bit value)
			// We want R G B A, so we use *_REV for 16-bit formats and GL_UNSIGNED_BYTE for 32-bit
		case GE_FORMAT_4444: // 16 bit RGBA
#ifdef USING_GLES2
			pixelType = GL_UNSIGNED_SHORT_4_4_4_4;
#else
			pixelType = (reverseOrder ? GL_UNSIGNED_SHORT_4_4_4_4_REV : GL_UNSIGNED_SHORT_4_4_4_4);
#endif
			pixelFormat = GL_RGBA;
			pixelSize = 2;
			align = 2;
			break;
		case GE_FORMAT_5551: // 16 bit RGBA
#ifdef USING_GLES2
			pixelType = GL_UNSIGNED_SHORT_5_5_5_1;
#else
			pixelType = (reverseOrder ? GL_UNSIGNED_SHORT_1_5_5_5_REV : GL_UNSIGNED_SHORT_5_5_5_1);
#endif
			pixelFormat = GL_RGBA;
			pixelSize = 2;
			align = 2;
			break;
		case GE_FORMAT_565: // 16 bit RGB
#ifdef USING_GLES2
			pixelType = GL_UNSIGNED_SHORT_5_6_5;
#else
			pixelType = (reverseOrder ? GL_UNSIGNED_SHORT_5_6_5_REV : GL_UNSIGNED_SHORT_5_6_5);
#endif
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
		fbStatus = (GLenum)fbo_check_framebuffer_status(vfb->fbo);

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
			SafeGLReadPixels(0, 0, vfb->fb_stride, vfb->height, UseBGRA8888() ? GL_BGRA_EXT : GL_RGBA, GL_UNSIGNED_BYTE, 0);
		} else {
			// Otherwise we'll directly request the format we need and let the GPU sort it out
			glPixelStorei(GL_PACK_ALIGNMENT, align);
			SafeGLReadPixels(0, 0, vfb->fb_stride, vfb->height, pixelFormat, pixelType, 0);
		}

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
	*/
}

void FramebufferManagerVulkan::PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	/*
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
	const int packWidth = x + w < vfb->width ? x + w : vfb->width;

	if (!convert) {
		packed = (GLubyte *)Memory::GetPointer(fb_address);
	} else { // End result may be 16-bit but we are reading 32-bit, so there may not be enough space at fb_address
		u32 neededSize = (u32)bufSize * sizeof(GLubyte);
		if (!convBuf_ || convBufSize_ < neededSize) {
			delete[] convBuf_;
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
		SafeGLReadPixels(0, y, h == 1 ? packWidth : vfb->fb_stride, h, glfmt, GL_UNSIGNED_BYTE, packed + byteOffset);

		if (convert) {
			int dstByteOffset = y * vfb->fb_stride * dstBpp;
			ConvertFromRGBA8888(Memory::GetPointer(fb_address + dstByteOffset), packed + byteOffset, vfb->fb_stride, vfb->fb_stride, packWidth, h, vfb->format);
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
	*/
}

#ifdef _WIN32
void ShowScreenResolution();
#endif

void FramebufferManagerVulkan::EndFrame() {
	if (resized_) {
		// TODO: Only do this if the new size actually changed the renderwidth/height.
		DestroyAllFBOs();

		// Probably not necessary
		//glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);

		// Check if postprocessing shader is doing upscaling as it requires native resolution
		const ShaderInfo *shaderInfo = 0;
		if (g_Config.sPostShaderName != "Off") {
			shaderInfo = GetPostShaderInfo(g_Config.sPostShaderName);
		}

		postShaderIsUpscalingFilter_ = shaderInfo ? shaderInfo->isUpscalingFilter : false;

		// Actually, auto mode should be more granular...
		// Round up to a zoom factor for the render size.
		int zoom = g_Config.iInternalResolution;
		if (zoom == 0) { // auto mode
										 // Use the longest dimension
			if (!g_Config.IsPortrait()) {
				zoom = (PSP_CoreParameter().pixelWidth + 479) / 480;
			} else {
				zoom = (PSP_CoreParameter().pixelHeight + 479) / 480;
			}
		}
		if (zoom <= 1 || postShaderIsUpscalingFilter_)
			zoom = 1;

		if (g_Config.IsPortrait()) {
			PSP_CoreParameter().renderWidth = 272 * zoom;
			PSP_CoreParameter().renderHeight = 480 * zoom;
		} else {
			PSP_CoreParameter().renderWidth = 480 * zoom;
			PSP_CoreParameter().renderHeight = 272 * zoom;
		}

		UpdateSize();

		resized_ = false;
#ifdef _WIN32
		// Seems related - if you're ok with numbers all the time, show some more :)
		if (g_Config.iShowFPSCounter != 0) {
			ShowScreenResolution();
		}
#endif
		ClearBuffer();
		DestroyDraw2DProgram();
	}

	// We flush to memory last requested framebuffer, if any.
	// Only do this in the read-framebuffer modes.
	if (updateVRAM_)
		PackFramebufferAsync_(nullptr);
}

void FramebufferManagerVulkan::DeviceLost() {
	DestroyAllFBOs();
	DestroyDraw2DProgram();
	resized_ = false;
}

std::vector<FramebufferInfo> FramebufferManagerVulkan::GetFramebufferList() {
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

void FramebufferManagerVulkan::DecimateFBOs() {
	currentRenderVfb_ = 0;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		int age = frameLastFramebufUsed_ - std::max(vfb->last_frame_render, vfb->last_frame_used);

		if (ShouldDownloadFramebuffer(vfb) && age == 0 && !vfb->memoryUpdated) {
			bool sync = true;
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
			delete it->second.fbo_vk;
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

void FramebufferManagerVulkan::DestroyAllFBOs() {
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
		delete it->second.fbo_vk;
	}
	tempFBOs_.clear();
}

void FramebufferManagerVulkan::FlushBeforeCopy() {
	// Flush anything not yet drawn before blitting, downloading, or uploading.
	// This might be a stalled list, or unflushed before a block transfer, etc.

	// TODO: It's really bad that we are calling SetRenderFramebuffer here with
	// all the irrelevant state checking it'll use to decide what to do. Should
	// do something more focused here.
	SetRenderFrameBuffer(gstate_c.framebufChanged, gstate_c.skipDrawReason);
	transformDraw_->Flush(curCmd_);
}

void FramebufferManagerVulkan::Resized() {
	resized_ = true;
}

bool FramebufferManagerVulkan::GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer) {
	// TODO: Doing this synchronously will require stalling the pipeline. Maybe better
	// to do it callback-style?
/*
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(fb_address | 0x04000000), fb_stride, 512, format);
		return true;
	}

	buffer.Allocate(vfb->renderWidth, vfb->renderHeight, GE_FORMAT_8888, false, true);
	if (vfb->fbo_vk)
		fbo_bind_for_read(vfb->fbo_vk);
	if (gl_extensions.GLES3 || !gl_extensions.IsGLES)
		glReadBuffer(GL_COLOR_ATTACHMENT0);

	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	SafeGLReadPixels(0, 0, vfb->renderWidth, vfb->renderHeight, GL_RGBA, GL_UNSIGNED_BYTE, buffer.GetData());
	*/
	return false;
}

bool FramebufferManagerVulkan::GetDisplayFramebuffer(GPUDebugBuffer &buffer) {
	// TODO: Doing this synchronously will require stalling the pipeline. Maybe better
	// to do it callback-style?
	/*
	fbo_unbind_read();

	int pw = PSP_CoreParameter().pixelWidth;
	int ph = PSP_CoreParameter().pixelHeight;

	// The backbuffer is flipped.
	buffer.Allocate(pw, ph, GPU_DBG_FORMAT_888_RGB, true);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	SafeGLReadPixels(0, 0, pw, ph, GL_RGB, GL_UNSIGNED_BYTE, buffer.GetData());
	*/
	return false;
}

bool FramebufferManagerVulkan::GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer) {
	// TODO: Doing this synchronously will require stalling the pipeline. Maybe better
	// to do it callback-style?
	/*
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(z_address | 0x04000000), z_stride, 512, GPU_DBG_FORMAT_16BIT);
		return true;
	}

	buffer.Allocate(vfb->renderWidth, vfb->renderHeight, GPU_DBG_FORMAT_FLOAT, false);
	if (vfb->fbo_vk)
		fbo_bind_for_read(vfb->fbo_vk);
	if (gl_extensions.GLES3 || !gl_extensions.IsGLES)
		glReadBuffer(GL_COLOR_ATTACHMENT0);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	SafeGLReadPixels(0, 0, vfb->renderWidth, vfb->renderHeight, GL_DEPTH_COMPONENT, GL_FLOAT, buffer.GetData());
	*/
	return false;
}

bool FramebufferManagerVulkan::GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer) {
	// TODO: Doing this synchronously will require stalling the pipeline. Maybe better
	// to do it callback-style?
	/*
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

	buffer.Allocate(vfb->renderWidth, vfb->renderHeight, GPU_DBG_FORMAT_8BIT, false);
	if (vfb->fbo_vk)
		fbo_bind_for_read(vfb->fbo_vk);
	if (gl_extensions.GLES3 || !gl_extensions.IsGLES)
		glReadBuffer(GL_COLOR_ATTACHMENT0);
	glPixelStorei(GL_PACK_ALIGNMENT, 2);
	SafeGLReadPixels(0, 0, vfb->renderWidth, vfb->renderHeight, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, buffer.GetData());

	return true;
	*/
	return false;
}
