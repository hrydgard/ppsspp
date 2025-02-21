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

#include "ext/imgui/imgui.h"
#include "ext/imgui/imgui_impl_thin3d.h"

#include "Common/GPU/thin3d.h"
#include "Common/Data/Collections/TinySet.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/LogReporting.h"
#include "Common/System/Display.h"
#include "Common/VR/PPSSPPVR.h"
#include "Common/CommonTypes.h"
#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/CoreParameter.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "GPU/Common/TextureCacheCommon.h"
#include "GPU/Common/ReinterpretFramebuffer.h"
#include "GPU/GPUCommon.h"
#include "GPU/GPUState.h"

static size_t FormatFramebufferName(const VirtualFramebuffer *vfb, char *tag, size_t len) {
	return snprintf(tag, len, "FB_%08x_%08x_%dx%d_%s", vfb->fb_address, vfb->z_address, vfb->bufferWidth, vfb->bufferHeight, GeBufferFormatToString(vfb->fb_format));
}

FramebufferManagerCommon::FramebufferManagerCommon(Draw::DrawContext *draw)
	: draw_(draw), draw2D_(draw_) {
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
	delete[] convBuf_;
}

void FramebufferManagerCommon::Init(int msaaLevel) {
	// We may need to override the render size if the shader is upscaling or SSAA.
	NotifyDisplayResized();
	NotifyRenderResized(msaaLevel);
}

// Returns true if we need to stop the render thread
bool FramebufferManagerCommon::UpdateRenderSize(int msaaLevel) {
	const bool newRender = renderWidth_ != (float)PSP_CoreParameter().renderWidth || renderHeight_ != (float)PSP_CoreParameter().renderHeight || msaaLevel_ != msaaLevel;

	int effectiveBloomHack = g_Config.iBloomHack;
	if (PSP_CoreParameter().compat.flags().ForceLowerResolutionForEffectsOn) {
		effectiveBloomHack = 3;
	} else if (PSP_CoreParameter().compat.flags().ForceLowerResolutionForEffectsOff) {
		effectiveBloomHack = 0;
	}

	bool newBuffered = !g_Config.bSkipBufferEffects;
	const bool newSettings = bloomHack_ != effectiveBloomHack || useBufferedRendering_ != newBuffered;

	renderWidth_ = (float)PSP_CoreParameter().renderWidth;
	renderHeight_ = (float)PSP_CoreParameter().renderHeight;
	renderScaleFactor_ = (float)PSP_CoreParameter().renderScaleFactor;
	msaaLevel_ = msaaLevel;

	bloomHack_ = effectiveBloomHack;
	useBufferedRendering_ = newBuffered;

	presentation_->UpdateRenderSize(renderWidth_, renderHeight_);

	// If just switching TO buffered rendering, no need to pause the threads. In fact this causes problems due to the open backbuffer renderpass.
	if (!useBufferedRendering_ && newBuffered) {
		return false;
	}
	return newRender || newSettings;
}

void FramebufferManagerCommon::CheckPostShaders() {
	if (updatePostShaders_) {
		presentation_->UpdatePostShader();
		updatePostShaders_ = false;
	}
}

void FramebufferManagerCommon::BeginFrame() {
	DecimateFBOs();
	presentation_->BeginFrame();
	currentRenderVfb_ = nullptr;
}

bool FramebufferManagerCommon::PresentedThisFrame() const {
	return presentation_->PresentedThisFrame();
}

void FramebufferManagerCommon::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	displayFramebufPtr_ = framebuf & 0x3FFFFFFF;
	if (Memory::IsVRAMAddress(displayFramebufPtr_))
		displayFramebufPtr_ = framebuf & 0x041FFFFF;
	displayStride_ = stride;
	displayFormat_ = format;
}

VirtualFramebuffer *FramebufferManagerCommon::GetVFBAt(u32 addr) const {
	addr &= 0x3FFFFFFF;
	if (Memory::IsVRAMAddress(addr))
		addr &= 0x041FFFFF;
	VirtualFramebuffer *match = nullptr;
	for (auto vfb : vfbs_) {
		if (vfb->fb_address == addr) {
			// Could check w too but whatever (actually, might very well make sense to do so, depending on context).
			if (!match || vfb->last_frame_render > match->last_frame_render) {
				match = vfb;
			}
		}
	}
	return match;
}

VirtualFramebuffer *FramebufferManagerCommon::GetExactVFB(u32 addr, int stride, GEBufferFormat format) const {
	addr &= 0x3FFFFFFF;
	if (Memory::IsVRAMAddress(addr))
		addr &= 0x041FFFFF;
	VirtualFramebuffer *newest = nullptr;
	for (auto vfb : vfbs_) {
		if (vfb->fb_address == addr && vfb->fb_stride == stride && vfb->fb_format == format) {
			if (newest) {
				if (vfb->colorBindSeq > newest->colorBindSeq) {
					newest = vfb;
				}
			} else {
				newest = vfb;
			}
		}
	}
	return newest;
}

VirtualFramebuffer *FramebufferManagerCommon::ResolveVFB(u32 addr, int stride, GEBufferFormat format) {
	addr &= 0x3FFFFFFF;
	if (Memory::IsVRAMAddress(addr))
		addr &= 0x041FFFFF;
	// Find the newest one matching addr and stride.
	VirtualFramebuffer *newest = nullptr;
	for (auto vfb : vfbs_) {
		if (vfb->fb_address == addr && vfb->FbStrideInBytes() == stride * BufferFormatBytesPerPixel(format)) {
			if (newest) {
				if (vfb->colorBindSeq > newest->colorBindSeq) {
					newest = vfb;
				}
			} else {
				newest = vfb;
			}
		}
	}

	if (newest && newest->fb_format != format) {
		WARN_LOG_ONCE(resolvevfb, Log::G3D, "ResolveVFB: Resolving from %s to %s at %08x/%d", GeBufferFormatToString(newest->fb_format), GeBufferFormatToString(format), addr, stride);
		return ResolveFramebufferColorToFormat(newest, format);
	}

	return newest;
}

VirtualFramebuffer *FramebufferManagerCommon::GetDisplayVFB() {
	return GetExactVFB(displayFramebufPtr_, displayStride_, displayFormat_);
}

// Heuristics to figure out the size of FBO to create.
// TODO: Possibly differentiate on whether through mode is used (since in through mode, viewport is meaningless?)
void FramebufferManagerCommon::EstimateDrawingSize(u32 fb_address, int fb_stride, GEBufferFormat fb_format, int viewport_width, int viewport_height, int region_width, int region_height, int scissor_width, int scissor_height, int &drawing_width, int &drawing_height) {
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
		for (auto vfb : vfbs_) {
			const u32 other_address = vfb->fb_address;
			if (other_address > fb_address && other_address < nearest_address) {
				nearest_address = other_address;
			}
		}

		// Unless the game is using overlapping buffers, the next buffer should be far enough away.
		// This catches some cases where we can know this.
		// Hmm.  The problem is that we could only catch it for the first of two buffers...
		const u32 bpp = BufferFormatBytesPerPixel(fb_format);
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

	bool margin = false;
	// Let's check if we're in a stride gap of a full-size framebuffer.
	for (auto vfb : vfbs_) {
		if (fb_address == vfb->fb_address) {
			continue;
		}
		if (vfb->fb_stride != 512) {
			continue;
		}

		int vfb_stride_in_bytes = BufferFormatBytesPerPixel(vfb->fb_format) * vfb->fb_stride;
		int stride_in_bytes = BufferFormatBytesPerPixel(fb_format) * fb_stride;
		if (stride_in_bytes != vfb_stride_in_bytes) {
			// Mismatching stride in bytes, not interesting
			continue;
		}

		if (fb_address > vfb->fb_address && fb_address < vfb->fb_address + vfb_stride_in_bytes) {
			// Candidate!
			if (vfb->height == drawing_height) {
				// Might have a margin texture! Fix the drawing width if it's too large.
				int width_in_bytes = vfb->fb_address + vfb_stride_in_bytes - fb_address;
				int width_in_pixels = width_in_bytes / BufferFormatBytesPerPixel(fb_format);

				// Final check
				if (width_in_pixels <= 32) {
					drawing_width = std::min(drawing_width, width_in_pixels);
					margin = true;
					// Don't really need to keep looking.
					break;
				}
			}
		}
	}

	DEBUG_LOG(Log::G3D, "Est: %08x V: %ix%i, R: %ix%i, S: %ix%i, STR: %i, THR:%i, Z:%08x = %ix%i %s", fb_address, viewport_width,viewport_height, region_width, region_height, scissor_width, scissor_height, fb_stride, gstate.isModeThrough(), gstate.isDepthWriteEnabled() ? gstate.getDepthBufAddress() : 0, drawing_width, drawing_height, margin ? " (margin!)" : "");
}

void GetFramebufferHeuristicInputs(FramebufferHeuristicParams *params, const GPUgstate &gstate) {
	// GetFramebufferHeuristicInputs is only called from rendering, and thus, it's VRAM.
	params->fb_address = gstate.getFrameBufRawAddress() | 0x04000000;
	params->fb_stride = gstate.FrameBufStride();

	params->z_address = gstate.getDepthBufRawAddress() | 0x04000000;
	params->z_stride = gstate.DepthBufStride();

	if (params->z_address == params->fb_address) {
		// Probably indicates that the game doesn't care about Z for this VFB.
		// Let's avoid matching it for Z copies and other shenanigans.
		params->z_address = 0;
		params->z_stride = 0;
	}

	params->fb_format = gstate_c.framebufFormat;

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
	const bool alphaBlending = gstate.isAlphaBlendEnabled();
	const bool logicOpBlending = gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_CLEAR && gstate.getLogicOp() != GE_LOGIC_COPY;
	params->isBlending = alphaBlending || logicOpBlending;

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

	params->scissorLeft = gstate.getScissorX1();
	params->scissorTop = gstate.getScissorY1();
	params->scissorRight = gstate.getScissorX2() + 1;
	params->scissorBottom = gstate.getScissorY2() + 1;

	if (gstate.getRegionRateX() != 0x100 || gstate.getRegionRateY() != 0x100) {
		WARN_LOG_REPORT_ONCE(regionRate, Log::G3D, "Drawing region rate add non-zero: %04x, %04x of %04x, %04x", gstate.getRegionRateX(), gstate.getRegionRateY(), gstate.getRegionX2(), gstate.getRegionY2());
	}
}

static void ApplyKillzoneFramebufferSplit(FramebufferHeuristicParams *params, int *drawing_width);

VirtualFramebuffer *FramebufferManagerCommon::DoSetRenderFrameBuffer(FramebufferHeuristicParams &params, u32 skipDrawReason) {
	gstate_c.Clean(DIRTY_FRAMEBUF);

	// Collect all parameters. This whole function has really become a cesspool of heuristics...
	// but it appears that's what it takes, unless we emulate VRAM layout more accurately somehow.

	// As there are no clear "framebuffer width" and "framebuffer height" registers,
	// we need to infer the size of the current framebuffer somehow.
	int drawing_width, drawing_height;
	EstimateDrawingSize(params.fb_address, std::max(params.fb_stride, (u16)4), params.fb_format, params.viewportWidth, params.viewportHeight, params.regionWidth, params.regionHeight, params.scissorRight, params.scissorBottom, drawing_width, drawing_height);

	if (params.fb_address == params.z_address) {
		// Most likely Z will not be used in this pass, as that would wreak havoc (undefined behavior for sure)
		// We probably don't need to do anything about that, but let's log it.
		WARN_LOG_ONCE(color_equal_z, Log::G3D, "Framebuffer bound with color addr == z addr, likely will not use Z in this pass: %08x", params.fb_address);
	}

	// Compatibility hack for Killzone, see issue #6207.
	if (PSP_CoreParameter().compat.flags().SplitFramebufferMargin && params.fb_format == GE_FORMAT_8888) {
		ApplyKillzoneFramebufferSplit(&params, &drawing_width);
	} else {
		gstate_c.SetCurRTOffset(0, 0);
	}

	// Find a matching framebuffer.
	VirtualFramebuffer *normal_vfb = nullptr;
	int y_offset;
	VirtualFramebuffer *large_offset_vfb = nullptr;

	for (auto v : vfbs_) {
		const u32 bpp = BufferFormatBytesPerPixel(v->fb_format);

		if (params.fb_address == v->fb_address && params.fb_format == v->fb_format && params.fb_stride == v->fb_stride) {
			if (!normal_vfb) {
				normal_vfb = v;
			}
		} else if (!PSP_CoreParameter().compat.flags().DisallowFramebufferAtOffset && !PSP_CoreParameter().compat.flags().SplitFramebufferMargin &&
			v->fb_stride == params.fb_stride && v->fb_format == params.fb_format) {
			u32 v_fb_first_line_end_ptr = v->fb_address + v->fb_stride * bpp;
			u32 v_fb_end_ptr = v->fb_address + v->fb_stride * v->height * bpp;

			if (!normal_vfb && params.fb_address > v->fb_address && params.fb_address < v_fb_first_line_end_ptr) {
				const int x_offset = (params.fb_address - v->fb_address) / bpp;
				if (x_offset < params.fb_stride && v->height >= drawing_height) {
					// Pretty certainly a pure render-to-X-offset.
					WARN_LOG_REPORT_ONCE(renderoffset, Log::FrameBuf, "Rendering to framebuffer offset at %08x +%dx%d (stride %d)", v->fb_address, x_offset, 0, v->fb_stride);
					normal_vfb = v;
					gstate_c.SetCurRTOffset(x_offset, 0);
					normal_vfb->width = std::max((int)normal_vfb->width, x_offset + drawing_width);
					// To prevent the newSize code from being confused.
					drawing_width += x_offset;
					break;
				}
			} else if (PSP_CoreParameter().compat.flags().FramebufferAllowLargeVerticalOffset &&
				params.fb_address > v->fb_address && v->fb_stride > 0 && (params.fb_address - v->fb_address) % v->FbStrideInBytes() == 0 &&
				params.fb_address != 0x04088000 && v->fb_address != 0x04000000) {  // Heuristic to avoid merging the main framebuffers.
				y_offset = (params.fb_address - v->fb_address) / v->FbStrideInBytes();
				if (y_offset <= v->bufferHeight) {  // note: v->height is misdetected as 256 instead of 272 here in tokimeki. Note that 272 is just the height of the upper part, it's supersampling vertically.
					large_offset_vfb = v;
					break;
				}
			}
		}
	}

	VirtualFramebuffer *vfb = nullptr;
	if (large_offset_vfb) {
		// These are prioritized over normal VFBs matches, to ensure things work even if the higher-address one
		// is created first. Only enabled under compat flag.
		vfb = large_offset_vfb;
		WARN_LOG_REPORT_ONCE(tokimeki, Log::FrameBuf, "Detected FBO at Y offset %d of %08x: %08x", y_offset, large_offset_vfb->fb_address, params.fb_address);
		gstate_c.SetCurRTOffset(0, y_offset);
		vfb->height = std::max((int)vfb->height, y_offset + drawing_height);
		drawing_height += y_offset;
		// TODO: We can allow X/Y overlaps too, but haven't seen any so safer to not.
	} else if (normal_vfb) {
		vfb = normal_vfb;
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
	}

	if (vfb) {
		bool resized = false;
		if ((drawing_width != vfb->bufferWidth || drawing_height != vfb->bufferHeight)) {
			// Even if it's not newly wrong, if this is larger we need to resize up.
			if (vfb->width > vfb->bufferWidth || vfb->height > vfb->bufferHeight) {
				ResizeFramebufFBO(vfb, vfb->width, vfb->height);
				resized = true;
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

				// Whether we resize or not, change the size parameters so we stop detecting a resize.
				// It might be larger if all drawing has been in throughmode.
				vfb->width = drawing_width;
				vfb->height = drawing_height;

				if (needsRecreate) {
					ResizeFramebufFBO(vfb, vfb->width, vfb->height, true);
					resized = true;
					// Let's discard this information, might be wrong now.
					vfb->safeWidth = 0;
					vfb->safeHeight = 0;
				}
			}
		} else {
			// It's not different, let's keep track of that too.
			vfb->lastFrameNewSize = gpuStats.numFlips;
		}

		if (!resized && renderScaleFactor_ != 1 && vfb->renderScaleFactor == 1) {
			// Might be time to change this framebuffer - have we used depth?
			if ((vfb->usageFlags & FB_USAGE_COLOR_MIXED_DEPTH) && !PSP_CoreParameter().compat.flags().ForceLowerResolutionForEffectsOn) {
				ResizeFramebufFBO(vfb, vfb->width, vfb->height, true);
				_assert_(vfb->renderScaleFactor != 1);
			}
		}
	}

	// None found? Create one.
	if (!vfb) {
		gstate_c.usingDepth = false;  // reset depth buffer tracking

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
		vfb->fb_format = params.fb_format;
		vfb->usageFlags = FB_USAGE_RENDER_COLOR;

		u32 colorByteSize = vfb->BufferByteSize(RASTER_COLOR);
		if (Memory::IsVRAMAddress(params.fb_address) && params.fb_address + colorByteSize > framebufColorRangeEnd_) {
			framebufColorRangeEnd_ = params.fb_address + colorByteSize;
		}

		// This is where we actually create the framebuffer. The true is "force".
		ResizeFramebufFBO(vfb, drawing_width, drawing_height, true);
		NotifyRenderFramebufferCreated(vfb);

		// Note that we do not even think about depth right now. That'll be handled
		// on the first depth access, which will call SetDepthFramebuffer.

		CopyToColorFromOverlappingFramebuffers(vfb);
		SetColorUpdated(vfb, skipDrawReason);

		INFO_LOG(Log::FrameBuf, "Creating FBO for %08x (z: %08x) : %d x %d x %s", vfb->fb_address, vfb->z_address, vfb->width, vfb->height, GeBufferFormatToString(vfb->fb_format));

		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfbs_.push_back(vfb);
		currentRenderVfb_ = vfb;

		// Assume that if we're clearing right when switching to a new framebuffer, we don't need to upload.
		if (useBufferedRendering_ && params.isDrawing) {
			gpu->PerformWriteColorFromMemory(params.fb_address, colorByteSize);
			// Alpha was already done by PerformWriteColorFromMemory.
			PerformWriteStencilFromMemory(params.fb_address, colorByteSize, WriteStencil::STENCIL_IS_ZERO | WriteStencil::IGNORE_ALPHA);
			// TODO: Is it worth trying to upload the depth buffer (only if it wasn't copied above..?)
		}

		DiscardFramebufferCopy();

		// We already have it!
	} else if (vfb != currentRenderVfb_) {
		// Use it as a render target.
		DEBUG_LOG(Log::FrameBuf, "Switching render target to FBO for %08x: %d x %d x %d ", vfb->fb_address, vfb->width, vfb->height, vfb->fb_format);
		vfb->usageFlags |= FB_USAGE_RENDER_COLOR;
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;

		VirtualFramebuffer *prev = currentRenderVfb_;
		currentRenderVfb_ = vfb;
		NotifyRenderFramebufferSwitched(prev, vfb, params.isClearingDepth);
		CopyToColorFromOverlappingFramebuffers(vfb);
		gstate_c.usingDepth = false;  // reset depth buffer tracking

		DiscardFramebufferCopy();
	} else {
		// Something changed, but we still got the same framebuffer we were already rendering to.
		// Might not be a lot to do here, we check in NotifyRenderFramebufferUpdated
		vfb->last_frame_render = gpuStats.numFlips;
		frameLastFramebufUsed_ = gpuStats.numFlips;
		vfb->dirtyAfterDisplay = true;
		if ((skipDrawReason & SKIPDRAW_SKIPFRAME) == 0)
			vfb->reallyDirtyAfterDisplay = true;
		NotifyRenderFramebufferUpdated(vfb);
	}

	vfb->colorBindSeq = GetBindSeqCount();

	gstate_c.curRTWidth = vfb->width;
	gstate_c.curRTHeight = vfb->height;
	gstate_c.curRTRenderWidth = vfb->renderWidth;
	gstate_c.curRTRenderHeight = vfb->renderHeight;
	return vfb;
}

// Called on the first use of depth in a render pass.
void FramebufferManagerCommon::SetDepthFrameBuffer(bool isClearingDepth) {
	if (!currentRenderVfb_) {
		return;
	}

	// First time use of this framebuffer's depth buffer.
	bool newlyUsingDepth = (currentRenderVfb_->usageFlags & FB_USAGE_RENDER_DEPTH) == 0;
	currentRenderVfb_->usageFlags |= FB_USAGE_RENDER_DEPTH;

	uint32_t boundDepthBuffer = gstate.getDepthBufRawAddress() | 0x04000000;
	uint32_t boundDepthStride = gstate.DepthBufStride();
	if (currentRenderVfb_->z_address != boundDepthBuffer || currentRenderVfb_->z_stride != boundDepthStride) {
		if (currentRenderVfb_->fb_address == boundDepthBuffer) {
			// Disallow setting depth buffer to the same address as the color buffer, usually means it's not used.
			WARN_LOG_N_TIMES(z_reassign, 5, Log::FrameBuf, "Ignoring color matching depth buffer at %08x", boundDepthBuffer);
			boundDepthBuffer = 0;
			boundDepthStride = 0;
		}
		WARN_LOG_N_TIMES(z_reassign, 5, Log::FrameBuf, "Framebuffer at %08x/%d has switched associated depth buffer from %08x to %08x, updating.",
			currentRenderVfb_->fb_address, currentRenderVfb_->fb_stride, currentRenderVfb_->z_address, boundDepthBuffer);

		// Technically, here we should copy away the depth buffer to another framebuffer that uses that z_address, or maybe
		// even write it back to RAM. However, this is rare. Silent Hill is one example, see #16126.
		currentRenderVfb_->z_address = boundDepthBuffer;
		// Update the stride in case it changed.
		currentRenderVfb_->z_stride = boundDepthStride;

		if (currentRenderVfb_->fbo) {
			char tag[128];
			FormatFramebufferName(currentRenderVfb_, tag, sizeof(tag));
			currentRenderVfb_->fbo->UpdateTag(tag);
		}
	}

	// If this first draw call is anything other than a clear, "resolve" the depth buffer,
	// by copying from any overlapping buffers with fresher content.
	if (!isClearingDepth && useBufferedRendering_) {
		CopyToDepthFromOverlappingFramebuffers(currentRenderVfb_);

		// Need to upload the first line of depth buffers, for Burnout Dominator lens flares. See issue #11100 and comments to #16081.
		// Might make this more generic and upload the whole depth buffer if we find it's needed for something.
		if (newlyUsingDepth && draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported) {
			// Sanity check the depth buffer pointer.
			if (Memory::IsValidRange(currentRenderVfb_->z_address, currentRenderVfb_->width * 2)) {
				const u16 *src = (const u16 *)Memory::GetPointerUnchecked(currentRenderVfb_->z_address);
				DrawPixels(currentRenderVfb_, 0, 0, (const u8 *)src, GE_FORMAT_DEPTH16, currentRenderVfb_->z_stride, currentRenderVfb_->width, currentRenderVfb_->height, RASTER_DEPTH, "Depth Upload");
			}
		}
	}

	currentRenderVfb_->depthBindSeq = GetBindSeqCount();
}

struct CopySource {
	VirtualFramebuffer *vfb;
	RasterChannel channel;
	int xOffset;
	int yOffset;

	int seq() const {
		return channel == RASTER_DEPTH ? vfb->depthBindSeq : vfb->colorBindSeq;
	}

	bool operator < (const CopySource &other) const {
		return seq() < other.seq();
	}
};

// Not sure if it's more profitable to always do these copies with raster (which may screw up early-Z due to explicit depth buffer write)
// or to use image copies when possible (which may make it easier for the driver to preserve early-Z, but on the other hand, will cost additional memory
// bandwidth on tilers due to the load operation, which we might otherwise be able to skip).
void FramebufferManagerCommon::CopyToDepthFromOverlappingFramebuffers(VirtualFramebuffer *dest) {
	std::vector<CopySource> sources;
	for (auto src : vfbs_) {
		if (src == dest)
			continue;

		if (src->fb_address == dest->z_address && src->fb_stride == dest->z_stride && src->fb_format == GE_FORMAT_565) {
			if (src->colorBindSeq > dest->depthBindSeq) {
				// Source has newer data than the current buffer, use it.
				sources.push_back(CopySource{ src, RASTER_COLOR, 0, 0 });
			}
		} else if (src->z_address == dest->z_address && src->z_stride == dest->z_stride && src->depthBindSeq > dest->depthBindSeq) {
			sources.push_back(CopySource{ src, RASTER_DEPTH, 0, 0 });
		} else {
			// TODO: Do more detailed overlap checks here.
		}
	}

	std::sort(sources.begin(), sources.end());

	// TODO: A full copy will overwrite anything else. So we can eliminate
	// anything that comes before such a copy.

	// For now, let's just do the last thing, if there are multiple.

	// for (auto &source : sources) {
	if (!sources.empty()) {
		draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);

		auto &source = sources.back();
		if (source.channel == RASTER_DEPTH) {
			// Good old depth->depth copy.
			BlitFramebufferDepth(source.vfb, dest);
			gpuStats.numDepthCopies++;
			dest->last_frame_depth_updated = gpuStats.numFlips;
		} else if (source.channel == RASTER_COLOR && draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported) {
			VirtualFramebuffer *src = source.vfb;
			if (src->fb_format != GE_FORMAT_565) {
				WARN_LOG_ONCE(not565, Log::FrameBuf, "fb_format of buffer at %08x not 565 as expected", src->fb_address);
			}

			// Really hate to do this, but tracking the depth swizzle state across multiple
			// copies is not easy.
			Draw2DShader shader = DRAW2D_565_TO_DEPTH;
			if (PSP_CoreParameter().compat.flags().DeswizzleDepth) {
				shader = DRAW2D_565_TO_DEPTH_DESWIZZLE;
			}

			gpuStats.numReinterpretCopies++;
			src->usageFlags |= FB_USAGE_COLOR_MIXED_DEPTH;
			dest->usageFlags |= FB_USAGE_COLOR_MIXED_DEPTH;

			// Copying color to depth.
			BlitUsingRaster(
				src->fbo, 0.0f, 0.0f, src->renderWidth, src->renderHeight,
				dest->fbo, 0.0f, 0.0f, src->renderWidth, src->renderHeight,
				false, dest->renderScaleFactor, Get2DPipeline(shader), "565_to_depth");
		}
	}

	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
}

// Can't easily dynamically create these strings, we just pass along the pointer.
static const char *reinterpretStrings[4][4] = {
	{
		"self_reinterpret_565",
		"reinterpret_565_to_5551",
		"reinterpret_565_to_4444",
		"reinterpret_565_to_8888",
	},
	{
		"reinterpret_5551_to_565",
		"self_reinterpret_5551",
		"reinterpret_5551_to_4444",
		"reinterpret_5551_to_8888",
	},
	{
		"reinterpret_4444_to_565",
		"reinterpret_4444_to_5551",
		"self_reinterpret_4444",
		"reinterpret_4444_to_8888",
	},
	{
		"reinterpret_8888_to_565",
		"reinterpret_8888_to_5551",
		"reinterpret_8888_to_4444",
		"self_reinterpret_8888",
	},
};

// Call this after the target has been bound for rendering. For color, raster is probably always going to win over blits/copies.
void FramebufferManagerCommon::CopyToColorFromOverlappingFramebuffers(VirtualFramebuffer *dst) {
	if (!useBufferedRendering_) {
		return;
	}

	std::vector<CopySource> sources;
	for (auto src : vfbs_) {
		// Discard old and equal potential inputs.
		if (src == dst || src->colorBindSeq < dst->colorBindSeq) {
			continue;
		}

		if (src->fb_address == dst->fb_address && src->fb_stride == dst->fb_stride) {
			// Another render target at the exact same location but gotta be a different format or a different stride, otherwise
			// it would be the same, and should have been detected in DoSetRenderFrameBuffer.
			if (src->fb_format != dst->fb_format) {
				// This will result in reinterpret later, if both formats are 16-bit.
				sources.push_back(CopySource{ src, RASTER_COLOR, 0, 0 });
			} else {
				// This shouldn't happen anymore. I think when it happened last, we still had
				// lax stride checking when video was incoming, and a resize happened causing a duplicate.
			}
		} else if (src->fb_stride == dst->fb_stride && src->fb_format == dst->fb_format) {
			u32 bytesPerPixel = BufferFormatBytesPerPixel(src->fb_format);

			u32 strideInBytes = src->fb_stride * bytesPerPixel;  // Same for both src and dest

			u32 srcColorStart = src->fb_address;
			u32 srcFirstLineEnd = src->fb_address + strideInBytes;
			u32 srcColorEnd = strideInBytes * src->height;

			u32 dstColorStart = dst->fb_address;
			u32 dstFirstLineEnd = dst->fb_address + strideInBytes;
			u32 dstColorEnd = strideInBytes * dst->height;

			// Initially we'll only allow pure horizontal and vertical overlap,
			// to reduce the risk for false positives. We can allow diagonal overlap too if needed
			// in the future.

			// Check for potential vertical overlap, like in Juiced 2.
			int xOffset = 0;
			int yOffset = 0;

			// TODO: Get rid of the compatibility flag check.
			if ((dstColorStart - srcColorStart) % strideInBytes == 0
				&& PSP_CoreParameter().compat.flags().AllowLargeFBTextureOffsets) {
				// Buffers are aligned.
				yOffset = ((int)dstColorStart - (int)srcColorStart) / strideInBytes;
				if (yOffset <= -(int)src->height) {
					// Not overlapping
					continue;
				} else if (yOffset >= dst->height) {
					// Not overlapping
					continue;
				}
			} else {
				// Buffers not stride-aligned - ignoring for now.
				// This is where we'll add the horizontal offset for GoW.
				continue;
			}
			sources.push_back(CopySource{ src, RASTER_COLOR, xOffset, yOffset });
		} else if (src->fb_address == dst->fb_address && src->FbStrideInBytes() == dst->FbStrideInBytes()) {
			if (src->fb_stride == dst->fb_stride * 2) {
				// Reinterpret from 16-bit to 32-bit.
				sources.push_back(CopySource{ src, RASTER_COLOR, 0, 0 });
			} else if (src->fb_stride * 2 == dst->fb_stride) {
				// Reinterpret from 32-bit to 16-bit.
				sources.push_back(CopySource{ src, RASTER_COLOR, 0, 0 });
			} else {
				// 16-to-16 reinterpret, should have been caught above already.
				_assert_msg_(false, "Reinterpret: Shouldn't get here");
			}
		}
	}

	std::sort(sources.begin(), sources.end());

	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);

	bool tookActions = false;

	// TODO: Only do the latest one.
	for (const CopySource &source : sources) {
		VirtualFramebuffer *src = source.vfb;

		// Copy a rectangle from the original to the new buffer.
		// Yes, we mean to look at src->width/height for the dest rectangle.

		// TODO: Try to bound the blit using gstate_c.vertBounds like depal does.

		int srcWidth = src->width * src->renderScaleFactor;
		int srcHeight = src->height * src->renderScaleFactor;
		int dstWidth = src->width * dst->renderScaleFactor;
		int dstHeight = src->height * dst->renderScaleFactor;

		int dstX1 = -source.xOffset * dst->renderScaleFactor;
		int dstY1 = -source.yOffset * dst->renderScaleFactor;
		int dstX2 = dstX1 + dstWidth;
		int dstY2 = dstY1 + dstHeight;

		if (source.channel == RASTER_COLOR) {
			Draw2DPipeline *pipeline = nullptr;
			const char *pass_name = "N/A";
			float scaleFactorX = 1.0f;
			if (src->fb_format == dst->fb_format) {
				gpuStats.numColorCopies++;
				pipeline = Get2DPipeline(DRAW2D_COPY_COLOR);
				pass_name = "copy_color";
			} else {
				if (PSP_CoreParameter().compat.flags().BlueToAlpha) {
					WARN_LOG_ONCE(bta, Log::FrameBuf, "WARNING: Reinterpret encountered with BlueToAlpha on");
				}

				// Reinterpret!
				WARN_LOG_N_TIMES(reint, 5, Log::FrameBuf, "Reinterpret detected from %08x_%s to %08x_%s",
					src->fb_address, GeBufferFormatToString(src->fb_format),
					dst->fb_address, GeBufferFormatToString(dst->fb_format));

				pipeline = GetReinterpretPipeline(src->fb_format, dst->fb_format, &scaleFactorX);
				dstX1 *= scaleFactorX;
				dstX2 *= scaleFactorX;

				pass_name = reinterpretStrings[(int)src->fb_format][(int)dst->fb_format];

				gpuStats.numReinterpretCopies++;
			}
			
			if (pipeline) {
				tookActions = true;
				// OK we have the pipeline, now just do the blit.
				BlitUsingRaster(src->fbo, 0.0f, 0.0f, srcWidth, srcHeight,
					dst->fbo, dstX1, dstY1, dstX2, dstY2, false, dst->renderScaleFactor, pipeline, pass_name);
			}

			if (scaleFactorX == 1.0f && dst->z_address == src->z_address && dst->z_stride == src->z_stride) {
				// We should also copy the depth buffer in this case!
				BlitFramebufferDepth(src, dst, true);
			}
		}
	}

	if (currentRenderVfb_ && dst != currentRenderVfb_ && tookActions) {
		// Will probably just change the name of the current renderpass, since one was started by the reinterpret itself.
		draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "After Reinterpret");
	}

	shaderManager_->DirtyLastShader();
	textureCache_->ForgetLastTexture();
}

Draw2DPipeline *FramebufferManagerCommon::GetReinterpretPipeline(GEBufferFormat from, GEBufferFormat to, float *scaleFactorX) {
	if (from == to) {
		*scaleFactorX = 1.0f;
		return Get2DPipeline(DRAW2D_COPY_COLOR);
	}

	if (IsBufferFormat16Bit(from) && !IsBufferFormat16Bit(to)) {
		// We halve the X coordinates in the destination framebuffer.
		// The shader will collect two pixels worth of input data and merge into one.
		*scaleFactorX = 0.5f;
	} else if (!IsBufferFormat16Bit(from) && IsBufferFormat16Bit(to)) {
		// We double the X coordinates in the destination framebuffer.
		// The shader will sample and depending on the X coordinate & 1, use the upper or lower bits.
		*scaleFactorX = 2.0f;
	} else {
		*scaleFactorX = 1.0f;
	}

	Draw2DPipeline *pipeline = reinterpretFromTo_[(int)from][(int)to];
	if (!pipeline) {
		pipeline = draw2D_.Create2DPipeline([=](ShaderWriter &shaderWriter) -> Draw2DPipelineInfo {
			return GenerateReinterpretFragmentShader(shaderWriter, from, to);
		});
		reinterpretFromTo_[(int)from][(int)to] = pipeline;
	}
	return pipeline;
}

void FramebufferManagerCommon::DestroyFramebuf(VirtualFramebuffer *v) {
	// Notify the texture cache of both the color and depth buffers.
	textureCache_->NotifyFramebuffer(v, NOTIFY_FB_DESTROYED);
	if (v->fbo) {
		v->fbo->Release();
		v->fbo = nullptr;
	}

	// Wipe some pointers
	DiscardFramebufferCopy();
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

void FramebufferManagerCommon::BlitFramebufferDepth(VirtualFramebuffer *src, VirtualFramebuffer *dst, bool allowSizeMismatch) {
	_dbg_assert_(src && dst);

	_dbg_assert_(src != dst);

	// Check that the depth address is even the same before actually blitting.
	bool matchingDepthBuffer = src->z_address == dst->z_address && src->z_stride != 0 && dst->z_stride != 0;
	bool matchingSize = (src->width == dst->width || (src->width == 512 && dst->width == 480) || (src->width == 480 && dst->width == 512)) && src->height == dst->height;
	if (!matchingDepthBuffer || (!matchingSize && !allowSizeMismatch)) {
		return;
	}

	// Copy depth value from the previously bound framebuffer to the current one.
	bool hasNewerDepth = src->last_frame_depth_render != 0 && src->last_frame_depth_render >= dst->last_frame_depth_updated;
	if (!src->fbo || !dst->fbo || !useBufferedRendering_ || !hasNewerDepth) {
		// If depth wasn't updated, then we're at least "two degrees" away from the data.
		// This is an optimization: it probably doesn't need to be copied in this case.
		return;
	}

	bool useCopy = draw_->GetDeviceCaps().framebufferSeparateDepthCopySupported || (!draw_->GetDeviceCaps().framebufferDepthBlitSupported && draw_->GetDeviceCaps().framebufferCopySupported);
	bool useBlit = draw_->GetDeviceCaps().framebufferDepthBlitSupported;

	bool useRaster = draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported && draw_->GetDeviceCaps().textureDepthSupported;

	if (src->fbo->MultiSampleLevel() > 0 && dst->fbo->MultiSampleLevel() > 0) {
		// If multisampling, we want to copy depth properly so we get all the samples, to avoid aliased edges.
		// Can be seen in the fire in Jeanne D'arc, for example.
		if (useRaster && useCopy) {
			useRaster = false;
		}
	}

	int w = std::min(src->renderWidth, dst->renderWidth);
	int h = std::min(src->renderHeight, dst->renderHeight);

	// Some GPUs can copy depth but only if stencil gets to come along for the ride. We only want to use this if there is no blit functionality.
	if (useRaster) {
		BlitUsingRaster(src->fbo, 0, 0, w, h, dst->fbo, 0, 0, w, h, false, dst->renderScaleFactor, Get2DPipeline(Draw2DShader::DRAW2D_COPY_DEPTH), "BlitDepthRaster");
	} else if (useCopy) {
		draw_->CopyFramebufferImage(src->fbo, 0, 0, 0, 0, dst->fbo, 0, 0, 0, 0, w, h, 1, Draw::Aspect::DEPTH_BIT, "CopyFramebufferDepth");
		RebindFramebuffer("After BlitFramebufferDepth");
	} else if (useBlit) {
		// We'll accept whether we get a separate depth blit or not...
		draw_->BlitFramebuffer(src->fbo, 0, 0, w, h, dst->fbo, 0, 0, w, h, Draw::Aspect::DEPTH_BIT, Draw::FB_BLIT_NEAREST, "BlitFramebufferDepth");
		RebindFramebuffer("After BlitFramebufferDepth");
	}

	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
}

void FramebufferManagerCommon::NotifyRenderFramebufferCreated(VirtualFramebuffer *vfb) {
	if (!useBufferedRendering_) {
		// Let's ignore rendering to targets that have not (yet) been displayed.
		gstate_c.skipDrawReason |= SKIPDRAW_NON_DISPLAYED_FB;
	} else if (currentRenderVfb_) {
		DownloadFramebufferOnSwitch(currentRenderVfb_);
	}

	textureCache_->NotifyFramebuffer(vfb, NOTIFY_FB_CREATED);

	NotifyRenderFramebufferUpdated(vfb);
}

void FramebufferManagerCommon::NotifyRenderFramebufferUpdated(VirtualFramebuffer *vfb) {
	if (gstate_c.curRTWidth != vfb->width || gstate_c.curRTHeight != vfb->height) {
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX | DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
	}
	if (gstate_c.curRTRenderWidth != vfb->renderWidth || gstate_c.curRTRenderHeight != vfb->renderHeight) {
		gstate_c.Dirty(DIRTY_PROJMATRIX);
		gstate_c.Dirty(DIRTY_PROJTHROUGHMATRIX);
	}
}

void FramebufferManagerCommon::DownloadFramebufferOnSwitch(VirtualFramebuffer *vfb) {
	if (vfb && vfb->safeWidth > 0 && vfb->safeHeight > 0 && !(vfb->usageFlags & FB_USAGE_FIRST_FRAME_SAVED) && !vfb->memoryUpdated) {
		// Some games will draw to some memory once, and use it as a render-to-texture later.
		// To support this, we save the first frame to memory when we have a safe w/h.
		// Saving each frame would be slow.

		// TODO: This type of download could be made async, for less stutter on framebuffer creation.
		if (GetSkipGPUReadbackMode() == SkipGPUReadbackMode::NO_SKIP && !PSP_CoreParameter().compat.flags().DisableFirstFrameReadback) {
			ReadFramebufferToMemory(vfb, 0, 0, vfb->safeWidth, vfb->safeHeight, RASTER_COLOR, Draw::ReadbackMode::BLOCK);
			vfb->usageFlags = (vfb->usageFlags | FB_USAGE_DOWNLOAD | FB_USAGE_FIRST_FRAME_SAVED) & ~FB_USAGE_DOWNLOAD_CLEAR;
			vfb->safeWidth = 0;
			vfb->safeHeight = 0;
		}
	}
}

bool FramebufferManagerCommon::ShouldDownloadFramebufferColor(const VirtualFramebuffer *vfb) {
	// Dangan Ronpa hack
	return PSP_CoreParameter().compat.flags().Force04154000Download && vfb->fb_address == 0x04154000;
}

bool FramebufferManagerCommon::ShouldDownloadFramebufferDepth(const VirtualFramebuffer *vfb) {
	// Download depth buffer if compat flag set (previously used for Syphon Filter lens flares, now used for nothing)
	if (!PSP_CoreParameter().compat.flags().ReadbackDepth || GetSkipGPUReadbackMode() != SkipGPUReadbackMode::NO_SKIP) {
		return false;
	}
	return (vfb->usageFlags & FB_USAGE_RENDER_DEPTH) != 0 && vfb->width >= 480 && vfb->height >= 272;
}

void FramebufferManagerCommon::NotifyRenderFramebufferSwitched(VirtualFramebuffer *prevVfb, VirtualFramebuffer *vfb, bool isClearingDepth) {
	if (prevVfb) {
		if (ShouldDownloadFramebufferColor(prevVfb) && !prevVfb->memoryUpdated) {
			// NOTE: This path is ONLY for the Dangan Ronpa hack, see ShouldDownloadFramebufferColor
			ReadFramebufferToMemory(prevVfb, 0, 0, prevVfb->width, prevVfb->height, RASTER_COLOR, Draw::ReadbackMode::OLD_DATA_OK);
			prevVfb->usageFlags = (prevVfb->usageFlags | FB_USAGE_DOWNLOAD | FB_USAGE_FIRST_FRAME_SAVED) & ~FB_USAGE_DOWNLOAD_CLEAR;
		} else {
			DownloadFramebufferOnSwitch(prevVfb);
		}

		if (ShouldDownloadFramebufferDepth(prevVfb)) {
			ReadFramebufferToMemory(prevVfb, 0, 0, prevVfb->width, prevVfb->height, RasterChannel::RASTER_DEPTH, Draw::ReadbackMode::BLOCK);
		}
	}

	textureCache_->ForgetLastTexture();
	shaderManager_->DirtyLastShader();

	if (useBufferedRendering_) {
		if (vfb->fbo) {
			shaderManager_->DirtyLastShader();
			Draw::RPAction depthAction = Draw::RPAction::KEEP;
			float clearDepth = 0.0f;
			if (vfb->usageFlags & FB_USAGE_INVALIDATE_DEPTH) {
				depthAction = Draw::RPAction::CLEAR;
				clearDepth = GetDepthScaleFactors(gstate_c.UseFlags()).Offset();
				vfb->usageFlags &= ~FB_USAGE_INVALIDATE_DEPTH;
			}
			draw_->BindFramebufferAsRenderTarget(vfb->fbo, {Draw::RPAction::KEEP, depthAction, Draw::RPAction::KEEP, 0, clearDepth}, "FBSwitch");
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

	NotifyRenderFramebufferUpdated(vfb);
}

void FramebufferManagerCommon::PerformWriteFormattedFromMemory(u32 addr, int size, int stride, GEBufferFormat fmt) {
	// Note: UpdateFromMemory() is still called later.
	// This is a special case where we have extra information prior to the invalidation,
	// because it's called from sceJpeg, sceMpeg, scePsmf etc.

	// TODO: Could possibly be at an offset...
	// Also, stride needs better handling.
	VirtualFramebuffer *vfb = ResolveVFB(addr, stride, fmt);
	if (vfb) {
		// Let's count this as a "render".  This will also force us to use the correct format.
		vfb->last_frame_render = gpuStats.numFlips;
		vfb->colorBindSeq = GetBindSeqCount();

		if (vfb->fb_stride < stride) {
			INFO_LOG(Log::FrameBuf, "Changing stride for %08x from %d to %d", addr, vfb->fb_stride, stride);
			const int bpp = BufferFormatBytesPerPixel(fmt);
			ResizeFramebufFBO(vfb, stride, size / (bpp * stride));
			// Resizing may change the viewport/etc.
			gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
			vfb->fb_stride = stride;
			// This might be a bit wider than necessary, but we'll redetect on next render.
			vfb->width = stride;
		}
	}
}

void FramebufferManagerCommon::UpdateFromMemory(u32 addr, int size) {
	// Take off the uncached flag from the address. Not to be confused with the start of VRAM.
	addr &= 0x3FFFFFFF;
	if (Memory::IsVRAMAddress(addr))
		addr &= 0x041FFFFF;
	// TODO: Could go through all FBOs, but probably not important?
	// TODO: Could also check for inner changes, but video is most important.
	// TODO: This shouldn't care if it's a display framebuf or not, should work exactly the same.
	bool isDisplayBuf = addr == CurrentDisplayFramebufAddr() || addr == PrevDisplayFramebufAddr();
	// TODO: Deleting the FBO is a heavy hammer solution, so let's only do it if it'd help.
	if (!Memory::IsValidAddress(displayFramebufPtr_))
		return;

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		if (vfb->fb_address == addr) {
			FlushBeforeCopy();

			if (useBufferedRendering_ && vfb->fbo) {
				GEBufferFormat fmt = vfb->fb_format;
				if (vfb->last_frame_render + 1 < gpuStats.numFlips && isDisplayBuf) {
					// If we're not rendering to it, format may be wrong.  Use displayFormat_ instead.
					// TODO: This doesn't seem quite right anymore.
					fmt = displayFormat_;
				}
				DrawPixels(vfb, 0, 0, Memory::GetPointerUnchecked(addr), fmt, vfb->fb_stride, vfb->width, vfb->height, RASTER_COLOR, "UpdateFromMemory_DrawPixels");
				SetColorUpdated(vfb, gstate_c.skipDrawReason);
			} else {
				INFO_LOG(Log::FrameBuf, "Invalidating FBO for %08x (%dx%d %s)", vfb->fb_address, vfb->width, vfb->height, GeBufferFormatToString(vfb->fb_format));
				DestroyFramebuf(vfb);
				vfbs_.erase(vfbs_.begin() + i--);
			}
		}
	}

	RebindFramebuffer("RebindFramebuffer - UpdateFromMemory");

	// TODO: Necessary?
	gstate_c.Dirty(DIRTY_FRAGMENTSHADER_STATE);
}

void FramebufferManagerCommon::DrawPixels(VirtualFramebuffer *vfb, int dstX, int dstY, const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height, RasterChannel channel, const char *tag) {
	textureCache_->ForgetLastTexture();
	shaderManager_->DirtyLastShader();
	float u0 = 0.0f, u1 = 1.0f;
	float v0 = 0.0f, v1 = 1.0f;

	DrawTextureFlags flags;
	if (useBufferedRendering_ && vfb && vfb->fbo) {
		if (channel == RASTER_DEPTH || PSP_CoreParameter().compat.flags().NearestFilteringOnFramebufferCreate) {
			flags = DRAWTEX_NEAREST;
		} else {
			flags = DRAWTEX_LINEAR;
		}
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, tag);
		SetViewport2D(0, 0, vfb->renderWidth, vfb->renderHeight);
		draw_->SetScissorRect(0, 0, vfb->renderWidth, vfb->renderHeight);
	} else {
		_dbg_assert_(channel == RASTER_COLOR);
		// We are drawing directly to the back buffer so need to flip.
		// Should more of this be handled by the presentation engine?
		if (needBackBufferYSwap_)
			std::swap(v0, v1);
		flags = g_Config.iDisplayFilter == SCALE_LINEAR ? DRAWTEX_LINEAR : DRAWTEX_NEAREST;
		flags = flags | DRAWTEX_TO_BACKBUFFER;
		FRect frame = GetScreenFrame(pixelWidth_, pixelHeight_);
		FRect rc;
		CalculateDisplayOutputRect(&rc, 480.0f, 272.0f, frame, ROTATION_LOCKED_HORIZONTAL);
		SetViewport2D(rc.x, rc.y, rc.w, rc.h);
		draw_->SetScissorRect(0, 0, pixelWidth_, pixelHeight_);
	}

	if (channel == RASTER_DEPTH) {
		_dbg_assert_(srcPixelFormat == GE_FORMAT_DEPTH16);
		flags = flags | DRAWTEX_DEPTH;
		if (vfb)
			vfb->usageFlags |= FB_USAGE_COLOR_MIXED_DEPTH;
	}

	Draw::Texture *pixelsTex = MakePixelTexture(srcPixels, srcPixelFormat, srcStride, width, height);
	if (pixelsTex) {
		draw_->BindTextures(0, 1, &pixelsTex, Draw::TextureBindFlags::VULKAN_BIND_ARRAY);

		// TODO: Replace with draw2D_.Blit() directly.
		DrawActiveTexture(dstX, dstY, width, height,
			vfb ? vfb->bufferWidth : g_display.pixel_xres,
			vfb ? vfb->bufferHeight : g_display.pixel_yres,
			u0, v0, u1, v1, ROTATION_LOCKED_HORIZONTAL, flags);

		draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);

		gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
	}
}

bool FramebufferManagerCommon::BindFramebufferAsColorTexture(int stage, VirtualFramebuffer *framebuffer, int flags, int layer) {
	if (!framebuffer->fbo || !useBufferedRendering_) {
		draw_->BindTexture(stage, nullptr);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return false;
	}

	// currentRenderVfb_ will always be set when this is called, except from the GE debugger.
	// Let's just not bother with the copy in that case.
	bool skipCopy = !(flags & BINDFBCOLOR_MAY_COPY);

	// Currently rendering to this framebuffer. Need to make a copy.
	if (!skipCopy && framebuffer == currentRenderVfb_) {
		// Self-texturing, need a copy currently (some backends can potentially support it though).
		WARN_LOG_ONCE(selfTextureCopy, Log::G3D, "Attempting to texture from current render target (src=%08x / target=%08x / flags=%d), making a copy", framebuffer->fb_address, currentRenderVfb_->fb_address, flags);
		// TODO: Maybe merge with bvfbs_?  Not sure if those could be packing, and they're created at a different size.
		if (currentFramebufferCopy_ && (flags & BINDFBCOLOR_UNCACHED) == 0) {
			// We have a copy already that hasn't been invalidated, let's keep using it.
			draw_->BindFramebufferAsTexture(currentFramebufferCopy_, stage, Draw::Aspect::COLOR_BIT, layer);
			return true;
		}

		Draw::Framebuffer *renderCopy = GetTempFBO(TempFBO::COPY, framebuffer->renderWidth, framebuffer->renderHeight);
		if (renderCopy) {
			VirtualFramebuffer copyInfo = *framebuffer;
			copyInfo.fbo = renderCopy;

			bool partial = false;
			CopyFramebufferForColorTexture(&copyInfo, framebuffer, flags, layer, &partial);
			RebindFramebuffer("After BindFramebufferAsColorTexture");
			draw_->BindFramebufferAsTexture(renderCopy, stage, Draw::Aspect::COLOR_BIT, layer);

			// Only cache the copy if it wasn't a partial copy.
			// TODO: Improve on this.
			if (!partial && (flags & BINDFBCOLOR_UNCACHED) == 0) {
				currentFramebufferCopy_ = renderCopy;
			}
			gpuStats.numCopiesForSelfTex++;
		} else {
			// Failed to get temp FBO? Weird.
			draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::Aspect::COLOR_BIT, layer);
		}
		return true;
	} else if (framebuffer != currentRenderVfb_ || (flags & BINDFBCOLOR_FORCE_SELF) != 0) {
		draw_->BindFramebufferAsTexture(framebuffer->fbo, stage, Draw::Aspect::COLOR_BIT, layer);
		return true;
	} else {
		// Here it's an error because for some reason skipCopy is true. That shouldn't really happen.
		ERROR_LOG_REPORT_ONCE(selfTextureFail, Log::G3D, "Attempting to texture from target (src=%08x / target=%08x / flags=%d)", framebuffer->fb_address, currentRenderVfb_->fb_address, flags);
		// To do this safely in Vulkan, we need to use input attachments.
		// Actually if the texture region and render regions don't overlap, this is safe, but we need
		// to transition to GENERAL image layout which will take some trickery.
		// Badness on D3D11 to bind the currently rendered-to framebuffer as a texture.
		draw_->BindTexture(stage, nullptr);
		gstate_c.skipDrawReason |= SKIPDRAW_BAD_FB_TEXTURE;
		return false;
	}
}

void FramebufferManagerCommon::CopyFramebufferForColorTexture(VirtualFramebuffer *dst, VirtualFramebuffer *src, int flags, int layer, bool *partial) {
	int x = 0;
	int y = 0;
	int w = src->drawnWidth;
	int h = src->drawnHeight;

	*partial = false;

	// If max is not > min, we probably could not detect it.  Skip.
	// See the vertex decoder, where this is updated.
	// TODO: We're currently not hitting this path in Dante. See #17032
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
		if (x != 0 || y != 0 || w < src->drawnWidth || h < src->drawnHeight) {
			*partial = true;
		}
		BlitFramebuffer(dst, x, y, src, x, y, w, h, 0, RASTER_COLOR, "CopyFBForColorTexture");
	}
}

Draw::Texture *FramebufferManagerCommon::MakePixelTexture(const u8 *srcPixels, GEBufferFormat srcPixelFormat, int srcStride, int width, int height) {
	Draw::DataFormat depthFormat = Draw::DataFormat::UNDEFINED;

	int bpp = BufferFormatBytesPerPixel(srcPixelFormat);
	int srcStrideInBytes = srcStride * bpp;
	int widthInBytes = width * bpp;

	// Compute hash of contents.
	uint64_t imageHash;
	if (widthInBytes == srcStrideInBytes) {
		imageHash = XXH3_64bits(srcPixels, widthInBytes * height);
	} else {
		XXH3_state_t *hashState = XXH3_createState();
		XXH3_64bits_reset(hashState);
		for (int y = 0; y < height; y++) {
			XXH3_64bits_update(hashState, srcPixels + srcStrideInBytes * y, widthInBytes);
		}
		imageHash = XXH3_64bits_digest(hashState);
		XXH3_freeState(hashState);
	}

	Draw::DataFormat texFormat = preferredPixelsFormat_;

	if (srcPixelFormat == GE_FORMAT_DEPTH16) {
		if ((draw_->GetDataFormatSupport(Draw::DataFormat::R16_UNORM) & Draw::FMT_TEXTURE) != 0) {
			texFormat = Draw::DataFormat::R16_UNORM;
		} else if ((draw_->GetDataFormatSupport(Draw::DataFormat::R8_UNORM) & Draw::FMT_TEXTURE) != 0) {
			// This could be improved by using specific draw shaders to pack full precision in two channels.
			// However, not really worth the trouble until we find a game that requires it.
			texFormat = Draw::DataFormat::R8_UNORM;
		} else {
			// No usable single channel format. Can't be bothered.
			return nullptr;
		}
	} else if (srcPixelFormat == GE_FORMAT_565) {
		// Check for supported matching formats.
		// This mainly benefits the redundant copies in God of War on low-end platforms.
		if ((draw_->GetDataFormatSupport(Draw::DataFormat::B5G6R5_UNORM_PACK16) & Draw::FMT_TEXTURE) != 0) {
			texFormat = Draw::DataFormat::B5G6R5_UNORM_PACK16;
		} else if ((draw_->GetDataFormatSupport(Draw::DataFormat::R5G6B5_UNORM_PACK16) & Draw::FMT_TEXTURE) != 0) {
			texFormat = Draw::DataFormat::R5G6B5_UNORM_PACK16;
		}
	}

	// TODO: We can just change the texture format and flip some bits around instead of this.
	// Could share code with the texture cache perhaps.
	auto generateTexture = [&](uint8_t *data, const uint8_t *initData, uint32_t w, uint32_t h, uint32_t d, uint32_t byteStride, uint32_t sliceByteStride) {
		for (int y = 0; y < height; y++) {
			const u16_le *src16 = (const u16_le *)srcPixels + srcStride * y;
			const u32_le *src32 = (const u32_le *)srcPixels + srcStride * y;
			u32 *dst = (u32 *)(data + byteStride * y);
			u16 *dst16 = (u16 *)(data + byteStride * y);
			u8 *dst8 = (u8 *)(data + byteStride * y);
			switch (srcPixelFormat) {
			case GE_FORMAT_565:
				if (texFormat == Draw::DataFormat::B5G6R5_UNORM_PACK16) {
					memcpy(dst16, src16, w * sizeof(uint16_t));
				} else if (texFormat == Draw::DataFormat::R5G6B5_UNORM_PACK16) {
					ConvertRGB565ToBGR565(dst16, src16, width);  // Fast!
				} else if (texFormat == Draw::DataFormat::B8G8R8A8_UNORM) {
					ConvertRGB565ToBGRA8888(dst, src16, width);
				} else {
					ConvertRGB565ToRGBA8888(dst, src16, width);
				}
				break;

			case GE_FORMAT_5551:
				if (texFormat == Draw::DataFormat::B8G8R8A8_UNORM)
					ConvertRGBA5551ToBGRA8888(dst, src16, width);
				else
					ConvertRGBA5551ToRGBA8888(dst, src16, width);
				break;

			case GE_FORMAT_4444:
				if (texFormat == Draw::DataFormat::B8G8R8A8_UNORM)
					ConvertRGBA4444ToBGRA8888(dst, src16, width);
				else
					ConvertRGBA4444ToRGBA8888(dst, src16, width);
				break;

			case GE_FORMAT_8888:
				if (texFormat == Draw::DataFormat::B8G8R8A8_UNORM)
					ConvertRGBA8888ToBGRA8888(dst, src32, width);
				// This means use original pointer as-is.  May avoid or optimize a copy.
				else if (srcStride == width)
					return false;
				else
					memcpy(dst, src32, width * 4);
				break;

			case GE_FORMAT_DEPTH16:
				// TODO: Must take the depth range into account, unless it's already 0-1.
				// TODO: Depending on the color buffer format used with this depth buffer, we need
				// to do one of two different swizzle operations. However, for the only use of this so far,
				// the Burnout lens flare trickery, swizzle doesn't matter since it's just a 0, 7fff, 0, 7fff pattern
				// which comes out the same.
				if (texFormat == Draw::DataFormat::R16_UNORM) {
					// We just use this format straight.
					memcpy(dst16, src16, w * 2);
				} else if (texFormat == Draw::DataFormat::R8_UNORM) {
					// We fall back to R8_UNORM. Precision is enough for most cases of depth clearing and initialization we've seen,
					// but hardly ideal.
					for (int i = 0; i < width; i++) {
						dst8[i] = src16[i] >> 8;
					}
				}
				break;

			case GE_FORMAT_INVALID:
			case GE_FORMAT_CLUT8:
				// Bad
				break;
			}
		}
		return true;
	};

	int frameNumber = draw_->GetFrameCount();

	// First look for an exact match (including contents hash) that we can re-use.
	for (auto &iter : drawPixelsCache_) {
		if (iter.contentsHash == imageHash && iter.tex->Width() == width && iter.tex->Height() == height && iter.tex->Format() == texFormat) {
			iter.frameNumber = frameNumber;
			gpuStats.numCachedUploads++;
			return iter.tex;
		}
	}

	// Then, look for an alternative one that's not been used recently that we can overwrite.
	for (auto &iter : drawPixelsCache_) {
		if (iter.frameNumber >= frameNumber - 3 || iter.tex->Width() != width || iter.tex->Height() != height || iter.tex->Format() != texFormat) {
			continue;
		}

		// OK, current one seems good, let's use it (and mark it used).
		gpuStats.numUploads++;
		draw_->UpdateTextureLevels(iter.tex, &srcPixels, generateTexture, 1);
		// NOTE: numFlips is no good - this is called every frame when paused sometimes!
		iter.frameNumber = frameNumber;
		// We need to update the hash for future matching.
		iter.contentsHash = imageHash;
		return iter.tex;
	}

	// Note: For depth, we create an R16_UNORM texture, that'll be just fine for uploading depth through a shader,
	// and likely more efficient.
	Draw::TextureDesc desc{
		Draw::TextureType::LINEAR2D,
		texFormat,
		width,
		height,
		1,
		1,
		false,
		Draw::TextureSwizzle::DEFAULT,
		"DrawPixels",
		{ (uint8_t *)srcPixels },
		generateTexture,
	};

	// Hot Shots Golf (#12355) does tons of these in a frame in some situations! So creating textures
	// better be fast. So does God of War, a lot of the time, a bit unclear what it's doing.
	Draw::Texture *tex = draw_->CreateTexture(desc);
	if (!tex) {
		ERROR_LOG(Log::G3D, "Failed to create DrawPixels texture");
	}
	// We don't need to count here, already counted by numUploads by the caller.

	// INFO_LOG(Log::G3D, "Creating drawPixelsCache texture: %dx%d", tex->Width(), tex->Height());

	DrawPixelsEntry entry{ tex, imageHash, frameNumber };
	drawPixelsCache_.push_back(entry);
	gpuStats.numUploads++;
	return tex;
}

bool FramebufferManagerCommon::DrawFramebufferToOutput(const u8 *srcPixels, int srcStride, GEBufferFormat srcPixelFormat) {
	textureCache_->ForgetLastTexture();
	shaderManager_->DirtyLastShader();

	float u0 = 0.0f, u1 = 480.0f / 512.0f;
	float v0 = 0.0f, v1 = 1.0f;
	Draw::Texture *pixelsTex = MakePixelTexture(srcPixels, srcPixelFormat, srcStride, 512, 272);
	if (!pixelsTex)
		return false;

	int uvRotation = useBufferedRendering_ ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
	OutputFlags flags = g_Config.iDisplayFilter == SCALE_LINEAR ? OutputFlags::LINEAR : OutputFlags::NEAREST;
	if (needBackBufferYSwap_) {
		flags |= OutputFlags::BACKBUFFER_FLIPPED;
	}
	// CopyToOutput reverses these, probably to match "up".
	if (GetGPUBackend() == GPUBackend::DIRECT3D11) {
		flags |= OutputFlags::POSITION_FLIPPED;
	}

	presentation_->UpdateUniforms(textureCache_->VideoIsPlaying());
	presentation_->SourceTexture(pixelsTex, 512, 272);
	presentation_->CopyToOutput(flags, uvRotation, u0, v0, u1, v1);

	// PresentationCommon sets all kinds of state, we can't rely on anything.
	gstate_c.Dirty(DIRTY_ALL);

	DiscardFramebufferCopy();
	currentRenderVfb_ = nullptr;

	return true;
}

void FramebufferManagerCommon::SetViewport2D(int x, int y, int w, int h) {
	Draw::Viewport viewport{ (float)x, (float)y, (float)w, (float)h, 0.0f, 1.0f };
	draw_->SetViewport(viewport);
}

void FramebufferManagerCommon::CopyDisplayToOutput(bool reallyDirty) {
	DownloadFramebufferOnSwitch(currentRenderVfb_);
	shaderManager_->DirtyLastShader();

	if (displayFramebufPtr_ == 0) {
		if (GetUIState() != UISTATE_PAUSEMENU) {
			if (Core_IsStepping())
				VERBOSE_LOG(Log::FrameBuf, "Display disabled, displaying only black");
			else
				DEBUG_LOG(Log::FrameBuf, "Display disabled, displaying only black");
		}
		// No framebuffer to display! Clear to black.
		if (useBufferedRendering_) {
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR }, "CopyDisplayToOutput");
		}
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
		presentation_->NotifyPresent();
		return;
	}

	u32 offsetX = 0;
	u32 offsetY = 0;

	// If it's not really dirty, we're probably frameskipping.  Use the last working one.
	u32 fbaddr = reallyDirty ? displayFramebufPtr_ : prevDisplayFramebufPtr_;
	prevDisplayFramebufPtr_ = fbaddr;

	VirtualFramebuffer *vfb = ResolveVFB(fbaddr, displayStride_, displayFormat_);
	if (!vfb) {
		// Let's search for a framebuf within this range. Note that we also look for
		// "framebuffers" sitting in RAM (created from block transfer or similar) so we only take off the kernel
		// and uncached bits of the address when comparing.
		const u32 addr = fbaddr;
		for (auto v : vfbs_) {
			const u32 v_addr = v->fb_address;
			const u32 v_size = v->BufferByteSize(RASTER_COLOR);

			if (v->fb_format != displayFormat_ || v->fb_stride != displayStride_) {
				// Displaying a buffer of the wrong format or stride is nonsense, ignore it.
				continue;
			}

			if (addr >= v_addr && addr < v_addr + v_size) {
				const u32 dstBpp = BufferFormatBytesPerPixel(v->fb_format);
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
			INFO_LOG_REPORT_ONCE(displayoffset, Log::FrameBuf, "Rendering from framebuf with offset %08x -> %08x+%dx%d", addr, vfb->fb_address, offsetX, offsetY);
		}
	}

	// Reject too-tiny framebuffers to display (Godfather, see issue #16915).
	if (vfb && vfb->height < 64) {
		vfb = nullptr;
	}

	if (!vfb) {
		if (Memory::IsValidAddress(fbaddr)) {
			// The game is displaying something directly from RAM. In GTA, it's decoded video.
			// If successful, this effectively calls presentation_->NotifyPresent();
			if (!DrawFramebufferToOutput(Memory::GetPointerUnchecked(fbaddr), displayStride_, displayFormat_)) {
				if (useBufferedRendering_) {
					// Bind and clear the backbuffer. This should be the first time during the frame that it's bound.
					draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR }, "CopyDisplayToOutput_DrawError");
				}
				presentation_->NotifyPresent();
			}
			return;
		} else {
			DEBUG_LOG(Log::FrameBuf, "Found no FBO to display! displayFBPtr = %08x", fbaddr);
			// No framebuffer to display! Clear to black.
			if (useBufferedRendering_) {
				// Bind and clear the backbuffer. This should be the first time during the frame that it's bound.
				draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR }, "CopyDisplayToOutput_NoFBO");
			} // For non-buffered rendering, every frame is cleared anyway.
			gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
			presentation_->NotifyPresent();
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
		if (GetUIState() != UISTATE_PAUSEMENU) {
			if (Core_IsStepping())
				VERBOSE_LOG(Log::FrameBuf, "Displaying FBO %08x", vfb->fb_address);
			else
				DEBUG_LOG(Log::FrameBuf, "Displaying FBO %08x", vfb->fb_address);
		}

		float u0 = offsetX / (float)vfb->bufferWidth;
		float v0 = offsetY / (float)vfb->bufferHeight;
		float u1 = (480.0f + offsetX) / (float)vfb->bufferWidth;
		float v1 = (272.0f + offsetY) / (float)vfb->bufferHeight;

		//clip the VR framebuffer to keep the aspect ratio
		if (IsVREnabled() && !IsFlatVRGame() && !IsGameVRScene()) {
			float aspect = 272.0f / 480.0f * (IsImmersiveVRMode() ? 2.0f : 1.0f);
			float clipY = 272.0f * (1.0f - aspect) / 2.0f;
			v0 = (clipY + offsetY) / (float)vfb->bufferHeight;
			v1 = (272.0f - clipY + offsetY) / (float)vfb->bufferHeight;

			//zoom inside
			float zoom = IsImmersiveVRMode() ? 0.4f : 0.1f;
			u0 += zoom / aspect;
			u1 -= zoom / aspect;
			v0 += zoom;
			v1 -= zoom;
		}

		textureCache_->ForgetLastTexture();

		int uvRotation = useBufferedRendering_ ? g_Config.iInternalScreenRotation : ROTATION_LOCKED_HORIZONTAL;
		OutputFlags flags = g_Config.iDisplayFilter == SCALE_LINEAR ? OutputFlags::LINEAR : OutputFlags::NEAREST;
		if (needBackBufferYSwap_) {
			flags |= OutputFlags::BACKBUFFER_FLIPPED;
		}
		// DrawActiveTexture reverses these, probably to match "up".
		if (GetGPUBackend() == GPUBackend::DIRECT3D11) {
			flags |= OutputFlags::POSITION_FLIPPED;
		}

		int actualWidth = (vfb->bufferWidth * vfb->renderWidth) / vfb->width;
		int actualHeight = (vfb->bufferHeight * vfb->renderHeight) / vfb->height;
		presentation_->UpdateUniforms(textureCache_->VideoIsPlaying());
		presentation_->SourceFramebuffer(vfb->fbo, actualWidth, actualHeight);
		presentation_->CopyToOutput(flags, uvRotation, u0, v0, u1, v1);
	} else if (useBufferedRendering_) {
		WARN_LOG(Log::FrameBuf, "Using buffered rendering, and current VFB lacks an FBO: %08x", vfb->fb_address);
	} else {
		presentation_->NotifyPresent();
	}

	// This may get called mid-draw if the game uses an immediate flip.
	// PresentationCommon sets all kinds of state, we can't rely on anything.
	gstate_c.Dirty(DIRTY_ALL);
	DiscardFramebufferCopy();
	currentRenderVfb_ = nullptr;
}

void FramebufferManagerCommon::DecimateFBOs() {
	DiscardFramebufferCopy();
	currentRenderVfb_ = nullptr;

	for (auto iter : fbosToDelete_) {
		iter->Release();
	}
	fbosToDelete_.clear();

	for (size_t i = 0; i < vfbs_.size(); ++i) {
		VirtualFramebuffer *vfb = vfbs_[i];
		int age = frameLastFramebufUsed_ - std::max(vfb->last_frame_render, vfb->last_frame_used);

		if (ShouldDownloadFramebufferColor(vfb) && age == 0 && !vfb->memoryUpdated) {
			ReadFramebufferToMemory(vfb, 0, 0, vfb->width, vfb->height, RASTER_COLOR, Draw::ReadbackMode::BLOCK);
			vfb->usageFlags = (vfb->usageFlags | FB_USAGE_DOWNLOAD | FB_USAGE_FIRST_FRAME_SAVED) & ~FB_USAGE_DOWNLOAD_CLEAR;
		}

		// Let's also "decimate" the usageFlags.
		UpdateFramebufUsage(vfb);

		if (vfb != displayFramebuf_ && vfb != prevDisplayFramebuf_ && vfb != prevPrevDisplayFramebuf_) {
			if (age > FBO_OLD_AGE) {
				INFO_LOG(Log::FrameBuf, "Decimating FBO for %08x (%ix%i %s), age %i", vfb->fb_address, vfb->width, vfb->height, GeBufferFormatToString(vfb->fb_format), age);
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
			INFO_LOG(Log::FrameBuf, "Decimating FBO for %08x (%dx%d %s), age %i", vfb->fb_address, vfb->width, vfb->height, GeBufferFormatToString(vfb->fb_format), age);
			DestroyFramebuf(vfb);
			bvfbs_.erase(bvfbs_.begin() + i--);
		}
	}

	// And DrawPixels cached textures.

	for (auto it = drawPixelsCache_.begin(); it != drawPixelsCache_.end(); ) {
		int age = draw_->GetFrameCount() - it->frameNumber;
		if (age > 10) {
			// INFO_LOG(Log::G3D, "Releasing drawPixelsCache texture: %dx%d", it->tex->Width(), it->tex->Height());
			it->tex->Release();
			it->tex = nullptr;
			it = drawPixelsCache_.erase(it);
		} else {
			++it;
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

	if ((vfb->usageFlags & FB_USAGE_COLOR_MIXED_DEPTH) && !PSP_CoreParameter().compat.flags().ForceLowerResolutionForEffectsOn) {
		force1x = false;
	}
	if (PSP_CoreParameter().compat.flags().Force04154000Download && vfb->fb_address == 0x04154000) {
		force1x = true;
	}

	if (force1x && g_Config.iInternalResolution != 1) {
		vfb->renderScaleFactor = 1;
		vfb->renderWidth = vfb->bufferWidth;
		vfb->renderHeight = vfb->bufferHeight;
	} else {
		vfb->renderScaleFactor = renderScaleFactor_;
		vfb->renderWidth = (u16)(vfb->bufferWidth * renderScaleFactor_);
		vfb->renderHeight = (u16)(vfb->bufferHeight * renderScaleFactor_);
	}

	bool creating = old.bufferWidth == 0;
	if (creating) {
		INFO_LOG(Log::FrameBuf, "Creating %s FBO at %08x/%08x stride=%d %dx%d (force=%d)", GeBufferFormatToString(vfb->fb_format), vfb->fb_address, vfb->z_address, vfb->fb_stride, vfb->bufferWidth, vfb->bufferHeight, (int)force);
	} else {
		INFO_LOG(Log::FrameBuf, "Resizing %s FBO at %08x/%08x stride=%d from %dx%d to %dx%d (force=%d, skipCopy=%d)", GeBufferFormatToString(vfb->fb_format), vfb->fb_address, vfb->z_address, vfb->fb_stride, old.bufferWidth, old.bufferHeight, vfb->bufferWidth, vfb->bufferHeight, (int)force, (int)skipCopy);
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
	size_t len = FormatFramebufferName(vfb, tag, sizeof(tag));

	gpuStats.numFBOsCreated++;

	vfb->fbo = draw_->CreateFramebuffer({ vfb->renderWidth, vfb->renderHeight, 1, GetFramebufferLayers(), msaaLevel_, true, tag });
	if (Memory::IsVRAMAddress(vfb->fb_address) && vfb->fb_stride != 0) {
		NotifyMemInfo(MemBlockFlags::ALLOC, vfb->fb_address, vfb->BufferByteSize(RASTER_COLOR), tag, len);
	}
	if (Memory::IsVRAMAddress(vfb->z_address) && vfb->z_stride != 0) {
		char buf[128];
		size_t len = snprintf(buf, sizeof(buf), "Z_%s", tag);
		NotifyMemInfo(MemBlockFlags::ALLOC, vfb->z_address, vfb->z_stride * vfb->height * sizeof(uint16_t), buf, len);
	}
	if (old.fbo) {
		INFO_LOG(Log::FrameBuf, "Resizing FBO for %08x : %dx%dx%s", vfb->fb_address, w, h, GeBufferFormatToString(vfb->fb_format));
		if (vfb->fbo) {
			draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR }, "ResizeFramebufFBO");
			if (!skipCopy) {
				BlitFramebuffer(vfb, 0, 0, &old, 0, 0, std::min((u16)oldWidth, std::min(vfb->bufferWidth, vfb->width)), std::min((u16)oldHeight, std::min(vfb->height, vfb->bufferHeight)), 0, RASTER_COLOR, "BlitColor_ResizeFramebufFBO");
			}
			if (vfb->usageFlags & FB_USAGE_RENDER_DEPTH) {
				BlitFramebuffer(vfb, 0, 0, &old, 0, 0, std::min((u16)oldWidth, std::min(vfb->bufferWidth, vfb->width)), std::min((u16)oldHeight, std::min(vfb->height, vfb->bufferHeight)), 0, RASTER_DEPTH, "BlitDepth_ResizeFramebufFBO");
			}
		}
		fbosToDelete_.push_back(old.fbo);
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "ResizeFramebufFBO");
	} else {
		draw_->BindFramebufferAsRenderTarget(vfb->fbo, { Draw::RPAction::CLEAR, Draw::RPAction::CLEAR, Draw::RPAction::CLEAR }, "ResizeFramebufFBO");
	}
	DiscardFramebufferCopy();
	currentRenderVfb_ = vfb;

	if (!vfb->fbo) {
		ERROR_LOG(Log::FrameBuf, "Error creating FBO during resize! %dx%d", vfb->renderWidth, vfb->renderHeight);
		vfb->last_frame_failed = gpuStats.numFlips;
	}
}

struct CopyCandidate {
	VirtualFramebuffer *vfb = nullptr;
	int y = 0;
	int h = 0;

	std::string ToString(RasterChannel channel) const {
		return StringFromFormat("%08x %s %dx%d y=%d h=%d", vfb->Address(channel), GeBufferFormatToString(vfb->Format(channel)), vfb->width, vfb->height, y, h);
	}
};

static const CopyCandidate *GetBestCopyCandidate(const TinySet<CopyCandidate, 4> &candidates, uint32_t basePtr, RasterChannel channel) {
	const CopyCandidate *best = nullptr;

	// Pick the "best" candidate by comparing to the old best using heuristics.
	for (size_t i = 0; i < candidates.size(); i++) {
		const CopyCandidate *candidate = &candidates[i];

		bool better = !best;
		if (!better) {
			// Heuristics determined from the old algorithm, that we might want to keep:
			// * Lower yOffsets are prioritized.
			// * Bindseq
			better = candidate->y < best->y;
			if (!better) {
				better = candidate->vfb->BindSeq(channel) > best->vfb->BindSeq(channel);
			}
		}

		if (better) {
			best = candidate;
		}
	}
	return best;
}

// This is called from detected memcopies and framebuffer initialization from VRAM. Not block transfers.
// Also with specialized flags from some replacement functions. Only those will currently request depth copies!
// NOTE: This is very tricky because there's no information about color depth here, so we'll have to make guesses
// about what underlying framebuffer is the most likely to be the relevant ones. For src, we can probably prioritize recent
// ones. For dst, less clear.
bool FramebufferManagerCommon::NotifyFramebufferCopy(u32 src, u32 dst, int size, GPUCopyFlag flags, u32 skipDrawReason) {
	if (size == 0) {
		return false;
	}

	dst &= 0x3FFFFFFF;
	src &= 0x3FFFFFFF;

	if (Memory::IsVRAMAddress(dst))
		dst &= 0x041FFFFF;
	if (Memory::IsVRAMAddress(src))
		src &= 0x041FFFFF;

	// TODO: Merge the below into FindTransferFramebuffer.
	// Or at least this should be like the other ones, gathering possible candidates
	// with the ability to list them out for debugging.

	bool ignoreDstBuffer = flags & GPUCopyFlag::FORCE_DST_MATCH_MEM;
	bool ignoreSrcBuffer = flags & (GPUCopyFlag::FORCE_SRC_MATCH_MEM | GPUCopyFlag::MEMSET);

	// TODO: In the future we should probably check both channels. Currently depth is only on request.
	RasterChannel channel = (flags & GPUCopyFlag::DEPTH_REQUESTED) ? RASTER_DEPTH : RASTER_COLOR;

	TinySet<CopyCandidate, 4> srcCandidates;
	TinySet<CopyCandidate, 4> dstCandidates;

	// TODO: These two loops should be merged into one utility function, similar to what's done with rectangle copies.

	// First find candidates for the source.
	// We only look at the color channel for now.
	for (auto vfb : vfbs_) {
		if (vfb->fb_stride == 0 || ignoreSrcBuffer) {
			continue;
		}

		// We only remove the kernel and uncached bits when comparing.
		const u32 vfb_address = vfb->Address(channel);
		const u32 vfb_size = vfb->BufferByteSize(channel);
		const u32 vfb_byteStride = vfb->BufferByteStride(channel);
		const int vfb_byteWidth = vfb->BufferByteWidth(channel);

		CopyCandidate srcCandidate;
		srcCandidate.vfb = vfb;

		// Special path for depth for now.
		if (channel == RASTER_DEPTH) {
			if (src == vfb->z_address && size == vfb->z_stride * 2 * vfb->height) {
				srcCandidate.y = 0;
				srcCandidate.h = vfb->height;
				srcCandidates.push_back(srcCandidate);
			}
			continue;
		}

		if (src >= vfb_address && (src + size <= vfb_address + vfb_size || src == vfb_address)) {
			// Heuristic originally from dest below, but just as valid looking for the source.
			// Fixes a misdetection in Brothers in Arms: D-Day, issue #18512.
			if (vfb_address == dst && ((size == 0x44000 && vfb_size == 0x88000) || (size == 0x88000 && vfb_size == 0x44000))) {
				// Not likely to be a correct color format copy for this buffer. Ignore it, there will either be RAM
				// that can be displayed from, or another matching buffer with the right format if rendering is going on.
				// If we had scoring here, we should strongly penalize this target instead of ignoring it.
				WARN_LOG_N_TIMES(notify_copy_2x, 5, Log::FrameBuf, "Framebuffer size %08x conspicuously not matching copy size %08x for source in NotifyFramebufferCopy. Ignoring.", size, vfb_size);
				continue;
			}

			if ((u32)size > vfb_size + 0x1000 && vfb->fb_format != GE_FORMAT_8888 && vfb->last_frame_render < gpuStats.numFlips) {
				// Seems likely we are looking at a potential copy of 32-bit pixels (like video) to an old 16-bit buffer,
				// which is very likely simply the wrong target, so skip it. See issue #17740 where this happens in Naruto Ultimate Ninja Heroes 2.
				// Probably no point to give it a bad score and let it pass to sorting, as we're pretty sure here.
				WARN_LOG_N_TIMES(notify_copy_2x, 5, Log::FrameBuf, "Framebuffer size %08x too small for %08x bytes of data and also 16-bit (%s), and not rendered to this frame. Ignoring.", vfb_size, size, GeBufferFormatToString(vfb->fb_format));
				continue;
			}

			const u32 offset = src - vfb_address;
			const u32 yOffset = offset / vfb_byteStride;
			if ((offset % vfb_byteStride) == 0 && (size == vfb_byteWidth || (size % vfb_byteStride) == 0)) {
				srcCandidate.y = yOffset;
				srcCandidate.h = size == vfb_byteWidth ? 1 : std::min((u32)size / vfb_byteStride, (u32)vfb->height);
			} else if ((offset % vfb_byteStride) == 0 && size == vfb->fb_stride) {
				// Valkyrie Profile reads 512 bytes at a time, rather than 2048.  So, let's whitelist fb_stride also.
				srcCandidate.y = yOffset;
				srcCandidate.h = 1;
			} else if (yOffset == 0 && (vfb->usageFlags & FB_USAGE_CLUT)) {
				// Okay, last try - it might be a clut.
				srcCandidate.y = yOffset;
				srcCandidate.h = 1;
			} else {
				continue;
			}
			srcCandidates.push_back(srcCandidate);
		}
	}

	for (auto vfb : vfbs_) {
		if (vfb->fb_stride == 0 || ignoreDstBuffer) {
			continue;
		}

		// We only remove the kernel and uncached bits when comparing.
		const u32 vfb_address = vfb->Address(channel);
		const u32 vfb_size = vfb->BufferByteSize(channel);
		const u32 vfb_byteStride = vfb->BufferByteStride(channel);
		const int vfb_byteWidth = vfb->BufferByteWidth(channel);

		// Heuristic to try to prevent potential glitches with video playback.
		if (vfb_address == dst && ((size == 0x44000 && vfb_size == 0x88000) || (size == 0x88000 && vfb_size == 0x44000))) {
			// Not likely to be a correct color format copy for this buffer. Ignore it, there will either be RAM
			// that can be displayed from, or another matching buffer with the right format if rendering is going on.
			// If we had scoring here, we should strongly penalize this target instead of ignoring it.
			WARN_LOG_N_TIMES(notify_copy_2x, 5, Log::FrameBuf, "Framebuffer size %08x conspicuously not matching copy size %08x for dest in NotifyFramebufferCopy. Ignoring.", size, vfb_size);
			continue;
		}

		CopyCandidate dstCandidate;
		dstCandidate.vfb = vfb;

		// Special path for depth for now.
		if (channel == RASTER_DEPTH) {
			// Let's assume exact matches only for simplicity.
			if (dst == vfb->z_address && size == vfb->z_stride * 2 * vfb->height) {
				dstCandidate.y = 0;
				dstCandidate.h = vfb->height;
				dstCandidates.push_back(dstCandidate);
			}
			continue;
		}

		if (!ignoreDstBuffer && dst >= vfb_address && (dst + size <= vfb_address + vfb_size || dst == vfb_address)) {
			const u32 offset = dst - vfb_address;
			const u32 yOffset = offset / vfb_byteStride;
			if ((offset % vfb_byteStride) == 0 && (size == vfb_byteWidth || (size % vfb_byteStride) == 0)) {
				dstCandidate.y = yOffset;
				dstCandidate.h = (size == vfb_byteWidth) ? 1 : std::min((u32)size / vfb_byteStride, (u32)vfb->height);
				dstCandidates.push_back(dstCandidate);
			}
		}
	}

	// For now fill in these old variables from the candidates to reduce the initial diff.
	VirtualFramebuffer *dstBuffer = nullptr;
	VirtualFramebuffer *srcBuffer = nullptr;
	int srcY;
	int srcH;
	int dstY;
	int dstH;

	const CopyCandidate *bestSrc = GetBestCopyCandidate(srcCandidates, src, channel);
	if (bestSrc) {
		srcBuffer = bestSrc->vfb;
		srcY = bestSrc->y;
		srcH = bestSrc->h;
	}
	const CopyCandidate *bestDst = GetBestCopyCandidate(dstCandidates, dst, channel);
	if (bestDst) {
		dstBuffer = bestDst->vfb;
		dstY = bestDst->y;
		dstH = bestDst->h;
	}

	if (srcCandidates.size() > 1) {
		if (Reporting::ShouldLogNTimes("mulblock", 5)) {
			std::string log;
			for (size_t i = 0; i < srcCandidates.size(); i++) {
				log += " - " + srcCandidates[i].ToString(channel);
				if (bestSrc && srcCandidates[i].vfb == bestSrc->vfb) {
					log += " * \n";
				} else {
					log += "\n";
				}
			}
			WARN_LOG(Log::FrameBuf, "Copy: Multiple src vfb candidates for (src: %08x, size: %d):\n%s (%s)", src, size, log.c_str(), RasterChannelToString(channel));
		}
	}

	if (dstCandidates.size() > 1) {
		if (Reporting::ShouldLogNTimes("mulblock", 5)) {
			std::string log;
			for (size_t i = 0; i < dstCandidates.size(); i++) {
				log += " - " + dstCandidates[i].ToString(channel);
				if (bestDst && dstCandidates[i].vfb == bestDst->vfb) {
					log += " * \n";
				} else {
					log += "\n";
				}
			}
			WARN_LOG(Log::FrameBuf, "Copy: Multiple dst vfb candidates for (dst: %08x, size: %d):\n%s (%s)", src, size, log.c_str(), RasterChannelToString(channel));
		}
	}

	if (!useBufferedRendering_) {
		// If we're copying into a recently used display buf, it's probably destined for the screen.
		if (channel == RASTER_DEPTH || srcBuffer || (dstBuffer != displayFramebuf_ && dstBuffer != prevDisplayFramebuf_)) {
			return false;
		}
	}

	if (!dstBuffer && srcBuffer && channel != RASTER_DEPTH) {
		// Note - if we're here, we're in a memcpy, not a block transfer. Not allowing IntraVRAMBlockTransferAllowCreateFB.
		// Technically, that makes BlockTransferAllowCreateFB a bit of a misnomer.
		bool allowCreateFB = (PSP_CoreParameter().compat.flags().BlockTransferAllowCreateFB || GetSkipGPUReadbackMode() == SkipGPUReadbackMode::COPY_TO_TEXTURE);
		if (allowCreateFB && !(flags & GPUCopyFlag::DISALLOW_CREATE_VFB)) {
			dstBuffer = CreateRAMFramebuffer(dst, srcBuffer->width, srcBuffer->height, srcBuffer->fb_stride, srcBuffer->fb_format);
			dstY = 0;
		}
	}
	if (dstBuffer) {
		dstBuffer->last_frame_used = gpuStats.numFlips;
		if (channel == RASTER_DEPTH && !srcBuffer)
			dstBuffer->usageFlags |= FB_USAGE_COLOR_MIXED_DEPTH;
	}
	if (srcBuffer && channel == RASTER_DEPTH && !dstBuffer)
		srcBuffer->usageFlags |= FB_USAGE_COLOR_MIXED_DEPTH;

	if (dstBuffer && srcBuffer) {
		if (srcBuffer == dstBuffer) {
			WARN_LOG_ONCE(dstsrccpy, Log::FrameBuf, "Intra-buffer memcpy (not supported) %08x -> %08x (size: %x)", src, dst, size);
		} else {
			WARN_LOG_ONCE(dstnotsrccpy, Log::FrameBuf, "Inter-buffer memcpy %08x -> %08x (size: %x)", src, dst, size);
			// Just do the blit!
			BlitFramebuffer(dstBuffer, 0, dstY, srcBuffer, 0, srcY, srcBuffer->width, srcH, 0, channel, "Blit_InterBufferMemcpy");
			SetColorUpdated(dstBuffer, skipDrawReason);
			RebindFramebuffer("RebindFramebuffer - Inter-buffer memcpy");
		}
		return false;
	} else if (dstBuffer) {
		if (flags & GPUCopyFlag::MEMSET) {
			gpuStats.numClears++;
		}
		WARN_LOG_N_TIMES(btucpy, 5, Log::FrameBuf, "Memcpy fbo upload %08x -> %08x (size: %x)", src, dst, size);
		FlushBeforeCopy();

		// TODO: Hot Shots Golf makes a lot of these during the "meter", to copy back the image to the screen, it copies line by line.
		// We could collect these in a buffer and flush on the next draw, or something like that, to avoid that. The line copies cause
		// awkward visual artefacts.
		const u8 *srcBase = Memory::GetPointerUnchecked(src);
		GEBufferFormat srcFormat = channel == RASTER_DEPTH ? GE_FORMAT_DEPTH16 : dstBuffer->fb_format;
		int srcStride = channel == RASTER_DEPTH ? dstBuffer->z_stride : dstBuffer->fb_stride;
		DrawPixels(dstBuffer, 0, dstY, srcBase, srcFormat, srcStride, dstBuffer->width, dstH, channel, "MemcpyFboUpload_DrawPixels");
		SetColorUpdated(dstBuffer, skipDrawReason);
		RebindFramebuffer("RebindFramebuffer - Memcpy fbo upload");
		// This is a memcpy, let's still copy just in case.
		return false;
	} else if (srcBuffer) {
		WARN_LOG_N_TIMES(btdcpy, 5, Log::FrameBuf, "Memcpy fbo download %08x -> %08x", src, dst);
		FlushBeforeCopy();
		// TODO: In Hot Shots Golf, check if we can do a readback to a framebuffer here.
		// Again we have the problem though that it's doing a lot of small copies here, one for each line.
		if (srcH == 0 || srcY + srcH > srcBuffer->bufferHeight) {
			WARN_LOG_ONCE(btdcpyheight, Log::FrameBuf, "Memcpy fbo download %08x -> %08x skipped, %d+%d is taller than %d", src, dst, srcY, srcH, srcBuffer->bufferHeight);
		} else if (GetSkipGPUReadbackMode() == SkipGPUReadbackMode::NO_SKIP && (!srcBuffer->memoryUpdated || channel == RASTER_DEPTH)) {
			ReadFramebufferToMemory(srcBuffer, 0, srcY, srcBuffer->width, srcH, channel, Draw::ReadbackMode::BLOCK);
			srcBuffer->usageFlags = (srcBuffer->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
		}
		return false;
	} else {
		return false;
	}
}

std::string BlockTransferRect::ToString() const {
	int bpp = BufferFormatBytesPerPixel(channel == RASTER_DEPTH ? GE_FORMAT_DEPTH16 : vfb->fb_format);
	return StringFromFormat("%s %08x/%d/%s seq:%d  %d,%d %dx%d", RasterChannelToString(channel), vfb->fb_address, vfb->FbStrideInBytes(), GeBufferFormatToString(vfb->fb_format), vfb->colorBindSeq, x_bytes / bpp, y, w_bytes / bpp, h);
}

// This is used when looking for framebuffers for a block transfer.
// The only known game to block transfer depth buffers is Iron Man, see #16530, so
// we have a compat flag and pretty limited functionality for that.
bool FramebufferManagerCommon::FindTransferFramebuffer(u32 basePtr, int stride_pixels, int x_pixels, int y, int w_pixels, int h, int bpp, bool destination, BlockTransferRect *rect) {
	basePtr &= 0x3FFFFFFF;
	if (Memory::IsVRAMAddress(basePtr))
		basePtr &= 0x041FFFFF;
	rect->vfb = nullptr;

	if (!stride_pixels) {
		WARN_LOG(Log::FrameBuf, "Zero stride in FindTransferFrameBuffer, ignoring");
		return false;
	}

	const u32 byteStride = stride_pixels * bpp;
	int x_bytes = x_pixels * bpp;
	int w_bytes = w_pixels * bpp;

	TinySet<BlockTransferRect, 4> candidates;

	// We work entirely in bytes when we do the matching, because games don't consistently use bpps that match
	// that of their buffers. Then after matching we try to map the copy to the simplest operation that does
	// what we need.

	// We are only looking at color for now, have not found any block transfers of depth data (although it's plausible).

	for (auto vfb : vfbs_) {
		BlockTransferRect candidate{ vfb, RASTER_COLOR };

		// Two cases so far of games depending on depth copies: Iron Man in issue #16530 (buffer->buffer)
		// and also #17878 where a game does ram->buffer to an auto-swizzling (|0x600000) address,
		// to initialize Z with a pre-rendered depth buffer.
		if (vfb->z_address == basePtr && vfb->BufferByteStride(RASTER_DEPTH) == byteStride && PSP_CoreParameter().compat.flags().BlockTransferDepth) {
			WARN_LOG_N_TIMES(z_xfer, 5, Log::FrameBuf, "FindTransferFramebuffer: found matching depth buffer, %08x (dest=%d, bpp=%d)", basePtr, (int)destination, bpp);
			candidate.channel = RASTER_DEPTH;
			candidate.x_bytes = x_pixels * bpp;
			candidate.w_bytes = w_pixels * bpp;
			candidate.y = y;
			candidate.h = h;
			candidates.push_back(candidate);
			continue;
		}

		const u32 vfb_address = vfb->fb_address;
		const u32 vfb_size = vfb->BufferByteSize(RASTER_COLOR);

		if (basePtr < vfb_address || basePtr >= vfb_address + vfb_size) {
			continue;
		}

		const u32 vfb_bpp = BufferFormatBytesPerPixel(vfb->fb_format);
		const u32 vfb_byteStride = vfb->FbStrideInBytes();
		const u32 vfb_byteWidth = vfb->WidthInBytes();

		candidate.w_bytes = w_pixels * bpp;
		candidate.h = h;

		const u32 byteOffset = basePtr - vfb_address;
		const int memXOffset = byteOffset % byteStride;
		const int memYOffset = byteOffset / byteStride;

		// Some games use mismatching bitdepths. But make sure the stride matches.
		// If it doesn't, generally this means we detected the framebuffer with too large a height.
		// Use bufferHeight in case of buffers that resize up and down often per frame (Valkyrie Profile.)

		// If it's outside the vfb by a single pixel, we currently disregard it.
		if (memYOffset > vfb->bufferHeight - h) {
			continue;
		}

		if (byteOffset == vfb->WidthInBytes() && w_bytes < vfb->FbStrideInBytes()) {
			// Looks like we're in a margin texture of the vfb, which is not the vfb itself.
			// Ignore the match.
			continue;
		}

		if (vfb_byteStride != byteStride) {
			// Grand Knights History occasionally copies with a mismatching stride but a full line at a time.
			// That's why we multiply by height, not width - this copy is a rectangle with the wrong stride but a line with the correct one.
			// Makes it hard to detect the wrong transfers in e.g. God of War.
			if (w_pixels != stride_pixels || (byteStride * h != vfb_byteStride && byteStride * h != vfb_byteWidth)) {
				if (destination) {
					// However, some other games write cluts to framebuffers.
					// Let's catch this and upload.  Otherwise reject the match.
					bool match = (vfb->usageFlags & FB_USAGE_CLUT) != 0;
					if (match) {
						candidate.w_bytes = byteStride * h;
						h = 1;
					} else {
						continue;
					}
				} else {
					continue;
				}
			} else {
				// This is the Grand Knights History case.
				candidate.w_bytes = byteStride * h;
				candidate.h = 1;
			}
		} else {
			candidate.w_bytes = w_bytes;
			candidate.h = h;
		}

		candidate.x_bytes = x_bytes + memXOffset;
		candidate.y = y + memYOffset;
		candidate.vfb = vfb;
		candidates.push_back(candidate);
	}

	const BlockTransferRect *best = nullptr;
	// Sort candidates by just recency for now, we might add other.
	for (size_t i = 0; i < candidates.size(); i++) {
		const BlockTransferRect *candidate = &candidates[i];

		bool better = !best;
		if (!better) {
			if (candidate->channel == best->channel) {
				better = candidate->vfb->BindSeq(candidate->channel) > best->vfb->BindSeq(candidate->channel);
			} else {
				// Prefer depth over color if the address match is perfect.
				if (candidate->channel == RASTER_DEPTH && best->channel == RASTER_COLOR && candidate->vfb->z_address == basePtr) {
					better = true;
				}
			}
		}

		if ((candidate->vfb->usageFlags & FB_USAGE_CLUT) && candidate->x_bytes == 0 && candidate->y == 0 && destination) {
			// Hack to prioritize copies to clut buffers.
			best = candidate;
			break;
		}
		if (better) {
			best = candidate;
		}
	}

	if (candidates.size() > 1) {
		if (Reporting::ShouldLogNTimes("mulblock", 5)) {
			std::string log;
			for (size_t i = 0; i < candidates.size(); i++) {
				log += " - " + candidates[i].ToString() + "\n";
			}
			WARN_LOG(Log::FrameBuf, "Multiple framebuffer candidates for %08x/%d/%d %d,%d %dx%d (dest = %d):\n%s", basePtr, stride_pixels, bpp, x_pixels, y, w_pixels, h, (int)destination, log.c_str());
		}
	}

	if (best) {
		*rect = *best;
		return true;
	} else {
		if (Memory::IsVRAMAddress(basePtr) && destination && h >= 128) {
			WARN_LOG_N_TIMES(nocands, 5, Log::FrameBuf, "Didn't find a destination candidate for %08x/%d/%d %d,%d %dx%d", basePtr, stride_pixels, bpp, x_pixels, y, w_pixels, h);
		}
		return false;
	}
}

VirtualFramebuffer *FramebufferManagerCommon::CreateRAMFramebuffer(uint32_t fbAddress, int width, int height, int stride, GEBufferFormat format) {
	INFO_LOG(Log::FrameBuf, "Creating RAM framebuffer at %08x (%dx%d, stride %d, fb_format %d)", fbAddress, width, height, stride, format);

	RasterChannel channel = format == GE_FORMAT_DEPTH16 ? RASTER_DEPTH : RASTER_COLOR;

	// A target for the destination is missing - so just create one!
	// Make sure this one would be found by the algorithm above so we wouldn't
	// create a new one each frame.
	VirtualFramebuffer *vfb = new VirtualFramebuffer{};
	vfb->fbo = nullptr;
	uint32_t mask = Memory::IsVRAMAddress(fbAddress) ? 0x041FFFFF : 0x3FFFFFFF;
	if (format == GE_FORMAT_DEPTH16) {
		vfb->fb_address = 0xFFFFFFFF;  // Invalid address
		vfb->fb_stride = 0;
		vfb->z_address = fbAddress;  // marks that if anyone tries to render with depth to this framebuffer, it should be dropped and recreated.
		vfb->z_stride = stride;
		vfb->width = width;
	} else {
		vfb->fb_address = fbAddress & mask;  // NOTE - not necessarily in VRAM!
		vfb->fb_stride = stride;
		vfb->z_address = 0;
		vfb->z_stride = 0;
		vfb->width = std::max(width, stride);
	}
	vfb->height = height;
	vfb->newWidth = vfb->width;
	vfb->newHeight = vfb->height;
	vfb->lastFrameNewSize = gpuStats.numFlips;
	vfb->renderScaleFactor = renderScaleFactor_;
	vfb->renderWidth = (u16)(vfb->width * renderScaleFactor_);
	vfb->renderHeight = (u16)(vfb->height * renderScaleFactor_);
	vfb->bufferWidth = vfb->width;
	vfb->bufferHeight = vfb->height;
	vfb->fb_format = format == GE_FORMAT_DEPTH16 ? GE_FORMAT_8888 : format;
	vfb->usageFlags = format == GE_FORMAT_DEPTH16 ? FB_USAGE_RENDER_DEPTH : FB_USAGE_RENDER_COLOR;
	if (format != GE_FORMAT_DEPTH16) {
		SetColorUpdated(vfb, 0);
	}
	char name[64];
	snprintf(name, sizeof(name), "%08x_%s_RAM", vfb->Address(channel), RasterChannelToString(channel));
	textureCache_->NotifyFramebuffer(vfb, NOTIFY_FB_CREATED);
	bool createDepthBuffer = format == GE_FORMAT_DEPTH16;
	vfb->fbo = draw_->CreateFramebuffer({ vfb->renderWidth, vfb->renderHeight, 1, GetFramebufferLayers(), 0, createDepthBuffer, name });
	vfbs_.push_back(vfb);

	u32 byteSize = vfb->BufferByteSize(channel);
	if (fbAddress + byteSize > framebufColorRangeEnd_) {
		framebufColorRangeEnd_ = fbAddress + byteSize;
	}

	return vfb;
}

// 1:1 pixel size buffers, we resize buffers to these before we read them back.
// TODO: We shouldn't keep whole VirtualFramebuffer structs for these - the fbo and last_frame_render is enough.
VirtualFramebuffer *FramebufferManagerCommon::FindDownloadTempBuffer(VirtualFramebuffer *vfb, RasterChannel channel) {
	// For now we'll keep these on the same struct as the ones that can get displayed
	// (and blatantly copy work already done above while at it).
	VirtualFramebuffer *nvfb = nullptr;

	// We maintain a separate vector of framebuffer objects for blitting.
	for (VirtualFramebuffer *v : bvfbs_) {
		if (v->Address(channel) == vfb->Address(channel) && v->Format(channel) == vfb->Format(channel)) {
			if (v->bufferWidth == vfb->bufferWidth && v->bufferHeight == vfb->bufferHeight) {
				nvfb = v;
				if (channel == RASTER_COLOR) {
					v->fb_stride = vfb->fb_stride;
				} else {
					v->z_stride = vfb->z_stride;
				}
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
		nvfb->fb_address = channel == RASTER_COLOR ? vfb->fb_address : 0;
		nvfb->fb_stride = channel == RASTER_COLOR ? vfb->fb_stride : 0;
		nvfb->z_address = channel == RASTER_DEPTH ? vfb->z_address : 0;
		nvfb->z_stride = channel == RASTER_DEPTH ? vfb->z_stride : 0;
		nvfb->width = vfb->width;
		nvfb->height = vfb->height;
		nvfb->renderWidth = vfb->bufferWidth;
		nvfb->renderHeight = vfb->bufferHeight;
		nvfb->renderScaleFactor = 1;  // For readbacks we resize to the original size, of course.
		nvfb->bufferWidth = vfb->bufferWidth;
		nvfb->bufferHeight = vfb->bufferHeight;
		nvfb->fb_format = vfb->fb_format;
		nvfb->drawnWidth = vfb->drawnWidth;
		nvfb->drawnHeight = vfb->drawnHeight;

		char name[64];
		snprintf(name, sizeof(name), "download_temp_%08x_%s", vfb->Address(channel), RasterChannelToString(channel));

		// We always create a color-only framebuffer here - readbacks of depth convert to color while translating the values.
		nvfb->fbo = draw_->CreateFramebuffer({ nvfb->bufferWidth, nvfb->bufferHeight, 1, 1, 0, false, name });
		if (!nvfb->fbo) {
			ERROR_LOG(Log::FrameBuf, "Error creating FBO! %d x %d", nvfb->renderWidth, nvfb->renderHeight);
			delete nvfb;
			return nullptr;
		}
		bvfbs_.push_back(nvfb);
	} else {
		UpdateDownloadTempBuffer(nvfb);
	}

	nvfb->usageFlags |= FB_USAGE_RENDER_COLOR;
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

	u8 *addr = Memory::GetPointerWriteUnchecked(gstate.getFrameBufAddress());
	const int bpp = BufferFormatBytesPerPixel(gstate_c.framebufFormat);

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

	// Skip checking if there's no framebuffers in that area. Make a special exception for obvious transfers to depth buffer, see issue #17878
	bool dstDepthSwizzle = Memory::IsVRAMAddress(dstBasePtr) && ((dstBasePtr & 0x600000) == 0x600000);

	if (!dstDepthSwizzle && !MayIntersectFramebufferColor(srcBasePtr) && !MayIntersectFramebufferColor(dstBasePtr)) {
		return false;
	}

	BlockTransferRect dstRect{};
	BlockTransferRect srcRect{};

	// These modify the X/Y/W/H parameters depending on the memory offset of the base pointers from the actual buffers.
	bool srcBuffer = FindTransferFramebuffer(srcBasePtr, srcStride, srcX, srcY, width, height, bpp, false, &srcRect);
	bool dstBuffer = FindTransferFramebuffer(dstBasePtr, dstStride, dstX, dstY, width, height, bpp, true, &dstRect);

	if (srcRect.channel == RASTER_DEPTH) {
		// Ignore the found buffer if it's not 16-bit - we create a new more suitable one instead.
		if (dstRect.channel == RASTER_COLOR && dstRect.vfb->fb_format == GE_FORMAT_8888) {
			dstBuffer = false;
		}
	}

	if (!srcBuffer && dstBuffer && dstRect.channel == RASTER_DEPTH) {
		dstBuffer = true;
	}

	if (srcBuffer && !dstBuffer) {
		// In here, we can't read from dstRect.
		if (PSP_CoreParameter().compat.flags().BlockTransferAllowCreateFB ||
			GetSkipGPUReadbackMode() == SkipGPUReadbackMode::COPY_TO_TEXTURE ||
			(PSP_CoreParameter().compat.flags().IntraVRAMBlockTransferAllowCreateFB &&
				Memory::IsVRAMAddress(srcRect.vfb->fb_address) && Memory::IsVRAMAddress(dstBasePtr))) {
			GEBufferFormat ramFormat;
			// Try to guess the appropriate format. We only know the bpp from the block transfer command (16 or 32 bit).
			if (srcRect.channel == RASTER_COLOR) {
				if (bpp == 4) {
					// Only one possibility unless it's doing split pixel tricks (which we could detect through stride maybe).
					ramFormat = GE_FORMAT_8888;
				} else if (srcRect.vfb->fb_format != GE_FORMAT_8888) {
					// We guess that the game will interpret the data the same as it was in the source of the copy.
					// Seems like a likely good guess, and works in Test Drive Unlimited.
					ramFormat = srcRect.vfb->fb_format;
				} else {
					// No info left - just fall back to something. But this is definitely split pixel tricks.
					ramFormat = GE_FORMAT_5551;
				}
				dstRect.vfb = CreateRAMFramebuffer(dstBasePtr, width, height, dstStride, ramFormat);
			} else {
				dstRect.vfb = CreateRAMFramebuffer(dstBasePtr, width, height, dstStride, GE_FORMAT_DEPTH16);
				dstRect.x_bytes = 0;
				dstRect.w_bytes = 2 * width;
				dstRect.y = 0;
				dstRect.h = height;
				dstRect.channel = RASTER_DEPTH;
			}
			dstBuffer = true;
		}
	}

	if (dstBuffer) {
		dstRect.vfb->last_frame_used = gpuStats.numFlips;
		// Mark the destination as fresh.
		if (dstRect.channel == RASTER_COLOR) {
			dstRect.vfb->colorBindSeq = GetBindSeqCount();
		} else {
			dstRect.vfb->depthBindSeq = GetBindSeqCount();
		}
	}

	if (dstBuffer && srcBuffer) {
		if (srcRect.vfb && srcRect.vfb == dstRect.vfb && srcRect.channel == dstRect.channel) {
			// Transfer within the same buffer.
			// This is a simple case because there will be no format conversion or similar shenanigans needed.
			// However, the BPP might still mismatch, but in such a case we can convert the coordinates.
			if (srcX == dstX && srcY == dstY) {
				// Ignore, nothing to do.  Tales of Phantasia X does this by accident.
				// Returning true to also skip the memory copy.
				return true;
			}

			int buffer_bpp = BufferFormatBytesPerPixel(srcRect.vfb->Format(srcRect.channel));

			if (bpp != buffer_bpp) {
				WARN_LOG_ONCE(intrabpp, Log::G3D, "Mismatched transfer bpp in intra-buffer block transfer. Was %d, expected %d.", bpp, buffer_bpp);
				// We just switch to using the buffer's bpp, since we've already converted the rectangle to byte offsets.
				bpp = buffer_bpp;
			}

			WARN_LOG_N_TIMES(dstsrc, 5, Log::G3D, "Intra-buffer block transfer %dx%d %dbpp from %08x (x:%d y:%d stride:%d) -> %08x (x:%d y:%d stride:%d)",
				width, height, bpp,
				srcBasePtr, srcRect.x_bytes / bpp, srcRect.y, srcStride,
				dstBasePtr, dstRect.x_bytes / bpp, dstRect.y, dstStride);
			FlushBeforeCopy();
			// Some backends can handle blitting within a framebuffer. Others will just have to deal with it or ignore it, apparently.
			BlitFramebuffer(dstRect.vfb, dstX, dstY, srcRect.vfb, srcX, srcY, dstRect.w_bytes / bpp, dstRect.h, bpp, dstRect.channel, "Blit_IntraBufferBlockTransfer");
			RebindFramebuffer("rebind after intra block transfer");
			SetColorUpdated(dstRect.vfb, skipDrawReason);
			return true;  // Skip the memory copy.
		}

		// Straightforward blit between two same-format framebuffers.
		if (srcRect.vfb && srcRect.channel == dstRect.channel && srcRect.vfb->Format(srcRect.channel) == dstRect.vfb->Format(dstRect.channel)) {
			WARN_LOG_N_TIMES(dstnotsrc, 5, Log::G3D, "Inter-buffer %s block transfer %dx%d %dbpp from %08x (x:%d y:%d stride:%d %s) -> %08x (x:%d y:%d stride:%d %s)",
				RasterChannelToString(srcRect.channel),
				width, height, bpp,
				srcBasePtr, srcRect.x_bytes / bpp, srcRect.y, srcStride, GeBufferFormatToString(srcRect.vfb->fb_format),
				dstBasePtr, dstRect.x_bytes / bpp, dstRect.y, dstStride, GeBufferFormatToString(dstRect.vfb->fb_format));

			// Straight blit will do, but check the bpp, we might need to convert coordinates differently.
			int buffer_bpp = BufferFormatBytesPerPixel(srcRect.vfb->Format(srcRect.channel));
			if (bpp != buffer_bpp) {
				WARN_LOG_ONCE(intrabpp, Log::G3D, "Mismatched transfer bpp in inter-buffer block transfer. Was %d, expected %d.", bpp, buffer_bpp);
				// We just switch to using the buffer's bpp, since we've already converted the rectangle to byte offsets.
				bpp = buffer_bpp;
			}
			FlushBeforeCopy();
			BlitFramebuffer(dstRect.vfb, dstRect.x_bytes / bpp, dstRect.y, srcRect.vfb, srcRect.x_bytes / bpp, srcRect.y, srcRect.w_bytes / bpp, height, bpp, srcRect.channel, "Blit_InterBufferBlockTransfer");
			RebindFramebuffer("RebindFramebuffer - Inter-buffer block transfer");
			SetColorUpdated(dstRect.vfb, skipDrawReason);
			return true;
		}

		// Getting to the more complex cases. Have not actually seen much of these yet.
		WARN_LOG_N_TIMES(blockformat, 5, Log::G3D, "Mismatched buffer formats in block transfer: %s->%s (%dx%d)",
			GeBufferFormatToString(srcRect.vfb->Format(srcRect.channel)), GeBufferFormatToString(dstRect.vfb->Format(dstRect.channel)),
			width, height);

		// TODO

		// No need to actually do the memory copy behind, probably.
		return true;

	} else if (dstBuffer) {
		// Handle depth uploads directly here, and let's not bother copying the data. This is compat-flag-gated for now,
		// may generalize it when I remove the compat flag.
		if (dstRect.channel == RASTER_DEPTH) {
			WARN_LOG_ONCE(btud, Log::G3D, "Block transfer upload %08x -> %08x (%dx%d %d,%d bpp=%d %s)", srcBasePtr, dstBasePtr, width, height, dstX, dstY, bpp, RasterChannelToString(dstRect.channel));
			FlushBeforeCopy();
			const u8 *srcBase = Memory::GetPointerUnchecked(srcBasePtr) + (srcX + srcY * srcStride) * bpp;
			DrawPixels(dstRect.vfb, dstX, dstY, srcBase, dstRect.vfb->Format(dstRect.channel), srcStride * bpp / 2, (int)(dstRect.w_bytes / 2), dstRect.h, dstRect.channel, "BlockTransferCopy_DrawPixelsDepth");
			RebindFramebuffer("RebindFramebuffer - UploadDepth");
			return true;
		}

		// Here we should just draw the pixels into the buffer. Return false to copy the memory first.
		// NotifyBlockTransferAfter will take care of the rest.
		return false;
	} else if (srcBuffer) {
		if (width == 48 && height == 48 && srcY == 224 && srcX == 432 && PSP_CoreParameter().compat.flags().TacticsOgreEliminateDebugReadback) {
			return false;
		}

		WARN_LOG_N_TIMES(btd, 10, Log::G3D, "Block transfer readback %dx%d %dbpp from %08x (x:%d y:%d stride:%d) -> %08x (x:%d y:%d stride:%d)",
			width, height, bpp,
			srcBasePtr, srcRect.x_bytes / bpp, srcRect.y, srcStride,
			dstBasePtr, dstRect.x_bytes / bpp, dstRect.y, dstStride);
		FlushBeforeCopy();
		if (GetSkipGPUReadbackMode() == SkipGPUReadbackMode::NO_SKIP && !srcRect.vfb->memoryUpdated) {
			const int srcBpp = BufferFormatBytesPerPixel(srcRect.vfb->fb_format);
			const float srcXFactor = (float)bpp / srcBpp;
			const bool tooTall = srcY + srcRect.h > srcRect.vfb->bufferHeight;
			if (srcRect.h <= 0 || (tooTall && srcY != 0)) {
				WARN_LOG_ONCE(btdheight, Log::G3D, "Block transfer download %08x -> %08x skipped, %d+%d is taller than %d", srcBasePtr, dstBasePtr, srcRect.y, srcRect.h, srcRect.vfb->bufferHeight);
			} else {
				if (tooTall) {
					WARN_LOG_ONCE(btdheight, Log::G3D, "Block transfer download %08x -> %08x dangerous, %d+%d is taller than %d", srcBasePtr, dstBasePtr, srcRect.y, srcRect.h, srcRect.vfb->bufferHeight);
				}
				ReadFramebufferToMemory(srcRect.vfb, static_cast<int>(srcX * srcXFactor), srcY, static_cast<int>(srcRect.w_bytes * srcXFactor), srcRect.h, RASTER_COLOR, Draw::ReadbackMode::BLOCK);
				srcRect.vfb->usageFlags = (srcRect.vfb->usageFlags | FB_USAGE_DOWNLOAD) & ~FB_USAGE_DOWNLOAD_CLEAR;
			}
		}
		return false;  // Let the bit copy happen
	} else {
		return false;
	}
}

SkipGPUReadbackMode FramebufferManagerCommon::GetSkipGPUReadbackMode() {
	if (PSP_CoreParameter().compat.flags().ForceEnableGPUReadback) {
		return SkipGPUReadbackMode::NO_SKIP;
	} else {
		return (SkipGPUReadbackMode)g_Config.iSkipGPUReadbackMode;
	}
}

void FramebufferManagerCommon::NotifyBlockTransferAfter(u32 dstBasePtr, int dstStride, int dstX, int dstY, u32 srcBasePtr, int srcStride, int srcX, int srcY, int width, int height, int bpp, u32 skipDrawReason) {
	// If it's a block transfer direct to the screen, and we're not using buffers, draw immediately.
	// We may still do a partial block draw below if this doesn't pass.
	if (!useBufferedRendering_ && dstStride >= 480 && width >= 480 && height == 272) {
		bool isPrevDisplayBuffer = PrevDisplayFramebufAddr() == dstBasePtr;
		bool isDisplayBuffer = CurrentDisplayFramebufAddr() == dstBasePtr;
		if (isPrevDisplayBuffer || isDisplayBuffer) {
			FlushBeforeCopy();
			DrawFramebufferToOutput(Memory::GetPointerUnchecked(dstBasePtr), dstStride, displayFormat_);
			return;
		}
	}

	if (MayIntersectFramebufferColor(srcBasePtr) || MayIntersectFramebufferColor(dstBasePtr)) {
		// TODO: Figure out how we can avoid repeating the search here.

		BlockTransferRect dstRect{};
		BlockTransferRect srcRect{};

		// These modify the X/Y/W/H parameters depending on the memory offset of the base pointers from the actual buffers.
		bool srcBuffer = FindTransferFramebuffer(srcBasePtr, srcStride, srcX, srcY, width, height, bpp, false, &srcRect);
		bool dstBuffer = FindTransferFramebuffer(dstBasePtr, dstStride, dstX, dstY, width, height, bpp, true, &dstRect);

		// A few games use this INSTEAD of actually drawing the video image to the screen, they just blast it to
		// the backbuffer. Detect this and have the framebuffermanager draw the pixels.
		if (!useBufferedRendering_ && currentRenderVfb_ != dstRect.vfb) {
			return;
		}

		if (dstBuffer && !srcBuffer) {
			WARN_LOG_ONCE(btu, Log::G3D, "Block transfer upload %08x -> %08x (%dx%d %d,%d bpp=%d)", srcBasePtr, dstBasePtr, width, height, dstX, dstY, bpp);
			FlushBeforeCopy();
			const u8 *srcBase = Memory::GetPointerUnchecked(srcBasePtr) + (srcX + srcY * srcStride) * bpp;

			int dstBpp = BufferFormatBytesPerPixel(dstRect.vfb->fb_format);
			float dstXFactor = (float)bpp / dstBpp;
			if (dstRect.w_bytes / bpp > dstRect.vfb->width || dstRect.h > dstRect.vfb->height) {
				// The buffer isn't big enough, and we have a clear hint of size. Resize.
				// This happens in Valkyrie Profile when uploading video at the ending.
				// Also happens to the CLUT framebuffer in the Burnout Dominator lens flare effect. See #16075
				ResizeFramebufFBO(dstRect.vfb, dstRect.w_bytes / bpp, dstRect.h, false, true);
				// Make sure we don't flop back and forth.
				dstRect.vfb->newWidth = std::max(dstRect.w_bytes / bpp, (int)dstRect.vfb->width);
				dstRect.vfb->newHeight = std::max(dstRect.h, (int)dstRect.vfb->height);
				dstRect.vfb->lastFrameNewSize = gpuStats.numFlips;
				// Resizing may change the viewport/etc.
				gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_CULLRANGE);
			}
			DrawPixels(dstRect.vfb, static_cast<int>(dstX * dstXFactor), dstY, srcBase, dstRect.vfb->fb_format, static_cast<int>(srcStride * dstXFactor), static_cast<int>(dstRect.w_bytes / bpp * dstXFactor), dstRect.h, RASTER_COLOR, "BlockTransferCopy_DrawPixels");
			SetColorUpdated(dstRect.vfb, skipDrawReason);
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

void FramebufferManagerCommon::NotifyDisplayResized() {
	pixelWidth_ = PSP_CoreParameter().pixelWidth;
	pixelHeight_ = PSP_CoreParameter().pixelHeight;
	presentation_->UpdateDisplaySize(pixelWidth_, pixelHeight_);

	INFO_LOG(Log::G3D, "FramebufferManagerCommon::NotifyDisplayResized: %dx%d", pixelWidth_, pixelHeight_);

	// No drawing is allowed here. This includes anything that might potentially touch a command buffer, like creating images!
	// So we need to defer the post processing initialization.
	updatePostShaders_ = true;
}

void FramebufferManagerCommon::NotifyRenderResized(int msaaLevel) {
	gstate_c.skipDrawReason &= ~SKIPDRAW_NON_DISPLAYED_FB;

	int w, h, scaleFactor;
	presentation_->CalculateRenderResolution(&w, &h, &scaleFactor, &postShaderIsUpscalingFilter_, &postShaderIsSupersampling_);
	PSP_CoreParameter().renderWidth = w;
	PSP_CoreParameter().renderHeight = h;
	PSP_CoreParameter().renderScaleFactor = scaleFactor;

	if (UpdateRenderSize(msaaLevel)) {
		draw_->StopThreads();
		DestroyAllFBOs();
		draw_->StartThreads();
	}

	// No drawing is allowed here. This includes anything that might potentially touch a command buffer, like creating images!
	// So we need to defer the post processing initialization.
	updatePostShaders_ = true;
}

void FramebufferManagerCommon::NotifyConfigChanged() {
	updatePostShaders_ = true;
}

void FramebufferManagerCommon::DestroyAllFBOs() {
	DiscardFramebufferCopy();
	currentRenderVfb_ = nullptr;
	displayFramebuf_ = nullptr;
	prevDisplayFramebuf_ = nullptr;
	prevPrevDisplayFramebuf_ = nullptr;

	for (VirtualFramebuffer *vfb : vfbs_) {
		INFO_LOG(Log::FrameBuf, "Destroying FBO for %08x : %i x %i x %i", vfb->fb_address, vfb->width, vfb->height, vfb->fb_format);
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

	for (auto &iter : fbosToDelete_) {
		iter->Release();
	}
	fbosToDelete_.clear();

	for (auto &iter : drawPixelsCache_) {
		iter.tex->Release();
	}
	drawPixelsCache_.clear();
}

static const char *TempFBOReasonToString(TempFBO reason) {
	switch (reason) {
	case TempFBO::DEPAL: return "depal";
	case TempFBO::BLIT: return "blit";
	case TempFBO::COPY: return "copy";
	case TempFBO::STENCIL: return "stencil";
	default: break;
	}
	return "";
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
	snprintf(name, sizeof(name), "tempfbo_%s_%dx%d", TempFBOReasonToString(reason), w / renderScaleFactor_, h / renderScaleFactor_);

	Draw::Framebuffer *fbo = draw_->CreateFramebuffer({ w, h, 1, GetFramebufferLayers(), 0, z_stencil, name });
	if (!fbo) {
		return nullptr;
	}

	const TempFBOInfo info = { fbo, gpuStats.numFlips };
	tempFBOs_[key] = info;
	return fbo;
}

void FramebufferManagerCommon::UpdateFramebufUsage(VirtualFramebuffer *vfb) const {
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
	checkFlag(FB_USAGE_RENDER_COLOR, vfb->last_frame_render);
	checkFlag(FB_USAGE_CLUT, vfb->last_frame_clut);
}

void FramebufferManagerCommon::ClearAllDepthBuffers() {
	for (auto vfb : vfbs_) {
		vfb->usageFlags |= FB_USAGE_INVALIDATE_DEPTH;
	}
}

// We might also want to implement an asynchronous callback-style version of this. Would probably
// only be possible to implement optimally on Vulkan, but on GL and D3D11 we could do pixel buffers
// and read on the next frame, then call the callback.
//
// The main use cases for this are:
// * GE debugging(in practice async will not matter because it will stall anyway.)
// * Video file recording(would probably be great if it was async.)
// * Screenshots(benefit slightly from async.)
// * Save state screenshots(could probably be async but need to manage the stall.)
bool FramebufferManagerCommon::GetFramebuffer(u32 fb_address, int fb_stride, GEBufferFormat format, GPUDebugBuffer &buffer, int maxScaleFactor) {
	VirtualFramebuffer *vfb = currentRenderVfb_;
	if (!vfb || vfb->fb_address != fb_address) {
		vfb = ResolveVFB(fb_address, fb_stride, format);
	}

	if (!vfb) {
		if (!Memory::IsValidAddress(fb_address))
			return false;
		// If there's no vfb and we're drawing there, must be memory?
		buffer = GPUDebugBuffer(Memory::GetPointerWriteUnchecked(fb_address), fb_stride, 512, format);
		return true;
	}

	int w = vfb->renderWidth, h = vfb->renderHeight;

	Draw::Framebuffer *bound = nullptr;

	if (vfb->fbo) {
		if (maxScaleFactor > 0 && vfb->renderWidth > vfb->width * maxScaleFactor) {
			w = vfb->width * maxScaleFactor;
			h = vfb->height * maxScaleFactor;

			Draw::Framebuffer *tempFBO = GetTempFBO(TempFBO::COPY, w, h);
			VirtualFramebuffer tempVfb = *vfb;
			tempVfb.fbo = tempFBO;
			tempVfb.bufferWidth = vfb->width;
			tempVfb.bufferHeight = vfb->height;
			tempVfb.renderWidth = w;
			tempVfb.renderHeight = h;
			tempVfb.renderScaleFactor = maxScaleFactor;
			BlitFramebuffer(&tempVfb, 0, 0, vfb, 0, 0, vfb->width, vfb->height, 0, RASTER_COLOR, "Blit_GetFramebuffer");

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
	bool retval = draw_->CopyFramebufferToMemory(bound, Draw::Aspect::COLOR_BIT, 0, 0, w, h, Draw::DataFormat::R8G8B8A8_UNORM, buffer.GetData(), w, Draw::ReadbackMode::BLOCK, "GetFramebuffer");

	// Don't need to increment gpu stats for readback count here, this is a debugger-only function.

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
		buffer = GPUDebugBuffer(Memory::GetPointerWriteUnchecked(z_address), z_stride, 512, GPU_DBG_FORMAT_16BIT);
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

	// Old code
	if (gstate_c.Use(GPU_SCALE_DEPTH_FROM_24BIT_TO_16BIT)) {
		buffer.Allocate(w, h, GPU_DBG_FORMAT_FLOAT_DIV_256, flipY);
	} else {
		buffer.Allocate(w, h, GPU_DBG_FORMAT_FLOAT, flipY);
	}
	// No need to free on failure, that's the caller's job (it likely will reuse a buffer.)
	bool retval = draw_->CopyFramebufferToMemory(vfb->fbo, Draw::Aspect::DEPTH_BIT, 0, 0, w, h, Draw::DataFormat::D32F, buffer.GetData(), w, Draw::ReadbackMode::BLOCK, "GetDepthBuffer");
	if (!retval) {
		// Try ReadbackDepthbufferSync, in case GLES.
		buffer.Allocate(w, h, GPU_DBG_FORMAT_16BIT, flipY);
		retval = ReadbackDepthbuffer(vfb->fbo, 0, 0, w, h, (uint16_t *)buffer.GetData(), w, w, h, Draw::ReadbackMode::BLOCK);
	}

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
		buffer = GPUDebugBuffer(Memory::GetPointerWrite(fb_address), fb_stride, 512, GPU_DBG_FORMAT_8888);
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
	bool retval = draw_->CopyFramebufferToMemory(vfb->fbo, Draw::Aspect::STENCIL_BIT, 0, 0, w,h, Draw::DataFormat::S8, buffer.GetData(), w, Draw::ReadbackMode::BLOCK, "GetStencilbuffer");
	if (!retval) {
		retval = ReadbackStencilbuffer(vfb->fbo, 0, 0, w, h, buffer.GetData(), w, Draw::ReadbackMode::BLOCK);
	}
	// That may have unbound the framebuffer, rebind to avoid crashes when debugging.
	RebindFramebuffer("RebindFramebuffer - GetStencilbuffer");
	return retval;
}

bool GetOutputFramebuffer(Draw::DrawContext *draw, GPUDebugBuffer &buffer) {
	int w, h;
	draw->GetFramebufferDimensions(nullptr, &w, &h);
	Draw::DataFormat fmt = draw->PreferredFramebufferReadbackFormat(nullptr);
	// Ignore preferred formats other than BGRA.
	if (fmt != Draw::DataFormat::B8G8R8A8_UNORM)
		fmt = Draw::DataFormat::R8G8B8A8_UNORM;

	bool flipped = g_Config.iGPUBackend == (int)GPUBackend::OPENGL;

	buffer.Allocate(w, h, fmt == Draw::DataFormat::R8G8B8A8_UNORM ? GPU_DBG_FORMAT_8888 : GPU_DBG_FORMAT_8888_BGRA, flipped);
	return draw->CopyFramebufferToMemory(nullptr, Draw::Aspect::COLOR_BIT, 0, 0, w, h, fmt, buffer.GetData(), w, Draw::ReadbackMode::BLOCK, "GetOutputFramebuffer");
}

bool FramebufferManagerCommon::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	bool retval = ::GetOutputFramebuffer(draw_, buffer);
	// That may have unbound the framebuffer, rebind to avoid crashes when debugging.
	RebindFramebuffer("RebindFramebuffer - GetOutputFramebuffer");
	return retval;
}

// This reads a channel of a framebuffer into emulated PSP VRAM, taking care of scaling down as needed.
//
// Color conversion is currently done on CPU but should theoretically be done on GPU.
// (Except using the GPU might cause problems because of various implementations'
// dithering behavior and games that expect exact colors like Danganronpa, so we
// can't entirely be rid of the CPU path.) -- unknown
void FramebufferManagerCommon::ReadbackFramebuffer(VirtualFramebuffer *vfb, int x, int y, int w, int h, RasterChannel channel, Draw::ReadbackMode mode) {
	if (w <= 0 || h <= 0) {
		ERROR_LOG(Log::FrameBuf, "Bad inputs to ReadbackFramebufferSync: %d %d %d %d", x, y, w, h);
		return;
	}

	// Note that ReadbackDepthBufferSync can stretch on its own while converting data format, so we don't need to downscale in that case.
	if (vfb->renderScaleFactor == 1 || channel == RASTER_DEPTH) {
		// No need to stretch-blit
	} else {
		VirtualFramebuffer *nvfb = FindDownloadTempBuffer(vfb, channel);
		if (nvfb) {
			BlitFramebuffer(nvfb, x, y, vfb, x, y, w, h, 0, channel, "Blit_ReadFramebufferToMemory");
			vfb = nvfb;
		}
	}

	const u32 fb_address = channel == RASTER_COLOR ? vfb->fb_address : vfb->z_address;

	Draw::DataFormat destFormat = channel == RASTER_COLOR ? GEFormatToThin3D(vfb->fb_format) : GEFormatToThin3D(GE_FORMAT_DEPTH16);
	const int dstBpp = (int)DataFormatSizeInBytes(destFormat);

	int stride = channel == RASTER_COLOR ? vfb->fb_stride : vfb->z_stride;

	const int dstByteOffset = (y * stride + x) * dstBpp;
	// Leave the gap between the end of the last line and the full stride.
	// This is only used for the NotifyMemInfo range.
	const int dstSize = ((h - 1) * stride + w) * dstBpp;

	if (!Memory::IsValidRange(fb_address + dstByteOffset, dstSize)) {
		ERROR_LOG_REPORT(Log::G3D, "ReadbackFramebufferSync would write outside of memory, ignoring");
		return;
	}

	u8 *destPtr = Memory::GetPointerWriteUnchecked(fb_address + dstByteOffset);

	// We always need to convert from the framebuffer native format.
	// Right now that's always 8888.
	DEBUG_LOG(Log::FrameBuf, "Reading framebuffer to mem, fb_address = %08x, ptr=%p", fb_address, destPtr);

	if (channel == RASTER_DEPTH) {
		_assert_msg_(vfb && vfb->z_address != 0 && vfb->z_stride != 0, "Depth buffer invalid");
		ReadbackDepthbuffer(vfb->fbo,
			x * vfb->renderScaleFactor, y * vfb->renderScaleFactor,
			w * vfb->renderScaleFactor, h * vfb->renderScaleFactor, (uint16_t *)destPtr, stride, w, h, mode);
	} else {
		draw_->CopyFramebufferToMemory(vfb->fbo, channel == RASTER_COLOR ? Draw::Aspect::COLOR_BIT : Draw::Aspect::DEPTH_BIT, x, y, w, h, destFormat, destPtr, stride, mode, "ReadbackFramebufferSync");
	}

	char tag[128];
	size_t len = snprintf(tag, sizeof(tag), "FramebufferPack/%08x_%08x_%dx%d_%s", vfb->fb_address, vfb->z_address, w, h, GeBufferFormatToString(vfb->fb_format));
	NotifyMemInfo(MemBlockFlags::WRITE, fb_address + dstByteOffset, dstSize, tag, len);

	if (mode == Draw::ReadbackMode::BLOCK) {
		gpuStats.numBlockingReadbacks++;
	} else {
		gpuStats.numReadbacks++;
	}
}

bool FramebufferManagerCommon::ReadbackStencilbuffer(Draw::Framebuffer *fbo, int x, int y, int w, int h, uint8_t *pixels, int pixelsStride, Draw::ReadbackMode mode) {
	return draw_->CopyFramebufferToMemory(fbo, Draw::Aspect::DEPTH_BIT, x, y, w, h, Draw::DataFormat::S8, pixels, pixelsStride, mode, "ReadbackStencilbufferSync");
}

void FramebufferManagerCommon::ReadFramebufferToMemory(VirtualFramebuffer *vfb, int x, int y, int w, int h, RasterChannel channel, Draw::ReadbackMode mode) {
	if (!vfb || !vfb->fbo) {
		return;
	}

	// Clamp to bufferWidth. Sometimes block transfers can cause this to hit.
	if (x + w >= vfb->bufferWidth) {
		w = vfb->bufferWidth - x;
	}
	if (gameUsesSequentialCopies_) {
		// Ignore the x/y/etc., read the entire thing.  See below.
		x = 0;
		y = 0;
		w = vfb->width;
		h = vfb->height;
		vfb->memoryUpdated = true;
		vfb->usageFlags |= FB_USAGE_DOWNLOAD;
	} else if (x == 0 && y == 0 && w == vfb->width && h == vfb->height) {
		// Mark it as fully downloaded until next render to it.
		if (channel == RASTER_COLOR)
			vfb->memoryUpdated = true;
		vfb->usageFlags |= FB_USAGE_DOWNLOAD;
	} else {
		// Let's try to set the flag eventually, if the game copies a lot.
		// Some games (like Grand Knights History) copy subranges very frequently.
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

	// This handles any required stretching internally.
	ReadbackFramebuffer(vfb, x, y, w, h, channel, mode);

	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
	textureCache_->ForgetLastTexture();
	RebindFramebuffer("RebindFramebuffer - ReadFramebufferToMemory");
}

void FramebufferManagerCommon::FlushBeforeCopy() {
	drawEngine_->FlushQueuedDepth();
	// Flush anything not yet drawn before blitting, downloading, or uploading.
	// This might be a stalled list, or unflushed before a block transfer, etc.
	// Only bother if any draws are pending.
	if (drawEngine_->GetNumDrawCalls() > 0) {
		// TODO: It's really bad that we are calling SetRenderFramebuffer here with
		// all the irrelevant state checking it'll use to decide what to do. Should
		// do something more focused here.
		bool changed;
		SetRenderFrameBuffer(gstate_c.IsDirty(DIRTY_FRAMEBUF), gstate_c.skipDrawReason, &changed);
		drawEngine_->Flush();
	}
}

// TODO: Replace with with depal, reading the palette from the texture on the GPU directly.
void FramebufferManagerCommon::DownloadFramebufferForClut(u32 fb_address, u32 loadBytes) {
	VirtualFramebuffer *vfb = GetVFBAt(fb_address);
	if (vfb && vfb->fb_stride != 0) {
		const u32 bpp = BufferFormatBytesPerPixel(vfb->fb_format);
		int x = 0;
		int y = 0;
		int pixels = loadBytes / bpp;
		// The height will be 1 for each stride or part thereof.
		int w = std::min(pixels % vfb->fb_stride, (int)vfb->width);
		int h = std::min((pixels + vfb->fb_stride - 1) / vfb->fb_stride, (int)vfb->height);

		if (w == 0 || h > 1) {
			// Exactly aligned, or more than one row.
			w = std::min(vfb->fb_stride, vfb->width);
		}

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

			// This function now handles scaling down internally.
			ReadbackFramebuffer(vfb, x, y, w, h, RASTER_COLOR, Draw::ReadbackMode::BLOCK);

			textureCache_->ForgetLastTexture();
			RebindFramebuffer("RebindFramebuffer - DownloadFramebufferForClut");
		}
	}
}

void FramebufferManagerCommon::RebindFramebuffer(const char *tag) {
	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);
	shaderManager_->DirtyLastShader();
	// Needed for D3D11 to run validation clean. I don't think it's actually an issue.
	// textureCache_->ForgetLastTexture();
	if (currentRenderVfb_ && currentRenderVfb_->fbo) {
		draw_->BindFramebufferAsRenderTarget(currentRenderVfb_->fbo, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, tag);
	} else {
		// This can happen (like it does in Parappa) when a frame starts with copies instead of rendering.
		// Let's do nothing and assume it'll take care of itself.
	}
}

std::vector<const VirtualFramebuffer *> FramebufferManagerCommon::GetFramebufferList() const {
	std::vector<const VirtualFramebuffer *> list;
	for (auto vfb : vfbs_) {
		list.push_back(vfb);
	}
	return list;
}

template <typename T>
static void DoRelease(T *&obj) {
	if (obj)
		obj->Release();
	obj = nullptr;
}

void FramebufferManagerCommon::ReleasePipelines() {
	for (int i = 0; i < ARRAY_SIZE(reinterpretFromTo_); i++) {
		for (int j = 0; j < ARRAY_SIZE(reinterpretFromTo_); j++) {
			DoRelease(reinterpretFromTo_[i][j]);
		}
	}
	DoRelease(stencilWriteSampler_);
	DoRelease(stencilWritePipeline_);
	DoRelease(stencilReadbackSampler_);
	DoRelease(stencilReadbackPipeline_);
	DoRelease(depthReadbackSampler_);
	DoRelease(depthReadbackPipeline_);
	DoRelease(draw2DPipelineCopyColor_);
	DoRelease(draw2DPipelineColorRect2Lin_);
	DoRelease(draw2DPipelineCopyDepth_);
	DoRelease(draw2DPipelineEncodeDepth_);
	DoRelease(draw2DPipeline565ToDepth_);
	DoRelease(draw2DPipeline565ToDepthDeswizzle_);
}

void FramebufferManagerCommon::DeviceLost() {
	DestroyAllFBOs();

	presentation_->DeviceLost();
	draw2D_.DeviceLost();

	ReleasePipelines();

	draw_ = nullptr;
}

void FramebufferManagerCommon::DeviceRestore(Draw::DrawContext *draw) {
	draw_ = draw;
	draw2D_.DeviceRestore(draw_);
	presentation_->DeviceRestore(draw_);
}

void FramebufferManagerCommon::DrawActiveTexture(float x, float y, float w, float h, float destW, float destH, float u0, float v0, float u1, float v1, int uvRotation, int flags) {
	// Will be drawn as a strip.
	Draw2DVertex coord[4] = {
		{x,     y,     u0, v0},
		{x + w, y,     u1, v0},
		{x + w, y + h, u1, v1},
		{x,     y + h, u0, v1},
	};

	if (uvRotation != ROTATION_LOCKED_HORIZONTAL) {
		float temp[8];
		int rotation = 0;
		switch (uvRotation) {
		case ROTATION_LOCKED_HORIZONTAL180: rotation = 2; break;
		case ROTATION_LOCKED_VERTICAL: rotation = 1; break;
		case ROTATION_LOCKED_VERTICAL180: rotation = 3; break;
		}
		for (int i = 0; i < 4; i++) {
			temp[i * 2] = coord[((i + rotation) & 3)].u;
			temp[i * 2 + 1] = coord[((i + rotation) & 3)].v;
		}

		for (int i = 0; i < 4; i++) {
			coord[i].u = temp[i * 2];
			coord[i].v = temp[i * 2 + 1];
		}
	}

	const float invDestW = 2.0f / destW;
	const float invDestH = 2.0f / destH;
	for (int i = 0; i < 4; i++) {
		coord[i].x = coord[i].x * invDestW - 1.0f;
		coord[i].y = coord[i].y * invDestH - 1.0f;
	}

	if ((flags & DRAWTEX_TO_BACKBUFFER) && g_display.rotation != DisplayRotation::ROTATE_0) {
		for (int i = 0; i < 4; i++) {
			// backwards notation, should fix that...
			Lin::Vec3 pos = Lin::Vec3(coord[i].x, coord[i].y, 0.0);
			pos = pos * g_display.rot_matrix;
			coord[i].x = pos.x;
			coord[i].y = pos.y;
		}
	}

	// Rearrange to strip form.
	std::swap(coord[2], coord[3]);

	draw2D_.DrawStrip2D(nullptr, coord, 4, (flags & DRAWTEX_LINEAR) != 0, Get2DPipeline((flags & DRAWTEX_DEPTH) ? DRAW2D_ENCODE_R16_TO_DEPTH : DRAW2D_COPY_COLOR));

	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
}

void FramebufferManagerCommon::BlitFramebuffer(VirtualFramebuffer *dst, int dstX, int dstY, VirtualFramebuffer *src, int srcX, int srcY, int w, int h, int bpp, RasterChannel channel, const char *tag) {
	if (!dst->fbo || !src->fbo || !useBufferedRendering_) {
		// This can happen if they recently switched from non-buffered.
		if (useBufferedRendering_) {
			// Just bind the back buffer for rendering, forget about doing anything else as we're in a weird state.
			draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, "BlitFramebuffer");
		}
		return;
	}

	if (channel == RASTER_DEPTH && !draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported) {
		// Can't do anything :(
		return;
	}

	// Perform a little bit of clipping first.
	// Block transfer coords are unsigned so I don't think we need to clip on the left side.. Although there are
	// other uses for BlitFramebuffer.
	if (dstX + w > dst->bufferWidth) {
		w -= dstX + w - dst->bufferWidth;
	}
	if (dstY + h > dst->bufferHeight) {
		h -= dstY + h - dst->bufferHeight;
	}
	if (srcX + w > src->bufferWidth) {
		w -= srcX + w - src->bufferWidth;
	}
	if (srcY + h > src->bufferHeight) {
		h -= srcY + h - src->bufferHeight;
	}

	if (w <= 0 || h <= 0) {
		// The whole rectangle got clipped.
		return;
	}

	bool useBlit = channel == RASTER_COLOR ? draw_->GetDeviceCaps().framebufferBlitSupported : false;
	bool useCopy = channel == RASTER_COLOR ? draw_->GetDeviceCaps().framebufferCopySupported : false;
	if (dst == currentRenderVfb_ || dst->fbo->MultiSampleLevel() != 0 || src->fbo->MultiSampleLevel() != 0) {
		// If already bound, using either a blit or a copy is unlikely to be an optimization.
		// So we're gonna use a raster draw instead. Also multisampling has problems with copies currently.
		useBlit = false;
		useCopy = false;
	}

	float srcXFactor = src->renderScaleFactor;
	float srcYFactor = src->renderScaleFactor;
	const int srcBpp = BufferFormatBytesPerPixel(src->Format(channel));
	if (srcBpp != bpp && bpp != 0) {
		// If we do this, we're kinda in nonsense territory since the actual formats won't match (unless intentionally blitting black or white).
		srcXFactor = (srcXFactor * bpp) / srcBpp;
	}
	int srcX1 = srcX * srcXFactor;
	int srcX2 = (srcX + w) * srcXFactor;
	int srcY1 = srcY * srcYFactor;
	int srcY2 = (srcY + h) * srcYFactor;

	float dstXFactor = dst->renderScaleFactor;
	float dstYFactor = dst->renderScaleFactor;
	const int dstBpp = BufferFormatBytesPerPixel(dst->Format(channel));
	if (dstBpp != bpp && bpp != 0) {
		// If we do this, we're kinda in nonsense territory since the actual formats won't match (unless intentionally blitting black or white).
		dstXFactor = (dstXFactor * bpp) / dstBpp;
	}
	int dstX1 = dstX * dstXFactor;
	int dstX2 = (dstX + w) * dstXFactor;
	int dstY1 = dstY * dstYFactor;
	int dstY2 = (dstY + h) * dstYFactor;

	if (src == dst && srcX == dstX && srcY == dstY) {
		// Let's just skip a copy where the destination is equal to the source.
		WARN_LOG_REPORT_ONCE(blitSame, Log::G3D, "Skipped blit with equal dst and src");
		return;
	}

	if (useCopy) {
		// glBlitFramebuffer can clip, but glCopyImageSubData is more restricted.
		// In case the src goes outside, we just skip the optimization in that case.
		const bool sameSize = dstX2 - dstX1 == srcX2 - srcX1 && dstY2 - dstY1 == srcY2 - srcY1;
		const bool srcInsideBounds = srcX2 <= src->renderWidth && srcY2 <= src->renderHeight;
		const bool dstInsideBounds = dstX2 <= dst->renderWidth && dstY2 <= dst->renderHeight;
		const bool xOverlap = src == dst && srcX2 > dstX1 && srcX1 < dstX2;
		const bool yOverlap = src == dst && srcY2 > dstY1 && srcY1 < dstY2;
		if (sameSize && srcInsideBounds && dstInsideBounds && !(xOverlap && yOverlap)) {
			draw_->CopyFramebufferImage(src->fbo, 0, srcX1, srcY1, 0, dst->fbo, 0, dstX1, dstY1, 0, dstX2 - dstX1, dstY2 - dstY1, 1, 
				channel == RASTER_COLOR ? Draw::Aspect::COLOR_BIT : Draw::Aspect::DEPTH_BIT, tag);
			return;
		}
	}

	if (useBlit) {
		draw_->BlitFramebuffer(src->fbo, srcX1, srcY1, srcX2, srcY2, dst->fbo, dstX1, dstY1, dstX2, dstY2,
			channel == RASTER_COLOR ? Draw::Aspect::COLOR_BIT : Draw::Aspect::DEPTH_BIT, Draw::FB_BLIT_NEAREST, tag);
	} else {
		Draw2DPipeline *pipeline = Get2DPipeline(channel == RASTER_COLOR ? DRAW2D_COPY_COLOR : DRAW2D_COPY_DEPTH);
		Draw::Framebuffer *srcFBO = src->fbo;
		if (src == dst) {
			Draw::Framebuffer *tempFBO = GetTempFBO(TempFBO::BLIT, src->renderWidth, src->renderHeight);
			BlitUsingRaster(src->fbo, srcX1, srcY1, srcX2, srcY2, tempFBO, dstX1, dstY1, dstX2, dstY2, false, dst->renderScaleFactor, pipeline, tag);
			srcFBO = tempFBO;
		}
		BlitUsingRaster(srcFBO, srcX1, srcY1, srcX2, srcY2, dst->fbo, dstX1, dstY1, dstX2, dstY2, false, dst->renderScaleFactor, pipeline, tag);
	}

	draw_->Invalidate(InvalidationFlags::CACHED_RENDER_STATE);

	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
}

// The input is raw pixel coordinates, scale not taken into account.
void FramebufferManagerCommon::BlitUsingRaster(
	Draw::Framebuffer *src, float srcX1, float srcY1, float srcX2, float srcY2,
	Draw::Framebuffer *dest, float destX1, float destY1, float destX2, float destY2,
	bool linearFilter,
	int scaleFactor,
	Draw2DPipeline *pipeline, const char *tag) {

	if (pipeline->info.writeChannel == RASTER_DEPTH) {
		_dbg_assert_(draw_->GetDeviceCaps().fragmentShaderDepthWriteSupported);
	}

	int destW, destH, srcW, srcH;
	draw_->GetFramebufferDimensions(src, &srcW, &srcH);
	draw_->GetFramebufferDimensions(dest, &destW, &destH);

	// Unbind the texture first to avoid the D3D11 hazard check (can't set render target to things bound as textures and vice versa, not even temporarily).
	draw_->BindTexture(0, nullptr);
	// This will get optimized away in case it's already bound (in VK and GL at least..)
	draw_->BindFramebufferAsRenderTarget(dest, { Draw::RPAction::KEEP, Draw::RPAction::KEEP, Draw::RPAction::KEEP }, tag ? tag : "BlitUsingRaster");
	draw_->BindFramebufferAsTexture(src, 0, pipeline->info.readChannel == RASTER_COLOR ? Draw::Aspect::COLOR_BIT : Draw::Aspect::DEPTH_BIT, Draw::ALL_LAYERS);

	if (destX1 == 0.0f && destY1 == 0.0f && destX2 >= destW && destY2 >= destH) {
		// We overwrite the whole channel of the framebuffer, so we can invalidate the current contents.
		draw_->InvalidateFramebuffer(Draw::FB_INVALIDATION_LOAD, pipeline->info.writeChannel == RASTER_COLOR ? Draw::Aspect::COLOR_BIT : Draw::Aspect::DEPTH_BIT);
	}

	Draw::Viewport viewport{ 0.0f, 0.0f, (float)dest->Width(), (float)dest->Height(), 0.0f, 1.0f };
	draw_->SetViewport(viewport);
	draw_->SetScissorRect(0, 0, (int)dest->Width(), (int)dest->Height());

	draw2D_.Blit(pipeline, srcX1, srcY1, srcX2, srcY2, destX1, destY1, destX2, destY2, (float)srcW, (float)srcH, (float)destW, (float)destH, linearFilter, scaleFactor);

	gstate_c.Dirty(DIRTY_ALL_RENDER_STATE);
}

int FramebufferManagerCommon::GetFramebufferLayers() const {
	int layers = 1;
	if (gstate_c.Use(GPU_USE_SINGLE_PASS_STEREO)) {
		layers = 2;
	}
	return layers;
}

VirtualFramebuffer *FramebufferManagerCommon::ResolveFramebufferColorToFormat(VirtualFramebuffer *src, GEBufferFormat newFormat) {
	// Look for an identical framebuffer with the new format
	_dbg_assert_(src->fb_format != newFormat);

	VirtualFramebuffer *vfb = nullptr;
	for (auto dest : vfbs_) {
		if (dest == src) {
			continue;
		}

		// Sanity check for things that shouldn't exist.
		if (dest->fb_address == src->fb_address && dest->fb_format == src->fb_format && dest->fb_stride == src->fb_stride) {
			_dbg_assert_msg_(false, "illegal clone of src found");
		}

		if (dest->fb_address == src->fb_address && dest->FbStrideInBytes() == src->FbStrideInBytes() && dest->fb_format == newFormat) {
			vfb = dest;
			break;
		}
	}

	if (!vfb) {
		// Create a clone!
		vfb = new VirtualFramebuffer();
		*vfb = *src;  // Copies everything, but watch out! Can't copy fbo.

		// Adjust width by bpp.
		float widthFactor = (float)BufferFormatBytesPerPixel(vfb->fb_format) / (float)BufferFormatBytesPerPixel(newFormat);

		vfb->width *= widthFactor;
		vfb->bufferWidth *= widthFactor;
		vfb->renderWidth *= widthFactor;
		vfb->drawnWidth *= widthFactor;
		vfb->newWidth *= widthFactor;
		vfb->safeWidth *= widthFactor;

		vfb->fb_format = newFormat;
		// stride stays the same since it's in pixels.

		WARN_LOG(Log::FrameBuf, "Creating %s clone of %08x/%08x/%s (%dx%d -> %dx%d)", GeBufferFormatToString(newFormat), src->fb_address, src->z_address, GeBufferFormatToString(src->fb_format), src->width, src->height, vfb->width, vfb->height);

		char tag[128];
		FormatFramebufferName(vfb, tag, sizeof(tag));
		vfb->fbo = draw_->CreateFramebuffer({ vfb->renderWidth, vfb->renderHeight, 1, GetFramebufferLayers(), 0, true, tag });
		vfbs_.push_back(vfb);
	}

	// OK, now resolve it so we can texture from it.
	// This will do any necessary reinterprets.
	CopyToColorFromOverlappingFramebuffers(vfb);
	// Now we consider the resolved one the latest at the address (though really, we could make them equivalent?).
	vfb->colorBindSeq = GetBindSeqCount();
	return vfb;
}

static void ApplyKillzoneFramebufferSplit(FramebufferHeuristicParams *params, int *drawing_width) {
	// Detect whether we're rendering to the margin.
	bool margin;
	if ((params->scissorRight - params->scissorLeft) == 32) {
		// Title screen has this easy case. It also uses non-through verts, so lucky for us that we have this.
		margin = true;
	} else if (params->scissorRight == 480) {
		margin = false;
	} else {
		// Go deep, look at the vertices. Killzone-specific, of course.
		margin = false;
		if ((gstate.vertType & 0xFFFFFF) == 0x00800102) {  // through, u16, s16
			u16 *vdata = (u16 *)Memory::GetPointerUnchecked(gstate_c.vertexAddr);
			int v0PosU = vdata[0];
			int v0PosX = vdata[2];
			if (v0PosX >= 480 && v0PosU < 480) {
				// Texturing from surface, writing to margin
				margin = true;
			}
		}

		// TODO: Implement this for Burnout Dominator. It has to handle self-reads inside
		// the margin framebuffer though, so framebuffer copies are still needed, just smaller.
		// It uses 0x0080019f (through, float texcoords, ABGR 8888 colors, float positions).
	}

	if (margin) {
		gstate_c.SetCurRTOffset(-480, 0);
		// Modify the fb_address and z_address too to avoid matching below.
		params->fb_address += 480 * 4;
		params->z_address += 480 * 2;
		*drawing_width = 32;
	} else {
		gstate_c.SetCurRTOffset(0, 0);
		*drawing_width = 480;
	}
}

void FramebufferManagerCommon::DrawImGuiDebug(int &selected) const {
	ImGui::BeginTable("framebuffers", 4);
	ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("Color Addr", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("Depth Addr", ImGuiTableColumnFlags_WidthFixed);
	ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);

	ImGui::TableHeadersRow();

	for (int i = 0; i < (int)vfbs_.size(); i++) {
		ImGui::TableNextRow();
		ImGui::TableNextColumn();

		auto &vfb = vfbs_[i];

		const char *tag = vfb->fbo ? vfb->fbo->Tag() : "(no tag)";

		ImGui::PushID(i);
		if (ImGui::Selectable(tag, selected == i, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_SpanAllColumns)) {
			selected = i;
		}
		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
			selected = i;
			ImGui::OpenPopup("framebufferPopup");
		}
		ImGui::TableNextColumn();
		ImGui::Text("%08x", vfb->fb_address);
		ImGui::TableNextColumn();
		ImGui::Text("%08x", vfb->z_address);
		ImGui::TableNextColumn();
		ImGui::Text("%dx%d", vfb->width, vfb->height);
		if (ImGui::BeginPopup("framebufferPopup")) {
			ImGui::Text("Framebuffer: %s", tag);
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::EndTable();

	// Fix out-of-bounds issues when framebuffers are removed.
	if (selected >= vfbs_.size()) {
		selected = -1;
	}

	if (selected != -1) {
		// Now, draw the image of the selected framebuffer.
		Draw::Framebuffer *fb = vfbs_[selected]->fbo;
		ImTextureID texId = ImGui_ImplThin3d_AddFBAsTextureTemp(fb, Draw::Aspect::COLOR_BIT, ImGuiPipeline::TexturedOpaque);
		ImGui::Image(texId, ImVec2(fb->Width(), fb->Height()));
	}
}
