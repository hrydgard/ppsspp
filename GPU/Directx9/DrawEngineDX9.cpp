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

#define VERTEXCACHE_DECIMATION_INTERVAL 17

enum { VAI_KILL_AGE = 120, VAI_UNRELIABLE_KILL_AGE = 240, VAI_UNRELIABLE_KILL_MAX = 4 };

static const D3DVERTEXELEMENT9 TransformedVertexElements[] = {
	{ 0, offsetof(TransformedVertex, pos), D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, offsetof(TransformedVertex, uv), D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
	{ 0, offsetof(TransformedVertex, color0), D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 0 },
	{ 0, offsetof(TransformedVertex, color1), D3DDECLTYPE_UBYTE4N, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR, 1 },
	{ 0, offsetof(TransformedVertex, fog), D3DDECLTYPE_FLOAT1, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0 },
	D3DDECL_END()
};

DrawEngineDX9::DrawEngineDX9(Draw::DrawContext *draw) : draw_(draw), vai_(256), vertexDeclMap_(64) {
	device_ = (LPDIRECT3DDEVICE9)draw->GetNativeObject(Draw::NativeObject::DEVICE);
	decOptions_.expandAllWeightsToFloat = true;
	decOptions_.expand8BitNormalsToFloat = true;

	decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;

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
	IDirect3DVertexDeclaration9 *vertexDeclCached = vertexDeclMap_.Get(pspFmt);

	if (vertexDeclCached) {
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
		VertexAttribSetup(VertexElement, decFmt.posfmt, decFmt.posoff, D3DDECLUSAGE_POSITION, 0);
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

void DrawEngineDX9::MarkUnreliable(VertexArrayInfoDX9 *vai) {
	vai->status = VertexArrayInfoDX9::VAI_UNRELIABLE;
	if (vai->vbo) {
		vai->vbo->Release();
		vai->vbo = nullptr;
	}
	if (vai->ebo) {
		vai->ebo->Release();
		vai->ebo = nullptr;
	}
}

void DrawEngineDX9::ClearTrackedVertexArrays() {
	vai_.Iterate([&](uint32_t hash, VertexArrayInfoDX9 *vai) {
		delete vai;
	});
	vai_.Clear();
}

void DrawEngineDX9::DecimateTrackedVertexArrays() {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	const int threshold = gpuStats.numFlips - VAI_KILL_AGE;
	const int unreliableThreshold = gpuStats.numFlips - VAI_UNRELIABLE_KILL_AGE;
	int unreliableLeft = VAI_UNRELIABLE_KILL_MAX;
	vai_.Iterate([&](uint32_t hash, VertexArrayInfoDX9 *vai) {
		bool kill;
		if (vai->status == VertexArrayInfoDX9::VAI_UNRELIABLE) {
			// We limit killing unreliable so we don't rehash too often.
			kill = vai->lastFrame < unreliableThreshold && --unreliableLeft >= 0;
		} else {
			kill = vai->lastFrame < threshold;
		}
		if (kill) {
			delete vai;
			vai_.Remove(hash);
		}
	});
	vai_.Maintain();

	// Enable if you want to see vertex decoders in the log output. Need a better way.
#if 0
	char buffer[16384];
	for (std::map<u32, VertexDecoder*>::iterator dec = decoderMap_.begin(); dec != decoderMap_.end(); ++dec) {
		char *ptr = buffer;
		ptr += dec->second->ToString(ptr);
		//		*ptr++ = '\n';
		NOTICE_LOG(G3D, buffer);
	}
#endif
}

VertexArrayInfoDX9::~VertexArrayInfoDX9() {
	if (vbo) {
		vbo->Release();
	}
	if (ebo) {
		ebo->Release();
	}
}

static uint32_t SwapRB(uint32_t c) {
	return (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000);
}

void DrawEngineDX9::BeginFrame() {
	gpuStats.numTrackedVertexArrays = (int)vai_.size();

	DecimateTrackedVertexArrays();

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
		LPDIRECT3DVERTEXBUFFER9 vb_ = NULL;
		LPDIRECT3DINDEXBUFFER9 ib_ = NULL;

		int vertexCount = 0;
		int maxIndex = 0;
		bool useElements = true;

		// Cannot cache vertex data with morph enabled.
		bool useCache = g_Config.bVertexCache && !(lastVType_ & GE_VTYPE_MORPHCOUNT_MASK);
		// Also avoid caching when software skinning.
		if (decOptions_.applySkinInDecode && (lastVType_ & GE_VTYPE_WEIGHT_MASK))
			useCache = false;

		if (useCache) {
			// getUVGenMode can have an effect on which UV decoder we need to use! And hence what the decoded data will look like. See #9263
			u32 dcid = (u32)XXH3_64bits(&drawCalls_, sizeof(DeferredDrawCall) * numDrawCalls_) ^ gstate.getUVGenMode();
			VertexArrayInfoDX9 *vai = vai_.Get(dcid);
			if (!vai) {
				vai = new VertexArrayInfoDX9();
				vai_.Insert(dcid, vai);
			}

			switch (vai->status) {
			case VertexArrayInfoDX9::VAI_NEW:
				{
					// Haven't seen this one before.
					uint64_t dataHash = ComputeHash();
					vai->hash = dataHash;
					vai->minihash = ComputeMiniHash();
					vai->status = VertexArrayInfoDX9::VAI_HASHING;
					vai->drawsUntilNextFullHash = 0;
					DecodeVerts(decoded_); // writes to indexGen
					vai->numVerts = indexGen.VertexCount();
					vai->prim = indexGen.Prim();
					vai->maxIndex = indexGen.MaxIndex();
					vai->flags = gstate_c.vertexFullAlpha ? VAI_FLAG_VERTEXFULLALPHA : 0;

					goto rotateVBO;
				}

				// Hashing - still gaining confidence about the buffer.
				// But if we get this far it's likely to be worth creating a vertex buffer.
			case VertexArrayInfoDX9::VAI_HASHING:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFlips) {
						vai->numFrames++;
					}
					if (vai->drawsUntilNextFullHash == 0) {
						// Let's try to skip a full hash if mini would fail.
						const u32 newMiniHash = ComputeMiniHash();
						uint64_t newHash = vai->hash;
						if (newMiniHash == vai->minihash) {
							newHash = ComputeHash();
						}
						if (newMiniHash != vai->minihash || newHash != vai->hash) {
							MarkUnreliable(vai);
							DecodeVerts(decoded_);
							goto rotateVBO;
						}
						if (vai->numVerts > 64) {
							// exponential backoff up to 16 draws, then every 24
							vai->drawsUntilNextFullHash = std::min(24, vai->numFrames);
						} else {
							// Lower numbers seem much more likely to change.
							vai->drawsUntilNextFullHash = 0;
						}
						// TODO: tweak
						//if (vai->numFrames > 1000) {
						//	vai->status = VertexArrayInfo::VAI_RELIABLE;
						//}
					} else {
						vai->drawsUntilNextFullHash--;
						u32 newMiniHash = ComputeMiniHash();
						if (newMiniHash != vai->minihash) {
							MarkUnreliable(vai);
							DecodeVerts(decoded_);
							goto rotateVBO;
						}
					}

					if (vai->vbo == 0) {
						DecodeVerts(decoded_);
						vai->numVerts = indexGen.VertexCount();
						vai->prim = indexGen.Prim();
						vai->maxIndex = indexGen.MaxIndex();
						vai->flags = gstate_c.vertexFullAlpha ? VAI_FLAG_VERTEXFULLALPHA : 0;
						useElements = !indexGen.SeenOnlyPurePrims();
						if (!useElements && indexGen.PureCount()) {
							vai->numVerts = indexGen.PureCount();
						}

						_dbg_assert_msg_(gstate_c.vertBounds.minV >= gstate_c.vertBounds.maxV, "Should not have checked UVs when caching.");

						void * pVb;
						u32 size = dec_->GetDecVtxFmt().stride * indexGen.MaxIndex();
						device_->CreateVertexBuffer(size, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vai->vbo, NULL);
						vai->vbo->Lock(0, size, &pVb, 0);
						memcpy(pVb, decoded_, size);
						vai->vbo->Unlock();
						if (useElements) {
							void * pIb;
							u32 size = sizeof(short) * indexGen.VertexCount();
							device_->CreateIndexBuffer(size, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &vai->ebo, NULL);
							vai->ebo->Lock(0, size, &pIb, 0);
							memcpy(pIb, decIndex_, size);
							vai->ebo->Unlock();
						} else {
							vai->ebo = 0;
						}
					} else {
						gpuStats.numCachedDrawCalls++;
						useElements = vai->ebo ? true : false;
						gpuStats.numCachedVertsDrawn += vai->numVerts;
						gstate_c.vertexFullAlpha = vai->flags & VAI_FLAG_VERTEXFULLALPHA;
					}
					vb_ = vai->vbo;
					ib_ = vai->ebo;
					vertexCount = vai->numVerts;
					maxIndex = vai->maxIndex;
					prim = static_cast<GEPrimitiveType>(vai->prim);
					break;
				}

				// Reliable - we don't even bother hashing anymore. Right now we don't go here until after a very long time.
			case VertexArrayInfoDX9::VAI_RELIABLE:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFlips) {
						vai->numFrames++;
					}
					gpuStats.numCachedDrawCalls++;
					gpuStats.numCachedVertsDrawn += vai->numVerts;
					vb_ = vai->vbo;
					ib_ = vai->ebo;

					vertexCount = vai->numVerts;

					maxIndex = vai->maxIndex;
					prim = static_cast<GEPrimitiveType>(vai->prim);

					gstate_c.vertexFullAlpha = vai->flags & VAI_FLAG_VERTEXFULLALPHA;
					break;
				}

			case VertexArrayInfoDX9::VAI_UNRELIABLE:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFlips) {
						vai->numFrames++;
					}
					DecodeVerts(decoded_);
					goto rotateVBO;
				}
			}

			vai->lastFrame = gpuStats.numFlips;
		} else {
			DecodeVerts(decoded_);
rotateVBO:
			gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
			useElements = !indexGen.SeenOnlyPurePrims();
			vertexCount = indexGen.VertexCount();
			maxIndex = indexGen.MaxIndex();
			if (!useElements && indexGen.PureCount()) {
				vertexCount = indexGen.PureCount();
			}
			prim = indexGen.Prim();
		}

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
					device_->DrawIndexedPrimitiveUP(d3d_prim[prim], 0, maxIndex + 1, D3DPrimCount(d3d_prim[prim], vertexCount), decIndex_, D3DFMT_INDEX16, decoded_, dec_->GetDecVtxFmt().stride);
				} else {
					device_->DrawPrimitiveUP(d3d_prim[prim], D3DPrimCount(d3d_prim[prim], vertexCount), decoded_, dec_->GetDecVtxFmt().stride);
				}
			} else {
				device_->SetStreamSource(0, vb_, 0, dec_->GetDecVtxFmt().stride);

				if (useElements) {
					device_->SetIndices(ib_);

					device_->DrawIndexedPrimitive(d3d_prim[prim], 0, 0, maxIndex + 1, 0, D3DPrimCount(d3d_prim[prim], vertexCount));
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
		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
		prim = indexGen.Prim();
		// Undo the strip optimization, not supported by the SW code yet.
		if (prim == GE_PRIM_TRIANGLE_STRIP)
			prim = GE_PRIM_TRIANGLES;
		VERBOSE_LOG(G3D, "Flush prim %i SW! %i verts in one go", prim, indexGen.VertexCount());

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

		int maxIndex = indexGen.MaxIndex();
		SoftwareTransform swTransform(params);

		// Half pixel offset hack.
		float xOffset = -1.0f / gstate_c.curRTRenderWidth;
		float yOffset = 1.0f / gstate_c.curRTRenderHeight;

		const Lin::Vec3 trans(gstate_c.vpXOffset + xOffset, -gstate_c.vpYOffset + yOffset, gstate_c.vpZOffset * 0.5f + 0.5f);
		const Lin::Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
		swTransform.SetProjMatrix(gstate.projMatrix, gstate_c.vpWidth < 0, gstate_c.vpHeight > 0, trans, scale);

		swTransform.Decode(prim, dec_->VertexType(), dec_->GetDecVtxFmt(), maxIndex, &result);
		// Non-zero depth clears are unusual, but some drivers don't match drawn depth values to cleared values.
		// Games sometimes expect exact matches (see #12626, for example) for equal comparisons.
		if (result.action == SW_CLEAR && everUsedEqualDepth_ && gstate.isClearModeDepthMask() && result.depth > 0.0f && result.depth < 1.0f)
			result.action = SW_NOT_READY;
		if (result.action == SW_NOT_READY) {
			swTransform.DetectOffsetTexture(maxIndex);
		}

		if (textureNeedsApply)
			textureCache_->ApplyTexture();

		ApplyDrawState(prim);

		if (result.action == SW_NOT_READY)
			swTransform.BuildDrawingParams(prim, indexGen.VertexCount(), dec_->VertexType(), inds, maxIndex, &result);
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
				device_->DrawIndexedPrimitiveUP(d3d_prim[prim], 0, maxIndex, D3DPrimCount(d3d_prim[prim], result.drawNumTrans), inds, D3DFMT_INDEX16, result.drawBuffer, sizeof(TransformedVertex));
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

	gpuStats.numFlushes++;
	gpuStats.numDrawCalls += numDrawCalls_;
	gpuStats.numVertsSubmitted += vertexCountInDrawCalls_;

	indexGen.Reset();
	decodedVerts_ = 0;
	numDrawCalls_ = 0;
	vertexCountInDrawCalls_ = 0;
	decodeCounter_ = 0;
	gstate_c.vertexFullAlpha = true;
	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

	// Now seems as good a time as any to reset the min/max coords, which we may examine later.
	gstate_c.vertBounds.minU = 512;
	gstate_c.vertBounds.minV = 512;
	gstate_c.vertBounds.maxU = 0;
	gstate_c.vertBounds.maxV = 0;

	GPUDebug::NotifyDraw();
}

void TessellationDataTransferDX9::SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) {
	// TODO
}
