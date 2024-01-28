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
#include "Common/Profiler/Profiler.h"

#include "Core/MemMap.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Debugger/Debugger.h"
#include "GPU/D3D11/FramebufferManagerD3D11.h"
#include "GPU/D3D11/TextureCacheD3D11.h"
#include "GPU/D3D11/DrawEngineD3D11.h"
#include "GPU/D3D11/ShaderManagerD3D11.h"
#include "GPU/D3D11/GPU_D3D11.h"

const D3D11_PRIMITIVE_TOPOLOGY d3d11prim[8] = {
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // Points are expanded to triangles.
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // Lines are expanded to triangles too.
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // Lines are expanded to triangles too.
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // Fans not supported
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,  // Need expansion - though we could do it with geom shaders in most cases
};

enum {
	VERTEX_PUSH_SIZE = 1024 * 1024 * 16,
	INDEX_PUSH_SIZE = 1024 * 1024 * 4,
};

static const D3D11_INPUT_ELEMENT_DESC TransformedVertexElements[] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(TransformedVertex, pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(TransformedVertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(TransformedVertex, color0), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 0, offsetof(TransformedVertex, color1), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(TransformedVertex, fog), D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

DrawEngineD3D11::DrawEngineD3D11(Draw::DrawContext *draw, ID3D11Device *device, ID3D11DeviceContext *context)
	: draw_(draw),
		device_(device),
		context_(context),
		inputLayoutMap_(32),
		blendCache_(32),
		blendCache1_(32),
		depthStencilCache_(64),
		rasterCache_(4) {
	device1_ = (ID3D11Device1 *)draw->GetNativeObject(Draw::NativeObject::DEVICE_EX);
	context1_ = (ID3D11DeviceContext1 *)draw->GetNativeObject(Draw::NativeObject::CONTEXT_EX);
	decOptions_.expandAllWeightsToFloat = true;
	decOptions_.expand8BitNormalsToFloat = true;

	// Allocate nicely aligned memory. Maybe graphics drivers will
	// appreciate it.
	// All this is a LOT of memory, need to see if we can cut down somehow.
	indexGen.Setup(decIndex_);

	InitDeviceObjects();

	// Vertex pushing buffers. For uniforms we use short DISCARD buffers, but we could use
	// this kind of buffer there as well with D3D11.1. We might be able to use the same buffer
	// for both vertices and indices, and possibly all three data types.
}

DrawEngineD3D11::~DrawEngineD3D11() {
	DestroyDeviceObjects();
}

void DrawEngineD3D11::InitDeviceObjects() {
	pushVerts_ = new PushBufferD3D11(device_, VERTEX_PUSH_SIZE, D3D11_BIND_VERTEX_BUFFER);
	pushInds_ = new PushBufferD3D11(device_, INDEX_PUSH_SIZE, D3D11_BIND_INDEX_BUFFER);

	tessDataTransferD3D11 = new TessellationDataTransferD3D11(context_, device_);
	tessDataTransfer = tessDataTransferD3D11;

	draw_->SetInvalidationCallback(std::bind(&DrawEngineD3D11::Invalidate, this, std::placeholders::_1));
}

void DrawEngineD3D11::ClearInputLayoutMap() {
	inputLayoutMap_.Iterate([&](const InputLayoutKey &key, ID3D11InputLayout *il) {
		if (il)
			il->Release();
	});
	inputLayoutMap_.Clear();
}

void DrawEngineD3D11::NotifyConfigChanged() {
	DrawEngineCommon::NotifyConfigChanged();
	ClearInputLayoutMap();
}

void DrawEngineD3D11::DestroyDeviceObjects() {
	if (draw_) {
		draw_->SetInvalidationCallback(InvalidationCallback());
	}

	ClearTrackedVertexArrays();
	ClearInputLayoutMap();
	delete tessDataTransferD3D11;
	tessDataTransferD3D11 = nullptr;
	tessDataTransfer = nullptr;
	delete pushVerts_;
	delete pushInds_;
	depthStencilCache_.Iterate([&](const uint64_t &key, ID3D11DepthStencilState *ds) {
		ds->Release();
	});
	depthStencilCache_.Clear();
	blendCache_.Iterate([&](const uint64_t &key, ID3D11BlendState *bs) {
		bs->Release();
	});
	blendCache_.Clear();
	blendCache1_.Iterate([&](const uint64_t &key, ID3D11BlendState1 *bs) {
		bs->Release();
	});
	blendCache1_.Clear();
	rasterCache_.Iterate([&](const uint32_t &key, ID3D11RasterizerState *rs) {
		rs->Release();
	});
	rasterCache_.Clear();
}

struct DeclTypeInfo {
	DXGI_FORMAT type;
	const char * name;
};

static const DeclTypeInfo VComp[] = {
	{ DXGI_FORMAT_UNKNOWN, "NULL" }, // DEC_NONE,
	{ DXGI_FORMAT_R32_FLOAT, "D3DDECLTYPE_FLOAT1 " },  // DEC_FLOAT_1,
	{ DXGI_FORMAT_R32G32_FLOAT, "D3DDECLTYPE_FLOAT2 " },  // DEC_FLOAT_2,
	{ DXGI_FORMAT_R32G32B32_FLOAT, "D3DDECLTYPE_FLOAT3 " },  // DEC_FLOAT_3,
	{ DXGI_FORMAT_R32G32B32A32_FLOAT, "D3DDECLTYPE_FLOAT4 " },  // DEC_FLOAT_4,

	{ DXGI_FORMAT_R8G8B8A8_SNORM, "UNUSED" }, // DEC_S8_3,

	{ DXGI_FORMAT_R16G16B16A16_SNORM, "D3DDECLTYPE_SHORT4N	" },	// DEC_S16_3,
	{ DXGI_FORMAT_R8G8B8A8_UNORM, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_1,
	{ DXGI_FORMAT_R8G8B8A8_UNORM, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_2,
	{ DXGI_FORMAT_R8G8B8A8_UNORM, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_3,
	{ DXGI_FORMAT_R8G8B8A8_UNORM, "D3DDECLTYPE_UBYTE4N	" },	// DEC_U8_4,

	{ DXGI_FORMAT_UNKNOWN, "UNUSED_DEC_U16_1" },	// 	DEC_U16_1,
	{ DXGI_FORMAT_UNKNOWN, "UNUSED_DEC_U16_2" },	// 	DEC_U16_2,
	{ DXGI_FORMAT_R16G16B16A16_UNORM	,"D3DDECLTYPE_USHORT4N "}, // DEC_U16_3,
	{ DXGI_FORMAT_R16G16B16A16_UNORM	,"D3DDECLTYPE_USHORT4N "}, // DEC_U16_4,
};

static void VertexAttribSetup(D3D11_INPUT_ELEMENT_DESC * VertexElement, u8 fmt, u8 offset, const char *semantic, u8 semantic_index = 0) {
	memset(VertexElement, 0, sizeof(D3D11_INPUT_ELEMENT_DESC));
	VertexElement->AlignedByteOffset = offset;
	VertexElement->Format = VComp[fmt].type;
	VertexElement->SemanticName = semantic;
	VertexElement->SemanticIndex = semantic_index;
}

ID3D11InputLayout *DrawEngineD3D11::SetupDecFmtForDraw(D3D11VertexShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt) {
	// TODO: Instead of one for each vshader, we can reduce it to one for each type of shader
	// that reads TEXCOORD or not, etc. Not sure if worth it.
	const InputLayoutKey key{ vshader, decFmt.id };
	ID3D11InputLayout *inputLayout;
	if (inputLayoutMap_.Get(key, &inputLayout)) {
		return inputLayout;
	} else {
		D3D11_INPUT_ELEMENT_DESC VertexElements[8];
		D3D11_INPUT_ELEMENT_DESC *VertexElement = &VertexElements[0];

		// Vertices Elements orders
		// WEIGHT
		if (decFmt.w0fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.w0fmt, decFmt.w0off, "TEXCOORD", 1);
			VertexElement++;
		}

		if (decFmt.w1fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.w1fmt, decFmt.w1off, "TEXCOORD", 2);
			VertexElement++;
		}

		// TC
		if (decFmt.uvfmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.uvfmt, decFmt.uvoff, "TEXCOORD", 0);
			VertexElement++;
		}

		// COLOR
		if (decFmt.c0fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.c0fmt, decFmt.c0off, "COLOR", 0);
			VertexElement++;
		}
		// Never used ?
		if (decFmt.c1fmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.c1fmt, decFmt.c1off, "COLOR", 1);
			VertexElement++;
		}

		// NORMAL
		if (decFmt.nrmfmt != 0) {
			VertexAttribSetup(VertexElement, decFmt.nrmfmt, decFmt.nrmoff, "NORMAL", 0);
			VertexElement++;
		}

		// POSITION
		// Always
		VertexAttribSetup(VertexElement, DecVtxFormat::PosFmt(), decFmt.posoff, "POSITION", 0);
		VertexElement++;

		// Create declaration
		HRESULT hr = device_->CreateInputLayout(VertexElements, VertexElement - VertexElements, vshader->bytecode().data(), vshader->bytecode().size(), &inputLayout);
		if (FAILED(hr)) {
			ERROR_LOG(G3D, "Failed to create input layout!");
			inputLayout = nullptr;
		}

		// Add it to map
		inputLayoutMap_.Insert(key, inputLayout);
		return inputLayout;
	}
}

void DrawEngineD3D11::BeginFrame() {
	pushVerts_->Reset();
	pushInds_->Reset();

	lastRenderStepId_ = -1;
}

// In D3D, we're synchronous and state carries over so all we reset here on a new step is the viewport/scissor.
void DrawEngineD3D11::Invalidate(InvalidationCallbackFlags flags) {
	if (flags & InvalidationCallbackFlags::RENDER_PASS_STATE) {
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE | DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
	}
}

// The inline wrapper in the header checks for numDrawCalls_ == 0
void DrawEngineD3D11::DoFlush() {
	bool textureNeedsApply = false;
	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		textureNeedsApply = true;
	} else if (gstate.getTextureAddress(0) == (gstate.getFrameBufRawAddress() | 0x04000000)) {
		// This catches the case of clearing a texture. (#10957)
		gstate_c.Dirty(DIRTY_TEXTURE_IMAGE);
	}

	// This is not done on every drawcall, we collect vertex data
	// until critical state changes. That's when we draw (flush).

	GEPrimitiveType prim = prevPrim_;

	// Always use software for flat shading to fix the provoking index.
	bool tess = gstate_c.submitType == SubmitType::HW_BEZIER || gstate_c.submitType == SubmitType::HW_SPLINE;
	bool useHWTransform = CanUseHardwareTransform(prim) && (tess || gstate.getShadeMode() != GE_SHADE_FLAT);

	if (useHWTransform) {
		ID3D11Buffer *vb_ = nullptr;
		ID3D11Buffer *ib_ = nullptr;

		int vertexCount;
		int maxIndex;
		bool useElements;
		DecodeVerts(decoded_);
		DecodeIndsAndGetData(&prim, &vertexCount, &maxIndex, &useElements, false);
		gpuStats.numUncachedVertsDrawn += vertexCount;

		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		if (textureNeedsApply) {
			textureCache_->ApplyTexture();
		}

		// Need to ApplyDrawState after ApplyTexture because depal can launch a render pass and that wrecks the state.
		ApplyDrawState(prim);
		ApplyDrawStateLate(true, dynState_.stencilRef);

		D3D11VertexShader *vshader;
		D3D11FragmentShader *fshader;
		shaderManager_->GetShaders(prim, dec_, &vshader, &fshader, pipelineState_, useHWTransform, useHWTessellation_, decOptions_.expandAllWeightsToFloat, decOptions_.applySkinInDecode);
		ID3D11InputLayout *inputLayout = SetupDecFmtForDraw(vshader, dec_->GetDecVtxFmt(), dec_->VertexType());
		context_->PSSetShader(fshader->GetShader(), nullptr, 0);
		context_->VSSetShader(vshader->GetShader(), nullptr, 0);
		shaderManager_->UpdateUniforms(framebufferManager_->UseBufferedRendering());
		shaderManager_->BindUniforms();

		context_->IASetInputLayout(inputLayout);
		UINT stride = dec_->GetDecVtxFmt().stride;
		context_->IASetPrimitiveTopology(d3d11prim[prim]);

		if (!vb_) {
			// Push!
			UINT vOffset;
			int vSize = numDecodedVerts_ * dec_->GetDecVtxFmt().stride;
			uint8_t *vptr = pushVerts_->BeginPush(context_, &vOffset, vSize);
			memcpy(vptr, decoded_, vSize);
			pushVerts_->EndPush(context_);
			ID3D11Buffer *buf = pushVerts_->Buf();
			context_->IASetVertexBuffers(0, 1, &buf, &stride, &vOffset);
			if (useElements) {
				UINT iOffset;
				int iSize = 2 * vertexCount;
				uint8_t *iptr = pushInds_->BeginPush(context_, &iOffset, iSize);
				memcpy(iptr, decIndex_, iSize);
				pushInds_->EndPush(context_);
				context_->IASetIndexBuffer(pushInds_->Buf(), DXGI_FORMAT_R16_UINT, iOffset);
				context_->DrawIndexed(vertexCount, 0, 0);
			} else {
				context_->Draw(vertexCount, 0);
			}
		} else {
			UINT offset = 0;
			context_->IASetVertexBuffers(0, 1, &vb_, &stride, &offset);
			if (useElements) {
				context_->IASetIndexBuffer(ib_, DXGI_FORMAT_R16_UINT, 0);
				context_->DrawIndexed(vertexCount, 0, 0);
			} else {
				context_->Draw(vertexCount, 0);
			}
		}
	} else {
		PROFILE_THIS_SCOPE("soft");
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
		params.allowSeparateAlphaClear = false;  // D3D11 doesn't support separate alpha clears
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

		SoftwareTransform swTransform(params);

		const Lin::Vec3 trans(gstate_c.vpXOffset, -gstate_c.vpYOffset, gstate_c.vpZOffset * 0.5f + 0.5f);
		const Lin::Vec3 scale(gstate_c.vpWidthScale, -gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
		swTransform.SetProjMatrix(gstate.projMatrix, gstate_c.vpWidth < 0, gstate_c.vpHeight < 0, trans, scale);

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

		// Need to ApplyDrawState after ApplyTexture because depal can launch a render pass and that wrecks the state.
		ApplyDrawState(prim);

		if (result.action == SW_NOT_READY)
			swTransform.BuildDrawingParams(prim, vertexCount, dec_->VertexType(), inds, RemainingIndices(inds), numDecodedVerts_, VERTEX_BUFFER_MAX, &result);
		if (result.setSafeSize)
			framebufferManager_->SetSafeSize(result.safeWidth, result.safeHeight);

		ApplyDrawStateLate(result.setStencil, result.stencilValue);

		if (result.action == SW_DRAW_PRIMITIVES) {
			D3D11VertexShader *vshader;
			D3D11FragmentShader *fshader;
			shaderManager_->GetShaders(prim, dec_, &vshader, &fshader, pipelineState_, false, false, decOptions_.expandAllWeightsToFloat, true);
			context_->PSSetShader(fshader->GetShader(), nullptr, 0);
			context_->VSSetShader(vshader->GetShader(), nullptr, 0);
			shaderManager_->UpdateUniforms(framebufferManager_->UseBufferedRendering());
			shaderManager_->BindUniforms();

			// We really do need a vertex layout for each vertex shader (or at least check its ID bits for what inputs it uses)!
			// Some vertex shaders ignore one of the inputs, and then the layout created from it will lack it, which will be a problem for others.
			InputLayoutKey key{ vshader, 0xFFFFFFFF };  // Let's use 0xFFFFFFFF to signify TransformedVertex
			ID3D11InputLayout *layout;
			if (!inputLayoutMap_.Get(key, &layout)) {
				ASSERT_SUCCESS(device_->CreateInputLayout(TransformedVertexElements, ARRAY_SIZE(TransformedVertexElements), vshader->bytecode().data(), vshader->bytecode().size(), &layout));
				inputLayoutMap_.Insert(key, layout);
			}
			context_->IASetInputLayout(layout);
			context_->IASetPrimitiveTopology(d3d11prim[prim]);

			UINT stride = sizeof(TransformedVertex);
			UINT vOffset = 0;
			int vSize = numDecodedVerts_ * stride;
			uint8_t *vptr = pushVerts_->BeginPush(context_, &vOffset, vSize);
			memcpy(vptr, result.drawBuffer, vSize);
			pushVerts_->EndPush(context_);
			ID3D11Buffer *buf = pushVerts_->Buf();
			context_->IASetVertexBuffers(0, 1, &buf, &stride, &vOffset);
			if (result.drawIndexed) {
				UINT iOffset;
				int iSize = sizeof(uint16_t) * result.drawNumTrans;
				uint8_t *iptr = pushInds_->BeginPush(context_, &iOffset, iSize);
				memcpy(iptr, inds, iSize);
				pushInds_->EndPush(context_);
				context_->IASetIndexBuffer(pushInds_->Buf(), DXGI_FORMAT_R16_UINT, iOffset);
				context_->DrawIndexed(result.drawNumTrans, 0, 0);
			} else {
				context_->Draw(result.drawNumTrans, 0);
			}
		} else if (result.action == SW_CLEAR) {
			u32 clearColor = result.color;
			float clearDepth = result.depth;

			uint32_t clearFlag = 0;

			if (gstate.isClearModeColorMask()) clearFlag |= Draw::FBChannel::FB_COLOR_BIT;
			if (gstate.isClearModeAlphaMask()) clearFlag |= Draw::FBChannel::FB_STENCIL_BIT;
			if (gstate.isClearModeDepthMask()) clearFlag |= Draw::FBChannel::FB_DEPTH_BIT;

			if (clearFlag & Draw::FBChannel::FB_COLOR_BIT) {
				framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);
			}

			uint8_t clearStencil = clearColor >> 24;
			draw_->Clear(clearFlag, clearColor, clearDepth, clearStencil);

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

TessellationDataTransferD3D11::TessellationDataTransferD3D11(ID3D11DeviceContext *context, ID3D11Device *device)
	: context_(context), device_(device) {
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
}

TessellationDataTransferD3D11::~TessellationDataTransferD3D11() {
	for (int i = 0; i < 3; ++i) {
		if (buf[i]) buf[i]->Release();
		if (view[i]) view[i]->Release();
	}
}

template <typename T>
static void DoRelease(T *&ptr) {
	if (ptr) {
		ptr->Release();
		ptr = nullptr;
	}
}

void TessellationDataTransferD3D11::SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) {
	struct TessData {
		float pos[3]; float pad1;
		float uv[2]; float pad2[2];
		float color[4];
	};

	int size = size_u * size_v;

	if (prevSize < size || !buf[0]) {
		prevSize = size;
		DoRelease(buf[0]);
		DoRelease(view[0]);

		desc.ByteWidth = size * sizeof(TessData);
		desc.StructureByteStride = sizeof(TessData);
		device_->CreateBuffer(&desc, nullptr, &buf[0]);
		if (buf[0])
			device_->CreateShaderResourceView(buf[0], nullptr, &view[0]);
		if (!buf[0] || !view[0])
			return;
		context_->VSSetShaderResources(0, 1, &view[0]);
	}
	D3D11_MAPPED_SUBRESOURCE map{};
	HRESULT hr = context_->Map(buf[0], 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	if (FAILED(hr))
		return;
	uint8_t *data = (uint8_t *)map.pData;

	float *pos = (float *)(data);
	float *tex = (float *)(data + offsetof(TessData, uv));
	float *col = (float *)(data + offsetof(TessData, color));
	int stride = sizeof(TessData) / sizeof(float);

	CopyControlPoints(pos, tex, col, stride, stride, stride, points, size, vertType);

	context_->Unmap(buf[0], 0);

	using Spline::Weight;

	// Weights U
	if (prevSizeWU < weights.size_u || !buf[1]) {
		prevSizeWU = weights.size_u;
		DoRelease(buf[1]);
		DoRelease(view[1]);

		desc.ByteWidth = weights.size_u * sizeof(Weight);
		desc.StructureByteStride = sizeof(Weight);
		device_->CreateBuffer(&desc, nullptr, &buf[1]);
		if (buf[1])
			device_->CreateShaderResourceView(buf[1], nullptr, &view[1]);
		if (!buf[1] || !view[1])
			return;
		context_->VSSetShaderResources(1, 1, &view[1]);
	}
	hr = context_->Map(buf[1], 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	if (SUCCEEDED(hr))
		memcpy(map.pData, weights.u, weights.size_u * sizeof(Weight));
	context_->Unmap(buf[1], 0);

	// Weights V
	if (prevSizeWV < weights.size_v) {
		prevSizeWV = weights.size_v;
		DoRelease(buf[2]);
		DoRelease(view[2]);

		desc.ByteWidth = weights.size_v * sizeof(Weight);
		desc.StructureByteStride = sizeof(Weight);
		device_->CreateBuffer(&desc, nullptr, &buf[2]);
		if (buf[2])
			device_->CreateShaderResourceView(buf[2], nullptr, &view[2]);
		if (!buf[2] || !view[2])
			return;
		context_->VSSetShaderResources(2, 1, &view[2]);
	}
	hr = context_->Map(buf[2], 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	if (SUCCEEDED(hr))
		memcpy(map.pData, weights.v, weights.size_v * sizeof(Weight));
	context_->Unmap(buf[2], 0);
}
