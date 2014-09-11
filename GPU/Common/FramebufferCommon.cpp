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
#include "Common/Common.h"
#include "Core/Config.h"
#include "Core/CoreParameter.h"
#include "Core/Reporting.h"
#include "Core/System.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

FramebufferManagerCommon::FramebufferManagerCommon() :
	displayFramebufPtr_(0),
	displayStride_(0),
	displayFormat_(GE_FORMAT_565),
	displayFramebuf_(0),
	prevDisplayFramebuf_(0),
	prevPrevDisplayFramebuf_(0),
	frameLastFramebufUsed_(0),
	currentRenderVfb_(0),
	framebufRangeEnd_(0),
	hackForce04154000Download_(false) {
}

FramebufferManagerCommon::~FramebufferManagerCommon() {
}

void FramebufferManagerCommon::BeginFrame() {
	DecimateFBOs();
	currentRenderVfb_ = 0;
	useBufferedRendering_ = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	updateVRAM_ = !(g_Config.iRenderingMode == FB_NON_BUFFERED_MODE || g_Config.iRenderingMode == FB_BUFFERED_MODE);
}

void FramebufferManagerCommon::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	displayFramebufPtr_ = framebuf;
	displayStride_ = stride;
	displayFormat_ = format;
}

VirtualFramebuffer *FramebufferManagerCommon::GetVFBAt(u32 addr) {
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

bool FramebufferManagerCommon::MaskedEqual(u32 addr1, u32 addr2) {
	return (addr1 & 0x03FFFFFF) == (addr2 & 0x03FFFFFF);
}

u32 FramebufferManagerCommon::FramebufferByteSize(const VirtualFramebuffer *vfb) const {
	return vfb->fb_stride * vfb->height * (vfb->format == GE_FORMAT_8888 ? 4 : 2);
}

bool FramebufferManagerCommon::ShouldDownloadFramebuffer(const VirtualFramebuffer *vfb) const {
	return updateVRAM_ || (hackForce04154000Download_ && vfb->fb_address == 0x00154000);
}

// Heuristics to figure out the size of FBO to create.
void FramebufferManagerCommon::EstimateDrawingSize(int &drawing_width, int &drawing_height) {
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

void FramebufferManagerCommon::DoSetRenderFrameBuffer() {
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
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *v = vfbs_[i];
		if (v->fb_address == fb_address) {
			vfb = v;
			// Update fb stride in case it changed
			if (vfb->fb_stride != fb_stride || vfb->format != fmt) {
				vfbFormatChanged = true;
				vfb->fb_stride = fb_stride;
				vfb->format = fmt;
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
				needsRecreate = needsRecreate || vfb->newHeight > vfb->bufferHeight || vfb->newHeight * 2 < vfb->bufferHeight;
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
		vfb->drawnWidth = 0;
		vfb->drawnHeight = 0;
		vfb->drawnFormat = fmt;
		vfb->usageFlags = FB_USAGE_RENDERTARGET;
		SetColorUpdated(vfb);
		vfb->depthUpdated = false;

		u32 byteSize = FramebufferByteSize(vfb);
		u32 fb_address_mem = (fb_address & 0x3FFFFFFF) | 0x04000000;
		if (Memory::IsVRAMAddress(fb_address_mem) && fb_address_mem + byteSize > framebufRangeEnd_) {
			framebufRangeEnd_ = fb_address_mem + byteSize;
		}

		ResizeFramebufFBO(vfb, drawing_width, drawing_height, true);
		NotifyRenderFramebufferCreated(vfb);

		INFO_LOG(SCEGE, "Creating FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);

		vfb->last_frame_render = gpuStats.numFlips;
		vfb->last_frame_used = 0;
		vfb->last_frame_attached = 0;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfbs_.push_back(vfb);
		currentRenderVfb_ = vfb;

		if (useBufferedRendering_ && !updateVRAM_ && !g_Config.bDisableSlowFramebufEffects) {
			gpu->PerformMemoryUpload(fb_address_mem, byteSize);
			NotifyStencilUpload(fb_address_mem, byteSize, true);
			// TODO: Is it worth trying to upload the depth buffer?
		}

		// Let's check for depth buffer overlap.  Might be interesting.
		bool sharingReported = false;
		bool writingDepth = true;
		// Technically, it may write depth later, but we're trying to detect it only when it's really true.
		if (gstate.isModeClear()) {
			writingDepth = !gstate.isClearModeDepthMask() && gstate.isDepthWriteEnabled();
		} else {
			writingDepth = gstate.isDepthWriteEnabled();
		}
		for (size_t i = 0, end = vfbs_.size(); i < end; ++i) {
			if (vfbs_[i]->z_stride != 0 && fb_address == vfbs_[i]->z_address) {
				// If it's clearing it, most likely it just needs more video memory.
				// Technically it could write something interesting and the other might not clear, but that's not likely.
				if (!gstate.isModeClear() || !gstate.isClearModeColorMask() || !gstate.isClearModeAlphaMask()) {
					if (fb_address != z_address && vfbs_[i]->fb_address != vfbs_[i]->z_address) {
						WARN_LOG_REPORT(SCEGE, "FBO created from existing depthbuffer as color, %08x/%08x and %08x/%08x", fb_address, z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
					}
				}
			} else if (z_stride != 0 && z_address == vfbs_[i]->fb_address) {
				// If it's clearing it, then it's probably just the reverse of the above case.
				if (writingDepth) {
					WARN_LOG_REPORT(SCEGE, "FBO using existing buffer as depthbuffer, %08x/%08x and %08x/%08x", fb_address, z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
				}
			} else if (vfbs_[i]->z_stride != 0 && z_address == vfbs_[i]->z_address && fb_address != vfbs_[i]->fb_address && !sharingReported) {
				// This happens a lot, but virtually always it's cleared.
				// It's possible the other might not clear, but when every game is reported it's not useful.
				if (writingDepth) {
					WARN_LOG_REPORT(SCEGE, "FBO reusing depthbuffer, %08x/%08x and %08x/%08x", fb_address, z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
					sharingReported = true;
				}
			}
		}

	// We already have it!
	} else if (vfb != currentRenderVfb_) {
		// Use it as a render target.
		DEBUG_LOG(SCEGE, "Switching render target to FBO for %08x: %i x %i x %i ", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		vfb->usageFlags |= FB_USAGE_RENDERTARGET;
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;

		VirtualFramebuffer *prev = currentRenderVfb_;
		currentRenderVfb_ = vfb;
		NotifyRenderFramebufferSwitched(prev, vfb);
	} else {
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((gstate_c.skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;

		NotifyRenderFramebufferUpdated(vfb, vfbFormatChanged);
	}

	gstate_c.curRTWidth = vfb->width;
	gstate_c.curRTHeight = vfb->height;
	gstate_c.curRTRenderWidth = vfb->renderWidth;
	gstate_c.curRTRenderHeight = vfb->renderHeight;
}
