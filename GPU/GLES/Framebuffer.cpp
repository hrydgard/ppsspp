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

#include "gfx_es2/glsl_program.h"
#include "gfx_es2/gl_state.h"
#include "gfx_es2/fbo.h"

#include "math/lin/matrix4x4.h"

#include "Core/Host.h"
#include "Core/MemMap.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

#include "GPU/GLES/Framebuffer.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/ShaderManager.h"

static const char tex_fs[] =
	"#ifdef GL_ES\n"
	"precision mediump float;\n"
	"#endif\n"
	"uniform sampler2D sampler0;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"	gl_FragColor = texture2D(sampler0, v_texcoord0);\n"
	"}\n";

static const char basic_vs[] =
#ifndef USING_GLES2
	"#version 120\n"
#endif
	"attribute vec4 a_position;\n"
	"attribute vec2 a_texcoord0;\n"
	"uniform mat4 u_viewproj;\n"
	"varying vec4 v_color;\n"
	"varying vec2 v_texcoord0;\n"
	"void main() {\n"
	"  v_texcoord0 = a_texcoord0;\n"
	"  gl_Position = u_viewproj * a_position;\n"
	"}\n";

// Aggressively delete unused FBO:s to save gpu memory.
enum {
	FBO_OLD_AGE = 5,
};

static bool MaskedEqual(u32 addr1, u32 addr2) {
	return (addr1 & 0x3FFFFFF) == (addr2 & 0x3FFFFFF);
}

void CenterRect(float *x, float *y, float *w, float *h,
                float origW, float origH, float frameW, float frameH)
{
	if (g_Config.bStretchToDisplay)
	{
		*x = 0;
		*y = 0;
		*w = frameW;
		*h = frameH;
		return;
	}

	float origRatio = origW/origH;
	float frameRatio = frameW/frameH;

	if (origRatio > frameRatio)
	{
		// Image is wider than frame. Center vertically.
		float scale = origW / frameW;
		*x = 0.0f;
		*w = frameW;
		*h = frameW / origRatio;
		*y = (frameH - *h) / 2.0f;
	}
	else
	{
		// Image is taller than frame. Center horizontally.
		float scale = origH / frameH;
		*y = 0.0f;
		*h = frameH;
		*w = frameH * origRatio;
		*x = (frameW - *w) / 2.0f;
	}
}

FramebufferManager::FramebufferManager() :
	displayFramebufPtr_(0),
	prevDisplayFramebuf_(0),
	prevPrevDisplayFramebuf_(0),
	frameLastFramebufUsed(0),
	currentRenderVfb_(0)
{
	glGenTextures(1, &backbufTex);

	//initialize backbuffer texture
	glBindTexture(GL_TEXTURE_2D, backbufTex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 480, 272, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	draw2dprogram = glsl_create_source(basic_vs, tex_fs);

	glsl_bind(draw2dprogram);
	glUniform1i(draw2dprogram->sampler0, 0);
	glsl_unbind();

	// And an initial clear. We don't clear per frame as the games are supposed to handle that
	// by themselves.
	glstate.depthWrite.set(GL_TRUE);
	glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0,0,0,1);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

	convBuf = new u8[480 * 272 * 4];
}

FramebufferManager::~FramebufferManager() {
	glDeleteTextures(1, &backbufTex);
	glsl_destroy(draw2dprogram);
	delete [] convBuf;
}

void FramebufferManager::DrawPixels(const u8 *framebuf, int pixelFormat, int linesize) {
	// TODO: We can trivially do these in the shader, and there's no need to
	// upconvert to 8888 for the 16-bit formats.
	for (int y = 0; y < 272; y++) {
		switch (pixelFormat) {
		case PSP_DISPLAY_PIXEL_FORMAT_565:
			{
				const u16 *src = (const u16 *)framebuf + linesize * y;
				u8 *dst = convBuf + 4 * 480 * y;
				for (int x = 0; x < 480; x++)
				{
					u16 col = src[x];
					dst[x * 4] = ((col) & 0x1f) << 3;
					dst[x * 4 + 1] = ((col >> 5) & 0x3f) << 2;
					dst[x * 4 + 2] = ((col >> 11) & 0x1f) << 3;
					dst[x * 4 + 3] = 255;
				}
			}
			break;

		case PSP_DISPLAY_PIXEL_FORMAT_5551:
			{
				const u16 *src = (const u16 *)framebuf + linesize * y;
				u8 *dst = convBuf + 4 * 480 * y;
				for (int x = 0; x < 480; x++)
				{
					u16 col = src[x];
					dst[x * 4] = ((col) & 0x1f) << 3;
					dst[x * 4 + 1] = ((col >> 5) & 0x1f) << 3;
					dst[x * 4 + 2] = ((col >> 10) & 0x1f) << 3;
					dst[x * 4 + 3] = (col >> 15) ? 255 : 0;
				}
			}
			break;

		case PSP_DISPLAY_PIXEL_FORMAT_8888:
			{
				const u8 *src = framebuf + linesize * 4 * y;
				u8 *dst = convBuf + 4 * 480 * y;
				for (int x = 0; x < 480; x++)
				{
					dst[x * 4] = src[x * 4];
					dst[x * 4 + 1] = src[x * 4 + 3];
					dst[x * 4 + 2] = src[x * 4 + 2];
					dst[x * 4 + 3] = src[x * 4 + 1];
				}
			}
			break;

		case PSP_DISPLAY_PIXEL_FORMAT_4444:
			{
				const u16 *src = (const u16 *)framebuf + linesize * y;
				u8 *dst = convBuf + 4 * 480 * y;
				for (int x = 0; x < 480; x++)
				{
					u16 col = src[x];
					dst[x * 4] = ((col >> 8) & 0xf) << 4;
					dst[x * 4 + 1] = ((col >> 4) & 0xf) << 4;
					dst[x * 4 + 2] = (col & 0xf) << 4;
					dst[x * 4 + 3] = (col >> 12) << 4;
				}
			}
			break;
		}
	}

	glBindTexture(GL_TEXTURE_2D,backbufTex);
	glTexSubImage2D(GL_TEXTURE_2D,0,0,0,480,272, GL_RGBA, GL_UNSIGNED_BYTE, convBuf);

	float x, y, w, h;
	CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight);
	DrawActiveTexture(x, y, w, h);
}

void FramebufferManager::DrawActiveTexture(float x, float y, float w, float h, bool flip) {
	float u2 = 1.0f;
	float v1 = flip ? 1.0f : 0.0f;
	float v2 = flip ? 0.0f : 1.0f;

	const float pos[12] = {x,y,0, x+w,y,0, x+w,y+h,0, x,y+h,0};
	const float texCoords[8] = {0, v1, u2, v1, u2, v2, 0, v2};

	glsl_bind(draw2dprogram);
	Matrix4x4 ortho;
	ortho.setOrtho(0, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight, 0, -1, 1);
	glUniformMatrix4fv(draw2dprogram->u_viewproj, 1, GL_FALSE, ortho.getReadPtr());
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(draw2dprogram->a_position);
	glEnableVertexAttribArray(draw2dprogram->a_texcoord0);
	glVertexAttribPointer(draw2dprogram->a_position, 3, GL_FLOAT, GL_FALSE, 12, pos);
	glVertexAttribPointer(draw2dprogram->a_texcoord0, 2, GL_FLOAT, GL_FALSE, 8, texCoords);	
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);	// TODO: TRIANGLE_STRIP is more likely to be optimized.
	glDisableVertexAttribArray(draw2dprogram->a_position);
	glDisableVertexAttribArray(draw2dprogram->a_texcoord0);
	glsl_unbind();
}

VirtualFramebuffer *FramebufferManager::GetDisplayFBO() {
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		VirtualFramebuffer *v = *iter;
		if (MaskedEqual(v->fb_address, displayFramebufPtr_) && v->format == displayFormat_) {
			// Could check w too but whatever
			return *iter;
		}
	}
	DEBUG_LOG(HLE, "Finding no FBO matching address %08x", displayFramebufPtr_);
#if 0  // defined(_DEBUG)
	std::string debug = "FBOs: ";
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		char temp[256];
		sprintf(temp, "%08x %i %i", (*iter)->fb_address, (*iter)->width, (*iter)->height);
		debug += std::string(temp);
	}
	ERROR_LOG(HLE, "FBOs: %s", debug.c_str());
#endif
	return 0;
}

void GetViewportDimensions(int &w, int &h) {
	float vpXa = getFloat24(gstate.viewportx1);
	float vpYa = getFloat24(gstate.viewporty1);
	w = (int)fabsf(vpXa * 2);
	h = (int)fabsf(vpYa * 2);
}

// Heuristics to figure out the size of FBO to create.
void GuessDrawingSize(int &drawing_width, int &drawing_height) {
	GetViewportDimensions(drawing_width, drawing_height);

	// HACK for first frame where some games don't init things right
	if (drawing_width <= 1 && drawing_height <= 1) {
		drawing_width = 480;
		drawing_height = 272;
	}

	// Now, cap using scissor. Hm, no, this doesn't work so well.
	/*
	if (drawing_width > gstate.getScissorX2() + 1)
		drawing_width = gstate.getScissorX2() + 1;
	if (drawing_height > gstate.getScissorY2() + 1)
		drawing_height = gstate.getScissorY2() + 1;*/
	
	// Cap at maximum texture size for now. Don't see much point in drawing bigger.
	drawing_width = std::min(drawing_width, 512);
	drawing_height = std::min(drawing_height, 512);
}

void FramebufferManager::SetRenderFrameBuffer() {
	if (!gstate_c.framebufChanged && currentRenderVfb_) {
		currentRenderVfb_->last_frame_used = gpuStats.numFrames;
		return;
	}
	gstate_c.framebufChanged = false;

	// Get parameters
	u32 fb_address = (gstate.fbptr & 0xFFE000) | ((gstate.fbwidth & 0xFF0000) << 8);
	int fb_stride = gstate.fbwidth & 0x3C0;

	u32 z_address = (gstate.zbptr & 0xFFE000) | ((gstate.zbwidth & 0xFF0000) << 8);
	int z_stride = gstate.zbwidth & 0x3C0;

	// Yeah this is not completely right. but it'll do for now.
	//int drawing_width = ((gstate.region2) & 0x3FF) + 1;
	//int drawing_height = ((gstate.region2 >> 10) & 0x3FF) + 1;

	// As there are no clear "framebuffer width" and "framebuffer height" registers,
	// we need to infer the size of the current framebuffer somehow. Let's try the viewport.
	
	int fmt = gstate.framebufpixformat & 3;

	int drawing_width, drawing_height;
	GuessDrawingSize(drawing_width, drawing_height);

	// Find a matching framebuffer
	VirtualFramebuffer *vfb = 0;
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		VirtualFramebuffer *v = *iter;
		if (MaskedEqual(v->fb_address, fb_address) && v->width == drawing_width && v->height == drawing_height && v->format == fmt) {
			// Let's not be so picky for now. Let's say this is the one.
			vfb = v;
			// Update fb stride in case it changed
			vfb->fb_stride = fb_stride;
			break;
		}
	}

	float renderWidthFactor = (float)PSP_CoreParameter().renderWidth / 480.0f;
	float renderHeightFactor = (float)PSP_CoreParameter().renderHeight / 272.0f;

	// None found? Create one.
	if (!vfb) {
		gstate_c.textureChanged = true;
		vfb = new VirtualFramebuffer();
		vfb->fbo = 0;
		vfb->fb_address = fb_address;
		vfb->fb_stride = fb_stride;
		vfb->z_address = z_address;
		vfb->z_stride = z_stride;
		vfb->width = drawing_width;
		vfb->height = drawing_height;
		vfb->renderWidth = (u16)(drawing_width * renderWidthFactor);
		vfb->renderHeight = (u16)(drawing_height * renderHeightFactor);
		vfb->format = fmt;
		vfb->usageFlags = FB_USAGE_RENDERTARGET;
		vfb->dirtyAfterDisplay = true;

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
					vfb->colorDepth = FBO_8888; 
					break;
				default: 
					vfb->colorDepth = FBO_8888; 
					break;
			}
		}
			
		//#ifdef ANDROID
		//	vfb->colorDepth = FBO_8888;
		//#endif

		if (g_Config.bBufferedRendering) {
			vfb->fbo = fbo_create(vfb->renderWidth, vfb->renderHeight, 1, true, vfb->colorDepth);
			fbo_bind_as_render_target(vfb->fbo);
		} else {
			fbo_unbind();
			// Let's ignore rendering to targets that have not (yet) been displayed.
			gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
		}

		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb);

		vfb->last_frame_used = gpuStats.numFrames;
		frameLastFramebufUsed = gpuStats.numFrames;
		vfbs_.push_back(vfb);
		glstate.depthWrite.set(GL_TRUE);
		glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		glEnable(GL_DITHER);
		currentRenderVfb_ = vfb;

		INFO_LOG(HLE, "Creating FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);

	// We already have it!
	} else if (vfb != currentRenderVfb_) {
		// Use it as a render target.
		DEBUG_LOG(HLE, "Switching render target to FBO for %08x", vfb->fb_address);
		vfb->usageFlags |= FB_USAGE_RENDERTARGET;
		gstate_c.textureChanged = true;
		vfb->last_frame_used = gpuStats.numFrames;
		frameLastFramebufUsed = gpuStats.numFrames;
		vfb->dirtyAfterDisplay = true;

		if (g_Config.bBufferedRendering) {
			if (vfb->fbo) {
				fbo_bind_as_render_target(vfb->fbo);
			} else {
				// wtf? This should only happen very briefly when toggling bBufferedRendering
				fbo_unbind();
			}
		} else {
			if (vfb->fbo) {
				// wtf? This should only happen very briefly when toggling bBufferedRendering
				textureCache_->NotifyFramebufferDestroyed(vfb->fb_address, vfb);
				fbo_destroy(vfb->fbo);
				vfb->fbo = 0;
			}
			fbo_unbind();

			// Let's ignore rendering to targets that have not (yet) been displayed.
			if (vfb->usageFlags & FB_USAGE_DISPLAYED_FRAMEBUFFER)
				gstate_c.skipDrawReason &= ~SKIPDRAW_NON_DISPLAYED_FB;
			else
				gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;

			/*
			if (drawing_width == 480 && drawing_height == 272) {
				gstate_c.skipDrawReason &= ~SKIPDRAW_SKIPNONFB;
				// OK!
			} else {
				gstate_c.skipDrawReason |= ~SKIPDRAW_SKIPNONFB;
			}*/
		}
		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb);

#ifdef USING_GLES2
		// Some tiled mobile GPUs benefit IMMENSELY from clearing an FBO before rendering
		// to it. This broke stuff before, so now it only clears on the first use of an
		// FBO in a frame. This means that some games won't be able to avoid the on-some-GPUs
		// performance-crushing framebuffer reloads from RAM, but we'll have to live with that.
		if (vfb->last_frame_used != gpuStats.numFrames)	{
			glstate.depthWrite.set(GL_TRUE);
			glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(0,0,0,1);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		}
#endif
		currentRenderVfb_ = vfb;
	} else {
		vfb->last_frame_used = gpuStats.numFrames;
		frameLastFramebufUsed = gpuStats.numFrames;
	}

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		shaderManager_->DirtyUniform(DIRTY_PROJTHROUGHMATRIX);
		gstate_c.curRTWidth = vfb->width;
		gstate_c.curRTHeight = vfb->height;
	}
}

void FramebufferManager::CopyDisplayToOutput() {
	fbo_unbind();

	VirtualFramebuffer *vfb = GetDisplayFBO();
	if (!vfb) {
		if (Memory::IsValidAddress(displayFramebufPtr_)) {
			// The game is displaying something directly from RAM. In GTA, it's decoded video.
			DrawPixels(Memory::GetPointer(displayFramebufPtr_), displayFormat_, displayStride_);
		} else {
			DEBUG_LOG(HLE, "Found no FBO to display! displayFBPtr = %08x", displayFramebufPtr_);
			// No framebuffer to display! Clear to black.
			glstate.depthWrite.set(GL_TRUE);
			glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
			glClearColor(0,0,0,1);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
		}
		return;
	}

	vfb->usageFlags |= FB_USAGE_DISPLAYED_FRAMEBUFFER;
	vfb->dirtyAfterDisplay = false;

	if (prevDisplayFramebuf_ != displayFramebuf_) {
		prevPrevDisplayFramebuf_ = prevDisplayFramebuf_;
	}
	if (displayFramebuf_ != vfb) {
		prevDisplayFramebuf_ = displayFramebuf_;
	}
	displayFramebuf_ = vfb;

	currentRenderVfb_ = 0;

	if (vfb->fbo) {
		glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		DEBUG_LOG(HLE, "Displaying FBO %08x", vfb->fb_address);
		glstate.blend.disable();
		glstate.cullFace.disable();
		glstate.depthTest.disable();
		glstate.scissorTest.disable();
		glstate.stencilTest.disable();

		fbo_bind_color_as_texture(vfb->fbo, 0);
	
	// These are in the output display coordinates
		float x, y, w, h;
		CenterRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)PSP_CoreParameter().pixelWidth, (float)PSP_CoreParameter().pixelHeight);
		DrawActiveTexture(x, y, w, h, true);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	if (resized_) {
		glstate.depthWrite.set(GL_TRUE);
		glstate.colorMask.set(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0,0,0,1);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	}
}

void FramebufferManager::EndFrame() {
	if (resized_) {
		DestroyAllFBOs();
		glstate.viewport.set(0, 0, PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight);
		resized_ = false;
	}
}

void FramebufferManager::BeginFrame() {
	DecimateFBOs();
	// NOTE - this is all wrong. At the beginning of the frame is a TERRIBLE time to draw the fb.
	if (g_Config.bDisplayFramebuffer && displayFramebufPtr_) {
		INFO_LOG(HLE, "Drawing the framebuffer");
		const u8 *pspframebuf = Memory::GetPointer((0x44000000) | (displayFramebufPtr_ & 0x1FFFFF));	// TODO - check
		glstate.cullFace.disable();
		glstate.depthTest.disable();
		glstate.blend.disable();
		glstate.scissorTest.disable();
		glstate.stencilTest.disable();
		DrawPixels(pspframebuf, displayFormat_, displayStride_);
		// TODO: restore state?
	}
	currentRenderVfb_ = 0;
}

void FramebufferManager::SetDisplayFramebuffer(u32 framebuf, u32 stride, int format) {

	if ((framebuf & 0x04000000) == 0) {
		DEBUG_LOG(HLE, "Non-VRAM display framebuffer address set: %08x", framebuf);
	} else {
		displayFramebufPtr_ = framebuf;
		displayStride_ = stride;
		displayFormat_ = format;
	}
}

std::vector<FramebufferInfo> FramebufferManager::GetFramebufferList() {
	std::vector<FramebufferInfo> list;

	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		VirtualFramebuffer *vfb = *iter;

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
	for (auto iter = vfbs_.begin(); iter != vfbs_.end();) {
		VirtualFramebuffer *vfb = *iter;
		if (vfb == displayFramebuf_ || vfb == prevDisplayFramebuf_ || vfb == prevPrevDisplayFramebuf_) {
			++iter;
			continue;
		}
		int age = frameLastFramebufUsed - (*iter)->last_frame_used;
		if (age > FBO_OLD_AGE) {
			INFO_LOG(HLE, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age)
			if (vfb->fbo) {
				textureCache_->NotifyFramebufferDestroyed(vfb->fb_address, vfb);
				fbo_destroy(vfb->fbo);
				vfb->fbo = 0;
			}
			delete vfb;
			vfbs_.erase(iter++);
		}
		else
			++iter;
	}
}

void FramebufferManager::DestroyAllFBOs() {
	fbo_unbind();
	for (auto iter = vfbs_.begin(); iter != vfbs_.end(); ++iter) {
		VirtualFramebuffer *vfb = *iter;
		textureCache_->NotifyFramebufferDestroyed(vfb->fb_address, vfb);
		if (vfb->fbo) {
			INFO_LOG(HLE, "Destroying FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);
			fbo_destroy(vfb->fbo);
			vfb->fbo = 0;
		}
		delete vfb;
	}
	vfbs_.clear();
}

void FramebufferManager::Resized() {
	resized_ = true;
}
