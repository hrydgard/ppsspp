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

#include "Common/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/TimeUtil.h"
#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"

#include "Common/GPU/D3D9/D3D9StateCache.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/DrawEngineDX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/GPU_DX9.h"

static const D3DPRIMITIVETYPE d3d_prim[8] = {
	// Points, which are expanded to triangles.
	D3DPT_TRIANGLELIST,
	// Lines and line strips, which are also expanded to triangles.
	D3DPT_TRIANGLELIST,
	D3DPT_TRIANGLELIST,
	D3DPT_TRIANGLELIST,
	D3DPT_TRIANGLESTRIP,
	D3DPT_TRIANGLEFAN,
	// Rectangles, which are expanded to triangles.
	D3DPT_TRIANGLELIST,
};

static const int D3DPRIMITIVEVERTEXCOUNT[8][2] = {
	{0, 0}, // invalid
	{1, 0}, // 1 = D3DPT_POINTLIST,
	{2, 0}, // 2 = D3DPT_LINELIST,
	{2, 1}, // 3 = D3DPT_LINESTRIP,
	{3, 0}, // 4 = D3DPT_TRIANGLELIST,
	{1, 2}, // 5 = D3DPT_TRIANGLESTRIP,
	{1, 2}, // 6 = D3DPT_TRIANGLEFAN,
};

inline int D3DPrimCount(D3DPRIMITIVETYPE prim, int size) {
	return (size / D3DPRIMITIVEVERTEXCOUNT[prim][0]) - D3DPRIMITIVEVERTEXCOUNT[prim][1];
}

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex)
};

static const D3DVERTEXELEMENT9 TransformedVertexElements[] = {
	{ 0, offsetof(TransformedVertex, pos), D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, offsetof(TransformedVertex, uv), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
	{ 0, offsetof(TransformedVertex, color0), D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
	{ 0, offsetof(TransformedVertex, color1), D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1 },
	{ 0, offsetof(TransformedVertex, fog), D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
	D3DDECL_END()
};

DrawEngineDX9::DrawEngineDX9(Draw::DrawContext *draw) : draw_(draw), vertexDeclMap_(64) {
	device_ = (LPDIRECT3DDEVICE9)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	decOptions_.expandAllWeightsToFloat = true;
	decOptions_.expand8BitNormalsToFloat = true;

	indexGen.Setup(decIndex_);

	InitDeviceObjects();

	tessDataTransferDX9 = new TessellationDataTransferDX9();
	tessDataTransfer = tessDataTransferDX9;

	device_->CreateVertexDeclaration(TransformedVertexElements, &transformedVertexDecl_);
}

DrawEngineDX9::~DrawEngineDX9() {
	if (transformedVertexDecl_) {
		transformedVertexDecl_->Release();
	}

	DestroyDeviceObjects();
	vertexDeclMap_.Iterate([&](const uint32_t &key, IDirect3DVertexDeclaration9 *decl) {
		if (decl) {
			decl->Release();
		}
	});
	vertexDeclMap_.Clear();
	delete tessDataTransferDX9;
}

void DrawEngineDX9::InitDeviceObjects() {
	draw_->SetInvalidationCallback(std::bind(&DrawEngineDX9::Invalidate, this, std::placeholders::_1));
}

void DrawEngineDX9::DestroyDeviceObjects() {
	if (draw_) {
		draw_->SetInvalidationCallback(InvalidationCallback());
	}
	ClearTrackedVertexArrays();
}

struct DeclTypeInfo {
	u32 type;
	const char * name;
};

static const DeclTypeInfo VComp[] = {
	{ 0, "NULL" }, // DEC_NONE,
	{ D3DDECLTYPE_FLOAT1, "D3DDECLTYPE_FLOAT1 " },  // DEC_FLOAT_1,
	{ D3DDECLTYPE_FLOAT2, "D3DDECLTYPE_FLOAT2 " },  // DEC_FLOAT_2,
	{ D3DDECLTYPE_FLOAT3, "D3DDECLTYPE_FLOAT3 " },  // DEC_FLOAT_3,
	{ D3DDECLTYPE_FLOAT4, "D3DDECLTYPE_FLOAT4 " },  // DEC_FLOAT_4,

	{ 0, "UNUSED" }, // DEC_S8_3,

	{ D3DDECLTYPE_SHORT4N, "D3DDECLTYPE_SHORT4N	" },	// DEC_S16_3,
	{ D3DDECLTYPE_UBYTE4N, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_1,
	{ D3DDECLTYPE_UBYTE4N, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_2,
	{ D3DDECLTYPE_UBYTE4N, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_3,
	{ D3DDECLTYPE_UBYTE4N, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_4,
	{0, "UNUSED_DEC_U16_1" },	// 	DEC_U16_1,
	{0, "UNUSED_DEC_U16_2" },	// 	DEC_U16_2,
	{D3DDECLTYPE_USHORT4N	,"D3DDECLTYPE_USHORT4N "}, // DEC_U16_3,
	{D3DDECLTYPE_USHORT4N	,"D3DDECLTYPE_USHORT4N "}, // DEC_U16_4,
};

static void VertexAttribSetup(D3DVERTEXELEMENT9 * VertexElement, u8 fmt, u8 offset, u8 usage, u8 usage_index = 0) {
	memset(VertexElement, 0, sizeof(D3DVERTEXELEMENT9));
	VertexElement->Offset = offset;
	VertexElement->Type = VComp[fmt].type;
	VertexElement->Usage = usage;
	VertexElement->UsageIndex = usage_index;
}

IDirect3DVertexDeclaration9 *DrawEngineDX9::SetupDecFmtForDraw(const DecVtxFormat &decFmt, u32 pspFmt) {
	IDirect3DVertexDeclaration9 *vertexDeclCached;
	if (vertexDeclMap_.Get(pspFmt, &vertexDeclCached)) {
		return vertexDeclCached;
	} else {
		D3DVERTEXELEMENT9 VertexElements[8];
		D3DVERTEXELEMENT9 *VertexElement = &VertexElements[0];

		// Vertices Elements orders
		// WEIGHT
		if (decFmt.w0fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.w0fmt, decFmt.w0off, D3DDECLUSAGE_TEXCOORD, 1);
			VertexElement++;
		}

		if (decFmt.w1fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.w1fmt, decFmt.w1off, D3DDECLUSAGE_TEXCOORD, 2);
			VertexElement++;
		}

		// TC
		if (decFmt.uvfmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.uvfmt, decFmt.uvoff, D3DDECLUSAGE_TEXCOORD, 0);
			VertexElement++;
		}

		// COLOR
		if (decFmt.c0fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.c0fmt, decFmt.c0off, D3DDECLUSAGE_COLOR, 0);
			VertexElement++;
		}
		// Never used ?
		if (decFmt.c1fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.c1fmt, decFmt.c1off, D3DDECLUSAGE_COLOR, 1);
			VertexElement++;
		}

		// NORMAL
		if (decFmt.nrmfmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.nrmfmt, decFmt.nrmoff, D3DDECLUSAGE_NORMAL, 0);
			VertexElement++;
		}

		// POSITION
		// Always
		VertexAttribSetup(VertexElement, DecVtxFormat::PosFmt(), decFmt.posoff, D3DDECLUSAGE_POSITION, 0);
		VertexElement++;

		// End
		D3DVERTEXELEMENT9 end = D3DDECL_END();
		memcpy(VertexElement, &end, sizeof(D3DVERTEXELEMENT9));

		// Create declaration
		IDirect3DVertexDeclaration9 *pHardwareVertexDecl = nullptr;
		HRESULT hr = device_->CreateVertexDeclaration( VertexElements, &pHardwareVertexDecl );
		if (FAILED(hr)) {
			ERROR_LOG(G3D, "Failed to create vertex declaration!");
			pHardwareVertexDecl = nullptr;
		}

		// Add it to map
		vertexDeclMap_.Insert(pspFmt, pHardwareVertexDecl);
		return pHardwareVertexDecl;
	}
}

static uint32_t SwapRB(uint32_t c) {
	return (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000);
}

void DrawEngineDX9::BeginFrame() {
	lastRenderStepId_ = -1;
}

// In D3D, we're synchronous and state carries over so all we reset here on a new step is the viewport/scissor.
void DrawEngineDX9::Invalidate(InvalidationCallbackFlags flags) {
	if (flags & InvalidationCallbackFlags::RENDER_PASS_STATE) {
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	}
}

// The inline wrapper in the header checks for numDrawCalls_ == 0
void DrawEngineDX9::DoFlush() {
	bool textureNeedsApply = false;
	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		textureNeedsApply = true;
	} else if (gstate.getTextureAddress(0) == (gstate.getFrameBufRawAddress() | 0x04000000)) {
		// This catches the case of clearing a texture. (#10957)
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	}

	GEPrimitiveType prim = prevPrim_;

	// Always use software for flat shading to fix the provoking index.
	bool tess = gstate_c.submitType == SubmitType::HW_BEZIER || gstate_c.submitType == SubmitType::HW_SPLINE;
	bool useHWTransform = CanUseHardwareTransform(prim) && (tess || gstate.getShadeMode() != GE_SHADE_FLAT);

	if (useHWTransform) {
		LPDIRECT3DVERTEXBUFFER9 vb_ = nullptr;
		LPDIRECT3DINDEXBUFFER9 ib_ = nullptr;

		int vertexCount;
		int maxIndex;
		bool useElements;
		DecodeVerts(decoded_);
		DecodeIndsAndGetData(&prim, &vertexCount, &maxIndex, &useElements, false);
		gpuStats.numUncachedVertsDrawn += vertexCount;

		_dbg_assert_((int)prim > 0);

		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		if (textureNeedsApply) {
			textureCache_->ApplyTexture();
		}

		ApplyDrawState(prim);
		ApplyDrawStateLate();

		VSShader *vshader = shaderManager_->ApplyShader(true, useHWTessellation_, dec_, decOptions_.expandAllWeightsToFloat, decOptions_.applySkinInDecode, pipelineState_);
		IDirect3DVertexDeclaration9 *pHardwareVertexDecl = SetupDecFmtForDraw(dec_->GetDecVtxFmt(), dec_->VertexType());

		if (pHardwareVertexDecl) {
			device_->SetVertexDeclaration(pHardwareVertexDecl);
			if (vb_ == NULL) {
				if (useElements) {
					device_->DrawIndexedPrimitiveUP(d3d_prim[prim], 0, numDecodedVerts_, D3DPrimCount(d3d_prim[prim], vertexCount), decIndex_, D3DFMT_INDEX16, decoded_, dec_->GetDecVtxFmt().stride);
				} else {
					device_->DrawPrimitiveUP(d3d_prim[prim], D3DPrimCount(d3d_prim[prim], vertexCount), decoded_, dec_->GetDecVtxFmt().stride);
				}
			} else {
				device_->SetStreamSource(0, vb_, 0, dec_->GetDecVtxFmt().stride);

				if (useElements) {
					device_->SetIndices(ib_);

					device_->DrawIndexedPrimitive(d3d_prim[prim], 0, 0, numDecodedVerts_, 0, D3DPrimCount(d3d_prim[prim], vertexCount));
				} else {
					device_->DrawPrimitive(d3d_prim[prim], 0, D3DPrimCount(d3d_prim[prim], vertexCount));
				}
			}
		}
	} else {
		if (!decOptions_.applySkinInDecode) {
			decOptions_.applySkinInDecode = true;
			lastVType_ |= (1 << 26);
			dec_ = GetVertexDecoder(lastVType_);
		}
		DecodeVerts(decoded_);
		int vertexCount = DecodeInds();

		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		gpuStats.numUncachedVertsDrawn += vertexCount;
		prim = IndexGenerator::GeneralPrim((GEPrimitiveType)drawInds_[0].prim);
		VERBOSE_LOG(G3D, "Flush prim %i SW! %i verts in one go", prim, vertexCount);

		u16 *inds = decIndex_;
		SoftwareTransformResult result{};
		SoftwareTransformParams params{};
		params.decoded = decoded_;
		params.transformed = transformed_;
		params.transformedExpanded = transformedExpanded_;
		params.fbman = framebufferManager_;
		params.texCache = textureCache_;
		params.allowClear = true;
		params.allowSeparateAlphaClear = false;
		params.provokeFlatFirst = true;
		params.flippedY = false;
		params.usesHalfZ = true;

		// We need correct viewport values in gstate_c already.
		if (gstate_c.IsDirty(DIRTY_VIEWPORTSCISSOR_STATE)) {
			ViewportAndScissor vpAndScissor;
			ConvertViewportAndScissor(framebufferManager_->UseBufferedRendering(),
				framebufferManager_->GetRenderWidth(), framebufferManager_->GetRenderHeight(),
				framebufferManager_->GetTargetBufferWidth(), framebufferManager_->GetTargetBufferHeight(),
				vpAndScissor);
			UpdateCachedViewportState(vpAndScissor);
		}

		int maxIndex = numDecodedVerts_;
		SoftwareTransform swTransform(params);

		// Half pixel offset hack.
		float xOffset = -1.0f / gstate_c.curRTRenderWidth;
		float yOffset = 1.0f / gstate_c.curRTRenderHeight;

		const Lin::Vec3 trans(gstate_c.vpXOffset + xOffset, -gstate_c.vpYOffset + yOffset, gstate_c.vpZOffset * 0.5f + 0.5f);
		const Lin::Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
		swTransform.SetProjMatrix(gstate.projMatrix, gstate_c.vpWidth < 0, gstate_c.vpHeight > 0, trans, scale);

		swTransform.Transform(prim, dec_->VertexType(), dec_->GetDecVtxFmt(), numDecodedVerts_, &result);
		// Non-zero depth clears are unusual, but some drivers don't match drawn depth values to cleared values.
		// Games sometimes expect exact matches (see #12626, for example) for equal comparisons.
		if (result.action == SW_CLEAR && everUsedEqualDepth_ && gstate.isClearModeDepthMask() && result.depth > 0.0f && result.depth < 1.0f)
			result.action = SW_NOT_READY;

		if (textureNeedsApply) {
			gstate_c.pixelMapped = result.pixelMapped;
			textureCache_->ApplyTexture();
			gstate_c.pixelMapped = false;
		}

		ApplyDrawState(prim);

		if (result.action == SW_NOT_READY)
			swTransform.BuildDrawingParams(prim, vertexCount, dec_->VertexType(), inds, RemainingIndices(inds), numDecodedVerts_, VERTEX_BUFFER_MAX, &result);
		if (result.setSafeSize)
			framebufferManager_->SetSafeSize(result.safeWidth, result.safeHeight);

		ApplyDrawStateLate();

		VSShader *vshader = shaderManager_->ApplyShader(false, false, dec_, decOptions_.expandAllWeightsToFloat, true, pipelineState_);

		if (result.action == SW_DRAW_PRIMITIVES) {
			if (result.setStencil) {
				dxstate.stencilFunc.set(D3DCMP_ALWAYS);
				dxstate.stencilRef.set(result.stencilValue);
				dxstate.stencilCompareMask.set(255);
			}

			// TODO: Add a post-transform cache here for multi-RECTANGLES only.
			// Might help for text drawing.

			device_->SetVertexDeclaration(transformedVertexDecl_);
			if (result.drawIndexed) {
				device_->DrawIndexedPrimitiveUP(d3d_prim[prim], 0, numDecodedVerts_, D3DPrimCount(d3d_prim[prim], result.drawNumTrans), inds, D3DFMT_INDEX16, result.drawBuffer, sizeof(TransformedVertex));
			} else {
				device_->DrawPrimitiveUP(d3d_prim[prim], D3DPrimCount(d3d_prim[prim], result.drawNumTrans), result.drawBuffer, sizeof(TransformedVertex));
			}
		} else if (result.action == SW_CLEAR) {
			u32 clearColor = result.color;
			float clearDepth = result.depth;

			int mask = gstate.isClearModeColorMask() ? D3DCLEAR_TARGET : 0;
			if (gstate.isClearModeAlphaMask()) mask |= D3DCLEAR_STENCIL;
			if (gstate.isClearModeDepthMask()) mask |= D3DCLEAR_ZBUFFER;

			if (mask & D3DCLEAR_TARGET) {
				framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);
			}

			device_->Clear(0, NULL, mask, SwapRB(clearColor), clearDepth, clearColor >> 24);

			if (gstate_c.Use(GPU_USE_CLEAR_RAM_HACK) && gstate.isClearModeColorMask() && (gstate.isClearModeAlphaMask() || gstate_c.framebufFormat == GE_FORMAT_565)) {
				int scissorX1 = gstate.getScissorX1();
				int scissorY1 = gstate.getScissorY1();
				int scissorX2 = gstate.getScissorX2() + 1;
				int scissorY2 = gstate.getScissorY2() + 1;
				framebufferManager_->ApplyClearToMemory(scissorX1, scissorY1, scissorX2, scissorY2, clearColor);
			}
		}
		decOptions_.applySkinInDecode = g_Config.bSoftwareSkinning;
	}

	ResetAfterDrawInline();
	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);
	GPUDebug::NotifyDraw();
}

void TessellationDataTransferDX9::SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) {
	// TODO
}
