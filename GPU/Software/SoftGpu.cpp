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

#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "GPU/Common/TextureDecoder.h"
#include "Common/ColorConv.h"
#include "Common/GraphicsContext.h"
#include "Core/Config.h"
#include "Core/ConfigValues.h"
#include "Core/Debugger/Breakpoints.h"
#include "Core/MemMap.h"
#include "Core/HLE/sceKernelInterrupt.h"
#include "Core/HLE/sceGe.h"
#include "Core/MIPS/MIPS.h"
#include "Core/Reporting.h"
#include "Core/Core.h"
#include "profiler/profiler.h"
#include "thin3d/thin3d.h"

#include "GPU/Software/Rasterizer.h"
#include "GPU/Software/Sampler.h"
#include "GPU/Software/SoftGpu.h"
#include "GPU/Software/TransformUnit.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/FramebufferCommon.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Debugger/Record.h"

const int FB_WIDTH = 480;
const int FB_HEIGHT = 272;

struct Vertex {
	float x, y, z;
	float u, v;
	uint32_t rgba;
};

u32 clut[4096];
FormatBuffer fb;
FormatBuffer depthbuf;

SoftGPU::SoftGPU(GraphicsContext *gfxCtx, Draw::DrawContext *draw)
	: GPUCommon(gfxCtx, draw)
{
	using namespace Draw;
	fbTex = nullptr;
	InputLayoutDesc inputDesc = {
		{
			{ sizeof(Vertex), false },
		},
		{
			{ 0, SEM_POSITION, DataFormat::R32G32B32_FLOAT, 0 },
			{ 0, SEM_TEXCOORD0, DataFormat::R32G32_FLOAT, 12 },
			{ 0, SEM_COLOR0, DataFormat::R8G8B8A8_UNORM, 20 },
		},
	};

	ShaderModule *vshader = draw_->GetVshaderPreset(VS_TEXTURE_COLOR_2D);

	vdata = draw_->CreateBuffer(sizeof(Vertex) * 4, BufferUsageFlag::DYNAMIC | BufferUsageFlag::VERTEXDATA);
	idata = draw_->CreateBuffer(sizeof(int) * 6, BufferUsageFlag::DYNAMIC | BufferUsageFlag::INDEXDATA);

	InputLayout *inputLayout = draw_->CreateInputLayout(inputDesc);
	DepthStencilState *depth = draw_->CreateDepthStencilState({ false, false, Comparison::LESS });
	BlendState *blendstateOff = draw_->CreateBlendState({ false, 0xF });
	RasterState *rasterNoCull = draw_->CreateRasterState({});

	samplerNearest = draw_->CreateSamplerState({ TextureFilter::NEAREST, TextureFilter::NEAREST, TextureFilter::NEAREST });
	samplerLinear = draw_->CreateSamplerState({ TextureFilter::LINEAR, TextureFilter::LINEAR, TextureFilter::LINEAR });

	PipelineDesc pipelineDesc{
		Primitive::TRIANGLE_LIST,
		{ draw_->GetVshaderPreset(VS_TEXTURE_COLOR_2D), draw_->GetFshaderPreset(FS_TEXTURE_COLOR_2D) },
		inputLayout, depth, blendstateOff, rasterNoCull, &vsTexColBufDesc
	};
	texColor = draw_->CreateGraphicsPipeline(pipelineDesc);
	inputLayout->Release();
	depth->Release();
	blendstateOff->Release();
	rasterNoCull->Release();

	fb.data = Memory::GetPointer(0x44000000); // TODO: correct default address?
	depthbuf.data = Memory::GetPointer(0x44000000); // TODO: correct default address?

	framebufferDirty_ = true;
	// TODO: Is there a default?
	displayFramebuf_ = 0;
	displayStride_ = 512;
	displayFormat_ = GE_FORMAT_8888;

	Sampler::Init();
	drawEngine_ = new SoftwareDrawEngine();
	drawEngineCommon_ = drawEngine_;
}

void SoftGPU::DeviceLost() {
	// Handled by thin3d.
}

void SoftGPU::DeviceRestore() {
	// Handled by thin3d.
}

SoftGPU::~SoftGPU() {
	texColor->Release();
	texColor = nullptr;

	if (fbTex) {
		fbTex->Release();
		fbTex = nullptr;
	}
	vdata->Release();
	vdata = nullptr;
	idata->Release();
	idata = nullptr;
	samplerNearest->Release();
	samplerNearest = nullptr;
	samplerLinear->Release();
	samplerLinear = nullptr;

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

// Copies RGBA8 data from RAM to the currently bound render target.
void SoftGPU::CopyToCurrentFboFromDisplayRam(int srcwidth, int srcheight) {
	if (!draw_)
		return;
	float u0 = 0.0f;
	float u1;

	if (fbTex) {
		fbTex->Release();
		fbTex = nullptr;
	}

	// For accuracy, try to handle 0 stride - sometimes used.
	if (displayStride_ == 0) {
		srcheight = 1;
	}

	Draw::TextureDesc desc{};
	desc.type = Draw::TextureType::LINEAR2D;
	desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
	desc.depth = 1;
	desc.mipLevels = 1;
	desc.tag = "SoftGPU";
	bool hasImage = true;
	if (!Memory::IsValidAddress(displayFramebuf_) || srcwidth == 0 || srcheight == 0) {
		hasImage = false;
		u1 = 1.0f;
	} else if (displayFormat_ == GE_FORMAT_8888) {
		u8 *data = Memory::GetPointer(displayFramebuf_);
		desc.width = displayStride_ == 0 ? srcwidth : displayStride_;
		desc.height = srcheight;
		desc.initData.push_back(data);
		desc.format = Draw::DataFormat::R8G8B8A8_UNORM;
		if (displayStride_ != 0) {
			u1 = (float)srcwidth / displayStride_;
		} else {
			u1 = 1.0f;
		}
	} else {
		// TODO: This should probably be converted in a shader instead..
		fbTexBuffer.resize(srcwidth * srcheight);
		FormatBuffer displayBuffer;
		displayBuffer.data = Memory::GetPointer(displayFramebuf_);
		for (int y = 0; y < srcheight; ++y) {
			u32 *buf_line = &fbTexBuffer[y * srcwidth];
			const u16 *fb_line = &displayBuffer.as16[y * displayStride_];

			switch (displayFormat_) {
			case GE_FORMAT_565:
				ConvertRGBA565ToRGBA8888(buf_line, fb_line, srcwidth);
				break;

			case GE_FORMAT_5551:
				ConvertRGBA5551ToRGBA8888(buf_line, fb_line, srcwidth);
				break;

			case GE_FORMAT_4444:
				ConvertRGBA4444ToRGBA8888(buf_line, fb_line, srcwidth);
				break;

			default:
				ERROR_LOG_REPORT(G3D, "Software: Unexpected framebuffer format: %d", displayFormat_);
			}
		}

		desc.width = srcwidth;
		desc.height = srcheight;
		desc.initData.push_back((uint8_t *)fbTexBuffer.data());
		u1 = 1.0f;
	}
	if (!hasImage) {
		draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
		return;
	}

	fbTex = draw_->CreateTexture(desc);

	float dstwidth = (float)PSP_CoreParameter().pixelWidth;
	float dstheight = (float)PSP_CoreParameter().pixelHeight;

	float x, y, w, h;
	CenterDisplayOutputRect(&x, &y, &w, &h, 480.0f, 272.0f, dstwidth, dstheight, ROTATION_LOCKED_HORIZONTAL);

	if (GetGPUBackend() == GPUBackend::DIRECT3D9) {
		x += 0.5f;
		y += 0.5f;
	}

	x /= 0.5f * dstwidth;
	y /= 0.5f * dstheight;
	w /= 0.5f * dstwidth;
	h /= 0.5f * dstheight;
	float x2 = x + w;
	float y2 = y + h;
	x -= 1.0f;
	y -= 1.0f;
	x2 -= 1.0f;
	y2 -= 1.0f;

	float v0 = 1.0f;
	float v1 = 0.0f;

	if (GetGPUBackend() == GPUBackend::VULKAN) {
		std::swap(v0, v1);
	}
	draw_->BindFramebufferAsRenderTarget(nullptr, { Draw::RPAction::CLEAR, Draw::RPAction::DONT_CARE, Draw::RPAction::DONT_CARE });
	Draw::Viewport viewport = { 0.0f, 0.0f, dstwidth, dstheight, 0.0f, 1.0f };
	draw_->SetViewports(1, &viewport);
	draw_->SetScissorRect(0, 0, dstwidth, dstheight);

	Draw::SamplerState *sampler;
	if (g_Config.iBufFilter == SCALE_NEAREST) {
		sampler = samplerNearest;
	} else {
		sampler = samplerLinear;
	}
	draw_->BindSamplerStates(0, 1, &sampler);

	const Vertex verts[4] = {
		{ x, y, 0,    u0, v0,  0xFFFFFFFF }, // TL
		{ x, y2, 0,   u0, v1,  0xFFFFFFFF }, // BL
		{ x2, y2, 0,  u1, v1,  0xFFFFFFFF }, // BR
		{ x2, y, 0,   u1, v0,  0xFFFFFFFF }, // TR
	};
	draw_->UpdateBuffer(vdata, (const uint8_t *)verts, 0, sizeof(verts), Draw::UPDATE_DISCARD);

	int indexes[] = { 0, 1, 2, 0, 2, 3 };
	draw_->UpdateBuffer(idata, (const uint8_t *)indexes, 0, sizeof(indexes), Draw::UPDATE_DISCARD);

	draw_->BindTexture(0, fbTex);

	static const float identity4x4[16] = {
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
	};

	Draw::VsTexColUB ub{};
	memcpy(ub.WorldViewProj, identity4x4, sizeof(float) * 16);
	draw_->BindPipeline(texColor);
	draw_->UpdateDynamicUniformBuffer(&ub, sizeof(ub));
	draw_->BindVertexBuffers(0, 1, &vdata, nullptr);
	draw_->BindIndexBuffer(idata, 0);
	draw_->DrawIndexed(6, 0);
	draw_->BindIndexBuffer(nullptr, 0);
}

void SoftGPU::CopyDisplayToOutput() {
	// The display always shows 480x272.
	CopyToCurrentFboFromDisplayRam(FB_WIDTH, FB_HEIGHT);
	framebufferDirty_ = false;

	// Force the render params to 480x272 so other things work.
	if (g_Config.IsPortrait()) {
		PSP_CoreParameter().renderWidth = 272;
		PSP_CoreParameter().renderHeight = 480;
	} else {
		PSP_CoreParameter().renderWidth = 480;
		PSP_CoreParameter().renderHeight = 272;
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

			CBreakPoints::ExecMemCheck(srcBasePtr + (srcY * srcStride + srcX) * bpp, false, height * srcStride * bpp, currentMIPS->pc);
			CBreakPoints::ExecMemCheck(dstBasePtr + (srcY * dstStride + srcX) * bpp, true, height * dstStride * bpp, currentMIPS->pc);

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
