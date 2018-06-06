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
#include <sstream>
#include <cassert>
#include <cmath>

#include "ext/native/thin3d/thin3d.h"
#include "base/timeutil.h"
#include "gfx_es2/gpu_features.h"

#include "i18n/i18n.h"
#include "Common/ColorConv.h"
#include "Common/Common.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/Host.h"
#include "Core/Reporting.h"
#include "Core/ELF/ParamSFO.h"
#include "Core/System.h"
#include "Core/HLE/sceDisplay.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/PostShader.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

void CenterDisplayOutputRect(float *x, float *y, float *w, float *h, float origW, float origH, float frameW, float frameH, int rotation) {
	float outW;
	float outH;

	bool rotated = rotation == ROTATION_LOCKED_VERTICAL || rotation == ROTATION_LOCKED_VERTICAL180;

	if (g_Config.iSmallDisplayZoomType == 0) { // Stretching
		outW = frameW;
		outH = frameH;
	} else {
		if (g_Config.iSmallDisplayZoomType == 3) { // Manual Scaling
			float offsetX = (g_Config.fSmallDisplayOffsetX - 0.5f) * 2.0f * frameW;
			float offsetY = (g_Config.fSmallDisplayOffsetY - 0.5f) * 2.0f * frameH;
			// Have to invert Y for GL
			if (GetGPUBackend() == GPUBackend::OPENGL) {
				offsetY = offsetY * -1.0f;
			}
			float customZoom = g_Config.fSmallDisplayZoomLevel;
			float smallDisplayW = origW * customZoom;
			float smallDisplayH = origH * customZoom;
			if (!rotated) {
				*x = floorf(((frameW - smallDisplayW) / 2.0f) + offsetX);
				*y = floorf(((frameH - smallDisplayH) / 2.0f) + offsetY);
				*w = floorf(smallDisplayW);
				*h = floorf(smallDisplayH);
				return;
			} else {
				*x = floorf(((frameW - smallDisplayH) / 2.0f) + offsetX);
				*y = floorf(((frameH - smallDisplayW) / 2.0f) + offsetY);
				*w = floorf(smallDisplayH);
				*h = floorf(smallDisplayW);
				return;
			}
		} else if (g_Config.iSmallDisplayZoomType == 2) { // Auto Scaling
			// Stretch to 1080 for 272*4.  But don't distort if not widescreen (i.e. ultrawide of halfwide.)
			float pixelCrop = frameH / 270.0f;
			float resCommonWidescreen = pixelCrop - floor(pixelCrop);
			if (!rotated && resCommonWidescreen == 0.0f && frameW >= pixelCrop * 480.0f) {
				*x = floorf((frameW - pixelCrop * 480.0f) * 0.5f);
				*y = floorf(-pixelCrop);
				*w = floorf(pixelCrop * 480.0f);
				*h = floorf(pixelCrop * 272.0f);
				return;
			}
		}

		float origRatio = !rotated ? origW / origH : origH / origW;
		float frameRatio = frameW / frameH;
		
		if (origRatio > frameRatio) {
			// Image is wider than frame. Center vertically.
			outW = frameW;
			outH = frameW / origRatio;
			// Stretch a little bit
			if (!rotated && g_Config.iSmallDisplayZoomType == 1) // Partial Stretch
				outH = (frameH + outH) / 2.0f; // (408 + 720) / 2 = 564
		} else {
			// Image is taller than frame. Center horizontally.
			outW = frameH * origRatio;
			outH = frameH;
			if (rotated && g_Config.iSmallDisplayZoomType == 1) // Partial Stretch
				outW = (frameH + outH) / 2.0f; // (408 + 720) / 2 = 564
		}
	}

	*x = floorf((frameW - outW) / 2.0f);
	*y = floorf((frameH - outH) / 2.0f);
	*w = floorf(outW);
	*h = floorf(outH);
}


FramebufferManagerCommon::FramebufferManagerCommon(Draw::DrawContext *draw)
	: draw_(draw),
		displayFormat_(GE_FORMAT_565) {
	UpdateSize();
}

FramebufferManagerCommon::~FramebufferManagerCommon() {
	DecimateFBOs();
	for (auto vfb : vfbs_) {
		DestroyFramebuf(vfb);
	}
	vfbs_.clear();

	for (auto &tempFB : tempFBOs_) {
		tempFB.second.fbo->Release();
	}
	tempFBOs_.clear();

	// Do the same for ReadFramebuffersToMemory's VFBs
	for (auto vfb : bvfbs_) {
		DestroyFramebuf(vfb);
	}
	bvfbs_.clear();

	SetNumExtraFBOs(0);
}

void FramebufferManagerCommon::Init() {
	BeginFrame();
}

bool FramebufferManagerCommon::UpdateSize() {
	const bool newRender = renderWidth_ != (float)PSP_CoreParameter().renderWidth || renderHeight_ != (float)PSP_CoreParameter().renderHeight;
	const bool newSettings = bloomHack_ != g_Config.iBloomHack || trueColor_ != g_Config.bTrueColor || useBufferedRendering_ != (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);

	renderWidth_ = (float)PSP_CoreParameter().renderWidth;
	renderHeight_ = (float)PSP_CoreParameter().renderHeight;
	pixelWidth_ = PSP_CoreParameter().pixelWidth;
	pixelHeight_ = PSP_CoreParameter().pixelHeight;
	bloomHack_ = g_Config.iBloomHack;
	trueColor_ = g_Config.bTrueColor;
	useBufferedRendering_ = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;

	return newRender || newSettings;
}

void FramebufferManagerCommon::BeginFrame() {
	DecimateFBOs();
	currentRenderVfb_ = nullptr;
}

void FramebufferManagerCommon::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	displayFramebufPtr_ = framebuf;
	displayStride_ = stride;
	displayFormat_ = format;
}

VirtualFramebuffer *FramebufferManagerCommon::GetVFBAt(u32 addr) {
	VirtualFramebuffer *match = nullptr;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *v = vfbs_[i];
		if (MaskedEqual(v->fb_address, addr)) {
			// Could check w too but whatever
			if (match == nullptr || match->last_frame_render < v->last_frame_render) {
				match = v;
			}
		}
	}

	return match;
}

bool FramebufferManagerCommon::MaskedEqual(u32 addr1, u32 addr2) {
	return (addr1 & 0x03FFFFFF) == (addr2 & 0x03FFFFFF);
}

u32 FramebufferManagerCommon::FramebufferByteSize(const VirtualFramebuffer *vfb) const {
	return vfb->fb_stride * vfb->height * (vfb->format == GE_FORMAT_8888 ? 4 : 2);
}

bool FramebufferManagerCommon::ShouldDownloadFramebuffer(const VirtualFramebuffer *vfb) const {
	return PSP_CoreParameter().compat.flags().Force04154000Download && vfb->fb_address == 0x00154000;
}

void FramebufferManagerCommon::SetNumExtraFBOs(int num) {
	for (size_t i = 0; i < extraFBOs_.size(); i++) {
		extraFBOs_[i]->ReleaseAssertLast();
	}
	extraFBOs_.clear();
	for (int i = 0; i < num; i++) {
		// No depth/stencil for post processing
		Draw::Framebuffer *fbo = draw_->CreateFramebuffer({ (int)renderWidth_, (int)renderHeight_, 1, 1, false, Draw::FBO_8888 });
		extraFBOs_.push_back(fbo);
	}
	currentRenderVfb_ = 0;
}

// Heuristics to figure out the size of FBO to create.
void FramebufferManagerCommon::EstimateDrawingSize(u32 fb_address, GEBufferFormat fb_format, int viewport_width, int viewport_height, int region_width, int region_height, int scissor_width, int scissor_height, int fb_stride, int &drawing_width, int &drawing_height) {
	static const int MAX_FRAMEBUF_HEIGHT = 512;

	// Games don't always set any of these.  Take the greatest parameter that looks valid based on stride.
	if (viewport_width > 4 && viewport_width <= fb_stride && viewport_height > 0) {
		drawing_width = viewport_width;
		drawing_height = viewport_height;
		// Some games specify a viewport with 0.5, but don't have VRAM for 273.  480x272 is the buffer size.
		if (viewport_width == 481 && region_width == 480 && viewport_height == 273 && region_height == 272) {
			drawing_width = 480;
			drawing_height = 272;
		}
		// Sometimes region is set larger than the VRAM for the framebuffer.
		// However, in one game it's correctly set as a larger height (see #7277) with the same width.
		// A bit of a hack, but we try to handle that unusual case here.
		if (region_width <= fb_stride && (region_width > drawing_width || (region_width == drawing_width && region_height > drawing_height)) && region_height <= MAX_FRAMEBUF_HEIGHT) {
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
		const u32 fb_normalized_address = fb_address | 0x44000000;
		u32 nearest_address = 0xFFFFFFFF;
		for (size_t i = 0; i < vfbs_.size(); ++i) {
			const u32 other_address = vfbs_[i]->fb_address | 0x44000000;
			if (other_address > fb_normalized_address && other_address < nearest_address) {
				nearest_address = other_address;
			}
		}

		// Unless the game is using overlapping buffers, the next buffer should be far enough away.
		// This catches some cases where we can know this.
		// Hmm.  The problem is that we could only catch it for the first of two buffers...
		const u32 bpp = fb_format == GE_FORMAT_8888 ? 4 : 2;
		int avail_height = (nearest_address - fb_normalized_address) / (fb_stride * bpp);
		if (avail_height < drawing_height && avail_height == region_height) {
			drawing_width = std::min(region_width, fb_stride);
			drawing_height = avail_height;
		}

		// Some games draw buffers interleaved, with a high stride/region/scissor but default viewport.
		if (fb_stride == 1024 && region_width == 1024 && scissor_width == 1024) {
			drawing_width = 1024;
		}
	}

	DEBUG_LOG(G3D, "Est: %08x V: %ix%i, R: %ix%i, S: %ix%i, STR: %i, THR:%i, Z:%08x = %ix%i", fb_address, viewport_width,viewport_height, region_width, region_height, scissor_width, scissor_height, fb_stride, gstate.isModeThrough(), gstate.isDepthWriteEnabled() ? gstate.getDepthBufAddress() : 0, drawing_width, drawing_height);
}

void GetFramebufferHeuristicInputs(FramebufferHeuristicParams *params, const GPUgstate &gstate) {
	params->fb_addr = gstate.getFrameBufAddress();
	params->fb_address = gstate.getFrameBufRawAddress();
	params->fb_stride = gstate.FrameBufStride();

	params->z_address = gstate.getDepthBufRawAddress();
	params->z_stride = gstate.DepthBufStride();

	params->fmt = gstate.FrameBufFormat();

	params->isClearingDepth = gstate.isModeClear() && gstate.isClearModeDepthMask();
	// Technically, it may write depth later, but we're trying to detect it only when it's really true.
	if (gstate.isModeClear()) {
		// Not quite seeing how this makes sense..
		params->isWritingDepth = !gstate.isClearModeDepthMask() && gstate.isDepthWriteEnabled();
	} else {
		params->isWritingDepth = gstate.isDepthWriteEnabled();
	}
	params->isDrawing = !gstate.isModeClear() || !gstate.isClearModeColorMask() || !gstate.isClearModeAlphaMask();
	params->isModeThrough = gstate.isModeThrough();

	// Viewport-X1 and Y1 are not the upper left corner, but half the width/height. A bit confusing.
	float vpx = gstate.getViewportXScale();
	float vpy = gstate.getViewportYScale();

	// Work around problem in F1 Grand Prix, where it draws in through mode with a bogus viewport.
	// We set bad values to 0 which causes the framebuffer size heuristic to rely on the other parameters instead.
	if (std::isnan(vpx) || vpx > 10000000.0f) {
		vpx = 0.f;
	}
	if (std::isnan(vpy) || vpy > 10000000.0f) {
		vpy = 0.f;
	}
	params->viewportWidth = (int)(fabsf(vpx) * 2.0f);
	params->viewportHeight = (int)(fabsf(vpy) * 2.0f);
	params->regionWidth = gstate.getRegionX2() + 1;
	params->regionHeight = gstate.getRegionY2() + 1;
	params->scissorWidth = gstate.getScissorX2() + 1;
	params->scissorHeight = gstate.getScissorY2() + 1;
}

VirtualFramebuffer *FramebufferManagerCommon::DoSetRenderFrameBuffer(const FramebufferHeuristicParams &params, u32 skipDrawReason) {
	gstate_c.Clean(DIRTY_FRAMEBUF);

	// Collect all parameters. This whole function has really become a cesspool of heuristics...
	// but it appears that's what it takes, unless we emulate VRAM layout more accurately somehow.

	// As there are no clear "framebuffer width" and "framebuffer height" registers,
	// we need to infer the size of the current framebuffer somehow.
	int drawing_width, drawing_height;
	EstimateDrawingSize(params.fb_address, params.fmt, params.viewportWidth, params.viewportHeight, params.regionWidth, params.regionHeight, params.scissorWidth, params.scissorHeight, std::max(params.fb_stride, 4), drawing_width, drawing_height);

	gstate_c.SetCurRTOffsetX(0);
	bool vfbFormatChanged = false;

	// Find a matching framebuffer
	VirtualFramebuffer *vfb = nullptr;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *v = vfbs_[i];
		if (v->fb_address == params.fb_address) {
			vfb = v;
			// Update fb stride in case it changed
			if (vfb->fb_stride != params.fb_stride || vfb->format != params.fmt) {
				vfbFormatChanged = true;
				vfb->fb_stride = params.fb_stride;
				vfb->format = params.fmt;
			}
			// Keep track, but this isn't really used.
			vfb->z_stride = params.z_stride;
			// Heuristic: In throughmode, a higher height could be used.  Let's avoid shrinking the buffer.
			if (params.isModeThrough && (int)vfb->width <= params.fb_stride) {
				vfb->width = std::max((int)vfb->width, drawing_width);
				vfb->height = std::max((int)vfb->height, drawing_height);
			} else {
				vfb->width = drawing_width;
				vfb->height = drawing_height;
			}
			break;
		} else if (v->fb_address < params.fb_address && v->fb_address + v->fb_stride * 4 > params.fb_address) {
			// Possibly a render-to-offset.
			const u32 bpp = v->format == GE_FORMAT_8888 ? 4 : 2;
			const int x_offset = (params.fb_address - v->fb_address) / bpp;
			if (v->format == params.fmt && v->fb_stride == params.fb_stride && x_offset < params.fb_stride && v->height >= drawing_height) {
				WARN_LOG_REPORT_ONCE(renderoffset, HLE, "Rendering to framebuffer offset: %08x +%dx%d", v->fb_address, x_offset, 0);
				vfb = v;
				gstate_c.SetCurRTOffsetX(x_offset);
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
				bool needsRecreate = vfb->bufferWidth > params.fb_stride;
				needsRecreate = needsRecreate || vfb->newWidth > vfb->bufferWidth || vfb->newWidth * 2 < vfb->bufferWidth;
				needsRecreate = needsRecreate || vfb->newHeight > vfb->bufferHeight || vfb->newHeight * 2 < vfb->bufferHeight;
				if (needsRecreate) {
					ResizeFramebufFBO(vfb, vfb->width, vfb->height, true);
					// Let's discard this information, might be wrong now.
					vfb->safeWidth = 0;
					vfb->safeHeight = 0;
				} else {
					// Even though we won't resize it, let's at least change the size params.
					vfb->width = drawing_width;
					vfb->height = drawing_height;
				}
			}
		} else {
			// It's not different, let's keep track of that too.
			vfb->lastFrameNewSize = gpuStats.numFlips;
		}
	}

	float renderWidthFactor = renderWidth_ / 480.0f;
	float renderHeightFactor = renderHeight_ / 272.0f;

	if (PSP_CoreParameter().compat.flags().Force04154000Download && params.fb_address == 0x00154000) {
		renderWidthFactor = 1.0;
		renderHeightFactor = 1.0;
	}

	// None found? Create one.
	if (!vfb) {
		vfb = new VirtualFramebuffer();
		memset(vfb, 0, sizeof(VirtualFramebuffer));
		vfb->fbo = nullptr;
		vfb->fb_address = params.fb_address;
		vfb->fb_stride = params.fb_stride;
		vfb->z_address = params.z_address;
		vfb->z_stride = params.z_stride;
		vfb->width = drawing_width;
		vfb->height = drawing_height;
		vfb->newWidth = drawing_width;
		vfb->newHeight = drawing_height;
		vfb->lastFrameNewSize = gpuStats.numFlips;
		vfb->renderWidth = (u16)(drawing_width * renderWidthFactor);
		vfb->renderHeight = (u16)(drawing_height * renderHeightFactor);
		vfb->bufferWidth = drawing_width;
		vfb->bufferHeight = drawing_height;
		vfb->format = params.fmt;
		vfb->drawnFormat = params.fmt;
		vfb->usageFlags = FB_USAGE_RENDERTARGET;
		SetColorUpdated(vfb, skipDrawReason);

		u32 byteSize = FramebufferByteSize(vfb);
		u32 fb_address_mem = (params.fb_address & 0x3FFFFFFF) | 0x04000000;
		if (Memory::IsVRAMAddress(fb_address_mem) && fb_address_mem + byteSize > framebufRangeEnd_) {
			framebufRangeEnd_ = fb_address_mem + byteSize;
		}

		ResizeFramebufFBO(vfb, drawing_width, drawing_height, true);
		NotifyRenderFramebufferCreated(vfb);

		INFO_LOG(FRAMEBUF, "Creating FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);

		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfbs_.push_back(vfb);
		currentRenderVfb_ = vfb;

		if (useBufferedRendering_ && !g_Config.bDisableSlowFramebufEffects) {
			gpu->PerformMemoryUpload(fb_address_mem, byteSize);
			NotifyStencilUpload(fb_address_mem, byteSize, true);
			// TODO: Is it worth trying to upload the depth buffer?
		}

		// Let's check for depth buffer overlap.  Might be interesting.
		bool sharingReported = false;
		for (size_t i = 0, end = vfbs_.size(); i < end; ++i) {
			if (vfbs_[i]->z_stride != 0 && params.fb_address == vfbs_[i]->z_address) {
				// If it's clearing it, most likely it just needs more video memory.
				// Technically it could write something interesting and the other might not clear, but that's not likely.
				if (params.isDrawing) {
					if (params.fb_address != params.z_address && vfbs_[i]->fb_address != vfbs_[i]->z_address) {
						WARN_LOG_REPORT(SCEGE, "FBO created from existing depthbuffer as color, %08x/%08x and %08x/%08x", params.fb_address, params.z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
					}
				}
			} else if (params.z_stride != 0 && params.z_address == vfbs_[i]->fb_address) {
				// If it's clearing it, then it's probably just the reverse of the above case.
				if (params.isWritingDepth) {
					WARN_LOG_REPORT(SCEGE, "FBO using existing buffer as depthbuffer, %08x/%08x and %08x/%08x", params.fb_address, params.z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
				}
			} else if (vfbs_[i]->z_stride != 0 && params.z_address == vfbs_[i]->z_address && params.fb_address != vfbs_[i]->fb_address && !sharingReported) {
				// This happens a lot, but virtually always it's cleared.
				// It's possible the other might not clear, but when every game is reported it's not useful.
				if (params.isWritingDepth) {
					WARN_LOG(SCEGE, "FBO reusing depthbuffer, %08x/%08x and %08x/%08x", params.fb_address, params.z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
					sharingReported = true;
				}
			}
		}

	// We already have it!
	} else if (vfb != currentRenderVfb_) {
		// Use it as a render target.
		DEBUG_LOG(FRAMEBUF, "Switching render target to FBO for %08x: %i x %i x %i ", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		vfb->usageFlags |= FB_USAGE_RENDERTARGET;
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;

		VirtualFramebuffer *prev = currentRenderVfb_;
		currentRenderVfb_ = vfb;
		NotifyRenderFramebufferSwitched(prev, vfb, params.isClearingDepth);
	} else {
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;

		NotifyRenderFramebufferUpdated(vfb, vfbFormatChanged);
	}

	gstate_c.curRTWidth = vfb->width;
	gstate_c.curRTHeight = vfb->height;
	gstate_c.curRTRenderWidth = vfb->renderWidth;
	gstate_c.curRTRenderHeight = vfb->renderHeight;
	return vfb;
}

void FramebufferManagerCommon::DestroyFramebuf(VirtualFramebuffer *v) {
	textureCache_->NotifyFramebuffer(v->fb_address, v, NOTIFY_FB_DESTROYED);
	if (v->fbo) {
		v->fbo->Release();
		v->fbo = nullptr;
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

void FramebufferManagerCommon::NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) {
	if (!useBufferedRendering_) {
		// Let's ignore rendering to targets that have not (yet) been displayed.
		gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
	}

	textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_CREATED);

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWPORTSCISSOR_STATE);
	}
	if (gstate_c.curRTRenderWidth != vfb->renderWidth || gstate_c.curRTRenderHeight != vfb->renderHeight) {
		gstate_c.Dirty(DIRTY_PROJMATRIX);
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX);
	}
}

void FramebufferManagerCommon::NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) {
	if (vfbFormatChanged) {
		textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);
		if (vfb->drawnFormat != vfb->format) {
			ReformatFramebufferFrom(vfb, vfb->drawnFormat);
		}
	}

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWPORTSCISSOR_STATE);
	}
	if (gstate_c.curRTRenderWidth != vfb->renderWidth || gstate_c.curRTRenderHeight != vfb->renderHeight) {
		gstate_c.Dirty(DIRTY_PROJMATRIX);
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX);
	}
}

void FramebufferManagerCommon::NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth) {
	if (ShouldDownloadFramebuffer(vfb) && !vfb->memoryUpdated) {
		ReadFramebufferToMemory(vfb, true, 0, 0, vfb->width, vfb->height);
		vfb->usageFlags = (vfb->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
	} else {
		DownloadFramebufferOnSwitch(prevVfb);
	}
	textureCache_->ForgetLastTexture();

	// Copy depth pixel value from the read framebuffer to the draw framebuffer
	if (prevVfb && !g_Config.bDisableSlowFramebufEffects) {
		bool hasNewerDepth = prevVfb->last_frame_depth_render != 0 && prevVfb->last_frame_depth_render >= vfb->last_frame_depth_updated;
		if (!prevVfb->fbo || !vfb->fbo || !useBufferedRendering_ || !hasNewerDepth || isClearingDepth) {
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

	if (useBufferedRendering_) {
		if (vfb->fbo) {
			if (gl_extensions.IsGLES) {
				// Some tiled mobile GPUs benefit IMMENSELY from clearing an FBO before rendering
				// to it. This broke stuff before, so now it only clears on the first use of an
				// FBO in a frame. This means that some games won't be able to avoid the on-some-GPUs
				// performance-crushing framebuffer reloads from RAM, but we'll have to live with that.
				if (vfb->last_frame_render != gpuStats.numFlips) {
					draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
					// GLES resets the blend state on clears.
					gstate_c.Dirty(DIRTY_BLEND_STATE);
				} else {
					draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP });
				}
			} else {
				draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP });
			}
		} else {
			// This should only happen very briefly when toggling useBufferedRendering_.
			ResizeFramebufFBO(vfb, vfb->width, vfb->height, true);
		}
	} else {
		if (vfb->fbo) {
			// This should only happen very briefly when toggling useBufferedRendering_.
			textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_DESTROYED);
			vfb->fbo->Release();
			vfb->fbo = nullptr;
		}

		// Let's ignore rendering to targets that have not (yet) been displayed.
		if (vfb->usageFlags & FB_USAGE_DISPLAYED_FRAMEBUFFER) {
			gstate_c.skipDrawReason &= ~SKIPDRAW_NON_DISPLAYED_FB;
		} else {
			gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
		}
	}
	textureCache_->NotifyFramebuffer(vfb->fb_address, vfb, NOTIFY_FB_UPDATED);

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWPORTSCISSOR_STATE);
	}
	if (gstate_c.curRTRenderWidth != vfb->renderWidth || gstate_c.curRTRenderHeight != vfb->renderHeight) {
		gstate_c.Dirty(DIRTY_PROJMATRIX);
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX);
	}
}

void FramebufferManagerCommon::NotifyVideoUpload(u32 addr, int size, int width, GEBufferFormat fmt) {
	// Note: UpdateFromMemory() is still called later.
	// This is a special case where we have extra information prior to the invalidation.

	// TODO: Could possibly be an offset...
	VirtualFramebuffer *vfb = GetVFBAt(addr);
	if (vfb) {
		if (vfb->format != fmt || vfb->drawnFormat != fmt) {
			DEBUG_LOG(ME, "Changing format for %08x from %d to %d", addr, vfb->drawnFormat, fmt);
			vfb->format = fmt;
			vfb->drawnFormat = fmt;

			// Let's count this as a "render".  This will also force us to use the correct format.
			vfb->last_frame_render = gpuStats.numFlips;
		}

		if (vfb->fb_stride < width) {
			DEBUG_LOG(ME, "Changing stride for %08x from %d to %d", addr, vfb->fb_stride, width);
			const int bpp = fmt == GE_FORMAT_8888 ? 4 : 2;
			ResizeFramebufFBO(vfb, width, size / (bpp * width));
			// Resizing may change the viewport/etc.
			gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
			vfb->fb_stride = width;
			// This might be a bit wider than necessary, but we'll redetect on next render.
			vfb->width = width;
		}
	}
}

void FramebufferManagerCommon::UpdateFromMemory(u32 addr, int size, bool safe) {
	addr &= ~0x40000000;
	// TODO: Could go through all FBOs, but probably not important?
	// TODO: Could also check for inner changes, but video is most important.
	bool isDisplayBuf = addr == DisplayFramebufAddr() || addr == PrevDisplayFramebufAddr();
	if (isDisplayBuf || safe) {
		// TODO: Deleting the FBO is a heavy hammer solution, so let's only do it if it'd help.
		if (!Memory::IsValidAddress(displayFramebufPtr_))
			return;

		for (size_t i = 0; i < vfbs_.size(); ++i) {
			VirtualFramebuffer *vfb = vfbs_[i];
			if (MaskedEqual(vfb->fb_address, addr)) {
				FlushBeforeCopy();

				if (useBufferedRendering_ && vfb->fbo) {
					GEBufferFormat fmt = vfb->format;
					if (vfb->last_frame_render + 1 < gpuStats.numFlips && isDisplayBuf) {
						// If we're not rendering to it, format may be wrong.  Use displayFormat_ instead.
						fmt = displayFormat_;
					}
					DrawPixels(vfb, 0, 0, Memory::GetPointer(addr | 0x04000000), fmt, vfb->fb_stride, vfb->width, vfb->height);
					SetColorUpdated(vfb, gstate_c.skipDrawReason);
				} else {
					INFO_LOG(FRAMEBUF, "Invalidating FBO for %08x (%i x %i x %i)", vfb->fb_address, vfb->width, vfb->height, vfb->format);
					DestroyFramebuf(vfb);
					vfbs_.erase(vfbs_.begin() + i--);
				}
			}
		}

		RebindFramebuffer();
	}
	// TODO: Necessary?
	gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
}

void FramebufferManagerCommon::DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	textureCache_->ForgetLastTexture();
	shaderManager_->DirtyLastShader();  // On GL, important that this is BEFORE drawing
	float u0 = 0.0f, u1 = 1.0f;
	float v0 = 0.0f, v1 = 1.0f;

	if (useBufferedRendering_ && vfb && vfb->fbo) {
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP });
		SetViewport2D(0, 0, vfb->renderWidth, vfb->renderHeight);
		draw_->SetScissorRect(0, 0, vfb->renderWidth, vfb->renderHeight);
	} else {
		// We are drawing to the back buffer so need to flip.
		if (needBackBufferYSwap_)
			std::swap(v0, v1);
		float x, y, w, h;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, ROTATION_LOCKED_HORIZONTAL);
		SetViewport2D(x, y, w, h);
		draw_->SetScissorRect(0, 0, pixelWidth_, pixelHeight_);
	}

	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, width, height, u1, v1);

	DrawTextureFlags flags = (vfb || g_Config.iBufFilter == SCALE_LINEAR) ? DRAWTEX_LINEAR : DRAWTEX_NEAREST;
	Bind2DShader();
	DrawActiveTexture(dstX, dstY, width, height, vfb->bufferWidth, vfb->bufferHeight, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags);
	gpuStats.numUploads++;
}

void FramebufferManagerCommon::CopyFramebufferForColorTexture(VirtualFramebuffer *dst, VirtualFramebuffer *src, int flags) {
	int x = 0;
	int y = 0;
	int w = src->drawnWidth;
	int h = src->drawnHeight;

	// If max is not > min, we probably could not detect it.  Skip.
	// See the vertex decoder, where this is updated.
	if ((flags & BINDFBCOLOR_MAY_COPY_WITH_UV) == BINDFBCOLOR_MAY_COPY_WITH_UV && gstate_c.vertBounds.maxU > gstate_c.vertBounds.minU) {
		x = std::max(gstate_c.vertBounds.minU, (u16)0);
		y = std::max(gstate_c.vertBounds.minV, (u16)0);
		w = std::min(gstate_c.vertBounds.maxU, src->drawnWidth) - x;
		h = std::min(gstate_c.vertBounds.maxV, src->drawnHeight) - y;

		// If we bound a framebuffer, apply the byte offset as pixels to the copy too.
		if (flags & BINDFBCOLOR_APPLY_TEX_OFFSET) {
			x += gstate_c.curTextureXOffset;
			y += gstate_c.curTextureYOffset;
		}
	}

	if (x < src->drawnWidth && y < src->drawnHeight && w > 0 && h > 0) {
		BlitFramebuffer(dst, x, y, src, x, y, w, h, 0);
	}
}

void FramebufferManagerCommon::DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, bool applyPostShader) {
	textureCache_->ForgetLastTexture();
	shaderManager_->DirtyLastShader();

	float u0 = 0.0f, u1 = 480.0f / 512.0f;
	float v0 = 0.0f, v1 = 1.0f;
	MakePixelTexture(srcPixels, srcPixelFormat, srcStride, 512, 272, u1, v1);

	struct CardboardSettings cardboardSettings;
	GetCardboardSettings(&cardboardSettings);

	// This might draw directly at the backbuffer (if so, applyPostShader is set) so if there's a post shader, we need to apply it here.
	// Should try to unify this path with the regular path somehow, but this simple solution works for most of the post shaders 
	// (it always runs at output resolution so FXAA may look odd).
	float x, y, w, h;
	int uvRotation = useBufferedRendering_ ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
	CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, uvRotation);
	if (applyPostShader && useBufferedRendering_) {
		// Might've changed if the shader was just changed to Off.
		if (usePostShader_) {
			PostShaderUniforms uniforms{};
			CalculatePostShaderUniforms(480, 272, renderWidth_, renderHeight_, &uniforms);
			BindPostShader(uniforms);
		} else {
			Bind2DShader();
		}
	} else {
		Bind2DShader();
	}

	// We are drawing directly to the back buffer.
	if (needBackBufferYSwap_)
		std::swap(v0, v1);

	DrawTextureFlags flags = g_Config.iBufFilter == SCALE_LINEAR ? DRAWTEX_LINEAR : DRAWTEX_NEAREST;
	if (cardboardSettings.enabled) {
		// Left Eye Image
		SetViewport2D(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
		DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags | DRAWTEX_KEEP_TEX);
		// Right Eye Image
		SetViewport2D(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
		DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags);
	} else {
		// Fullscreen Image
		SetViewport2D(0, 0, pixelWidth_, pixelHeight_);
		DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, uvRotation, flags);
	}

	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE);
}

void FramebufferManagerCommon::DownloadFramebufferOnSwitch(VirtualFramebuffer *vfb) {
	if (vfb && vfb->safeWidth > 0 && vfb->safeHeight > 0 && !vfb->firstFrameSaved && !vfb->memoryUpdated) {
		// Some games will draw to some memory once, and use it as a render-to-texture later.
		// To support this, we save the first frame to memory when we have a safe w/h.
		// Saving each frame would be slow.
		if (!g_Config.bDisableSlowFramebufEffects) {
			ReadFramebufferToMemory(vfb, true, 0, 0, vfb->safeWidth, vfb->safeHeight);
			vfb->usageFlags = (vfb->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
			vfb->firstFrameSaved = true;
			vfb->safeWidth = 0;
			vfb->safeHeight = 0;
		}
	}
}

void FramebufferManagerCommon::SetViewport2D(int x, int y, int w, int h) {
	Draw::Viewport vp{ (float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f };
	draw_->SetViewports(1, &vp);
}

void FramebufferManagerCommon::CopyDisplayToOutput() {
	DownloadFramebufferOnSwitch(currentRenderVfb_);

	currentRenderVfb_ = 0;

	if (displayFramebufPtr_ == 0) {
		DEBUG_LOG(FRAMEBUF, "Display disabled, displaying only black");
		// No framebuffer to display! Clear to black.
		if (useBufferedRendering_) {
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
		}
		gstate_c.Dirty(DIRTY_BLEND_STATE);
		return;
	}

	u32 offsetX = 0;
	u32 offsetY = 0;

	CardboardSettings cardboardSettings;
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
				if (useBufferedRendering_) {
					// Bind and clear the backbuffer. This should be the first time during the frame that it's bound.
					draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
				}
				// Just a pointer to plain memory to draw. We should create a framebuffer, then draw to it.
				SetViewport2D(0, 0, pixelWidth_, pixelHeight_);
				draw_->SetScissorRect(0, 0, pixelWidth_, pixelHeight_);
				DrawFramebufferToOutput(Memory::GetPointer(displayFramebufPtr_), displayFormat_, displayStride_, true);
				gstate_c.Dirty(DIRTY_BLEND_STATE);
				return;
			}
		} else {
			DEBUG_LOG(FRAMEBUF, "Found no FBO to display! displayFBPtr = %08x", displayFramebufPtr_);
			// No framebuffer to display! Clear to black.
			if (useBufferedRendering_) {
				// Bind and clear the backbuffer. This should be the first time during the frame that it's bound.
				draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
			}
			gstate_c.Dirty(DIRTY_BLEND_STATE);
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

	if (vfb->fbo) {
		DEBUG_LOG(FRAMEBUF, "Displaying FBO %08x", vfb->fb_address);

		int uvRotation = useBufferedRendering_ ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;

		// Output coordinates
		float x, y, w, h;
		CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, (float)pixelWidth_, (float)pixelHeight_, uvRotation);

		// TODO ES3: Use glInvalidateFramebuffer to discard depth/stencil data at the end of frame.

		float u0 = offsetX / (float)vfb->bufferWidth;
		float v0 = offsetY / (float)vfb->bufferHeight;
		float u1 = (480.0f + offsetX) / (float)vfb->bufferWidth;
		float v1 = (272.0f + offsetY) / (float)vfb->bufferHeight;

		if (!usePostShader_) {
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
			draw_->BindFramebufferAsTexture(vfb->fbo, 0, Draw::FB_COLOR_BIT, 0);
			draw_->SetScissorRect(0, 0, pixelWidth_, pixelHeight_);
			DrawTextureFlags flags = g_Config.iBufFilter == SCALE_LINEAR ? DRAWTEX_LINEAR : DRAWTEX_NEAREST;
			// We are doing the DrawActiveTexture call directly to the backbuffer here. Hence, we must
			// flip V.
			Bind2DShader();
			if (needBackBufferYSwap_)
				std::swap(v0, v1);
			if (cardboardSettings.enabled) {
				// Left Eye Image
				SetViewport2D(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags | DRAWTEX_KEEP_TEX);

				// Right Eye Image
				SetViewport2D(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags);
			} else {
				// Fullscreen Image
				SetViewport2D(0, 0, pixelWidth_, pixelHeight_);
				DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, uvRotation, flags);
			}
		} else if (usePostShader_ && extraFBOs_.size() == 1 && !postShaderAtOutputResolution_) {
			// An additional pass, post-processing shader to the extra FBO.
			draw_->BindFramebufferAsRenderTarget(extraFBOs_[0], { Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
			draw_->BindFramebufferAsTexture(vfb->fbo, 0, Draw::FB_COLOR_BIT, 0);
			int fbo_w, fbo_h;
			draw_->GetFramebufferDimensions(extraFBOs_[0], &fbo_w, &fbo_h);
			SetViewport2D(0, 0, fbo_w, fbo_h);
			draw_->SetScissorRect(0, 0, fbo_w, fbo_h);
			shaderManager_->DirtyLastShader();  // dirty lastShader_
			PostShaderUniforms uniforms{};
			CalculatePostShaderUniforms(vfb->bufferWidth, vfb->bufferHeight, renderWidth_, renderHeight_, &uniforms);
			BindPostShader(uniforms);
			DrawTextureFlags flags = g_Config.iBufFilter == SCALE_LINEAR ? DRAWTEX_LINEAR : DRAWTEX_NEAREST;
			DrawActiveTexture(0, 0, fbo_w, fbo_h, fbo_w, fbo_h, 0.0f, 0.0f, 1.0f, 1.0f, ROTATION_LOCKED_HORIZONTAL, flags);

			draw_->SetScissorRect(0, 0, pixelWidth_, pixelHeight_);
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });

			// Use the extra FBO, with applied post-processing shader, as a texture.
			// fbo_bind_as_texture(extraFBOs_[0], FB_COLOR_BIT, 0);
			if (extraFBOs_.size() == 0) {
				ERROR_LOG(FRAMEBUF, "Unexpected: No extra FBOs?");
				return;
			}
			draw_->BindFramebufferAsTexture(extraFBOs_[0], 0, Draw::FB_COLOR_BIT, 0);

			// We are doing the DrawActiveTexture call directly to the backbuffer after here. Hence, we must
			// flip V.
			if (needBackBufferYSwap_)
				std::swap(v0, v1);
			Bind2DShader();
			flags = (!postShaderIsUpscalingFilter_ && g_Config.iBufFilter == SCALE_LINEAR) ? DRAWTEX_LINEAR : DRAWTEX_NEAREST;
			if (g_Config.bEnableCardboard) {
				// Left Eye Image
				SetViewport2D(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags | DRAWTEX_KEEP_TEX);

				// Right Eye Image
				SetViewport2D(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags);
			} else {
				// Fullscreen Image
				SetViewport2D(0, 0, pixelWidth_, pixelHeight_);
				DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, uvRotation, flags);
			}
		} else {
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
			draw_->BindFramebufferAsTexture(vfb->fbo, 0, Draw::FB_COLOR_BIT, 0);
			draw_->SetScissorRect(0, 0, pixelWidth_, pixelHeight_);
			// We are doing the DrawActiveTexture call directly to the backbuffer here. Hence, we must
			// flip V.
			if (needBackBufferYSwap_)
				std::swap(v0, v1);
			DrawTextureFlags flags = (!postShaderIsUpscalingFilter_ && g_Config.iBufFilter == SCALE_LINEAR) ? DRAWTEX_LINEAR : DRAWTEX_NEAREST;

			shaderManager_->DirtyLastShader();  // dirty lastShader_ BEFORE drawing
			PostShaderUniforms uniforms{};
			CalculatePostShaderUniforms(vfb->bufferWidth, vfb->bufferHeight, vfb->renderWidth, vfb->renderHeight, &uniforms);
			BindPostShader(uniforms);
			if (g_Config.bEnableCardboard) {
				// Left Eye Image
				SetViewport2D(cardboardSettings.leftEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags | DRAWTEX_KEEP_TEX);

				// Right Eye Image
				SetViewport2D(cardboardSettings.rightEyeXPosition, cardboardSettings.screenYPosition, cardboardSettings.screenWidth, cardboardSettings.screenHeight);
				DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags);
			} else {
				// Fullscreen Image
				SetViewport2D(0, 0, pixelWidth_, pixelHeight_);
				DrawActiveTexture(x, y, w, h, (float)pixelWidth_, (float)pixelHeight_, u0, v0, u1, v1, uvRotation, flags);
			}
		}
	}
	else if (useBufferedRendering_) {
		WARN_LOG(FRAMEBUF, "Current VFB lacks an FBO: %08x", vfb->fb_address);
	}

	// This may get called mid-draw if the game uses an immediate flip.
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE);
}

void FramebufferManagerCommon::DecimateFBOs() {
	currentRenderVfb_ = 0;

	for (auto iter : fbosToDelete_) {
		iter->Release();
	}
	fbosToDelete_.clear();

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		int age = frameLastFramebufUsed_ - std::max(vfb->last_frame_render, vfb->last_frame_used);

		if (ShouldDownloadFramebuffer(vfb) && age == 0 && !vfb->memoryUpdated) {
			bool sync = gl_extensions.IsGLES;
			ReadFramebufferToMemory(vfb, sync, 0, 0, vfb->width, vfb->height);
			vfb->usageFlags = (vfb->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
		}

		// Let's also "decimate" the usageFlags.
		UpdateFramebufUsage(vfb);

		if (vfb != displayFramebuf_ && vfb != prevDisplayFramebuf_ && vfb != prevPrevDisplayFramebuf_) {
			if (age > FBO_OLD_AGE) {
				INFO_LOG(FRAMEBUF, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age);
				DestroyFramebuf(vfb);
				vfbs_.erase(vfbs_.begin() + i--);
			}
		}
	}

	for (auto it = tempFBOs_.begin(); it != tempFBOs_.end(); ) {
		int age = frameLastFramebufUsed_ - it->second.last_frame_used;
		if (age > FBO_OLD_AGE) {
			it->second.fbo->Release();
			it = tempFBOs_.erase(it);
		} else {
			++it;
		}
	}

	// Do the same for ReadFramebuffersToMemory's VFBs
	for (size_t i = 0; i < bvfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = bvfbs_[i];
		int age = frameLastFramebufUsed_ - vfb->last_frame_render;
		if (age > FBO_OLD_AGE) {
			INFO_LOG(FRAMEBUF, "Decimating FBO for %08x (%i x %i x %i), age %i", vfb->fb_address, vfb->width, vfb->height, vfb->format, age);
			DestroyFramebuf(vfb);
			bvfbs_.erase(bvfbs_.begin() + i--);
		}
	}
}

void FramebufferManagerCommon::ResizeFramebufFBO(VirtualFramebuffer *vfb, int w, int h, bool force, bool skipCopy) {
	assert(w > 0);
	assert(h > 0);
	VirtualFramebuffer old = *vfb;

	int oldWidth = vfb->bufferWidth;
	int oldHeight = vfb->bufferHeight;

	if (force) {
		vfb->bufferWidth = w;
		vfb->bufferHeight = h;
	} else {
		if (vfb->bufferWidth >= w && vfb->bufferHeight >= h) {
			return;
		}

		// In case it gets thin and wide, don't resize down either side.
		vfb->bufferWidth = std::max((int)vfb->bufferWidth, w);
		vfb->bufferHeight = std::max((int)vfb->bufferHeight, h);
	}

	SetRenderSize(vfb);

	bool trueColor = trueColor_;
	if (PSP_CoreParameter().compat.flags().Force04154000Download && vfb->fb_address == 0x00154000) {
		trueColor = true;
	}

	if (trueColor) {
		vfb->colorDepth = Draw::FBO_8888;
	} else {
		switch (vfb->format) {
		case GE_FORMAT_4444:
			vfb->colorDepth = Draw::FBO_4444;
			break;
		case GE_FORMAT_5551:
			vfb->colorDepth = Draw::FBO_5551;
			break;
		case GE_FORMAT_565:
			vfb->colorDepth = Draw::FBO_565;
			break;
		case GE_FORMAT_8888:
		default:
			vfb->colorDepth = Draw::FBO_8888;
			break;
		}
	}

	textureCache_->ForgetLastTexture();

	if (!useBufferedRendering_) {
		if (vfb->fbo) {
			vfb->fbo->Release();
			vfb->fbo = nullptr;
		}
		return;
	}
	if (!old.fbo && vfb->last_frame_failed != 0 && vfb->last_frame_failed - gpuStats.numFlips < 63) {
		// Don't constantly retry FBOs which failed to create.
		return;
	}

	vfb->fbo = draw_->CreateFramebuffer({ vfb->renderWidth, vfb->renderHeight, 1, 1, true, (Draw::FBColorDepth)vfb->colorDepth });
	if (old.fbo) {
		INFO_LOG(FRAMEBUF, "Resizing FBO for %08x : %d x %d x %d", vfb->fb_address, w, h, vfb->format);
		if (vfb->fbo) {
			draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
			// GLES resets the blend state on clears.
			gstate_c.Dirty(DIRTY_BLEND_STATE);
			if (!skipCopy && !g_Config.bDisableSlowFramebufEffects) {
				BlitFramebuffer(vfb, 0, 0, &old, 0, 0, std::min((u16)oldWidth, std::min(vfb->bufferWidth, vfb->width)), std::min((u16)oldHeight, std::min(vfb->height, vfb->bufferHeight)), 0);
			}
		}
		fbosToDelete_.push_back(old.fbo);
		if (needGLESRebinds_) {
			draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP });
		}
	} else {
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR });
		// GLES resets the blend state on clears.
		gstate_c.Dirty(DIRTY_BLEND_STATE);
	}

	if (!vfb->fbo) {
		ERROR_LOG(FRAMEBUF, "Error creating FBO! %i x %i", vfb->renderWidth, vfb->renderHeight);
		vfb->last_frame_failed = gpuStats.numFlips;
	}
}

bool FramebufferManagerCommon::NotifyFramebufferCopy(u32 src, u32 dst, int size, bool isMemset, u32 skipDrawReason) {
	if (size == 0) {
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
		if (vfb->fb_stride == 0) {
			continue;
		}

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
			} else if ((offset % vfb_byteStride) == 0 && size == vfb->fb_stride && yOffset < srcY) {
				// Valkyrie Profile reads 512 bytes at a time, rather than 2048.  So, let's whitelist fb_stride also.
				srcBuffer = vfb;
				srcY = yOffset;
				srcH = 1;
			} else if (yOffset == 0 && yOffset < srcY) {
				// Okay, last try - it might be a clut.
				if (vfb->usageFlags & FB_USAGE_CLUT) {
					srcBuffer = vfb;
					srcY = yOffset;
					srcH = 1;
				}
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
				BlitFramebuffer(dstBuffer, 0, dstY, srcBuffer, 0, srcY, srcBuffer->width, srcH, 0);
				SetColorUpdated(dstBuffer, skipDrawReason);
				RebindFramebuffer();
			}
		}
		return false;
	} else if (dstBuffer) {
		if (isMemset) {
			gpuStats.numClears++;
		}
		WARN_LOG_ONCE(btucpy, G3D, "Memcpy fbo upload %08x -> %08x", src, dst);
		if (g_Config.bBlockTransferGPU) {
			FlushBeforeCopy();
			const u8 *srcBase = Memory::GetPointerUnchecked(src);
			DrawPixels(dstBuffer, 0, dstY, srcBase, dstBuffer->format, dstBuffer->fb_stride, dstBuffer->width, dstH);
			SetColorUpdated(dstBuffer, skipDrawReason);
			RebindFramebuffer();
			// This is a memcpy, let's still copy just in case.
			return false;
		}
		return false;
	} else if (srcBuffer) {
		WARN_LOG_ONCE(btdcpy, G3D, "Memcpy fbo download %08x -> %08x", src, dst);
		FlushBeforeCopy();
		if (srcH == 0 || srcY + srcH > srcBuffer->bufferHeight) {
			WARN_LOG_REPORT_ONCE(btdcpyheight, G3D, "Memcpy fbo download %08x -> %08x skipped, %d+%d is taller than %d", src, dst, srcY, srcH, srcBuffer->bufferHeight);
		} else if (g_Config.bBlockTransferGPU && !srcBuffer->memoryUpdated && !PSP_CoreParameter().compat.flags().DisableReadbacks) {
			ReadFramebufferToMemory(srcBuffer, true, 0, srcY, srcBuffer->width, srcH);
			srcBuffer->usageFlags = (srcBuffer->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
		}
		return false;
	} else {
		return false;
	}
}

void FramebufferManagerCommon::FindTransferFramebuffers(VirtualFramebuffer *&dstBuffer, VirtualFramebuffer *&srcBuffer, u32 dstBasePtr, int dstStride, int &dstX, int &dstY, u32 srcBasePtr, int srcStride, int &srcX, int &srcY, int &srcWidth, int &srcHeight, int &dstWidth, int &dstHeight, int bpp) const {
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
			bool match = yOffset < dstYOffset && (int)yOffset <= (int)vfb->height - dstHeight;
			if (match && vfb_byteStride != byteStride) {
				// Grand Knights History copies with a mismatching stride but a full line at a time.
				// Makes it hard to detect the wrong transfers in e.g. God of War.
				if (width != dstStride || (byteStride * height != vfb_byteStride && byteStride * height != vfb_byteWidth)) {
					// However, some other games write cluts to framebuffers.
					// Let's catch this and upload.  Otherwise reject the match.
					match = (vfb->usageFlags & FB_USAGE_CLUT) != 0;
					if (match) {
						dstWidth = byteStride * height / vfb_bpp;
						dstHeight = 1;
					}
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
				dstXOffset = dstStride == 0 ? 0 : (byteOffset / bpp) % dstStride;
				dstBuffer = vfb;
			}
		}
		if (vfb_address <= srcBasePtr && srcBasePtr < vfb_address + vfb_size) {
			const u32 byteOffset = srcBasePtr - vfb_address;
			const u32 byteStride = srcStride * bpp;
			const u32 yOffset = byteOffset / byteStride;
			bool match = yOffset < srcYOffset && (int)yOffset <= (int)vfb->height - srcHeight;
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
				srcXOffset = srcStride == 0 ? 0 : (byteOffset / bpp) % srcStride;
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

// 1:1 pixel sides buffers, we resize buffers to these before we read them back.
VirtualFramebuffer *FramebufferManagerCommon::FindDownloadTempBuffer(VirtualFramebuffer *vfb) {
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
		memset(nvfb, 0, sizeof(VirtualFramebuffer));
		nvfb->fbo = nullptr;
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
		nvfb->colorDepth = vfb->colorDepth;

		if (!CreateDownloadTempBuffer(nvfb)) {
			delete nvfb;
			return nullptr;
		}

		bvfbs_.push_back(nvfb);
	} else {
		UpdateDownloadTempBuffer(nvfb);
	}

	nvfb->usageFlags |= FB_USAGE_RENDERTARGET;
	nvfb->last_frame_render = gpuStats.numFlips;
	nvfb->dirtyAfterDisplay = true;

	return nvfb;
}

void FramebufferManagerCommon::ApplyClearToMemory(int x1, int y1, int x2, int y2, u32 clearColor) {
	if (currentRenderVfb_) {
		if ((currentRenderVfb_->usageFlags & FB_USAGE_DOWNLOAD_CLEAR) != 0) {
			// Already zeroed in memory.
			return;
		}
	}

	u8 *addr = Memory::GetPointer(gstate.getFrameBufAddress());
	const int bpp = gstate.FrameBufFormat() == GE_FORMAT_8888 ? 4 : 2;

	u32 clearBits = clearColor;
	if (bpp == 2) {
		u16 clear16 = 0;
		switch (gstate.FrameBufFormat()) {
		case GE_FORMAT_565: ConvertRGBA8888ToRGB565(&clear16, &clearColor, 1); break;
		case GE_FORMAT_5551: ConvertRGBA8888ToRGBA5551(&clear16, &clearColor, 1); break;
		case GE_FORMAT_4444: ConvertRGBA8888ToRGBA4444(&clear16, &clearColor, 1); break;
		default: _dbg_assert_(G3D, 0); break;
		}
		clearBits = clear16 | (clear16 << 16);
	}

	const bool singleByteClear = (clearBits >> 16) == (clearBits & 0xFFFF) && (clearBits >> 24) == (clearBits & 0xFF);
	const int stride = gstate.FrameBufStride();
	const int width = x2 - x1;

	// Can use memset for simple cases. Often alpha is different and gums up the works.
	if (singleByteClear) {
		const int byteStride = stride * bpp;
		const int byteWidth = width * bpp;
		addr += x1 * bpp;
		for (int y = y1; y < y2; ++y) {
			memset(addr + y * byteStride, clearBits, byteWidth);
		}
	} else {
		// This will most often be true - rarely is the width not aligned.
		// TODO: We should really use non-temporal stores here to avoid the cache,
		// as it's unlikely that these bytes will be read.
		if ((width & 3) == 0 && (x1 & 3) == 0) {
			u64 val64 = clearBits | ((u64)clearBits << 32);
			int xstride = 8 / bpp;

			u64 *addr64 = (u64 *)addr;
			const int stride64 = stride / xstride;
			const int x1_64 = x1 / xstride;
			const int x2_64 = x2 / xstride;
			for (int y = y1; y < y2; ++y) {
				for (int x = x1_64; x < x2_64; ++x) {
					addr64[y * stride64 + x] = val64;
				}
			}
		} else if (bpp == 4) {
			u32 *addr32 = (u32 *)addr;
			for (int y = y1; y < y2; ++y) {
				for (int x = x1; x < x2; ++x) {
					addr32[y * stride + x] = clearBits;
				}
			}
		} else if (bpp == 2) {
			u16 *addr16 = (u16 *)addr;
			for (int y = y1; y < y2; ++y) {
				for (int x = x1; x < x2; ++x) {
					addr16[y * stride + x] = (u16)clearBits;
				}
			}
		}
	}

	if (currentRenderVfb_) {
		// The current content is in memory now, so update the flag.
		if (x1 == 0 && y1 == 0 && x2 >= currentRenderVfb_->width && y2 >= currentRenderVfb_->height) {
			currentRenderVfb_->usageFlags |= FB_USAGE_DOWNLOAD_CLEAR;
			currentRenderVfb_->memoryUpdated = true;
		}
	}
}

void FramebufferManagerCommon::OptimizeDownloadRange(VirtualFramebuffer * vfb, int & x, int & y, int & w, int & h) {
	if (gameUsesSequentialCopies_) {
		// Ignore the x/y/etc., read the entire thing.
		x = 0;
		y = 0;
		w = vfb->width;
		h = vfb->height;
	}
	if (x == 0 && y == 0 && w == vfb->width && h == vfb->height) {
		// Mark it as fully downloaded until next render to it.
		vfb->memoryUpdated = true;
		vfb->usageFlags |= FB_USAGE_DOWNLOAD;
	} else {
		// Let's try to set the flag eventually, if the game copies a lot.
		// Some games copy subranges very frequently.
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
}

bool FramebufferManagerCommon::NotifyBlockTransferBefore(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int width, int height, int bpp, u32 skipDrawReason) {
	if (!useBufferedRendering_) {
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
					BlitFramebuffer(dstBuffer, dstX, dstY, srcBuffer, srcX, srcY, dstWidth, dstHeight, bpp);
					RebindFramebuffer();
					SetColorUpdated(dstBuffer, skipDrawReason);
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
				BlitFramebuffer(dstBuffer, dstX, dstY, srcBuffer, srcX, srcY, dstWidth, dstHeight, bpp);
				RebindFramebuffer();
				SetColorUpdated(dstBuffer, skipDrawReason);
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
			const int srcBpp = srcBuffer->format == GE_FORMAT_8888 ? 4 : 2;
			const float srcXFactor = (float)bpp / srcBpp;
			const bool tooTall = srcY + srcHeight > srcBuffer->bufferHeight;
			if (srcHeight <= 0 || (tooTall && srcY != 0)) {
				WARN_LOG_ONCE(btdheight, G3D, "Block transfer download %08x -> %08x skipped, %d+%d is taller than %d", srcBasePtr, dstBasePtr, srcY, srcHeight, srcBuffer->bufferHeight);
			} else {
				if (tooTall)
					WARN_LOG_ONCE(btdheight, G3D, "Block transfer download %08x -> %08x dangerous, %d+%d is taller than %d", srcBasePtr, dstBasePtr, srcY, srcHeight, srcBuffer->bufferHeight);
				ReadFramebufferToMemory(srcBuffer, true, static_cast<int>(srcX * srcXFactor), srcY, static_cast<int>(srcWidth * srcXFactor), srcHeight);
				srcBuffer->usageFlags = (srcBuffer->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
			}
		}
		return false;  // Let the bit copy happen
	} else {
		return false;
	}
}

void FramebufferManagerCommon::NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int width, int height, int bpp, u32 skipDrawReason) {
	// A few games use this INSTEAD of actually drawing the video image to the screen, they just blast it to
	// the backbuffer. Detect this and have the framebuffermanager draw the pixels.

	u32 backBuffer = PrevDisplayFramebufAddr();
	u32 displayBuffer = DisplayFramebufAddr();

	// TODO: Is this not handled by upload?  Should we check !dstBuffer to avoid a double copy?
	if (((backBuffer != 0 && dstBasePtr == backBuffer) ||
		(displayBuffer != 0 && dstBasePtr == displayBuffer)) &&
		dstStride == 512 && height == 272 && !useBufferedRendering_) {
		FlushBeforeCopy();
		DrawFramebufferToOutput(Memory::GetPointerUnchecked(dstBasePtr), displayFormat_, 512, false);
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
				int dstBpp = dstBuffer->format == GE_FORMAT_8888 ? 4 : 2;
				float dstXFactor = (float)bpp / dstBpp;
				if (dstWidth > dstBuffer->width || dstHeight > dstBuffer->height) {
					// The buffer isn't big enough, and we have a clear hint of size.  Resize.
					// This happens in Valkyrie Profile when uploading video at the ending.
					ResizeFramebufFBO(dstBuffer, dstWidth, dstHeight, false, true);
					// Make sure we don't flop back and forth.
					dstBuffer->newWidth = std::max(dstWidth, (int)dstBuffer->width);
					dstBuffer->newHeight = std::max(dstHeight, (int)dstBuffer->height);
					dstBuffer->lastFrameNewSize = gpuStats.numFlips;
					// Resizing may change the viewport/etc.
					gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
				}
				DrawPixels(dstBuffer, static_cast<int>(dstX * dstXFactor), dstY, srcBase, dstBuffer->format, static_cast<int>(srcStride * dstXFactor), static_cast<int>(dstWidth * dstXFactor), dstHeight);
				SetColorUpdated(dstBuffer, skipDrawReason);
				RebindFramebuffer();
			}
		}
	}
}

void FramebufferManagerCommon::SetRenderSize(VirtualFramebuffer *vfb) {
	float renderWidthFactor = renderWidth_ / 480.0f;
	float renderHeightFactor = renderHeight_ / 272.0f;
	bool force1x = false;
	switch (bloomHack_) {
	case 1:
		force1x = vfb->bufferWidth <= 128 || vfb->bufferHeight <= 64;
		break;
	case 2:
		force1x = vfb->bufferWidth <= 256 || vfb->bufferHeight <= 128;
		break;
	case 3:
		force1x = vfb->bufferWidth < 480 || vfb->bufferHeight < 272;
		break;
	}

	if (PSP_CoreParameter().compat.flags().Force04154000Download && vfb->fb_address == 0x00154000) {
		force1x = true;
	}

	if (force1x && g_Config.iInternalResolution != 1) {
		vfb->renderWidth = vfb->bufferWidth;
		vfb->renderHeight = vfb->bufferHeight;
	} else {
		vfb->renderWidth = (u16)(vfb->bufferWidth * renderWidthFactor);
		vfb->renderHeight = (u16)(vfb->bufferHeight * renderHeightFactor);
	}
}

void FramebufferManagerCommon::SetSafeSize(u16 w, u16 h) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (vfb) {
		vfb->safeWidth = std::max(vfb->safeWidth, w);
		vfb->safeHeight = std::max(vfb->safeHeight, h);
	}
}

void FramebufferManagerCommon::Resized() {
	// Check if postprocessing shader is doing upscaling as it requires native resolution
	const ShaderInfo *shaderInfo = nullptr;
	if (g_Config.sPostShaderName != "Off") {
		shaderInfo = GetPostShaderInfo(g_Config.sPostShaderName);
	}

	postShaderIsUpscalingFilter_ = shaderInfo ? shaderInfo->isUpscalingFilter : false;
	postShaderSSAAFilterLevel_ = shaderInfo ? shaderInfo->SSAAFilterLevel : 0;

	// Actually, auto mode should be more granular...
	// Round up to a zoom factor for the render size.
	int zoom = g_Config.iInternalResolution;
	if (zoom == 0 || postShaderSSAAFilterLevel_ >= 2) {
		// auto mode, use the longest dimension
		if (!g_Config.IsPortrait()) {
			zoom = (PSP_CoreParameter().pixelWidth + 479) / 480;
		} else {
			zoom = (PSP_CoreParameter().pixelHeight + 479) / 480;
		}
		if (postShaderSSAAFilterLevel_ >= 2)
			zoom *= postShaderSSAAFilterLevel_;
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

	gstate_c.skipDrawReason &= ~SKIPDRAW_NON_DISPLAYED_FB;

#ifdef _WIN32
	// Seems related - if you're ok with numbers all the time, show some more :)
	if (g_Config.iShowFPSCounter != 0) {
		ShowScreenResolution();
	}
#endif
}

void FramebufferManagerCommon::CalculatePostShaderUniforms(int bufferWidth, int bufferHeight, int renderWidth, int renderHeight, PostShaderUniforms *uniforms) {
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
	int flipCount = __DisplayGetFlipCount();
	int vCount = __DisplayGetVCount();
	float time[4] = { time_now(), (vCount % 60) * 1.0f / 60.0f, (float)vCount, (float)(flipCount % 60) };

	uniforms->texelDelta[0] = u_delta;
	uniforms->texelDelta[1] = v_delta;
	uniforms->pixelDelta[0] = u_pixel_delta;
	uniforms->pixelDelta[1] = v_pixel_delta;
	memcpy(uniforms->time, time, 4 * sizeof(float));
	uniforms->video = textureCache_->VideoIsPlaying();
}

void FramebufferManagerCommon::GetCardboardSettings(CardboardSettings *cardboardSettings) {
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

	cardboardSettings->enabled = g_Config.bEnableCardboard;
	cardboardSettings->leftEyeXPosition = cardboardLeftEyeX;
	cardboardSettings->rightEyeXPosition = cardboardRightEyeX;
	cardboardSettings->screenYPosition = cardboardScreenY;
	cardboardSettings->screenWidth = cardboardScreenWidth;
	cardboardSettings->screenHeight = cardboardScreenHeight;
}

Draw::Framebuffer *FramebufferManagerCommon::GetTempFBO(TempFBO reason, u16 w, u16 h, Draw::FBColorDepth depth) {
	u64 key = ((u64)reason << 48) | ((u64)depth << 32) | ((u32)w << 16) | h;
	auto it = tempFBOs_.find(key);
	if (it != tempFBOs_.end()) {
		it->second.last_frame_used = gpuStats.numFlips;
		return it->second.fbo;
	}

	textureCache_->ForgetLastTexture();
	Draw::Framebuffer *fbo = draw_->CreateFramebuffer({ w, h, 1, 1, false, depth });
	if (!fbo)
		return fbo;

	const TempFBOInfo info = { fbo, gpuStats.numFlips };
	tempFBOs_[key] = info;
	return fbo;
}

void FramebufferManagerCommon::UpdateFramebufUsage(VirtualFramebuffer *vfb) {
	auto checkFlag = [&](u16 flag, int last_frame) {
		if (vfb->usageFlags & flag) {
			const int age = frameLastFramebufUsed_ - last_frame;
			if (age > FBO_OLD_USAGE_FLAG) {
				vfb->usageFlags &= ~flag;
			}
		}
	};

	checkFlag(FB_USAGE_DISPLAYED_FRAMEBUFFER, vfb->last_frame_displayed);
	checkFlag(FB_USAGE_TEXTURE, vfb->last_frame_used);
	checkFlag(FB_USAGE_RENDERTARGET, vfb->last_frame_render);
	checkFlag(FB_USAGE_CLUT, vfb->last_frame_clut);
}

void FramebufferManagerCommon::ShowScreenResolution() {
	I18NCategory *gr = GetI18NCategory("Graphics");

	std::ostringstream messageStream;
	messageStream << gr->T("Internal Resolution") << ": ";
	messageStream << PSP_CoreParameter().renderWidth << "x" << PSP_CoreParameter().renderHeight << " ";
	if (postShaderIsUpscalingFilter_) {
		messageStream << gr->T("(upscaling)") << " ";
	} else if (postShaderSSAAFilterLevel_ >= 2) {
		messageStream << gr->T("(supersampling)") << " ";
	}
	messageStream << gr->T("Window Size") << ": ";
	messageStream << PSP_CoreParameter().pixelWidth << "x" << PSP_CoreParameter().pixelHeight;

	host->NotifyUserMessage(messageStream.str(), 2.0f, 0xFFFFFF, "resize");
	INFO_LOG(SYSTEM, "%s", messageStream.str().c_str());
}

// We might also want to implement an asynchronous callback-style version of this. Would probably
// only be possible to implement optimally on Vulkan, but on GL and D3D11 we could do pixel buffers
// and read on the next frame, then call the callback. PackFramebufferAsync_ on OpenGL already does something similar.
//
// The main use cases for this are:
// * GE debugging(in practice async will not matter because it will stall anyway.)
// * Video file recording(would probably be great if it was async.)
// * Screenshots(benefit slightly from async.)
// * Save state screenshots(could probably be async but need to manage the stall.)
bool FramebufferManagerCommon::GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxRes) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(fb_address | 0x04000000), fb_stride, 512, format);
		return true;
	}

	int w = vfb->renderWidth, h = vfb->renderHeight;

	Draw::Framebuffer *bound = nullptr;

	if (vfb->fbo) {
		if (maxRes > 0 && vfb->renderWidth > vfb->width * maxRes) {
			w = vfb->width * maxRes;
			h = vfb->height * maxRes;

			Draw::Framebuffer *tempFBO = GetTempFBO(TempFBO::COPY, w, h);
			VirtualFramebuffer tempVfb = *vfb;
			tempVfb.fbo = tempFBO;
			tempVfb.bufferWidth = vfb->width;
			tempVfb.bufferHeight = vfb->height;
			tempVfb.renderWidth = w;
			tempVfb.renderHeight = h;
			BlitFramebuffer(&tempVfb, 0, 0, vfb, 0, 0, vfb->width, vfb->height, 0);

			bound = tempFBO;
		} else {
			bound = vfb->fbo;
		}
	}

	if (!useBufferedRendering_) {
		// Safety check.
		w = std::min(w, PSP_CoreParameter().pixelWidth);
		h = std::min(h, PSP_CoreParameter().pixelHeight);
	}

	// TODO: Maybe should handle flipY inside CopyFramebufferToMemorySync somehow?
	bool flipY = (GetGPUBackend() == GPUBackend::OPENGL && !useBufferedRendering_) ? true : false;
	buffer.Allocate(w, h, GE_FORMAT_8888, flipY, true);
	bool retval = draw_->CopyFramebufferToMemorySync(bound, Draw::FB_COLOR_BIT, 0, 0, w, h, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), w);
	gpuStats.numReadbacks++;
	// After a readback we'll have flushed and started over, need to dirty a bunch of things to be safe.
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	// We may have blitted to a temp FBO.
	RebindFramebuffer();
	return retval;
}

bool FramebufferManagerCommon::GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(z_address | 0x04000000), z_stride, 512, GPU_DBG_FORMAT_16BIT);
		return true;
	}

	int w = vfb->renderWidth;
	int h = vfb->renderHeight;
	if (!useBufferedRendering_) {
		// Safety check.
		w = std::min(w, PSP_CoreParameter().pixelWidth);
		h = std::min(h, PSP_CoreParameter().pixelHeight);
	}

	bool flipY = (GetGPUBackend() == GPUBackend::OPENGL && !useBufferedRendering_) ? true : false;
	if (gstate_c.Supports(GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT)) {
		buffer.Allocate(w, h, GPU_DBG_FORMAT_FLOAT_DIV_256, flipY);
	} else {
		buffer.Allocate(w, h, GPU_DBG_FORMAT_FLOAT, flipY);
	}
	// No need to free on failure, that's the caller's job (it likely will reuse a buffer.)
	bool retval = draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_DEPTH_BIT, 0, 0, w, h, Draw::DataFormat::D32F, buffer.GetData(), w);
	// After a readback we'll have flushed and started over, need to dirty a bunch of things to be safe.
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	// That may have unbound the framebuffer, rebind to avoid crashes when debugging.
	RebindFramebuffer();
	return retval;
}

bool FramebufferManagerCommon::GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer) {
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

	int w = vfb->renderWidth;
	int h = vfb->renderHeight;
	if (!useBufferedRendering_) {
		// Safety check.
		w = std::min(w, PSP_CoreParameter().pixelWidth);
		h = std::min(h, PSP_CoreParameter().pixelHeight);
	}

	bool flipY = (GetGPUBackend() == GPUBackend::OPENGL && !useBufferedRendering_) ? true : false;
	// No need to free on failure, the caller/destructor will do that.  Usually this is a reused buffer, anyway.
	buffer.Allocate(w, h, GPU_DBG_FORMAT_8BIT, flipY);
	bool retval = draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_STENCIL_BIT, 0, 0, w,h, Draw::DataFormat::S8, buffer.GetData(), w);
	// That may have unbound the framebuffer, rebind to avoid crashes when debugging.
	RebindFramebuffer();
	return retval;
}

bool FramebufferManagerCommon::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	int w, h;
	draw_->GetFramebufferDimensions(nullptr, &w, &h);
	buffer.Allocate(w, h, GE_FORMAT_8888, false, true);
	bool retval = draw_->CopyFramebufferToMemorySync(nullptr, Draw::FB_COLOR_BIT, 0, 0, w, h, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), w);
	// That may have unbound the framebuffer, rebind to avoid crashes when debugging.
	RebindFramebuffer();
	return retval;
}

// This function takes an already correctly-sized framebuffer and packs it into RAM.
// Does not need to account for scaling.
// Color conversion is currently done on CPU but should theoretically be done on GPU.
// (Except using the GPU might cause problems because of various implementations'
// dithering behavior and games that expect exact colors like Danganronpa, so we
// can't entirely be rid of the CPU path.) -- unknown
void FramebufferManagerCommon::PackFramebufferSync_(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	if (!vfb->fbo) {
		ERROR_LOG_REPORT_ONCE(vfbfbozero, SCEGE, "PackFramebufferSync_: vfb->fbo == 0");
		return;
	}

	const u32 fb_address = (0x04000000) | vfb->fb_address;

	Draw::DataFormat destFormat = GEFormatToThin3D(vfb->format);
	const int dstBpp = (int)DataFormatSizeInBytes(destFormat);

	const int dstByteOffset = (y * vfb->fb_stride + x) * dstBpp;
	u8 *destPtr = Memory::GetPointer(fb_address + dstByteOffset);

	// We always need to convert from the framebuffer native format.
	// Right now that's always 8888.
	DEBUG_LOG(G3D, "Reading framebuffer to mem, fb_address = %08x", fb_address);

	draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_COLOR_BIT, x, y, w, h, destFormat, destPtr, vfb->fb_stride);

	// A new command buffer will begin after CopyFrameBufferToMemorySync, so we need to trigger
	// updates of any dynamic command buffer state by dirtying some stuff.
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE);
	gpuStats.numReadbacks++;
}

void FramebufferManagerCommon::ReadFramebufferToMemory(VirtualFramebuffer *vfb, bool sync, int x, int y, int w, int h) {
	// Clamp to bufferWidth. Sometimes block transfers can cause this to hit.
	if (x + w >= vfb->bufferWidth) {
		w = vfb->bufferWidth - x;
	}
	if (vfb && vfb->fbo) {
		// We'll pseudo-blit framebuffers here to get a resized version of vfb.
		OptimizeDownloadRange(vfb, x, y, w, h);
		if (vfb->renderWidth == vfb->width && vfb->renderHeight == vfb->height) {
			// No need to blit
			PackFramebufferSync_(vfb, x, y, w, h);
		} else {
			VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
			BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0);
			PackFramebufferSync_(nvfb, x, y, w, h);
		}

		textureCache_->ForgetLastTexture();
		RebindFramebuffer();
	}
}

void FramebufferManagerCommon::FlushBeforeCopy() {
	// Flush anything not yet drawn before blitting, downloading, or uploading.
	// This might be a stalled list, or unflushed before a block transfer, etc.

	// TODO: It's really bad that we are calling SetRenderFramebuffer here with
	// all the irrelevant state checking it'll use to decide what to do. Should
	// do something more focused here.
	SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason);
	drawEngine_->DispatchFlush();
}

void FramebufferManagerCommon::DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) {
	VirtualFramebuffer *vfb = GetVFBAt(fb_address);
	if (vfb && vfb->fb_stride != 0) {
		const u32 bpp = vfb->drawnFormat == GE_FORMAT_8888 ? 4 : 2;
		int x = 0;
		int y = 0;
		int pixels = loadBytes / bpp;
		// The height will be 1 for each stride or part thereof.
		int w = std::min(pixels % vfb->fb_stride, (int)vfb->width);
		int h = std::min((pixels + vfb->fb_stride - 1) / vfb->fb_stride, (int)vfb->height);

		// We might still have a pending draw to the fb in question, flush if so.
		FlushBeforeCopy();

		// No need to download if we already have it.
		if (w > 0 && h > 0 && !vfb->memoryUpdated && vfb->clutUpdatedBytes < loadBytes) {
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

void FramebufferManagerCommon::RebindFramebuffer() {
	if (currentRenderVfb_ && currentRenderVfb_->fbo) {
		draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP });
	} else {
		// Should this even happen?  It could while debugging, but maybe we can just skip binding at all.
		draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP });
	}
	gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
}

std::vector<FramebufferInfo> FramebufferManagerCommon::GetFramebufferList() {
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
