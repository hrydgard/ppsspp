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

#include "Common/System/Display.h"
#include "Common/GPU/OpenGL/GLFeatures.h"

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/TextureDecoder.h"
#include "Common/Data/Convert/ColorConv.h"
#include "Common/GraphicsContext.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Core.h"
#include "Core/Debugger/MemBlockInfo.h"
#include "Core/MemMap.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "Core/Util/PPGeDraw.h"
#include "Common/Profiler/Profiler.h"
#include "Common/GPU/thin3d.h"

#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/PresentationCommon.h"
#include "Common/GPU/ShaderTranslation.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Debugger/Record.h"

const int FB_WIDTH = 480;
const int FB_HEIGHT = 272;

u32 clut[4096];
FormatBuffer fb;
FormatBuffer depthbuf;

SoftGPU::SoftGPU(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw)
{
	fb.data = Memory::GetPointer(0x44000000); // TODO: correct default address?
	depthbuf.data = Memory::GetPointer(0x44000000); // TODO: correct default address?

	framebufferDirty_ = true;
	// TODO: Is there a default?
	displayFramebuf_ = 0;
	displayStride_ = 512;
	displayFormat_ = GE_FORMAT_8888;

	Sampler::Init();
	drawEngine_ = new SoftwareDrawEngine();
	drawEngine_->Init();
	drawEngineCommon_ = drawEngine_;

	if (gfxCtx && draw) {
		presentation_ = new PresentationCommon(draw_);
		presentation_->SetLanguage(draw_->GetShaderLanguageDesc().shaderLanguage);
	}
	Resized();
}

void SoftGPU::DeviceLost() {
	if (presentation_)
		presentation_->DeviceLost();
	draw_ = nullptr;
	if (fbTex) {
		fbTex->Release();
		fbTex = nullptr;
	}
}

void SoftGPU::DeviceRestore() {
	if (PSP_CoreParameter().graphicsContext)
		draw_ = (Draw::DrawContext *)PSP_CoreParameter().graphicsContext->GetDrawContext();
	if (presentation_)
		presentation_->DeviceRestore(draw_);
	PPGeSetDrawContext(draw_);
}

SoftGPU::~SoftGPU() {
	if (fbTex) {
		fbTex->Release();
		fbTex = nullptr;
	}

	if (presentation_) {
		delete presentation_;
	}

	Sampler::Shutdown();
}

void SoftGPU::SetDisplayFramebuffer(u32 framebuf, u32 stride, GEBufferFormat format) {
	// Seems like this can point into RAM, but should be VRAM if not in RAM.
	displayFramebuf_ = (framebuf & 0xFF000000) == 0 ? 0x44000000 | framebuf : framebuf;
	displayStride_ = stride;
	displayFormat_ = format;
	GPUDebug::NotifyDisplay(framebuf, stride, format);
	GPURecord::NotifyDisplay(framebuf, stride, format);
}

DSStretch g_DarkStalkerStretch;

void SoftGPU::ConvertTextureDescFrom16(Draw::TextureDesc &desc, int srcwidth, int srcheight, u8 *overrideData) {
	// TODO: This should probably be converted in a shader instead..
	fbTexBuffer_.resize(srcwidth * srcheight);
	FormatBuffer displayBuffer;
	displayBuffer.data = overrideData ? overrideData : Memory::GetPointer(displayFramebuf_);
	for (int y = 0; y < srcheight; ++y) {
		u32 *buf_line = &fbTexBuffer_[y * srcwidth];
		const u16 *fb_line = &displayBuffer.as16[y * displayStride_];

		switch (displayFormat_) {
		case GE_FORMAT_565:
			ConvertRGB565ToRGBA8888(buf_line, fb_line, srcwidth);
			break;

		case GE_FORMAT_5551:
			ConvertRGBA5551ToRGBA8888(buf_line, fb_line, srcwidth);
			break;

		case GE_FORMAT_4444:
			ConvertRGBA4444ToRGBA8888(buf_line, fb_line, srcwidth);
			break;

		default:
			ERROR_LOG_REPORT(G3D, "Software: Unexpected framebuffer format: %d", displayFormat_);
			break;
		}
	}

	desc.width = srcwidth;
	desc.height = srcheight;
	desc.initData.push_back((uint8_t *)fbTexBuffer_.data());
}

// Copies RGBA8 data from RAM to the currently bound render target.
void SoftGPU::CopyToCurrentFboFromDisplayRam(int srcwidth, int srcheight) {
	if (!draw_ || !presentation_)
		return;
	float u0 = 0.0f;
	float u1;
	float v0 = 0.0f;
	float v1 = 1.0f;

	if (fbTex) {
		fbTex->Release();
		fbTex = nullptr;
	}

	// For accuracy, try to handle 0 stride - sometimes used.
	if (displayStride_ == 0) {
		srcheight = 1;
		u1 = 1.0f;
	} else {
		u1 = (float)srcwidth / displayStride_;
	}

	Draw::TextureDesc desc{};
	desc.type = Draw::TextureType::LINEAR2D;
	desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
	desc.depth = 1;
	desc.mipLevels = 1;
	desc.tag = "SoftGPU";
	bool hasImage = true;

	OutputFlags outputFlags = g_Config.iBufFilter == SCALE_NEAREST ? OutputFlags::NEAREST : OutputFlags::LINEAR;
	bool hasPostShader = presentation_ && presentation_->HasPostShader();

	if (PSP_CoreParameter().compat.flags().DarkStalkersPresentHack && displayFormat_ == GE_FORMAT_5551 && g_DarkStalkerStretch != DSStretch::Off) {
		u8 *data = Memory::GetPointer(0x04088000);
		bool fillDesc = true;
		if (draw_->GetDataFormatSupport(Draw::DataFormat::A1B5G5R5_UNORM_PACK16) & Draw::FMT_TEXTURE) {
			// The perfect one.
			desc.format = Draw::DataFormat::A1B5G5R5_UNORM_PACK16;
		} else if (!hasPostShader && (draw_->GetDataFormatSupport(Draw::DataFormat::A1R5G5B5_UNORM_PACK16) & Draw::FMT_TEXTURE)) {
			// RB swapped, compensate with a shader.
			desc.format = Draw::DataFormat::A1R5G5B5_UNORM_PACK16;
			outputFlags |= OutputFlags::RB_SWIZZLE;
		} else {
			ConvertTextureDescFrom16(desc, srcwidth, srcheight, data);
			fillDesc = false;
		}
		if (fillDesc) {
			desc.width = displayStride_ == 0 ? srcwidth : displayStride_;
			desc.height = srcheight;
			desc.initData.push_back(data);
		}
		u0 = 64.5f / (float)desc.width;
		u1 = 447.5f / (float)desc.width;
		v0 = 16.0f / (float)desc.height;
		v1 = 240.0f / (float)desc.height;
		if (g_DarkStalkerStretch == DSStretch::Normal) {
			outputFlags |= OutputFlags::PILLARBOX;
		}
	} else if (!Memory::IsValidAddress(displayFramebuf_) || srcwidth == 0 || srcheight == 0) {
		hasImage = false;
		u1 = 1.0f;
	} else if (displayFormat_ == GE_FORMAT_8888) {
		u8 *data = Memory::GetPointer(displayFramebuf_);
		desc.width = displayStride_ == 0 ? srcwidth : displayStride_;
		desc.height = srcheight;
		desc.initData.push_back(data);
		desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
	} else if (displayFormat_ == GE_FORMAT_5551) {
		u8 *data = Memory::GetPointer(displayFramebuf_);
		bool fillDesc = true;
		if (draw_->GetDataFormatSupport(Draw::DataFormat::A1B5G5R5_UNORM_PACK16) & Draw::FMT_TEXTURE) {
			// The perfect one.
			desc.format = Draw::DataFormat::A1B5G5R5_UNORM_PACK16;
		} else if (!hasPostShader && (draw_->GetDataFormatSupport(Draw::DataFormat::A1R5G5B5_UNORM_PACK16) & Draw::FMT_TEXTURE)) {
			// RB swapped, compensate with a shader.
			desc.format = Draw::DataFormat::A1R5G5B5_UNORM_PACK16;
			outputFlags |= OutputFlags::RB_SWIZZLE;
		} else {
			ConvertTextureDescFrom16(desc, srcwidth, srcheight);
			u1 = 1.0f;
			fillDesc = false;
		}
		if (fillDesc) {
			desc.width = displayStride_ == 0 ? srcwidth : displayStride_;
			desc.height = srcheight;
			desc.initData.push_back(data);
		}
	} else {
		ConvertTextureDescFrom16(desc, srcwidth, srcheight);
		u1 = 1.0f;
	}
	if (!hasImage) {
		draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE }, "CopyToCurrentFboFromDisplayRam");
		return;
	}

	fbTex = draw_->CreateTexture(desc);

	switch (GetGPUBackend()) {
	case GPUBackend::OPENGL:
		outputFlags |= OutputFlags::BACKBUFFER_FLIPPED;
		break;
	case GPUBackend::DIRECT3D9:
	case GPUBackend::DIRECT3D11:
		outputFlags |= OutputFlags::POSITION_FLIPPED;
		break;
	case GPUBackend::VULKAN:
		break;
	}

	presentation_->SourceTexture(fbTex, desc.width, desc.height);
	presentation_->CopyToOutput(outputFlags, g_Config.iInternalScreenRotation, u0, v0, u1, v1);
}

void SoftGPU::CopyDisplayToOutput(bool reallyDirty) {
	// The display always shows 480x272.
	CopyToCurrentFboFromDisplayRam(FB_WIDTH, FB_HEIGHT);
	framebufferDirty_ = false;
}

void SoftGPU::Resized() {
	// Force the render params to 480x272 so other things work.
	if (g_Config.IsPortrait()) {
		PSP_CoreParameter().renderWidth = 272;
		PSP_CoreParameter().renderHeight = 480;
	} else {
		PSP_CoreParameter().renderWidth = 480;
		PSP_CoreParameter().renderHeight = 272;
	}

	if (presentation_) {
		presentation_->UpdateSize(PSP_CoreParameter().pixelWidth, PSP_CoreParameter().pixelHeight, PSP_CoreParameter().renderWidth, PSP_CoreParameter().renderHeight);
		presentation_->UpdatePostShader();
	}
}

void SoftGPU::FastRunLoop(DisplayList &list) {
	PROFILE_THIS_SCOPE("soft_runloop");
	for (; downcount > 0; --downcount) {
		u32 op = Memory::ReadUnchecked_U32(list.pc);
		u32 cmd = op >> 24;

		u32 diff = op ^ gstate.cmdmem[cmd];
		gstate.cmdmem[cmd] = op;
		ExecuteOp(op, diff);

		list.pc += 4;
	}
}

void SoftGPU::ExecuteOp(u32 op, u32 diff) {
	u32 cmd = op >> 24;
	u32 data = op & 0xFFFFFF;

	// Handle control and drawing commands here directly. The others we delegate.
	switch (cmd) {
	case GE_CMD_BASE:
		break;

	case GE_CMD_VADDR:
		gstate_c.vertexAddr = gstate_c.getRelativeAddress(data);
		break;

	case GE_CMD_IADDR:
		gstate_c.indexAddr	= gstate_c.getRelativeAddress(data);
		break;

	case GE_CMD_PRIM:
		{
			u32 count = data & 0xFFFF;
			// Upper bits are ignored.
			GEPrimitiveType prim = static_cast<GEPrimitiveType>((data >> 16) & 7);

			if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
				ERROR_LOG_REPORT(G3D, "Software: Bad vertex address %08x!", gstate_c.vertexAddr);
				break;
			}

			void *verts = Memory::GetPointer(gstate_c.vertexAddr);
			void *indices = NULL;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG_REPORT(G3D, "Software: Bad index address %08x!", gstate_c.indexAddr);
					break;
				}
				indices = Memory::GetPointer(gstate_c.indexAddr);
			}

			cyclesExecuted += EstimatePerVertexCost() * count;
			int bytesRead;
			drawEngine_->transformUnit.SubmitPrimitive(verts, indices, prim, count, gstate.vertType, &bytesRead, drawEngine_);
			framebufferDirty_ = true;

			// After drawing, we advance the vertexAddr (when non indexed) or indexAddr (when indexed).
			// Some games rely on this, they don't bother reloading VADDR and IADDR.
			// The VADDR/IADDR registers are NOT updated.
			AdvanceVerts(gstate.vertType, count, bytesRead);
		}
		break;

	case GE_CMD_BEZIER:
		{
			// We don't dirty on normal changes anymore as we prescale, but it's needed for splines/bezier.
			gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

			// This also make skipping drawing very effective.
			if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB))	{
				// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
				return;
			}

			if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
				ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
				return;
			}

			void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
			void *indices = NULL;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
					return;
				}
				indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
			}

			if ((gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) || vertTypeIsSkinningEnabled(gstate.vertType)) {
				DEBUG_LOG_REPORT(G3D, "Unusual bezier/spline vtype: %08x, morph: %d, bones: %d", gstate.vertType, (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT, vertTypeGetNumBoneWeights(gstate.vertType));
			}

			Spline::BezierSurface surface;
			surface.tess_u = gstate.getPatchDivisionU();
			surface.tess_v = gstate.getPatchDivisionV();
			surface.num_points_u = op & 0xFF;
			surface.num_points_v = (op >> 8) & 0xFF;
			surface.num_patches_u = (surface.num_points_u - 1) / 3;
			surface.num_patches_v = (surface.num_points_v - 1) / 3;
			surface.primType = gstate.getPatchPrimitiveType();
			surface.patchFacing = gstate.patchfacing & 1;

			SetDrawType(DRAW_BEZIER, PatchPrimToPrim(surface.primType));

			int bytesRead = 0;
			drawEngineCommon_->SubmitCurve(control_points, indices, surface, gstate.vertType, &bytesRead, "bezier");
			framebufferDirty_ = true;

			// After drawing, we advance pointers - see SubmitPrim which does the same.
			int count = surface.num_points_u * surface.num_points_v;
			AdvanceVerts(gstate.vertType, count, bytesRead);
		}
		break;

	case GE_CMD_SPLINE:
		{
			// We don't dirty on normal changes anymore as we prescale, but it's needed for splines/bezier.
			gstate_c.Dirty(DIRTY_UVSCALEOFFSET);

			// This also make skipping drawing very effective.
			if (gstate_c.skipDrawReason & (SKIPDRAW_SKIPFRAME | SKIPDRAW_NON_DISPLAYED_FB))	{
				// TODO: Should this eat some cycles?  Probably yes.  Not sure if important.
				return;
			}

			if (!Memory::IsValidAddress(gstate_c.vertexAddr)) {
				ERROR_LOG_REPORT(G3D, "Bad vertex address %08x!", gstate_c.vertexAddr);
				return;
			}

			void *control_points = Memory::GetPointerUnchecked(gstate_c.vertexAddr);
			void *indices = NULL;
			if ((gstate.vertType & GE_VTYPE_IDX_MASK) != GE_VTYPE_IDX_NONE) {
				if (!Memory::IsValidAddress(gstate_c.indexAddr)) {
					ERROR_LOG_REPORT(G3D, "Bad index address %08x!", gstate_c.indexAddr);
					return;
				}
				indices = Memory::GetPointerUnchecked(gstate_c.indexAddr);
			}

			if ((gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) || vertTypeIsSkinningEnabled(gstate.vertType)) {
				DEBUG_LOG_REPORT(G3D, "Unusual bezier/spline vtype: %08x, morph: %d, bones: %d", gstate.vertType, (gstate.vertType & GE_VTYPE_MORPHCOUNT_MASK) >> GE_VTYPE_MORPHCOUNT_SHIFT, vertTypeGetNumBoneWeights(gstate.vertType));
			}

			Spline::SplineSurface surface;
			surface.tess_u = gstate.getPatchDivisionU();
			surface.tess_v = gstate.getPatchDivisionV();
			surface.type_u = (op >> 16) & 0x3;
			surface.type_v = (op >> 18) & 0x3;
			surface.num_points_u = op & 0xFF;
			surface.num_points_v = (op >> 8) & 0xFF;
			surface.num_patches_u = surface.num_points_u - 3;
			surface.num_patches_v = surface.num_points_v - 3;
			surface.primType = gstate.getPatchPrimitiveType();
			surface.patchFacing = gstate.patchfacing & 1;

			SetDrawType(DRAW_SPLINE, PatchPrimToPrim(surface.primType));

			int bytesRead = 0;
			drawEngineCommon_->SubmitCurve(control_points, indices, surface, gstate.vertType, &bytesRead, "spline");
			framebufferDirty_ = true;

			// After drawing, we advance pointers - see SubmitPrim which does the same.
			int count = surface.num_points_u * surface.num_points_v;
			AdvanceVerts(gstate.vertType, count, bytesRead);
		}
		break;

	case GE_CMD_BOUNDINGBOX:
		if (data == 0) {
			currentList->bboxResult = false;
		} else if (((data & 7) == 0) && data <= 64) {  // Sanity check
			DEBUG_LOG(G3D, "Unsupported bounding box: %06x", data);
			void *control_points = Memory::GetPointer(gstate_c.vertexAddr);
			if (!control_points) {
				ERROR_LOG_REPORT_ONCE(boundingbox, G3D, "Invalid verts in bounding box check");
				currentList->bboxResult = true;
				return;
			}

			if (gstate.vertType & GE_VTYPE_IDX_MASK) {
				ERROR_LOG_REPORT_ONCE(boundingbox, G3D, "Indexed bounding box data not supported.");
				// Data seems invalid. Let's assume the box test passed.
				currentList->bboxResult = true;
				return;
			}

			// Test if the bounding box is within the drawing region.
			int bytesRead;
			currentList->bboxResult = drawEngineCommon_->TestBoundingBox(control_points, data, gstate.vertType, &bytesRead);
			AdvanceVerts(gstate.vertType, data, bytesRead);
		} else {
			ERROR_LOG_REPORT_ONCE(boundingbox, G3D, "Bad bounding box data: %06x", data);
			// Data seems invalid. Let's assume the box test passed.
			currentList->bboxResult = true;
		}
		break;

	case GE_CMD_VERTEXTYPE:
		break;

	case GE_CMD_REGION1:
	case GE_CMD_REGION2:
		break;

	case GE_CMD_DEPTHCLAMPENABLE:
		break;

	case GE_CMD_CULLFACEENABLE:
	case GE_CMD_CULL:
		break;

	case GE_CMD_TEXTUREMAPENABLE: 
		break;

	case GE_CMD_LIGHTINGENABLE:
		break;

	case GE_CMD_FOGCOLOR:
	case GE_CMD_FOG1:
	case GE_CMD_FOG2:
	case GE_CMD_FOGENABLE:
		break;

	case GE_CMD_DITHERENABLE:
		break;

	case GE_CMD_OFFSETX:
		break;

	case GE_CMD_OFFSETY:
		break;

	case GE_CMD_TEXSCALEU:
		gstate_c.uv.uScale = getFloat24(data);
		break;

	case GE_CMD_TEXSCALEV:
		gstate_c.uv.vScale = getFloat24(data);
		break;

	case GE_CMD_TEXOFFSETU:
		gstate_c.uv.uOff = getFloat24(data);
		break;

	case GE_CMD_TEXOFFSETV:
		gstate_c.uv.vOff = getFloat24(data);
		break;

	case GE_CMD_SCISSOR1:
	case GE_CMD_SCISSOR2:
		break;

	case GE_CMD_MINZ:
		break;

	case GE_CMD_FRAMEBUFPTR:
		fb.data = Memory::GetPointer(gstate.getFrameBufAddress());
		break;

	case GE_CMD_FRAMEBUFWIDTH:
		fb.data = Memory::GetPointer(gstate.getFrameBufAddress());
		break;

	case GE_CMD_FRAMEBUFPIXFORMAT:
		break;

	case GE_CMD_TEXADDR0:
	case GE_CMD_TEXADDR1:
	case GE_CMD_TEXADDR2:
	case GE_CMD_TEXADDR3:
	case GE_CMD_TEXADDR4:
	case GE_CMD_TEXADDR5:
	case GE_CMD_TEXADDR6:
	case GE_CMD_TEXADDR7:
		break;

	case GE_CMD_TEXBUFWIDTH0:
	case GE_CMD_TEXBUFWIDTH1:
	case GE_CMD_TEXBUFWIDTH2:
	case GE_CMD_TEXBUFWIDTH3:
	case GE_CMD_TEXBUFWIDTH4:
	case GE_CMD_TEXBUFWIDTH5:
	case GE_CMD_TEXBUFWIDTH6:
	case GE_CMD_TEXBUFWIDTH7:
		break;

	case GE_CMD_CLUTADDR:
	case GE_CMD_CLUTADDRUPPER:
		break;

	case GE_CMD_LOADCLUT:
		{
			u32 clutAddr = gstate.getClutAddress();
			u32 clutTotalBytes = gstate.getClutLoadBytes();

			if (Memory::IsValidAddress(clutAddr)) {
				u32 validSize = Memory::ValidSize(clutAddr, clutTotalBytes);
				Memory::MemcpyUnchecked(clut, clutAddr, validSize);
				if (validSize < clutTotalBytes) {
					// Zero out the parts that were outside valid memory.
					memset((u8 *)clut + validSize, 0x00, clutTotalBytes - validSize);
				}
			} else if (clutAddr != 0) {
				// Some invalid addresses trigger a crash, others fill with zero.  We always fill zero.
				DEBUG_LOG(G3D, "Software: Invalid CLUT address, filling with garbage instead of crashing");
				memset(clut, 0x00, clutTotalBytes);
			}
		}
		break;

	// Don't need to do anything, just state for transferstart.
	case GE_CMD_TRANSFERSRC:
	case GE_CMD_TRANSFERSRCW:
	case GE_CMD_TRANSFERDST:
	case GE_CMD_TRANSFERDSTW:
	case GE_CMD_TRANSFERSRCPOS:
	case GE_CMD_TRANSFERDSTPOS:
	case GE_CMD_TRANSFERSIZE:
		break;

	case GE_CMD_TRANSFERSTART:
		{
			u32 srcBasePtr = gstate.getTransferSrcAddress();
			u32 srcStride = gstate.getTransferSrcStride();

			u32 dstBasePtr = gstate.getTransferDstAddress();
			u32 dstStride = gstate.getTransferDstStride();

			int srcX = gstate.getTransferSrcX();
			int srcY = gstate.getTransferSrcY();

			int dstX = gstate.getTransferDstX();
			int dstY = gstate.getTransferDstY();

			int width = gstate.getTransferWidth();
			int height = gstate.getTransferHeight();

			int bpp = gstate.getTransferBpp();

			DEBUG_LOG(G3D, "Block transfer: %08x/%x -> %08x/%x, %ix%ix%i (%i,%i)->(%i,%i)", srcBasePtr, srcStride, dstBasePtr, dstStride, width, height, bpp, srcX, srcY, dstX, dstY);

			for (int y = 0; y < height; y++) {
				const u8 *src = Memory::GetPointer(srcBasePtr + ((y + srcY) * srcStride + srcX) * bpp);
				u8 *dst = Memory::GetPointer(dstBasePtr + ((y + dstY) * dstStride + dstX) * bpp);
				memcpy(dst, src, width * bpp);
			}

			const uint32_t src = srcBasePtr + (srcY * srcStride + srcX) * bpp;
			const uint32_t srcSize = height * srcStride * bpp;
			const std::string tag = "GPUBlockTransfer/" + GetMemWriteTagAt(src, srcSize);
			NotifyMemInfo(MemBlockFlags::READ, src, srcSize, tag.c_str(), tag.size());
			NotifyMemInfo(MemBlockFlags::WRITE, dstBasePtr + (dstY * dstStride + dstX) * bpp, height * dstStride * bpp, tag.c_str(), tag.size());

			// TODO: Correct timing appears to be 1.9, but erring a bit low since some of our other timing is inaccurate.
			cyclesExecuted += ((height * width * bpp) * 16) / 10;

			// Could theoretically dirty the framebuffer.
			framebufferDirty_ = true;
			break;
		}

	case GE_CMD_TEXSIZE0:
	case GE_CMD_TEXSIZE1:
	case GE_CMD_TEXSIZE2:
	case GE_CMD_TEXSIZE3:
	case GE_CMD_TEXSIZE4:
	case GE_CMD_TEXSIZE5:
	case GE_CMD_TEXSIZE6:
	case GE_CMD_TEXSIZE7:
		break;

	case GE_CMD_ZBUFPTR:
		depthbuf.data = Memory::GetPointer(gstate.getDepthBufAddress());
		break;

	case GE_CMD_ZBUFWIDTH:
		depthbuf.data = Memory::GetPointer(gstate.getDepthBufAddress());
		break;

	case GE_CMD_AMBIENTCOLOR:
	case GE_CMD_AMBIENTALPHA:
	case GE_CMD_MATERIALAMBIENT:
	case GE_CMD_MATERIALDIFFUSE:
	case GE_CMD_MATERIALEMISSIVE:
	case GE_CMD_MATERIALSPECULAR:
	case GE_CMD_MATERIALALPHA:
	case GE_CMD_MATERIALSPECULARCOEF:
		break;

	case GE_CMD_LIGHTTYPE0:
	case GE_CMD_LIGHTTYPE1:
	case GE_CMD_LIGHTTYPE2:
	case GE_CMD_LIGHTTYPE3:
		break;

	case GE_CMD_LX0:case GE_CMD_LY0:case GE_CMD_LZ0:
	case GE_CMD_LX1:case GE_CMD_LY1:case GE_CMD_LZ1:
	case GE_CMD_LX2:case GE_CMD_LY2:case GE_CMD_LZ2:
	case GE_CMD_LX3:case GE_CMD_LY3:case GE_CMD_LZ3:
		break;

	case GE_CMD_LDX0:case GE_CMD_LDY0:case GE_CMD_LDZ0:
	case GE_CMD_LDX1:case GE_CMD_LDY1:case GE_CMD_LDZ1:
	case GE_CMD_LDX2:case GE_CMD_LDY2:case GE_CMD_LDZ2:
	case GE_CMD_LDX3:case GE_CMD_LDY3:case GE_CMD_LDZ3:
		break;

	case GE_CMD_LKA0:case GE_CMD_LKB0:case GE_CMD_LKC0:
	case GE_CMD_LKA1:case GE_CMD_LKB1:case GE_CMD_LKC1:
	case GE_CMD_LKA2:case GE_CMD_LKB2:case GE_CMD_LKC2:
	case GE_CMD_LKA3:case GE_CMD_LKB3:case GE_CMD_LKC3:
		break;

	case GE_CMD_LAC0:case GE_CMD_LAC1:case GE_CMD_LAC2:case GE_CMD_LAC3:
	case GE_CMD_LDC0:case GE_CMD_LDC1:case GE_CMD_LDC2:case GE_CMD_LDC3:
	case GE_CMD_LSC0:case GE_CMD_LSC1:case GE_CMD_LSC2:case GE_CMD_LSC3:
		break;

	case GE_CMD_VIEWPORTXSCALE:
	case GE_CMD_VIEWPORTYSCALE:
	case GE_CMD_VIEWPORTZSCALE:
	case GE_CMD_VIEWPORTXCENTER:
	case GE_CMD_VIEWPORTYCENTER:
	case GE_CMD_VIEWPORTZCENTER:
		break;

	case GE_CMD_LIGHTENABLE0:
	case GE_CMD_LIGHTENABLE1:
	case GE_CMD_LIGHTENABLE2:
	case GE_CMD_LIGHTENABLE3:
		break;

	case GE_CMD_LIGHTMODE:
		break;

	case GE_CMD_PATCHDIVISION:
		break;

	case GE_CMD_MATERIALUPDATE:
		break;

	//////////////////////////////////////////////////////////////////
	//	CLEARING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_CLEARMODE:
		break;

	//////////////////////////////////////////////////////////////////
	//	ALPHA BLENDING
	//////////////////////////////////////////////////////////////////
	case GE_CMD_ALPHABLENDENABLE:
		break;

	case GE_CMD_BLENDMODE:
		break;

	case GE_CMD_BLENDFIXEDA:
	case GE_CMD_BLENDFIXEDB:
		break;

	case GE_CMD_ALPHATESTENABLE:
	case GE_CMD_ALPHATEST:
		break;

	case GE_CMD_TEXFUNC:
	case GE_CMD_TEXFILTER:
		break;

	//////////////////////////////////////////////////////////////////
	//	Z/STENCIL TESTING
	//////////////////////////////////////////////////////////////////

	case GE_CMD_ZTESTENABLE:
	case GE_CMD_STENCILTESTENABLE:
	case GE_CMD_ZTEST:
		break;

	case GE_CMD_MORPHWEIGHT0:
	case GE_CMD_MORPHWEIGHT1:
	case GE_CMD_MORPHWEIGHT2:
	case GE_CMD_MORPHWEIGHT3:
	case GE_CMD_MORPHWEIGHT4:
	case GE_CMD_MORPHWEIGHT5:
	case GE_CMD_MORPHWEIGHT6:
	case GE_CMD_MORPHWEIGHT7:
		gstate_c.morphWeights[cmd - GE_CMD_MORPHWEIGHT0] = getFloat24(data);
		break;
 
	case GE_CMD_DITH0:
	case GE_CMD_DITH1:
	case GE_CMD_DITH2:
	case GE_CMD_DITH3:
		break;

	case GE_CMD_WORLDMATRIXNUMBER:
		gstate.worldmtxnum = data & 0xF;
		break;

	case GE_CMD_WORLDMATRIXDATA:
		{
			int num = gstate.worldmtxnum & 0xF;
			if (num < 12) {
				gstate.worldMatrix[num] = getFloat24(data);
			}
			gstate.worldmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_VIEWMATRIXNUMBER:
		gstate.viewmtxnum = data & 0xF;
		break;

	case GE_CMD_VIEWMATRIXDATA:
		{
			int num = gstate.viewmtxnum & 0xF;
			if (num < 12) {
				gstate.viewMatrix[num] = getFloat24(data);
			}
			gstate.viewmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_PROJMATRIXNUMBER:
		gstate.projmtxnum = data & 0x1F;
		break;

	case GE_CMD_PROJMATRIXDATA:
		{
			int num = gstate.projmtxnum & 0x1F; // NOTE: Changed from 0xF to catch overflows
			gstate.projMatrix[num] = getFloat24(data);
			if (num <= 16)
				gstate.projmtxnum = (++num) & 0x1F;
		}
		break;

	case GE_CMD_TGENMATRIXNUMBER:
		gstate.texmtxnum = data&0xF;
		break;

	case GE_CMD_TGENMATRIXDATA:
		{
			int num = gstate.texmtxnum & 0xF;
			if (num < 12) {
				gstate.tgenMatrix[num] = getFloat24(data);
			}
			gstate.texmtxnum = (++num) & 0xF;
		}
		break;

	case GE_CMD_BONEMATRIXNUMBER:
		gstate.boneMatrixNumber = data & 0x7F;
		break;

	case GE_CMD_BONEMATRIXDATA:
		{
			int num = gstate.boneMatrixNumber & 0x7F;
			if (num < 96) {
				gstate.boneMatrix[num] = getFloat24(data);
			}
			gstate.boneMatrixNumber = (++num) & 0x7F;
		}
		break;

	default:
		GPUCommon::ExecuteOp(op, diff);
		break;
	}
}

void SoftGPU::GetStats(char *buffer, size_t bufsize) {
	snprintf(buffer, bufsize, "SoftGPU: (N/A)");
}

void SoftGPU::InvalidateCache(u32 addr, int size, GPUInvalidationType type)
{
	// Nothing to invalidate.
}

void SoftGPU::NotifyVideoUpload(u32 addr, int size, int width, int format)
{
	// Ignore.
}

bool SoftGPU::PerformMemoryCopy(u32 dest, u32 src, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	GPURecord::NotifyMemcpy(dest, src, size);
	// Let's just be safe.
	framebufferDirty_ = true;
	return false;
}

bool SoftGPU::PerformMemorySet(u32 dest, u8 v, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	GPURecord::NotifyMemset(dest, v, size);
	// Let's just be safe.
	framebufferDirty_ = true;
	return false;
}

bool SoftGPU::PerformMemoryDownload(u32 dest, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	return false;
}

bool SoftGPU::PerformMemoryUpload(u32 dest, int size)
{
	// Nothing to update.
	InvalidateCache(dest, size, GPU_INVALIDATE_HINT);
	GPURecord::NotifyUpload(dest, size);
	return false;
}

bool SoftGPU::PerformStencilUpload(u32 dest, int size)
{
	return false;
}

bool SoftGPU::FramebufferDirty() {
	if (g_Config.iFrameSkip != 0) {
		bool dirty = framebufferDirty_;
		framebufferDirty_ = false;
		return dirty;
	}
	return true;
}

bool SoftGPU::GetCurrentFramebuffer(GPUDebugBuffer &buffer, GPUDebugFramebufferType type, int maxRes) {
	int x1 = gstate.getRegionX1();
	int y1 = gstate.getRegionY1();
	int x2 = gstate.getRegionX2() + 1;
	int y2 = gstate.getRegionY2() + 1;
	int stride = gstate.FrameBufStride();
	GEBufferFormat fmt = gstate.FrameBufFormat();

	if (type == GPU_DBG_FRAMEBUF_DISPLAY) {
		x1 = 0;
		y1 = 0;
		x2 = 480;
		y2 = 272;
		stride = displayStride_;
		fmt = displayFormat_;
	}

	buffer.Allocate(x2 - x1, y2 - y1, fmt);

	const int depth = fmt == GE_FORMAT_8888 ? 4 : 2;
	const u8 *src = fb.data + stride * depth * y1;
	u8 *dst = buffer.GetData();
	const int byteWidth = (x2 - x1) * depth;
	for (int y = y1; y < y2; ++y) {
		memcpy(dst, src + x1, byteWidth);
		dst += byteWidth;
		src += stride * depth;
	}
	return true;
}

bool SoftGPU::GetOutputFramebuffer(GPUDebugBuffer &buffer) {
	return GetCurrentFramebuffer(buffer, GPU_DBG_FRAMEBUF_DISPLAY, 1);
}

bool SoftGPU::GetCurrentDepthbuffer(GPUDebugBuffer &buffer)
{
	const int w = gstate.getRegionX2() - gstate.getRegionX1() + 1;
	const int h = gstate.getRegionY2() - gstate.getRegionY1() + 1;
	buffer.Allocate(w, h, GPU_DBG_FORMAT_16BIT);

	const int depth = 2;
	const u8 *src = depthbuf.data + gstate.DepthBufStride() * depth * gstate.getRegionY1();
	u8 *dst = buffer.GetData();
	for (int y = gstate.getRegionY1(); y <= gstate.getRegionY2(); ++y) {
		memcpy(dst, src + gstate.getRegionX1(), (gstate.getRegionX2() + 1) * depth);
		dst += w * depth;
		src += gstate.DepthBufStride() * depth;
	}
	return true;
}

bool SoftGPU::GetCurrentStencilbuffer(GPUDebugBuffer &buffer)
{
	return Rasterizer::GetCurrentStencilbuffer(buffer);
}

bool SoftGPU::GetCurrentTexture(GPUDebugBuffer &buffer, int level)
{
	return Rasterizer::GetCurrentTexture(buffer, level);
}

bool SoftGPU::GetCurrentClut(GPUDebugBuffer &buffer)
{
	const u32 bpp = gstate.getClutPaletteFormat() == GE_CMODE_32BIT_ABGR8888 ? 4 : 2;
	const u32 pixels = 1024 / bpp;

	buffer.Allocate(pixels, 1, (GEBufferFormat)gstate.getClutPaletteFormat());
	memcpy(buffer.GetData(), clut, 1024);
	return true;
}

bool SoftGPU::GetCurrentSimpleVertices(int count, std::vector<GPUDebugVertex> &vertices, std::vector<u16> &indices)
{
	return drawEngine_->transformUnit.GetCurrentSimpleVertices(count, vertices, indices);
}

bool SoftGPU::DescribeCodePtr(const u8 *ptr, std::string &name) {
	std::string subname;
	if (Sampler::DescribeCodePtr(ptr, subname)) {
		name = "SamplerJit:" + subname;
		return true;
	}
	return false;
}
