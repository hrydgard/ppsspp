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
#include <cmath>

#include "Common/GPU/thin3d.h"
#include "Common/GPU/OpenGL/GLFeatures.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Text/I18n.h"
#include "Common/CommonTypes.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/CoreParameter.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/Host.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PostShader.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/ReinterpretFramebuffer.h"
#include "GPU/Debugger/Record.h"
#include "GPU/Debugger/Stepping.h"
#include "GPU/GPUInterface.h"
#include "GPU/GPUState.h"

FramebufferManagerCommon::FramebufferManagerCommon(Draw::DrawContext *draw)
	: draw_(draw),
		displayFormat_(GE_FORMAT_565) {
	presentation_ = new PresentationCommon(draw);
}

FramebufferManagerCommon::~FramebufferManagerCommon() {
	DeviceLost();

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

	delete presentation_;
}

void FramebufferManagerCommon::Init() {
	// We may need to override the render size if the shader is upscaling or SSAA.
	Resized();
}

bool FramebufferManagerCommon::UpdateSize() {
	const bool newRender = renderWidth_ != (float)PSP_CoreParameter().renderWidth || renderHeight_ != (float)PSP_CoreParameter().renderHeight;
	const bool newSettings = bloomHack_ != g_Config.iBloomHack || useBufferedRendering_ != (g_Config.iRenderingMode != FB_NON_BUFFERED_MODE);

	renderWidth_ = (float)PSP_CoreParameter().renderWidth;
	renderHeight_ = (float)PSP_CoreParameter().renderHeight;
	renderScaleFactor_ = (float)PSP_CoreParameter().renderScaleFactor;
	pixelWidth_ = PSP_CoreParameter().pixelWidth;
	pixelHeight_ = PSP_CoreParameter().pixelHeight;
	bloomHack_ = g_Config.iBloomHack;
	useBufferedRendering_ = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;

	presentation_->UpdateSize(pixelWidth_, pixelHeight_, renderWidth_, renderHeight_);

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
	GPURecord::NotifyDisplay(framebuf, stride, format);
}

VirtualFramebuffer *FramebufferManagerCommon::GetVFBAt(u32 addr) {
	addr &= 0x3FFFFFFF;
	VirtualFramebuffer *match = nullptr;
	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *v = vfbs_[i];
		if (v->fb_address == addr) {
			// Could check w too but whatever
			if (match == nullptr || match->last_frame_render < v->last_frame_render) {
				match = v;
			}
		}
	}
	return match;
}

u32 FramebufferManagerCommon::ColorBufferByteSize(const VirtualFramebuffer *vfb) const {
	return vfb->fb_stride * vfb->height * (vfb->format == GE_FORMAT_8888 ? 4 : 2);
}

bool FramebufferManagerCommon::ShouldDownloadFramebuffer(const VirtualFramebuffer *vfb) const {
	return PSP_CoreParameter().compat.flags().Force04154000Download && vfb->fb_address == 0x04154000;
}

// Heuristics to figure out the size of FBO to create.
// TODO: Possibly differentiate on whether through mode is used (since in through mode, viewport is meaningless?)
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

	if (scissor_width == 481 && region_width == 480 && scissor_height == 273 && region_height == 272) {
		drawing_width = 480;
		drawing_height = 272;
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
		u32 nearest_address = 0xFFFFFFFF;
		for (size_t i = 0; i < vfbs_.size(); ++i) {
			const u32 other_address = vfbs_[i]->fb_address & 0x3FFFFFFF;
			if (other_address > fb_address && other_address < nearest_address) {
				nearest_address = other_address;
			}
		}

		// Unless the game is using overlapping buffers, the next buffer should be far enough away.
		// This catches some cases where we can know this.
		// Hmm.  The problem is that we could only catch it for the first of two buffers...
		const u32 bpp = fb_format == GE_FORMAT_8888 ? 4 : 2;
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

	DEBUG_LOG(G3D, "Est: %08x V: %ix%i, R: %ix%i, S: %ix%i, STR: %i, THR:%i, Z:%08x = %ix%i", fb_address, viewport_width,viewport_height, region_width, region_height, scissor_width, scissor_height, fb_stride, gstate.isModeThrough(), gstate.isDepthWriteEnabled() ? gstate.getDepthBufAddress() : 0, drawing_width, drawing_height);
}

void GetFramebufferHeuristicInputs(FramebufferHeuristicParams *params, const GPUgstate &gstate) {
	params->fb_address = (gstate.getFrameBufRawAddress() & 0x3FFFFFFF) | 0x04000000;  // GetFramebufferHeuristicInputs is only called from rendering, and thus, it's VRAM.
	params->fb_stride = gstate.FrameBufStride();

	params->z_address = (gstate.getDepthBufRawAddress() & 0x3FFFFFFF) | 0x04000000;
	params->z_stride = gstate.DepthBufStride();

	if (params->z_address == params->fb_address) {
		// Probably indicates that the game doesn't care about Z for this VFB.
		// Let's avoid matching it for Z copies and other shenanigans.
		params->z_address = 0;
		params->z_stride = 0;
	}

	params->fmt = gstate_c.framebufFormat;

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

	if (gstate.getRegionRateX() != 0x100 || gstate.getRegionRateY() != 0x100) {
		WARN_LOG_REPORT_ONCE(regionRate, G3D, "Drawing region rate add non-zero: %04x, %04x of %04x, %04x", gstate.getRegionRateX(), gstate.getRegionRateY(), gstate.getRegionX2(), gstate.getRegionY2());
	}
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
			if (vfb->fb_stride != params.fb_stride) {
				vfb->fb_stride = params.fb_stride;
				vfbFormatChanged = true;
			}
			if (vfb->format != params.fmt) {
				vfb->format = params.fmt;
				vfbFormatChanged = true;
			}

			if (vfb->z_address == 0 && vfb->z_stride == 0 && params.z_stride != 0) {
				// Got one that was created by CreateRAMFramebuffer. Since it has no depth buffer,
				// we just recreate it immediately.
				ResizeFramebufFBO(vfb, vfb->width, vfb->height, true);
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

	// None found? Create one.
	if (!vfb) {
		vfb = new VirtualFramebuffer{};
		vfb->fbo = nullptr;
		vfb->fb_address = params.fb_address;
		vfb->fb_stride = params.fb_stride;
		vfb->z_address = params.z_address;
		vfb->z_stride = params.z_stride;

		// The other width/height parameters are set in ResizeFramebufFBO below.
		vfb->width = drawing_width;
		vfb->height = drawing_height;
		vfb->newWidth = drawing_width;
		vfb->newHeight = drawing_height;
		vfb->lastFrameNewSize = gpuStats.numFlips;
		vfb->format = params.fmt;
		vfb->drawnFormat = params.fmt;
		vfb->usageFlags = FB_USAGE_RENDERTARGET;

		u32 byteSize = ColorBufferByteSize(vfb);
		if (Memory::IsVRAMAddress(params.fb_address) && params.fb_address + byteSize > framebufRangeEnd_) {
			framebufRangeEnd_ = params.fb_address + byteSize;
		}

		// This is where we actually create the framebuffer. The true is "force".
		ResizeFramebufFBO(vfb, drawing_width, drawing_height, true);
		NotifyRenderFramebufferCreated(vfb);

		// We might already want to copy depth, in case this is a temp buffer.  See #7810.
		if (currentRenderVfb_ && !params.isClearingDepth) {
			BlitFramebufferDepth(currentRenderVfb_, vfb);
		}

		SetColorUpdated(vfb, skipDrawReason);

		INFO_LOG(FRAMEBUF, "Creating FBO for %08x (z: %08x) : %i x %i x %i", vfb->fb_address, vfb->z_address, vfb->width, vfb->height, vfb->format);

		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfbs_.push_back(vfb);
		currentRenderVfb_ = vfb;

		if (useBufferedRendering_ && !g_Config.bDisableSlowFramebufEffects) {
			gpu->PerformMemoryUpload(params.fb_address, byteSize);
			NotifyStencilUpload(params.fb_address, byteSize, StencilUpload::STENCIL_IS_ZERO);
			// TODO: Is it worth trying to upload the depth buffer (only if it wasn't copied above..?)
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
					WARN_LOG(SCEGE, "FBO reusing depthbuffer, c=%08x/d=%08x and c=%08x/d=%08x", params.fb_address, params.z_address, vfbs_[i]->fb_address, vfbs_[i]->z_address);
					sharingReported = true;
				}
			}
		}

	// We already have it!
	} else if (vfb != currentRenderVfb_) {
		// Use it as a render target.
		DEBUG_LOG(FRAMEBUF, "Switching render target to FBO for %08x: %d x %d x %d ", vfb->fb_address, vfb->width, vfb->height, vfb->format);
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
	// Notify the texture cache of both the color and depth buffers.
	textureCache_->NotifyFramebuffer(v, NOTIFY_FB_DESTROYED);
	if (v->fbo) {
		v->fbo->Release();
		v->fbo = nullptr;
	}

	// Wipe some pointers
	if (currentRenderVfb_ == v)
		currentRenderVfb_ = nullptr;
	if (displayFramebuf_ == v)
		displayFramebuf_ = nullptr;
	if (prevDisplayFramebuf_ == v)
		prevDisplayFramebuf_ = nullptr;
	if (prevPrevDisplayFramebuf_ == v)
		prevPrevDisplayFramebuf_ = nullptr;

	delete v;
}

void FramebufferManagerCommon::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst) {
	_dbg_assert_(src && dst);

	// Check that the depth address is even the same before actually blitting.
	bool matchingDepthBuffer = src->z_address == dst->z_address && src->z_stride != 0 && dst->z_stride != 0;
	bool matchingSize = src->width == dst->width && src->height == dst->height;
	if (!matchingDepthBuffer || !matchingSize)
		return;

	// Copy depth value from the previously bound framebuffer to the current one.
	bool hasNewerDepth = src->last_frame_depth_render != 0 && src->last_frame_depth_render >= dst->last_frame_depth_updated;
	if (!src->fbo || !dst->fbo || !useBufferedRendering_ || !hasNewerDepth) {
		// If depth wasn't updated, then we're at least "two degrees" away from the data.
		// This is an optimization: it probably doesn't need to be copied in this case.
		return;
	}

	int w = std::min(src->renderWidth, dst->renderWidth);
	int h = std::min(src->renderHeight, dst->renderHeight);

	// Note: We prefer Blit ahead of Copy here, since at least on GL, Copy will always also copy stencil which we don't want. See #9740.
	if (gstate_c.Supports(GPU_SUPPORTS_FRAMEBUFFER_BLIT_TO_DEPTH)) {
		draw_->BlitFramebuffer(src->fbo, 0, 0, w, h, dst->fbo, 0, 0, w, h, Draw::FB_DEPTH_BIT, Draw::FB_BLIT_NEAREST, "BlitFramebufferDepth");
		RebindFramebuffer("After BlitFramebufferDepth");
	} else if (gstate_c.Supports(GPU_SUPPORTS_COPY_IMAGE)) {
		draw_->CopyFramebufferImage(src->fbo, 0, 0, 0, 0, dst->fbo, 0, 0, 0, 0, w, h, 1, Draw::FB_DEPTH_BIT, "BlitFramebufferDepth");
		RebindFramebuffer("After BlitFramebufferDepth");
	}
	dst->last_frame_depth_updated = gpuStats.numFlips;
}

void FramebufferManagerCommon::NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) {
	if (!useBufferedRendering_) {
		// Let's ignore rendering to targets that have not (yet) been displayed.
		gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
	} else if (currentRenderVfb_) {
		DownloadFramebufferOnSwitch(currentRenderVfb_);
	}

	textureCache_->NotifyFramebuffer(vfb, NOTIFY_FB_CREATED);

	// Ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
	}
	if (gstate_c.curRTRenderWidth != vfb->renderWidth || gstate_c.curRTRenderHeight != vfb->renderHeight) {
		gstate_c.Dirty(DIRTY_PROJMATRIX);
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX);
	}
}

void FramebufferManagerCommon::NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb, bool vfbFormatChanged) {
	if (vfbFormatChanged) {
		textureCache_->NotifyFramebuffer(vfb, NOTIFY_FB_UPDATED);
		if (vfb->drawnFormat != vfb->format) {
			ReinterpretFramebuffer(vfb, vfb->drawnFormat, vfb->format);
		}
	}

	// ugly...
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
	}
	if (gstate_c.curRTRenderWidth != vfb->renderWidth || gstate_c.curRTRenderHeight != vfb->renderHeight) {
		gstate_c.Dirty(DIRTY_PROJMATRIX);
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX);
	}
}

// Can't easily dynamically create these strings, we just pass along the pointer.
static const char *reinterpretStrings[3][3] = {
	{
		"self_reinterpret_565",
		"reinterpret_565_to_5551",
		"reinterpret_565_to_4444",
	},
	{
		"reinterpret_5551_to_565",
		"self_reinterpret_5551",
		"reinterpret_5551_to_4444",
	},
	{
		"reinterpret_4444_to_565",
		"reinterpret_4444_to_5551",
		"self_reinterpret_4444",
	},
};

void FramebufferManagerCommon::ReinterpretFramebuffer(VirtualFramebuffer *vfb, GEBufferFormat oldFormat, GEBufferFormat newFormat) {
	if (!useBufferedRendering_ || !vfb->fbo) {
		return;
	}

	_assert_(newFormat != oldFormat);
	// The caller is responsible for updating the format.
	_assert_(newFormat == vfb->format);

	ShaderLanguage lang = draw_->GetShaderLanguageDesc().shaderLanguage;

	bool doReinterpret = PSP_CoreParameter().compat.flags().ReinterpretFramebuffers &&
		(lang == HLSL_D3D11 || lang == GLSL_VULKAN || lang == GLSL_3xx);
	// Copy image required for now.
	if (!gstate_c.Supports(GPU_SUPPORTS_COPY_IMAGE))
		doReinterpret = false;
	if (!doReinterpret) {
		// Fake reinterpret - just clear the way we always did on Vulkan. Just clear color and stencil.
		if (oldFormat == GE_FORMAT_565) {
			// We have to bind here instead of clear, since it can be that no framebuffer is bound.
			// The backend can sometimes directly optimize it to a clear.

			// Games that are marked as doing reinterpret just ignore this - better to keep the data than to clear.
			// Fixes #13717.
			if (!PSP_CoreParameter().compat.flags().ReinterpretFramebuffers && !PSP_CoreParameter().compat.flags().BlueToAlpha) {
				draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::KEEP, Draw::RPAction::CLEAR }, "FakeReinterpret");
				// Need to dirty anything that has command buffer dynamic state, in case we started a new pass above.
				// Should find a way to feed that information back, maybe... Or simply correct the issue in the rendermanager.
				gstate_c.Dirty(DIRTY_DEPTHSTENCIL_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_BLEND_STATE);

				if (currentRenderVfb_ != vfb) {
					// In case ReinterpretFramebuffer was called from the texture manager.
					draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "After FakeReinterpret");
				}
			}
		}
		return;
	}

	// We only reinterpret between 16 - bit formats, for now.
	if (!IsGeBufferFormat16BitColor(oldFormat) || !IsGeBufferFormat16BitColor(newFormat)) {
		// 16->32 and 32->16 will require some more specialized shaders.
		return;
	}

	char *vsCode = nullptr;
	char *fsCode = nullptr;

	if (!reinterpretVS_) {
		vsCode = new char[4000];
		const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();
		GenerateReinterpretVertexShader(vsCode, shaderLanguageDesc);
		reinterpretVS_ = draw_->CreateShaderModule(ShaderStage::Vertex, shaderLanguageDesc.shaderLanguage, (const uint8_t *)vsCode, strlen(vsCode), "reinterpret_vs");
		_assert_(reinterpretVS_);
	}

	if (!reinterpretSampler_) {
		Draw::SamplerStateDesc samplerDesc{};
		samplerDesc.magFilter = Draw::TextureFilter::LINEAR;
		samplerDesc.minFilter = Draw::TextureFilter::LINEAR;
		reinterpretSampler_ = draw_->CreateSamplerState(samplerDesc);
	}

	if (!reinterpretVBuf_) {
		reinterpretVBuf_ = draw_->CreateBuffer(12 * 3, Draw::BufferUsageFlag::DYNAMIC | Draw::BufferUsageFlag::VERTEXDATA);
	}

	// See if we need to create a new pipeline.

	Draw::Pipeline *pipeline = reinterpretFromTo_[(int)oldFormat][(int)newFormat];
	if (!pipeline) {
		fsCode = new char[4000];
		const ShaderLanguageDesc &shaderLanguageDesc = draw_->GetShaderLanguageDesc();
		GenerateReinterpretFragmentShader(fsCode, oldFormat, newFormat, shaderLanguageDesc);
		Draw::ShaderModule *reinterpretFS = draw_->CreateShaderModule(ShaderStage::Fragment, shaderLanguageDesc.shaderLanguage, (const uint8_t *)fsCode, strlen(fsCode), "reinterpret_fs");
		_assert_(reinterpretFS);

		std::vector<Draw::ShaderModule *> shaders;
		shaders.push_back(reinterpretVS_);
		shaders.push_back(reinterpretFS);

		using namespace Draw;
		Draw::PipelineDesc desc{};
		// We use a "fullscreen triangle".
		// TODO: clear the stencil buffer. Hard to actually initialize it with the new alpha, though possible - let's see if
		// we need it.
		DepthStencilState *depth = draw_->CreateDepthStencilState({ false, false, Comparison::LESS });
		BlendState *blendstateOff = draw_->CreateBlendState({ false, 0xF });
		RasterState *rasterNoCull = draw_->CreateRasterState({});

		// No uniforms for these, only a single texture input.
		PipelineDesc pipelineDesc{ Primitive::TRIANGLE_LIST, shaders, nullptr, depth, blendstateOff, rasterNoCull, nullptr };
		pipeline = draw_->CreateGraphicsPipeline(pipelineDesc);
		_assert_(pipeline != nullptr);
		reinterpretFromTo_[(int)oldFormat][(int)newFormat] = pipeline;

		depth->Release();
		blendstateOff->Release();
		rasterNoCull->Release();
		reinterpretFS->Release();
	}

	// Copy to a temp framebuffer.
	Draw::Framebuffer *temp = GetTempFBO(TempFBO::REINTERPRET, vfb->renderWidth, vfb->renderHeight);

	// Ideally on Vulkan this should be using the original framebuffer as an input attachment, allowing it to read from
	// itself while writing.
	draw_->InvalidateCachedState();
	draw_->CopyFramebufferImage(vfb->fbo, 0, 0, 0, 0, temp, 0, 0, 0, 0, vfb->renderWidth, vfb->renderHeight, 1, Draw::FBChannel::FB_COLOR_BIT, "reinterpret_prep");
	draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::DONT_CARE, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, reinterpretStrings[(int)oldFormat][(int)newFormat]);
	draw_->BindPipeline(pipeline);
	draw_->BindFramebufferAsTexture(temp, 0, Draw::FBChannel::FB_COLOR_BIT, 0);
	draw_->BindSamplerStates(0, 1, &reinterpretSampler_);
	draw_->SetScissorRect(0, 0, vfb->renderWidth, vfb->renderHeight);
	Draw::Viewport vp = Draw::Viewport{ 0.0f, 0.0f, (float)vfb->renderWidth, (float)vfb->renderHeight, 0.0f, 1.0f };
	draw_->SetViewports(1, &vp);
	// Vertex buffer not used - vertices generated in shader.
	// TODO: Switch to a vertex buffer for GLES2/D3D9 compat.
	draw_->BindVertexBuffers(0, 1, &reinterpretVBuf_, nullptr);
	draw_->Draw(3, 0);
	draw_->InvalidateCachedState();

	// Unbind.
	draw_->BindTexture(0, nullptr);

	shaderManager_->DirtyLastShader();
	textureCache_->ForgetLastTexture();

	gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_RASTER_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_VERTEXSHADER_STATE | DIRTY_FRAGMENTSHADER_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);

	if (currentRenderVfb_ != vfb) {
		// In case ReinterpretFramebuffer was called from the texture manager.
		draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "After reinterpret");
	}
	delete[] vsCode;
	delete[] fsCode;
}

void FramebufferManagerCommon::NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth) {
	if (ShouldDownloadFramebuffer(vfb) && !vfb->memoryUpdated) {
		ReadFramebufferToMemory(vfb, 0, 0, vfb->width, vfb->height);
		vfb->usageFlags = (vfb->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
		vfb->firstFrameSaved = true;
	} else {
		DownloadFramebufferOnSwitch(prevVfb);
	}
	textureCache_->ForgetLastTexture();
	shaderManager_->DirtyLastShader();

	// Copy depth between the framebuffers, if the z_address is the same (checked inside.)
	if (prevVfb && !isClearingDepth) {
		BlitFramebufferDepth(prevVfb, vfb);
	}

	if (vfb->drawnFormat != vfb->format) {
		ReinterpretFramebuffer(vfb, vfb->drawnFormat, vfb->format);
	}

	if (useBufferedRendering_) {
		if (vfb->fbo) {
			shaderManager_->DirtyLastShader();
			draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "FramebufferSwitch");
		} else {
			// This should only happen very briefly when toggling useBufferedRendering_.
			ResizeFramebufFBO(vfb, vfb->width, vfb->height, true);
		}
	} else {
		if (vfb->fbo) {
			// This should only happen very briefly when toggling useBufferedRendering_.
			textureCache_->NotifyFramebuffer(vfb, NOTIFY_FB_DESTROYED);
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
	textureCache_->NotifyFramebuffer(vfb, NOTIFY_FB_UPDATED);

	// ugly... is all this needed?
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
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
			gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
			vfb->fb_stride = width;
			// This might be a bit wider than necessary, but we'll redetect on next render.
			vfb->width = width;
		}
	}
}

void FramebufferManagerCommon::UpdateFromMemory(u32 addr, int size, bool safe) {
	// Take off the uncached flag from the address. Not to be confused with the start of VRAM.
	addr &= 0x3FFFFFFF;
	// TODO: Could go through all FBOs, but probably not important?
	// TODO: Could also check for inner changes, but video is most important.
	bool isDisplayBuf = addr == DisplayFramebufAddr() || addr == PrevDisplayFramebufAddr();
	if (isDisplayBuf || safe) {
		// TODO: Deleting the FBO is a heavy hammer solution, so let's only do it if it'd help.
		if (!Memory::IsValidAddress(displayFramebufPtr_))
			return;

		for (size_t i = 0; i < vfbs_.size(); ++i) {
			VirtualFramebuffer *vfb = vfbs_[i];
			if (vfb->fb_address == addr) {
				FlushBeforeCopy();

				if (useBufferedRendering_ && vfb->fbo) {
					GEBufferFormat fmt = vfb->format;
					if (vfb->last_frame_render + 1 < gpuStats.numFlips && isDisplayBuf) {
						// If we're not rendering to it, format may be wrong.  Use displayFormat_ instead.
						fmt = displayFormat_;
					}
					DrawPixels(vfb, 0, 0, Memory::GetPointer(addr), fmt, vfb->fb_stride, vfb->width, vfb->height);
					SetColorUpdated(vfb, gstate_c.skipDrawReason);
				} else {
					INFO_LOG(FRAMEBUF, "Invalidating FBO for %08x (%i x %i x %i)", vfb->fb_address, vfb->width, vfb->height, vfb->format);
					DestroyFramebuf(vfb);
					vfbs_.erase(vfbs_.begin() + i--);
				}
			}
		}

		RebindFramebuffer("RebindFramebuffer - UpdateFromMemory");
	}
	// TODO: Necessary?
	gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
}

void FramebufferManagerCommon::DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	textureCache_->ForgetLastTexture();
	shaderManager_->DirtyLastShader();  // On GL, important that this is BEFORE drawing
	float u0 = 0.0f, u1 = 1.0f;
	float v0 = 0.0f, v1 = 1.0f;

	DrawTextureFlags flags;
	if (useBufferedRendering_ && vfb && vfb->fbo) {
		flags = DRAWTEX_LINEAR;
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "DrawPixels");
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
		SetViewport2D(0, 0, vfb->renderWidth, vfb->renderHeight);
		draw_->SetScissorRect(0, 0, vfb->renderWidth, vfb->renderHeight);
	} else {
		// We are drawing directly to the back buffer so need to flip.
		// Should more of this be handled by the presentation engine?
		if (needBackBufferYSwap_)
			std::swap(v0, v1);
		flags = g_Config.iBufFilter == SCALE_LINEAR ? DRAWTEX_LINEAR : DRAWTEX_NEAREST;
		flags = flags | DRAWTEX_TO_BACKBUFFER;
		FRect frame = GetScreenFrame(pixelWidth_, pixelHeight_);
		FRect rc;
		CenterDisplayOutputRect(&rc, 480.0f, 272.0f, frame, ROTATION_LOCKED_HORIZONTAL);
		SetViewport2D(rc.x, rc.y, rc.w, rc.h);
		draw_->SetScissorRect(0, 0, pixelWidth_, pixelHeight_);
	}

	Draw::Texture *pixelsTex = MakePixelTexture(srcPixels, srcPixelFormat, srcStride, width, height, u1, v1);
	if (pixelsTex) {
		draw_->BindTextures(0, 1, &pixelsTex);
		Bind2DShader();
		DrawActiveTexture(dstX, dstY, width, height, vfb->bufferWidth, vfb->bufferHeight, u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags);
		gpuStats.numUploads++;
		pixelsTex->Release();
		draw_->InvalidateCachedState();

		gstate_c.Dirty(DIRTY_BLEND_STATE | DIRTY_RASTER_STATE | DIRTY_DEPTHSTENCIL_STATE | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	}
}

bool FramebufferManagerCommon::BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags) {
	if (!framebuffer->fbo || !useBufferedRendering_) {
		draw_->BindTexture(stage, nullptr);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return false;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	bool skipCopy = !(flags & BINDFBCOLOR_MAY_COPY) || GPUStepping::IsStepping();

	// Currently rendering to this framebuffer. Need to make a copy.
	if (!skipCopy && framebuffer == currentRenderVfb_) {
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		Draw::Framebuffer *renderCopy = GetTempFBO(TempFBO::COPY, framebuffer->renderWidth, framebuffer->renderHeight);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;
			CopyFramebufferForColorTexture(&copyInfo, framebuffer, flags);
			RebindFramebuffer("After BindFramebufferAsColorTexture");
			draw_->BindFramebufferAsTexture(renderCopy, stage, Draw::FB_COLOR_BIT, 0);
		} else {
			draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		}
		return true;
	} else if (framebuffer != currentRenderVfb_ || (flags & BINDFBCOLOR_FORCE_SELF) != 0) {
		draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::FB_COLOR_BIT, 0);
		return true;
	} else {
		ERROR_LOG_REPORT_ONCE(vulkanSelfTexture, G3D, "Attempting to texture from target (src=%08x / target=%08x / flags=%d)", framebuffer->fb_address, currentRenderVfb_->fb_address, flags);
		// To do this safely in Vulkan, we need to use input attachments.
		// Actually if the texture region and render regions don't overlap, this is safe, but we need
		// to transition to GENERAL image layout which will take some trickery.
		// Badness on D3D11 to bind the currently rendered-to framebuffer as a texture.
		draw_->BindTexture(stage, nullptr);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return false;
	}
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

		// We'll have to reapply these next time since we cropped to UV.
		gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
	}

	if (x < src->drawnWidth && y < src->drawnHeight && w > 0 && h > 0) {
		BlitFramebuffer(dst, x, y, src, x, y, w, h, 0, "Blit_CopyFramebufferForColorTexture");
	}
}

Draw::Texture *FramebufferManagerCommon::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, float &u1, float &v1) {
	// TODO: We can just change the texture format and flip some bits around instead of this.
	// Could share code with the texture cache perhaps.
	auto generateTexture = [&](uint8_t *data, const uint8_t *initData, uint32_t w, uint32_t h, uint32_t d, uint32_t byteStride, uint32_t sliceByteStride) {
		for (int y = 0; y < height; y++) {
			const u16_le *src16 = (const u16_le *)srcPixels + srcStride * y;
			const u32_le *src32 = (const u32_le *)srcPixels + srcStride * y;
			u32 *dst = (u32 *)(data + byteStride * y);
			switch (srcPixelFormat) {
			case GE_FORMAT_565:
				if (preferredPixelsFormat_ == Draw::DataFormat::B8G8R8A8_UNORM)
					ConvertRGB565ToBGRA8888(dst, src16, width);
				else
					ConvertRGB565ToRGBA8888(dst, src16, width);
				break;

			case GE_FORMAT_5551:
				if (preferredPixelsFormat_ == Draw::DataFormat::B8G8R8A8_UNORM)
					ConvertRGBA5551ToBGRA8888(dst, src16, width);
				else
					ConvertRGBA5551ToRGBA8888(dst, src16, width);
				break;

			case GE_FORMAT_4444:
				if (preferredPixelsFormat_ == Draw::DataFormat::B8G8R8A8_UNORM)
					ConvertRGBA4444ToBGRA8888(dst, src16, width);
				else
					ConvertRGBA4444ToRGBA8888(dst, src16, width);
				break;

			case GE_FORMAT_8888:
				if (preferredPixelsFormat_ == Draw::DataFormat::B8G8R8A8_UNORM)
					ConvertRGBA8888ToBGRA8888(dst, src32, width);
				// This means use original pointer as-is.  May avoid or optimize a copy.
				else if (srcStride == width)
					return false;
				else
					memcpy(dst, src32, width * 4);
				break;

			case GE_FORMAT_INVALID:
			case GE_FORMAT_DEPTH16:
				_dbg_assert_msg_(false, "Invalid pixelFormat passed to DrawPixels().");
				break;
			}
		}
		return true;
	};

	Draw::TextureDesc desc{
		Draw::TextureType::LINEAR2D,
		preferredPixelsFormat_,
		width,
		height,
		1,
		1,
		false,
		"DrawPixels",
		{ (uint8_t *)srcPixels },
		generateTexture,
	};
	// Hot Shots Golf (#12355) does tons of these in a frame in some situations! So creating textures
	// better be fast.
	Draw::Texture *tex = draw_->CreateTexture(desc);
	if (!tex)
		ERROR_LOG(G3D, "Failed to create drawpixels texture");
	return tex;
}

void FramebufferManagerCommon::DrawFramebufferToOutput(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride) {
	textureCache_->ForgetLastTexture();
	shaderManager_->DirtyLastShader();

	float u0 = 0.0f, u1 = 480.0f / 512.0f;
	float v0 = 0.0f, v1 = 1.0f;
	Draw::Texture *pixelsTex = MakePixelTexture(srcPixels, srcPixelFormat, srcStride, 512, 272, u1, v1);
	if (!pixelsTex)
		return;

	int uvRotation = useBufferedRendering_ ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
	OutputFlags flags = g_Config.iBufFilter == SCALE_LINEAR ? OutputFlags::LINEAR : OutputFlags::NEAREST;
	if (needBackBufferYSwap_) {
		flags |= OutputFlags::BACKBUFFER_FLIPPED;
	}
	// DrawActiveTexture reverses these, probably to match "up".
	if (GetGPUBackend() == GPUBackend::DIRECT3D9 || GetGPUBackend() == GPUBackend::DIRECT3D11) {
		flags |= OutputFlags::POSITION_FLIPPED;
	}

	presentation_->UpdateUniforms(textureCache_->VideoIsPlaying());
	presentation_->SourceTexture(pixelsTex, 512, 272);
	presentation_->CopyToOutput(flags, uvRotation, u0, v0, u1, v1);
	pixelsTex->Release();

	// PresentationCommon sets all kinds of state, we can't rely on anything.
	gstate_c.Dirty(DIRTY_ALL);

	currentRenderVfb_ = nullptr;
}

void FramebufferManagerCommon::DownloadFramebufferOnSwitch(VirtualFramebuffer *vfb) {
	if (vfb && vfb->safeWidth > 0 && vfb->safeHeight > 0 && !vfb->firstFrameSaved && !vfb->memoryUpdated) {
		// Some games will draw to some memory once, and use it as a render-to-texture later.
		// To support this, we save the first frame to memory when we have a safe w/h.
		// Saving each frame would be slow.
		if (!g_Config.bDisableSlowFramebufEffects && !PSP_CoreParameter().compat.flags().DisableFirstFrameReadback) {
			ReadFramebufferToMemory(vfb, 0, 0, vfb->safeWidth, vfb->safeHeight);
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

void FramebufferManagerCommon::CopyDisplayToOutput(bool reallyDirty) {
	DownloadFramebufferOnSwitch(currentRenderVfb_);
	shaderManager_->DirtyLastShader();

	if (displayFramebufPtr_ == 0) {
		if (Core_IsStepping())
			VERBOSE_LOG(FRAMEBUF, "Display disabled, displaying only black");
		else
			DEBUG_LOG(FRAMEBUF, "Display disabled, displaying only black");
		// No framebuffer to display! Clear to black.
		if (useBufferedRendering_) {
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR }, "CopyDisplayToOutput");
		}
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
		return;
	}

	u32 offsetX = 0;
	u32 offsetY = 0;

	// If it's not really dirty, we're probably frameskipping.  Use the last working one.
	u32 fbaddr = reallyDirty ? displayFramebufPtr_ : prevDisplayFramebufPtr_;
	prevDisplayFramebufPtr_ = fbaddr;

	VirtualFramebuffer *vfb = GetVFBAt(fbaddr);
	if (!vfb) {
		// Let's search for a framebuf within this range. Note that we also look for
		// "framebuffers" sitting in RAM (created from block transfer or similar) so we only take off the kernel
		// and uncached bits of the address when comparing.
		const u32 addr = fbaddr & 0x3FFFFFFF;
		for (size_t i = 0; i < vfbs_.size(); ++i) {
			VirtualFramebuffer *v = vfbs_[i];
			const u32 v_addr = v->fb_address & 0x3FFFFFFF;
			const u32 v_size = ColorBufferByteSize(v);
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
			// Log should be "Displaying from framebuf" but not worth changing the report.
			INFO_LOG_REPORT_ONCE(displayoffset, FRAMEBUF, "Rendering from framebuf with offset %08x -> %08x+%dx%d", addr, vfb->fb_address, offsetX, offsetY);
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
		if (Memory::IsValidAddress(fbaddr)) {
			// The game is displaying something directly from RAM. In GTA, it's decoded video.
			if (!vfb) {
				DrawFramebufferToOutput(Memory::GetPointer(fbaddr), displayFormat_, displayStride_);
				return;
			}
		} else {
			DEBUG_LOG(FRAMEBUF, "Found no FBO to display! displayFBPtr = %08x", fbaddr);
			// No framebuffer to display! Clear to black.
			if (useBufferedRendering_) {
				// Bind and clear the backbuffer. This should be the first time during the frame that it's bound.
				draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR }, "CopyDisplayToOutput_NoFBO");
			}
			gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
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
		if (Core_IsStepping())
			VERBOSE_LOG(FRAMEBUF, "Displaying FBO %08x", vfb->fb_address);
		else
			DEBUG_LOG(FRAMEBUF, "Displaying FBO %08x", vfb->fb_address);

		// TODO ES3: Use glInvalidateFramebuffer to discard depth/stencil data at the end of frame.

		float u0 = offsetX / (float)vfb->bufferWidth;
		float v0 = offsetY / (float)vfb->bufferHeight;
		float u1 = (480.0f + offsetX) / (float)vfb->bufferWidth;
		float v1 = (272.0f + offsetY) / (float)vfb->bufferHeight;

		textureCache_->ForgetLastTexture();

		int uvRotation = useBufferedRendering_ ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
		OutputFlags flags = g_Config.iBufFilter == SCALE_LINEAR ? OutputFlags::LINEAR : OutputFlags::NEAREST;
		if (needBackBufferYSwap_) {
			flags |= OutputFlags::BACKBUFFER_FLIPPED;
		}
		// DrawActiveTexture reverses these, probably to match "up".
		if (GetGPUBackend() == GPUBackend::DIRECT3D9 || GetGPUBackend() == GPUBackend::DIRECT3D11) {
			flags |= OutputFlags::POSITION_FLIPPED;
		}

		int actualWidth = (vfb->bufferWidth * vfb->renderWidth) / vfb->width;
		int actualHeight = (vfb->bufferHeight * vfb->renderHeight) / vfb->height;
		presentation_->UpdateUniforms(textureCache_->VideoIsPlaying());
		presentation_->SourceFramebuffer(vfb->fbo, actualWidth, actualHeight);
		presentation_->CopyToOutput(flags, uvRotation, u0, v0, u1, v1);
	} else if (useBufferedRendering_) {
		WARN_LOG(FRAMEBUF, "Current VFB lacks an FBO: %08x", vfb->fb_address);
	}

	// This may get called mid-draw if the game uses an immediate flip.
	// PresentationCommon sets all kinds of state, we can't rely on anything.
	gstate_c.Dirty(DIRTY_ALL);
	currentRenderVfb_ = nullptr;
}

void FramebufferManagerCommon::DecimateFBOs() {
	currentRenderVfb_ = nullptr;

	for (auto iter : fbosToDelete_) {
		iter->Release();
	}
	fbosToDelete_.clear();

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		int age = frameLastFramebufUsed_ - std::max(vfb->last_frame_render, vfb->last_frame_used);

		if (ShouldDownloadFramebuffer(vfb) && age == 0 && !vfb->memoryUpdated) {
			ReadFramebufferToMemory(vfb, 0, 0, vfb->width, vfb->height);
			vfb->usageFlags = (vfb->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
			vfb->firstFrameSaved = true;
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

// Requires width/height to be set already.
void FramebufferManagerCommon::ResizeFramebufFBO(VirtualFramebuffer *vfb, int w, int h, bool force, bool skipCopy) {
	_dbg_assert_(w > 0);
	_dbg_assert_(h > 0);
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

	bool force1x = false;
	switch (bloomHack_) {
	case 1:
		force1x = vfb->bufferWidth <= 128 || vfb->bufferHeight <= 64;
		break;
	case 2:
		force1x = vfb->bufferWidth <= 256 || vfb->bufferHeight <= 128;
		break;
	case 3:
		force1x = vfb->bufferWidth < 480 || vfb->bufferWidth > 800 || vfb->bufferHeight < 272; // GOW uses 864x272
		break;
	}

	if (PSP_CoreParameter().compat.flags().Force04154000Download && vfb->fb_address == 0x04154000) {
		force1x = true;
	}

	if (force1x && g_Config.iInternalResolution != 1) {
		vfb->renderScaleFactor = 1.0f;
		vfb->renderWidth = vfb->bufferWidth;
		vfb->renderHeight = vfb->bufferHeight;
	} else {
		vfb->renderScaleFactor = renderScaleFactor_;
		vfb->renderWidth = (u16)(vfb->bufferWidth * renderScaleFactor_);
		vfb->renderHeight = (u16)(vfb->bufferHeight * renderScaleFactor_);
	}

	// During hardware rendering, we always render at full color depth even if the game wouldn't on real hardware.
	// It's not worth the trouble trying to support lower bit-depth rendering, just
	// more cases to test that nobody will ever use.

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

	shaderManager_->DirtyLastShader();
	char tag[128];
	size_t len = snprintf(tag, sizeof(tag), "FB_%08x_%08x_%dx%d_%s", vfb->fb_address, vfb->z_address, w, h, GeBufferFormatToString(vfb->format));
	vfb->fbo = draw_->CreateFramebuffer({ vfb->renderWidth, vfb->renderHeight, 1, 1, true, tag });
	if (Memory::IsVRAMAddress(vfb->fb_address) && vfb->fb_stride != 0) {
		NotifyMemInfo(MemBlockFlags::ALLOC, vfb->fb_address, ColorBufferByteSize(vfb), tag, len);
	}
	if (Memory::IsVRAMAddress(vfb->z_address) && vfb->z_stride != 0) {
		char buf[128];
		size_t len = snprintf(buf, sizeof(buf), "Z_%s", tag);
		NotifyMemInfo(MemBlockFlags::ALLOC, vfb->z_address, vfb->fb_stride * vfb->height * sizeof(uint16_t), buf, len);
	}
	if (old.fbo) {
		INFO_LOG(FRAMEBUF, "Resizing FBO for %08x : %dx%dx%s", vfb->fb_address, w, h, GeBufferFormatToString(vfb->format));
		if (vfb->fbo) {
			// TODO: Swap the order of the below? That way we can avoid the needGLESRebinds_ check below I think.
			draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR }, "ResizeFramebufFBO");
			if (!skipCopy) {
				// TODO: In this case, it'll nearly always be better to draw the old framebuffer to the new one than to do an actual blit.
				// Usually hardly a performance issue though.
				BlitFramebuffer(vfb, 0, 0, &old, 0, 0, std::min((u16)oldWidth, std::min(vfb->bufferWidth, vfb->width)), std::min((u16)oldHeight, std::min(vfb->height, vfb->bufferHeight)), 0, "Blit_ResizeFramebufFBO");
			}
		}
		fbosToDelete_.push_back(old.fbo);
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "ResizeFramebufFBO");
	} else {
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR }, "ResizeFramebufFBO");
	}
	currentRenderVfb_ = vfb;

	if (!vfb->fbo) {
		ERROR_LOG(FRAMEBUF, "Error creating FBO during resize! %dx%d", vfb->renderWidth, vfb->renderHeight);
		vfb->last_frame_failed = gpuStats.numFlips;
	}
}

// This is called from detected memcopies and framebuffer initialization from VRAM. Not block transfers.
// MotoGP goes this path so we need to catch those copies here.
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

		// We only remove the kernel and uncached bits when comparing.
		const u32 vfb_address = vfb->fb_address & 0x3FFFFFFF;
		const u32 vfb_size = ColorBufferByteSize(vfb);
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

	if (!useBufferedRendering_) {
		// If we're copying into a recently used display buf, it's probably destined for the screen.
		if (srcBuffer || (dstBuffer != displayFramebuf_ && dstBuffer != prevDisplayFramebuf_)) {
			return false;
		}
	}

	if (!dstBuffer && srcBuffer) {
		// Note - if we're here, we're in a memcpy, not a block transfer. Not allowing IntraVRAMBlockTransferAllowCreateFB.
		// Technically, that makes BlockTransferAllowCreateFB a bit of a misnomer.
		if (PSP_CoreParameter().compat.flags().BlockTransferAllowCreateFB) {
			dstBuffer = CreateRAMFramebuffer(dst, srcBuffer->width, srcBuffer->height, srcBuffer->fb_stride, srcBuffer->format);
			dstY = 0;
		}
	}
	if (dstBuffer) {
		dstBuffer->last_frame_used = gpuStats.numFlips;
	}

	if (dstBuffer && srcBuffer && !isMemset) {
		if (srcBuffer == dstBuffer) {
			WARN_LOG_ONCE(dstsrccpy, G3D, "Intra-buffer memcpy (not supported) %08x -> %08x (size: %x)", src, dst, size);
		} else {
			WARN_LOG_ONCE(dstnotsrccpy, G3D, "Inter-buffer memcpy %08x -> %08x (size: %x)", src, dst, size);
			// Just do the blit!
			BlitFramebuffer(dstBuffer, 0, dstY, srcBuffer, 0, srcY, srcBuffer->width, srcH, 0, "Blit_InterBufferMemcpy");
			SetColorUpdated(dstBuffer, skipDrawReason);
			RebindFramebuffer("RebindFramebuffer - Inter-buffer memcpy");
		}
		return false;
	} else if (dstBuffer) {
		if (isMemset) {
			gpuStats.numClears++;
		}
		WARN_LOG_ONCE(btucpy, G3D, "Memcpy fbo upload %08x -> %08x (size: %x)", src, dst, size);
		FlushBeforeCopy();
		const u8 *srcBase = Memory::GetPointerUnchecked(src);
		DrawPixels(dstBuffer, 0, dstY, srcBase, dstBuffer->format, dstBuffer->fb_stride, dstBuffer->width, dstH);
		SetColorUpdated(dstBuffer, skipDrawReason);
		RebindFramebuffer("RebindFramebuffer - Memcpy fbo upload");
		// This is a memcpy, let's still copy just in case.
		return false;
	} else if (srcBuffer) {
		WARN_LOG_ONCE(btdcpy, G3D, "Memcpy fbo download %08x -> %08x", src, dst);
		FlushBeforeCopy();
		if (srcH == 0 || srcY + srcH > srcBuffer->bufferHeight) {
			WARN_LOG_ONCE(btdcpyheight, G3D, "Memcpy fbo download %08x -> %08x skipped, %d+%d is taller than %d", src, dst, srcY, srcH, srcBuffer->bufferHeight);
		} else if (g_Config.bBlockTransferGPU && !srcBuffer->memoryUpdated && !PSP_CoreParameter().compat.flags().DisableReadbacks) {
			ReadFramebufferToMemory(srcBuffer, 0, srcY, srcBuffer->width, srcH);
			srcBuffer->usageFlags = (srcBuffer->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
		}
		return false;
	} else {
		return false;
	}
}

// Can't be const, in case it has to create a vfb unfortunately.
void FramebufferManagerCommon::FindTransferFramebuffers(VirtualFramebuffer *&dstBuffer, VirtualFramebuffer *&srcBuffer, u32 dstBasePtr, int dstStride, int &dstX, int &dstY, u32 srcBasePtr, int srcStride, int &srcX, int &srcY, int &srcWidth, int &srcHeight, int &dstWidth, int &dstHeight, int bpp) {
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
		const u32 vfb_address = vfb->fb_address & 0x3FFFFFFF;
		const u32 vfb_size = ColorBufferByteSize(vfb);
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
			// Use bufferHeight in case of buffers that resize up and down often per frame (Valkyrie Profile.)
			bool match = yOffset < dstYOffset && (int)yOffset <= (int)vfb->bufferHeight - dstHeight;
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
			bool match = yOffset < srcYOffset && (int)yOffset <= (int)vfb->bufferHeight - srcHeight;
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

	if (srcBuffer && !dstBuffer) {
		if (PSP_CoreParameter().compat.flags().BlockTransferAllowCreateFB ||
			(PSP_CoreParameter().compat.flags().IntraVRAMBlockTransferAllowCreateFB &&
				Memory::IsVRAMAddress(srcBuffer->fb_address) && Memory::IsVRAMAddress(dstBasePtr))) {
			GEBufferFormat ramFormat;
			// Try to guess the appropriate format. We only know the bpp from the block transfer command (16 or 32 bit).
			if (bpp == 4) {
				// Only one possibility unless it's doing split pixel tricks (which we could detect through stride maybe).
				ramFormat = GE_FORMAT_8888;
			} else if (srcBuffer->format != GE_FORMAT_8888) {
				// We guess that the game will interpret the data the same as it was in the source of the copy.
				// Seems like a likely good guess, and works in Test Drive Unlimited.
				ramFormat = srcBuffer->format;
			} else {
				// No info left - just fall back to something. But this is definitely split pixel tricks.
				ramFormat = GE_FORMAT_5551;
			}
			dstBuffer = CreateRAMFramebuffer(dstBasePtr, dstWidth, dstHeight, dstStride, ramFormat);
		}
	}

	if (dstBuffer)
		dstBuffer->last_frame_used = gpuStats.numFlips;

	if (dstYOffset != (u32)-1) {
		dstY += dstYOffset;
		dstX += dstXOffset;
	}
	if (srcYOffset != (u32)-1) {
		srcY += srcYOffset;
		srcX += srcXOffset;
	}
}

VirtualFramebuffer *FramebufferManagerCommon::CreateRAMFramebuffer(uint32_t fbAddress, int width, int height, int stride, GEBufferFormat format) {
	INFO_LOG(G3D, "Creating RAM framebuffer at %08x (%dx%d, stride %d, format %d)", fbAddress, width, height, stride, format);

	// A target for the destination is missing - so just create one!
	// Make sure this one would be found by the algorithm above so we wouldn't
	// create a new one each frame.
	VirtualFramebuffer *vfb = new VirtualFramebuffer{};
	vfb->fbo = nullptr;
	vfb->fb_address = fbAddress;  // NOTE - not necessarily in VRAM!
	vfb->fb_stride = stride;
	vfb->z_address = 0;  // marks that if anyone tries to render to this framebuffer, it should be dropped and recreated.
	vfb->z_stride = 0;
	vfb->width = std::max(width, stride);
	vfb->height = height;
	vfb->newWidth = vfb->width;
	vfb->newHeight = vfb->height;
	vfb->lastFrameNewSize = gpuStats.numFlips;
	vfb->renderScaleFactor = renderScaleFactor_;
	vfb->renderWidth = (u16)(vfb->width * renderScaleFactor_);
	vfb->renderHeight = (u16)(vfb->height * renderScaleFactor_);
	vfb->bufferWidth = vfb->width;
	vfb->bufferHeight = vfb->height;
	vfb->format = format;
	vfb->drawnFormat = GE_FORMAT_8888;
	vfb->usageFlags = FB_USAGE_RENDERTARGET;
	SetColorUpdated(vfb, 0);
	char name[64];
	snprintf(name, sizeof(name), "%08x_color_RAM", vfb->fb_address);
	textureCache_->NotifyFramebuffer(vfb, NOTIFY_FB_CREATED);
	vfb->fbo = draw_->CreateFramebuffer({ vfb->renderWidth, vfb->renderHeight, 1, 1, true, name });
	vfbs_.push_back(vfb);

	u32 byteSize = ColorBufferByteSize(vfb);
	if (fbAddress + byteSize > framebufRangeEnd_) {
		framebufRangeEnd_ = fbAddress + byteSize;
	}

	return vfb;
}

// 1:1 pixel sides buffers, we resize buffers to these before we read them back.
VirtualFramebuffer *FramebufferManagerCommon::FindDownloadTempBuffer(VirtualFramebuffer *vfb) {
	// For now we'll keep these on the same struct as the ones that can get displayed
	// (and blatantly copy work already done above while at it).
	VirtualFramebuffer *nvfb = nullptr;

	// We maintain a separate vector of framebuffer objects for blitting.
	for (VirtualFramebuffer *v : bvfbs_) {
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
		nvfb = new VirtualFramebuffer{};
		nvfb->fbo = nullptr;
		nvfb->fb_address = vfb->fb_address;
		nvfb->fb_stride = vfb->fb_stride;
		nvfb->z_address = vfb->z_address;
		nvfb->z_stride = vfb->z_stride;
		nvfb->width = vfb->width;
		nvfb->height = vfb->height;
		nvfb->renderWidth = vfb->bufferWidth;
		nvfb->renderHeight = vfb->bufferHeight;
		nvfb->renderScaleFactor = 1.0f;  // For readbacks we resize to the original size, of course.
		nvfb->bufferWidth = vfb->bufferWidth;
		nvfb->bufferHeight = vfb->bufferHeight;
		nvfb->format = vfb->format;
		nvfb->drawnWidth = vfb->drawnWidth;
		nvfb->drawnHeight = vfb->drawnHeight;
		nvfb->drawnFormat = vfb->format;

		char name[64];
		snprintf(name, sizeof(name), "download_temp");
		nvfb->fbo = draw_->CreateFramebuffer({ nvfb->bufferWidth, nvfb->bufferHeight, 1, 1, false, name });
		if (!nvfb->fbo) {
			ERROR_LOG(FRAMEBUF, "Error creating FBO! %d x %d", nvfb->renderWidth, nvfb->renderHeight);
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

	if (!Memory::IsValidAddress(gstate.getFrameBufAddress())) {
		return;
	}

	u8 *addr = Memory::GetPointerUnchecked(gstate.getFrameBufAddress());
	const int bpp = gstate_c.framebufFormat == GE_FORMAT_8888 ? 4 : 2;

	u32 clearBits = clearColor;
	if (bpp == 2) {
		u16 clear16 = 0;
		switch (gstate_c.framebufFormat) {
		case GE_FORMAT_565: clear16 = RGBA8888toRGB565(clearColor); break;
		case GE_FORMAT_5551: clear16 = RGBA8888toRGBA5551(clearColor); break;
		case GE_FORMAT_4444: clear16 = RGBA8888toRGBA4444(clearColor); break;
		default: _dbg_assert_(0); break;
		}
		clearBits = clear16 | (clear16 << 16);
	}

	const bool singleByteClear = (clearBits >> 16) == (clearBits & 0xFFFF) && (clearBits >> 24) == (clearBits & 0xFF);
	const int stride = gstate.FrameBufStride();
	const int width = x2 - x1;

	const int byteStride = stride * bpp;
	const int byteWidth = width * bpp;
	for (int y = y1; y < y2; ++y) {
		NotifyMemInfo(MemBlockFlags::WRITE, gstate.getFrameBufAddress() + x1 * bpp + y * byteStride, byteWidth, "FramebufferClear");
	}

	// Can use memset for simple cases. Often alpha is different and gums up the works.
	if (singleByteClear) {
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

	// This looks at the compat flags BlockTransferAllowCreateFB*.
	FindTransferFramebuffers(dstBuffer, srcBuffer, dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, srcWidth, srcHeight, dstWidth, dstHeight, bpp);

	if (dstBuffer && srcBuffer) {
		if (srcBuffer == dstBuffer) {
			if (srcX != dstX || srcY != dstY) {
				WARN_LOG_N_TIMES(dstsrc, 100, G3D, "Intra-buffer block transfer %dx%d %dbpp from %08x (x:%d y:%d stride:%d) -> %08x (x:%d y:%d stride:%d)",
					width, height, bpp,
					srcBasePtr, srcX, srcY, srcStride,
					dstBasePtr, dstX, dstY, dstStride);
				FlushBeforeCopy();
				// Some backends can handle blitting within a framebuffer. Others will just have to deal with it or ignore it, apparently.
				BlitFramebuffer(dstBuffer, dstX, dstY, srcBuffer, srcX, srcY, dstWidth, dstHeight, bpp, "Blit_IntraBufferBlockTransfer");
				RebindFramebuffer("rebind after intra block transfer");
				SetColorUpdated(dstBuffer, skipDrawReason);
				return true;  // Skip the memory copy.
			} else {
				// Ignore, nothing to do.  Tales of Phantasia X does this by accident.
				return true;  // Skip the memory copy.
			}
		} else {
			WARN_LOG_N_TIMES(dstnotsrc, 100, G3D, "Inter-buffer block transfer %dx%d %dbpp from %08x (x:%d y:%d stride:%d) -> %08x (x:%d y:%d stride:%d)",
				width, height, bpp,
				srcBasePtr, srcX, srcY, srcStride,
				dstBasePtr, dstX, dstY, dstStride);
			// Straightforward blit between two framebuffers.
			FlushBeforeCopy();
			BlitFramebuffer(dstBuffer, dstX, dstY, srcBuffer, srcX, srcY, dstWidth, dstHeight, bpp, "Blit_InterBufferBlockTransfer");
			RebindFramebuffer("RebindFramebuffer - Inter-buffer block transfer");
			SetColorUpdated(dstBuffer, skipDrawReason);
			return true;  // No need to actually do the memory copy behind, probably.
		}
		return false;
	} else if (dstBuffer) {
		// Here we should just draw the pixels into the buffer.  Copy first.
		return false;
	} else if (srcBuffer) {
		WARN_LOG_N_TIMES(btd, 100, G3D, "Block transfer readback %dx%d %dbpp from %08x (x:%d y:%d stride:%d) -> %08x (x:%d y:%d stride:%d)",
			width, height, bpp,
			srcBasePtr, srcX, srcY, srcStride,
			dstBasePtr, dstX, dstY, dstStride);
		FlushBeforeCopy();
		if (g_Config.bBlockTransferGPU && !srcBuffer->memoryUpdated) {
			const int srcBpp = srcBuffer->format == GE_FORMAT_8888 ? 4 : 2;
			const float srcXFactor = (float)bpp / srcBpp;
			const bool tooTall = srcY + srcHeight > srcBuffer->bufferHeight;
			if (srcHeight <= 0 || (tooTall && srcY != 0)) {
				WARN_LOG_ONCE(btdheight, G3D, "Block transfer download %08x -> %08x skipped, %d+%d is taller than %d", srcBasePtr, dstBasePtr, srcY, srcHeight, srcBuffer->bufferHeight);
			} else {
				if (tooTall) {
					WARN_LOG_ONCE(btdheight, G3D, "Block transfer download %08x -> %08x dangerous, %d+%d is taller than %d", srcBasePtr, dstBasePtr, srcY, srcHeight, srcBuffer->bufferHeight);
				}
				ReadFramebufferToMemory(srcBuffer, static_cast<int>(srcX * srcXFactor), srcY, static_cast<int>(srcWidth * srcXFactor), srcHeight);
				srcBuffer->usageFlags = (srcBuffer->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
			}
		}
		return false;  // Let the bit copy happen
	} else {
		return false;
	}
}

void FramebufferManagerCommon::NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int width, int height, int bpp, u32 skipDrawReason) {
	// If it's a block transfer direct to the screen, and we're not using buffers, draw immediately.
	// We may still do a partial block draw below if this doesn't pass.
	if (!useBufferedRendering_ && dstStride >= 480 && width >= 480 && height == 272) {
		bool isPrevDisplayBuffer = PrevDisplayFramebufAddr() == dstBasePtr;
		bool isDisplayBuffer = DisplayFramebufAddr() == dstBasePtr;
		if (isPrevDisplayBuffer || isDisplayBuffer) {
			FlushBeforeCopy();
			DrawFramebufferToOutput(Memory::GetPointerUnchecked(dstBasePtr), displayFormat_, dstStride);
			return;
		}
	}

	if (MayIntersectFramebuffer(srcBasePtr) || MayIntersectFramebuffer(dstBasePtr)) {
		VirtualFramebuffer *dstBuffer = 0;
		VirtualFramebuffer *srcBuffer = 0;
		int srcWidth = width;
		int srcHeight = height;
		int dstWidth = width;
		int dstHeight = height;
		FindTransferFramebuffers(dstBuffer, srcBuffer, dstBasePtr, dstStride, dstX, dstY, srcBasePtr, srcStride, srcX, srcY, srcWidth, srcHeight, dstWidth, dstHeight, bpp);

		// A few games use this INSTEAD of actually drawing the video image to the screen, they just blast it to
		// the backbuffer. Detect this and have the framebuffermanager draw the pixels.
		if (!useBufferedRendering_ && currentRenderVfb_ != dstBuffer) {
			return;
		}

		if (dstBuffer && !srcBuffer) {
			WARN_LOG_ONCE(btu, G3D, "Block transfer upload %08x -> %08x", srcBasePtr, dstBasePtr);
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
				gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
			}
			DrawPixels(dstBuffer, static_cast<int>(dstX * dstXFactor), dstY, srcBase, dstBuffer->format, static_cast<int>(srcStride * dstXFactor), static_cast<int>(dstWidth * dstXFactor), dstHeight);
			SetColorUpdated(dstBuffer, skipDrawReason);
			RebindFramebuffer("RebindFramebuffer - NotifyBlockTransferAfter");
		}
	}
}

void FramebufferManagerCommon::SetSafeSize(u16 w, u16 h) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (vfb) {
		vfb->safeWidth = std::min(vfb->bufferWidth, std::max(vfb->safeWidth, w));
		vfb->safeHeight = std::min(vfb->bufferHeight, std::max(vfb->safeHeight, h));
	}
}

void FramebufferManagerCommon::Resized() {
	gstate_c.skipDrawReason &= ~SKIPDRAW_NON_DISPLAYED_FB;

	int w, h, scaleFactor;
	presentation_->CalculateRenderResolution(&w, &h, &scaleFactor, &postShaderIsUpscalingFilter_, &postShaderIsSupersampling_);
	PSP_CoreParameter().renderWidth = w;
	PSP_CoreParameter().renderHeight = h;
	PSP_CoreParameter().renderScaleFactor = scaleFactor;

	if (UpdateSize()) {
		DestroyAllFBOs();
	}

	// Might have a new post shader - let's compile it.
	presentation_->UpdatePostShader();

#ifdef _WIN32
	// Seems related - if you're ok with numbers all the time, show some more :)
	if (g_Config.iShowFPSCounter != 0) {
		ShowScreenResolution();
	}
#endif
}

void FramebufferManagerCommon::DestroyAllFBOs() {
	currentRenderVfb_ = nullptr;
	displayFramebuf_ = nullptr;
	prevDisplayFramebuf_ = nullptr;
	prevPrevDisplayFramebuf_ = nullptr;

	for (VirtualFramebuffer *vfb : vfbs_) {
		INFO_LOG(FRAMEBUF, "Destroying FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->format);
		DestroyFramebuf(vfb);
	}
	vfbs_.clear();

	for (VirtualFramebuffer *vfb : bvfbs_) {
		DestroyFramebuf(vfb);
	}
	bvfbs_.clear();

	for (auto &tempFB : tempFBOs_) {
		tempFB.second.fbo->Release();
	}
	tempFBOs_.clear();

	for (auto iter : fbosToDelete_) {
		iter->Release();
	}
	fbosToDelete_.clear();
}

Draw::Framebuffer *FramebufferManagerCommon::GetTempFBO(TempFBO reason, u16 w, u16 h) {
	u64 key = ((u64)reason << 48) | ((u32)w << 16) | h;
	auto it = tempFBOs_.find(key);
	if (it != tempFBOs_.end()) {
		it->second.last_frame_used = gpuStats.numFlips;
		return it->second.fbo;
	}

	bool z_stencil = reason == TempFBO::STENCIL;
	char name[128];
	snprintf(name, sizeof(name), "temp_fbo_%dx%d%s", w, h, z_stencil ? "_depth" : "");
	Draw::Framebuffer *fbo = draw_->CreateFramebuffer({ w, h, 1, 1, z_stencil, name });
	if (!fbo) {
		return nullptr;
	}

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
	auto gr = GetI18NCategory("Graphics");

	std::ostringstream messageStream;
	messageStream << gr->T("Internal Resolution") << ": ";
	messageStream << PSP_CoreParameter().renderWidth << "x" << PSP_CoreParameter().renderHeight << " ";
	if (postShaderIsUpscalingFilter_) {
		messageStream << gr->T("(upscaling)") << " ";
	} else if (postShaderIsSupersampling_) {
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
		if (!Memory::IsValidAddress(fb_address))
			return false;
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(fb_address), fb_stride, 512, format);
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
			tempVfb.renderScaleFactor = (float)maxRes;
			BlitFramebuffer(&tempVfb, 0, 0, vfb, 0, 0, vfb->width, vfb->height, 0, "Blit_GetFramebuffer");

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
	buffer.Allocate(w, h, GE_FORMAT_8888, flipY);
	bool retval = draw_->CopyFramebufferToMemorySync(bound, Draw::FB_COLOR_BIT, 0, 0, w, h, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), w, "GetFramebuffer");
	gpuStats.numReadbacks++;
	// After a readback we'll have flushed and started over, need to dirty a bunch of things to be safe.
	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	// We may have blitted to a temp FBO.
	RebindFramebuffer("RebindFramebuffer - GetFramebuffer");
	return retval;
}

bool FramebufferManagerCommon::GetDepthbuffer(u32 fb_address, int fb_stride, u32 z_address, int z_stride, GPUDebugBuffer &buffer) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		if (!Memory::IsValidAddress(z_address))
			return false;
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointer(z_address), z_stride, 512, GPU_DBG_FORMAT_16BIT);
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
	bool retval = draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_DEPTH_BIT, 0, 0, w, h, Draw::DataFormat::D32F, buffer.GetData(), w, "GetDepthBuffer");
	// After a readback we'll have flushed and started over, need to dirty a bunch of things to be safe.
	gstate_c.Dirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	// That may have unbound the framebuffer, rebind to avoid crashes when debugging.
	RebindFramebuffer("RebindFramebuffer - GetDepthbuffer");
	return retval;
}

bool FramebufferManagerCommon::GetStencilbuffer(u32 fb_address, int fb_stride, GPUDebugBuffer &buffer) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb) {
		vfb = GetVFBAt(fb_address);
	}

	if (!vfb) {
		if (!Memory::IsValidAddress(fb_address))
			return false;
		// If there's no vfb and we're drawing there, must be memory?
		// TODO: Actually get the stencil.
		buffer = GPUDebugBuffer(Memory::GetPointer(fb_address), fb_stride, 512, GPU_DBG_FORMAT_8888);
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
	bool retval = draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_STENCIL_BIT, 0, 0, w,h, Draw::DataFormat::S8, buffer.GetData(), w, "GetStencilbuffer");
	// That may have unbound the framebuffer, rebind to avoid crashes when debugging.
	RebindFramebuffer("RebindFramebuffer - GetStencilbuffer");
	return retval;
}

bool FramebufferManagerCommon::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	int w, h;
	draw_->GetFramebufferDimensions(nullptr, &w, &h);
	Draw::DataFormat fmt = draw_->PreferredFramebufferReadbackFormat(nullptr);
	// Ignore preferred formats other than BGRA.
	if (fmt != Draw::DataFormat::B8G8R8A8_UNORM)
		fmt = Draw::DataFormat::R8G8B8A8_UNORM;
	buffer.Allocate(w, h, fmt == Draw::DataFormat::R8G8B8A8_UNORM ? GPU_DBG_FORMAT_8888 : GPU_DBG_FORMAT_8888_BGRA, false);
	bool retval = draw_->CopyFramebufferToMemorySync(nullptr, Draw::FB_COLOR_BIT, 0, 0, w, h, fmt, buffer.GetData(), w, "GetOutputFramebuffer");
	// That may have unbound the framebuffer, rebind to avoid crashes when debugging.
	RebindFramebuffer("RebindFramebuffer - GetOutputFramebuffer");
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

	if (w <= 0 || h <= 0) {
		ERROR_LOG(G3D, "Bad inputs to PackFramebufferSync_: %d %d %d %d", x, y, w, h);
		return;
	}

	const u32 fb_address = vfb->fb_address & 0x3FFFFFFF;

	Draw::DataFormat destFormat = GEFormatToThin3D(vfb->format);
	const int dstBpp = (int)DataFormatSizeInBytes(destFormat);

	const int dstByteOffset = (y * vfb->fb_stride + x) * dstBpp;
	const int dstSize = (h * vfb->fb_stride + w - 1) * dstBpp;

	if (!Memory::IsValidRange(fb_address + dstByteOffset, dstSize)) {
		ERROR_LOG_REPORT(G3D, "PackFramebufferSync_ would write outside of memory, ignoring");
		return;
	}

	u8 *destPtr = Memory::GetPointer(fb_address + dstByteOffset);

	// We always need to convert from the framebuffer native format.
	// Right now that's always 8888.
	DEBUG_LOG(G3D, "Reading framebuffer to mem, fb_address = %08x, ptr=%p", fb_address, destPtr);

	if (destPtr) {
		draw_->CopyFramebufferToMemorySync(vfb->fbo, Draw::FB_COLOR_BIT, x, y, w, h, destFormat, destPtr, vfb->fb_stride, "PackFramebufferSync_");
		char tag[128];
		size_t len = snprintf(tag, sizeof(tag), "FramebufferPack/%08x_%08x_%dx%d_%s", vfb->fb_address, vfb->z_address, w, h, GeBufferFormatToString(vfb->format));
		NotifyMemInfo(MemBlockFlags::WRITE, fb_address + dstByteOffset, dstSize, tag, len);
	} else {
		ERROR_LOG(G3D, "PackFramebufferSync_: Tried to readback to bad address %08x (stride = %d)", fb_address + dstByteOffset, vfb->fb_stride);
	}

	gpuStats.numReadbacks++;
}

void FramebufferManagerCommon::ReadFramebufferToMemory(VirtualFramebuffer *vfb, int x, int y, int w, int h) {
	// Clamp to bufferWidth. Sometimes block transfers can cause this to hit.
	if (x + w >= vfb->bufferWidth) {
		w = vfb->bufferWidth - x;
	}
	if (vfb && vfb->fbo) {
		// We'll pseudo-blit framebuffers here to get a resized version of vfb.
		if (gameUsesSequentialCopies_) {
			// Ignore the x/y/etc., read the entire thing.
			x = 0;
			y = 0;
			w = vfb->width;
			h = vfb->height;
			vfb->memoryUpdated = true;
			vfb->usageFlags |= FB_USAGE_DOWNLOAD;
		} else if (x == 0 && y == 0 && w == vfb->width && h == vfb->height) {
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

		if (vfb->renderWidth == vfb->width && vfb->renderHeight == vfb->height) {
			// No need to blit
			PackFramebufferSync_(vfb, x, y, w, h);
		} else {
			VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
			if (nvfb) {
				BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0, "Blit_ReadFramebufferToMemory");
				PackFramebufferSync_(nvfb, x, y, w, h);
			}
		}

		textureCache_->ForgetLastTexture();
		RebindFramebuffer("RebindFramebuffer - ReadFramebufferToMemory");
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
			// We intentionally don't try to optimize into a full download here - we don't want to over download.

			// CLUT framebuffers are often incorrectly estimated in size.
			if (x == 0 && y == 0 && w == vfb->width && h == vfb->height) {
				vfb->memoryUpdated = true;
			}
			vfb->clutUpdatedBytes = loadBytes;

			// We'll pseudo-blit framebuffers here to get a resized version of vfb.
			VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb);
			if (nvfb) {
				BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0, "Blit_DownloadFramebufferForClut");
				PackFramebufferSync_(nvfb, x, y, w, h);
			}

			textureCache_->ForgetLastTexture();
			RebindFramebuffer("RebindFramebuffer - DownloadFramebufferForClut");
		}
	}
}

void FramebufferManagerCommon::RebindFramebuffer(const char *tag) {
	shaderManager_->DirtyLastShader();
	if (currentRenderVfb_ && currentRenderVfb_->fbo) {
		draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, tag);
	} else {
		// Should this even happen?  It could while debugging, but maybe we can just skip binding at all.
		draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "RebindFramebuffer_Bad");
	}
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

template <typename T>
static void DoRelease(T *&obj) {
	if (obj)
		obj->Release();
	obj = nullptr;
}

void FramebufferManagerCommon::DeviceLost() {
	DestroyAllFBOs();
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			DoRelease(reinterpretFromTo_[i][j]);
		}
	}
	DoRelease(reinterpretVBuf_);
	DoRelease(reinterpretSampler_);
	DoRelease(reinterpretVS_);
	presentation_->DeviceLost();
	draw_ = nullptr;
}

void FramebufferManagerCommon::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	presentation_->DeviceRestore(draw);
}
