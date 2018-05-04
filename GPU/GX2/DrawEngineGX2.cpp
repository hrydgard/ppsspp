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

#include "Common/Profiler/Profiler.h"
#include "Common/MemoryUtil.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Reporting.h"
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
#include "GPU/GX2/FramebufferManagerGX2.h"
#include "GPU/GX2/TextureCacheGX2.h"
#include "GPU/GX2/DrawEngineGX2.h"
#include "GPU/GX2/ShaderManagerGX2.h"
#include "GPU/GX2/GPU_GX2.h"

static const GX2PrimitiveMode GX2prim[8] = {
	GX2_PRIMITIVE_MODE_POINTS,
	GX2_PRIMITIVE_MODE_LINES,
	GX2_PRIMITIVE_MODE_LINE_STRIP,
	GX2_PRIMITIVE_MODE_TRIANGLES,
	GX2_PRIMITIVE_MODE_TRIANGLE_STRIP,
	GX2_PRIMITIVE_MODE_TRIANGLES,
	GX2_PRIMITIVE_MODE_TRIANGLES,
	GX2_PRIMITIVE_MODE_INVALID
};

#define VERTEXCACHE_DECIMATION_INTERVAL 17

enum { VAI_KILL_AGE = 120, VAI_UNRELIABLE_KILL_AGE = 240, VAI_UNRELIABLE_KILL_MAX = 4 };
enum {
	VERTEX_PUSH_SIZE = 1024 * 1024 * 16,
	INDEX_PUSH_SIZE = 1024 * 1024 * 4,
	UBO_PUSH_SIZE = 1024 * 1024 * 16,
};

static const GX2AttribStream TransformedVertexElements[] = {
	{ 0, 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0, GX2_COMP_SEL(_x, _y, _z, _w), GX2_ENDIAN_SWAP_DEFAULT },
	{ 1, 0, 16, GX2_ATTRIB_FORMAT_FLOAT_32_32_32, GX2_ATTRIB_INDEX_PER_VERTEX, 0, GX2_COMP_SEL(_x, _y, _z, _1), GX2_ENDIAN_SWAP_DEFAULT },
	{ 2, 0, 28, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, GX2_ATTRIB_INDEX_PER_VERTEX, 0, GX2_COMP_SEL(_r, _g, _b, _a), GX2_ENDIAN_SWAP_8_IN_32 },
	{ 3, 0, 32, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8, GX2_ATTRIB_INDEX_PER_VERTEX, 0, GX2_COMP_SEL(_r, _g, _b, _a), GX2_ENDIAN_SWAP_8_IN_32 },
};

DrawEngineGX2::DrawEngineGX2(Draw::DrawContext *draw, GX2ContextState *context)
	: draw_(draw),
	  context_(context),
	  vai_(256),
	  fetchShaderMap_(32),
	  blendCache_(32),
	  depthStencilCache_(64),
	  rasterCache_(4)
{
	decOptions_.expandAllWeightsToFloat = true;
	decOptions_.expand8BitNormalsToFloat = true;

	decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;

	// All this is a LOT of memory, need to see if we can cut down somehow.
	decoded = (u8 *)MEM2_alloc(DECODED_VERTEX_BUFFER_SIZE, GX2_VERTEX_BUFFER_ALIGNMENT);
	decIndex = (u16 *)MEM2_alloc(DECODED_INDEX_BUFFER_SIZE, GX2_INDEX_BUFFER_ALIGNMENT);

	indexGen.Setup(decIndex);

	InitDeviceObjects();
}

DrawEngineGX2::~DrawEngineGX2() {
	DestroyDeviceObjects();
	MEM2_free(decoded);
	MEM2_free(decIndex);
}

void DrawEngineGX2::InitDeviceObjects() {
	pushVerts_ = new PushBufferGX2(VERTEX_PUSH_SIZE, GX2_VERTEX_BUFFER_ALIGNMENT, GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER);
	pushInds_ = new PushBufferGX2(INDEX_PUSH_SIZE, GX2_INDEX_BUFFER_ALIGNMENT, GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER);
	pushUBO_ = new PushBufferGX2(UBO_PUSH_SIZE, GX2_UNIFORM_BLOCK_ALIGNMENT, GX2_INVALIDATE_MODE_CPU_UNIFORM_BLOCK);

	tessDataTransfer = new TessellationDataTransferGX2(context_);
}

void DrawEngineGX2::ClearTrackedVertexArrays() {
	vai_.Iterate([&](u32 hash, VertexArrayInfoGX2 *vai) { delete vai; });
	vai_.Clear();
}

void DrawEngineGX2::ClearInputLayoutMap() {
	fetchShaderMap_.Iterate([&](const FetchShaderKey &key, GX2FetchShader *il) {
		MEM2_free(il->program);
		delete il;
	});
	fetchShaderMap_.Clear();
}

void DrawEngineGX2::Resized() {
	DrawEngineCommon::Resized();
	ClearInputLayoutMap();
}

void DrawEngineGX2::DestroyDeviceObjects() {
	ClearTrackedVertexArrays();
	ClearInputLayoutMap();
	delete tessDataTransfer;
	delete pushVerts_;
	delete pushInds_;
	delete pushUBO_;
	tessDataTransfer = nullptr;
	pushVerts_ = nullptr;
	pushInds_ = nullptr;
	pushUBO_ = nullptr;
	depthStencilCache_.Iterate([&](const u64 &key, GX2DepthStencilControlReg *ds) { free(ds); });
	depthStencilCache_.Clear();
	blendCache_.Iterate([&](const u64 &key, GX2BlendState *bs) { free(bs); });
	blendCache_.Clear();
	rasterCache_.Iterate([&](const u32 &key, GX2RasterizerState *rs) { free(rs); });
	rasterCache_.Clear();
}

struct DeclTypeInfo {
	u32 mask;
	GX2EndianSwapMode endianSwap;
	GX2AttribFormat format;
};

static const DeclTypeInfo VComp[] = {
	{ GX2_COMP_SEL(_0, _0, _0, _0), GX2_ENDIAN_SWAP_DEFAULT, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32 }, // DEC_NONE,
	{ GX2_COMP_SEL(_x, _0, _0, _1), GX2_ENDIAN_SWAP_DEFAULT, GX2_ATTRIB_FORMAT_FLOAT_32 },          // DEC_FLOAT_1,
	{ GX2_COMP_SEL(_x, _y, _0, _1), GX2_ENDIAN_SWAP_DEFAULT, GX2_ATTRIB_FORMAT_FLOAT_32_32 },       // DEC_FLOAT_2,
	{ GX2_COMP_SEL(_x, _y, _z, _1), GX2_ENDIAN_SWAP_DEFAULT, GX2_ATTRIB_FORMAT_FLOAT_32_32_32 },    // DEC_FLOAT_3,
	{ GX2_COMP_SEL(_x, _y, _z, _w), GX2_ENDIAN_SWAP_DEFAULT, GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32 }, // DEC_FLOAT_4,
	{ GX2_COMP_SEL(_x, _y, _z, _1), GX2_ENDIAN_SWAP_8_IN_32, GX2_ATTRIB_FORMAT_SNORM_8_8_8_8 },     // DEC_S8_3,
	{ GX2_COMP_SEL(_x, _y, _z, _1), GX2_ENDIAN_SWAP_8_IN_16, GX2_ATTRIB_FORMAT_SNORM_16_16_16_16 }, // DEC_S16_3,
	{ GX2_COMP_SEL(_x, _0, _0, _1), GX2_ENDIAN_SWAP_DEFAULT, GX2_ATTRIB_FORMAT_UNORM_8 },           // DEC_U8_1,
	{ GX2_COMP_SEL(_x, _y, _0, _1), GX2_ENDIAN_SWAP_8_IN_16, GX2_ATTRIB_FORMAT_UNORM_8_8 },         // DEC_U8_2,
	{ GX2_COMP_SEL(_x, _y, _z, _1), GX2_ENDIAN_SWAP_8_IN_32, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8 },     // DEC_U8_3,
	{ GX2_COMP_SEL(_x, _y, _z, _w), GX2_ENDIAN_SWAP_8_IN_32, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8 },     // DEC_U8_4,
	{ GX2_COMP_SEL(_x, _0, _0, _1), GX2_ENDIAN_SWAP_8_IN_16, GX2_ATTRIB_FORMAT_UNORM_16 },          // DEC_U16_1,
	{ GX2_COMP_SEL(_x, _y, _0, _1), GX2_ENDIAN_SWAP_8_IN_16, GX2_ATTRIB_FORMAT_UNORM_16_16 },       // DEC_U16_2,
	{ GX2_COMP_SEL(_x, _y, _z, _1), GX2_ENDIAN_SWAP_8_IN_16, GX2_ATTRIB_FORMAT_UNORM_16_16_16_16 }, // DEC_U16_3,
	{ GX2_COMP_SEL(_x, _y, _z, _w), GX2_ENDIAN_SWAP_8_IN_16, GX2_ATTRIB_FORMAT_UNORM_16_16_16_16 }, // DEC_U16_4,
};

static void VertexAttribSetup(GX2AttribStream *VertexElement, u8 fmt, u8 offset, u8 location) {
	VertexElement->location = location;
	VertexElement->buffer = 0;
	VertexElement->offset = offset;
	VertexElement->format = VComp[fmt & 0xF].format;
	VertexElement->type = GX2_ATTRIB_INDEX_PER_VERTEX;
	VertexElement->aluDivisor = 0;
	VertexElement->mask = VComp[fmt & 0xF].mask;
	VertexElement->endianSwap = VComp[fmt & 0xF].endianSwap;
}

GX2FetchShader *DrawEngineGX2::SetupFetchShaderForDraw(GX2VertexShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt) {
	// TODO: Instead of one for each vshader, we can reduce it to one for each type of shader
	// that reads TEXCOORD or not, etc. Not sure if worth it.
	FetchShaderKey key{ vshader, decFmt.id };
	GX2FetchShader *fetchShader = fetchShaderMap_.Get(key);
	if (fetchShader) {
		return fetchShader;
	}

	GX2AttribStream VertexElements[8];
	GX2AttribStream *VertexElement = &VertexElements[0];

	// POSITION
	// Always
	VertexAttribSetup(VertexElement, decFmt.posfmt, decFmt.posoff, (u32)GX2Gen::VSInput::POSITION);
	VertexElement++;

	// TC
	if (decFmt.uvfmt != 0) {
		VertexAttribSetup(VertexElement, decFmt.uvfmt, decFmt.uvoff, (u32)GX2Gen::VSInput::COORDS);
		VertexElement++;
	}

	// COLOR
	if (decFmt.c0fmt != 0) {
		VertexAttribSetup(VertexElement, decFmt.c0fmt, decFmt.c0off, (u32)GX2Gen::VSInput::COLOR0);
		VertexElement++;
	}

	// Never used ?
	if (decFmt.c1fmt != 0) {
		VertexAttribSetup(VertexElement, decFmt.c1fmt, decFmt.c1off, (u32)GX2Gen::VSInput::COLOR1);
		VertexElement++;
	}

	// NORMAL
	if (decFmt.nrmfmt != 0) {
		VertexAttribSetup(VertexElement, decFmt.nrmfmt, decFmt.nrmoff, (u32)GX2Gen::VSInput::NORMAL);
		VertexElement++;
	}

	// WEIGHT
	if (decFmt.w0fmt != 0) {
		VertexAttribSetup(VertexElement, decFmt.w0fmt, decFmt.w0off, (u32)GX2Gen::VSInput::WEIGHT0);
		VertexElement++;
	}

	if (decFmt.w1fmt != 0) {
		VertexAttribSetup(VertexElement, decFmt.w1fmt, decFmt.w1off, (u32)GX2Gen::VSInput::WEIGHT1);
		VertexElement++;
	}

	// Create fetchShader
	fetchShader = new GX2FetchShader;
	fetchShader->size = GX2CalcFetchShaderSize(VertexElement - VertexElements);
	fetchShader->program = (u8 *)MEM2_alloc(fetchShader->size, GX2_SHADER_ALIGNMENT);
	GX2InitFetchShader(fetchShader, fetchShader->program, VertexElement - VertexElements, VertexElements);
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, fetchShader->program, fetchShader->size);

	// Add it to map
	fetchShaderMap_.Insert(key, fetchShader);
	return fetchShader;
}

void DrawEngineGX2::MarkUnreliable(VertexArrayInfoGX2 *vai) {
	vai->status = VertexArrayInfoGX2::VAI_UNRELIABLE;

	MEM2_free(vai->vbo);
	vai->vbo = nullptr;

	MEM2_free(vai->ebo);
	vai->ebo = nullptr;
}

void DrawEngineGX2::BeginFrame() {
	pushVerts_->Reset();
	pushInds_->Reset();
	pushUBO_->Reset();

	if (--decimationCounter_ <= 0) {
		decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	const int threshold = gpuStats.numFlips - VAI_KILL_AGE;
	const int unreliableThreshold = gpuStats.numFlips - VAI_UNRELIABLE_KILL_AGE;
	int unreliableLeft = VAI_UNRELIABLE_KILL_MAX;
	vai_.Iterate([&](u32 hash, VertexArrayInfoGX2 *vai) {
		bool kill;
		if (vai->status == VertexArrayInfoGX2::VAI_UNRELIABLE) {
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

	lastRenderStepId_ = -1;
}

VertexArrayInfoGX2::~VertexArrayInfoGX2() {
	MEM2_free(vbo);
	MEM2_free(ebo);
}

static u32 SwapRB(u32 c) { return (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000); }

// The inline wrapper in the header checks for numDrawCalls == 0
void DrawEngineGX2::DoFlush() {
	PROFILE_THIS_SCOPE("Flush");
	gpuStats.numFlushes++;
	gpuStats.numTrackedVertexArrays = (int)vai_.size();

	// In GX2, we're synchronous and state carries over so all we reset here on a new step is the viewport/scissor.
	int curRenderStepId = draw_->GetCurrentStepId();
	if (lastRenderStepId_ != curRenderStepId) {
		// Dirty everything that has dynamic state that will need re-recording.
		gstate_c.Dirty(DIRTY_VIEWPORTSCISSOR_STATE);
		lastRenderStepId_ = curRenderStepId;
	}

	// This is not done on every drawcall, we collect vertex data
	// until critical state changes. That's when we draw (flush).

	GEPrimitiveType prim = prevPrim_;
	ApplyDrawState(prim);

	// Always use software for flat shading to fix the provoking index.
	bool tess = gstate_c.bezier || gstate_c.spline;
	bool useHWTransform = CanUseHardwareTransform(prim);

	if (useHWTransform) {
		PROFILE_THIS_SCOPE("T-hard");
		void *vb_ = nullptr;
		void *ib_ = nullptr;

		int vertexCount = 0;
		int maxIndex = 0;
		bool useElements = true;

		// Cannot cache vertex data with morph enabled.
		bool useCache = g_Config.bVertexCache && !(lastVType_ & GE_VTYPE_MORPHCOUNT_MASK);
		// Also avoid caching when software skinning.
		if (g_Config.bSoftwareSkinning && (lastVType_ & GE_VTYPE_WEIGHT_MASK))
			useCache = false;

		if (useCache) {
			PROFILE_THIS_SCOPE("vcache");
			u32 id = dcid_ ^ gstate.getUVGenMode(); // This can have an effect on which UV decoder we need to use! And hence what the decoded data will look like. See #9263

			VertexArrayInfoGX2 *vai = vai_.Get(id);
			if (!vai) {
				vai = new VertexArrayInfoGX2();
				vai_.Insert(id, vai);
			}

			switch (vai->status) {
			case VertexArrayInfoGX2::VAI_NEW: {
				// Haven't seen this one before.
				uint64_t dataHash = ComputeHash();
				vai->hash = dataHash;
				vai->minihash = ComputeMiniHash();
				vai->status = VertexArrayInfoGX2::VAI_HASHING;
				vai->drawsUntilNextFullHash = 0;
				DecodeVerts(decoded); // writes to indexGen
				vai->numVerts = indexGen.VertexCount();
				vai->prim = indexGen.Prim();
				vai->maxIndex = indexGen.MaxIndex();
				vai->flags = gstate_c.vertexFullAlpha ? VAI11_FLAG_VERTEXFULLALPHA : 0;
				goto rotateVBO;
			}

				// Hashing - still gaining confidence about the buffer.
				// But if we get this far it's likely to be worth creating a vertex buffer.
			case VertexArrayInfoGX2::VAI_HASHING: {
				PROFILE_THIS_SCOPE("vcachehash");
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
						DecodeVerts(decoded);
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
					// if (vai->numFrames > 1000) {
					//	vai->status = VertexArrayInfo::VAI_RELIABLE;
					//}
				} else {
					vai->drawsUntilNextFullHash--;
					u32 newMiniHash = ComputeMiniHash();
					if (newMiniHash != vai->minihash) {
						MarkUnreliable(vai);
						DecodeVerts(decoded);
						goto rotateVBO;
					}
				}

				if (vai->vbo == 0) {
					DecodeVerts(decoded);
					vai->numVerts = indexGen.VertexCount();
					vai->prim = indexGen.Prim();
					vai->maxIndex = indexGen.MaxIndex();
					vai->flags = gstate_c.vertexFullAlpha ? VAI11_FLAG_VERTEXFULLALPHA : 0;
					useElements = !indexGen.SeenOnlyPurePrims() || prim == GE_PRIM_TRIANGLE_FAN;
					if (!useElements && indexGen.PureCount()) {
						vai->numVerts = indexGen.PureCount();
					}

					_dbg_assert_msg_(gstate_c.vertBounds.minV >= gstate_c.vertBounds.maxV, "Should not have checked UVs when caching.");

					// TODO: Combine these two into one buffer?
					u32 size = dec_->GetDecVtxFmt().stride * indexGen.MaxIndex();
					vai->vbo = MEM2_alloc(size, GX2_VERTEX_BUFFER_ALIGNMENT);
					memcpy(vai->vbo, decoded, size);
					GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vai->vbo, size);
					if (useElements) {
						u32 size = sizeof(short) * indexGen.VertexCount();
						vai->ebo = MEM2_alloc(size, GX2_INDEX_BUFFER_ALIGNMENT);
						memcpy(vai->ebo, decIndex, size);
						GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, vai->ebo, size);
					} else {
						vai->ebo = 0;
					}
				} else {
					gpuStats.numCachedDrawCalls++;
					useElements = vai->ebo ? true : false;
					gpuStats.numCachedVertsDrawn += vai->numVerts;
					gstate_c.vertexFullAlpha = vai->flags & VAI11_FLAG_VERTEXFULLALPHA;
				}
				vb_ = vai->vbo;
				ib_ = vai->ebo;
				vertexCount = vai->numVerts;
				maxIndex = vai->maxIndex;
				prim = static_cast<GEPrimitiveType>(vai->prim);
				break;
			}

				// Reliable - we don't even bother hashing anymore. Right now we don't go here until after a very long time.
			case VertexArrayInfoGX2::VAI_RELIABLE: {
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

				gstate_c.vertexFullAlpha = vai->flags & VAI11_FLAG_VERTEXFULLALPHA;
				break;
			}

			case VertexArrayInfoGX2::VAI_UNRELIABLE: {
				vai->numDraws++;
				if (vai->lastFrame != gpuStats.numFlips) {
					vai->numFrames++;
				}
				DecodeVerts(decoded);
				goto rotateVBO;
			}
			}

			vai->lastFrame = gpuStats.numFlips;
		} else {
			DecodeVerts(decoded);
		rotateVBO:
			gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
			useElements = !indexGen.SeenOnlyPurePrims() || prim == GE_PRIM_TRIANGLE_FAN;
			vertexCount = indexGen.VertexCount();
			maxIndex = indexGen.MaxIndex();
			if (!useElements && indexGen.PureCount()) {
				vertexCount = indexGen.PureCount();
			}
			prim = indexGen.Prim();
		}

		VERBOSE_LOG(G3D, "Flush prim %i! %i verts in one go", prim, vertexCount);
		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		ApplyDrawStateLate(true, dynState_.stencilRef);

		GX2VShader *vshader;
		GX2PShader *fshader;
		shaderManager_->GetShaders(prim, lastVType_, &vshader, &fshader, useHWTransform, useHWTessellation_);
		GX2FetchShader *fetchShader = SetupFetchShaderForDraw(vshader, dec_->GetDecVtxFmt(), dec_->VertexType());
		GX2SetPixelShader(fshader);
		GX2SetVertexShader(vshader);
		shaderManager_->UpdateUniforms(pushUBO_, framebufferManager_->UseBufferedRendering());

		GX2SetFetchShader(fetchShader);
		u32 stride = dec_->GetDecVtxFmt().stride;
		//		GX2prim[prim];
		if (!vb_) {
			// Push!
			u32 vOffset;
			int vSize = (maxIndex + 1) * dec_->GetDecVtxFmt().stride;
			u8 *vptr = pushVerts_->BeginPush(&vOffset, vSize);
			memcpy(vptr, decoded, vSize);
			pushVerts_->EndPush();
			GX2SetAttribBuffer(0, vSize, stride, vptr);
			if (useElements) {
				u32 iOffset;
				int iSize = 2 * indexGen.VertexCount();
				u8 *iptr = pushInds_->BeginPush(&iOffset, iSize);
				memcpy(iptr, decIndex, iSize);
				pushInds_->EndPush();
				GX2DrawIndexedEx(GX2prim[prim], vertexCount, GX2_INDEX_TYPE_U16, iptr, 0, 1);
			} else {
				GX2DrawEx(GX2prim[prim], vertexCount, 0, 1);
			}
		} else {
			GX2SetAttribBuffer(0, vertexCount * stride, stride, vb_);
			if (useElements) {
				GX2DrawIndexedEx(GX2prim[prim], vertexCount, GX2_INDEX_TYPE_U16, ib_, 0, 1);
			} else {
				GX2DrawEx(GX2prim[prim], vertexCount, 0, 1);
			}
		}
	} else {
		PROFILE_THIS_SCOPE("T-soft");
		DecodeVerts(decoded);
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

		u16 *inds = decIndex;
		SoftwareTransformResult result{};
		SoftwareTransformParams params{};
		params.decoded = decoded;
		params.transformed = transformed;
		params.transformedExpanded = transformedExpanded;
		params.fbman = framebufferManager_;
		params.texCache = textureCache_;
		params.allowClear = true;
		params.allowSeparateAlphaClear = false; // GX2 doesn't support separate alpha clears
		params.provokeFlatFirst = true;

		int maxIndex = indexGen.MaxIndex();
		SoftwareTransform swTransform(params);
		swTransform.Decode(prim, dec_->VertexType(), dec_->GetDecVtxFmt(), maxIndex, &result);
		if (result.action == SW_NOT_READY) {
			swTransform.DetectOffsetTexture(maxIndex);
			swTransform.BuildDrawingParams(prim, indexGen.VertexCount(), dec_->VertexType(), inds, maxIndex, &result);
		}

		if (result.setSafeSize)
			framebufferManager_->SetSafeSize(result.safeWidth, result.safeHeight);

		if (result.action == SW_DRAW_PRIMITIVES) {
			ApplyDrawStateLate(result.setStencil, result.stencilValue);

			GX2VShader *vshader;
			GX2PShader *fshader;
			shaderManager_->GetShaders(prim, lastVType_, &vshader, &fshader, false, false);
			GX2SetPixelShader(fshader);
			GX2SetVertexShader(vshader);
			shaderManager_->UpdateUniforms(pushUBO_, framebufferManager_->UseBufferedRendering());

			// We really do need a vertex layout for each vertex shader (or at least check its ID bits for what inputs it uses)!
			// Some vertex shaders ignore one of the inputs, and then the layout created from it will lack it, which will be a problem for others.
			FetchShaderKey key{ vshader, 0xFFFFFFFF }; // Let's use 0xFFFFFFFF to signify TransformedVertex
			GX2FetchShader *fetchShader = fetchShaderMap_.Get(key);
			if (!fetchShader) {
				fetchShader = new GX2FetchShader;
				fetchShader->size = GX2CalcFetchShaderSize(ARRAY_SIZE(TransformedVertexElements));
				fetchShader->program = (u8 *)MEM2_alloc(fetchShader->size, GX2_SHADER_ALIGNMENT);
				GX2InitFetchShader(fetchShader, fetchShader->program, ARRAY_SIZE(TransformedVertexElements), TransformedVertexElements);
				GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, fetchShader->program, fetchShader->size);
				fetchShaderMap_.Insert(key, fetchShader);
			}
			GX2SetFetchShader(fetchShader);

			u32 stride = sizeof(TransformedVertex);
			u32 vOffset = 0;
			int vSize = maxIndex * stride;
			u8 *vptr = pushVerts_->BeginPush(&vOffset, vSize);
			memcpy(vptr, result.drawBuffer, vSize);
			pushVerts_->EndPush();
			GX2SetAttribBuffer(0, vSize, stride, vptr);
			if (result.drawIndexed) {
				u32 iOffset;
				int iSize = sizeof(u16) * result.drawNumTrans;
				u8 *iptr = pushInds_->BeginPush(&iOffset, iSize);
				memcpy(iptr, inds, iSize);
				pushInds_->EndPush();
				GX2DrawIndexedEx(GX2prim[prim], result.drawNumTrans, GX2_INDEX_TYPE_U16, iptr, 0, 1);
			} else {
				GX2DrawEx(GX2prim[prim], result.drawNumTrans, 0, 1);
			}
		} else if (result.action == SW_CLEAR) {
			u32 clearColor = result.color;
			float clearDepth = result.depth;

			u32 clearFlag = 0;

			if (gstate.isClearModeColorMask())
				clearFlag |= Draw::FBChannel::FB_COLOR_BIT;
			if (gstate.isClearModeAlphaMask())
				clearFlag |= Draw::FBChannel::FB_STENCIL_BIT;
			if (gstate.isClearModeDepthMask())
				clearFlag |= Draw::FBChannel::FB_DEPTH_BIT;

			if (clearFlag & Draw::FBChannel::FB_DEPTH_BIT) {
				framebufferManager_->SetDepthUpdated();
			}
			if (clearFlag & Draw::FBChannel::FB_COLOR_BIT) {
				framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);
			}

			u8 clearStencil = clearColor >> 24;
			draw_->Clear(clearFlag, clearColor, clearDepth, clearStencil);

			if ((gstate_c.featureFlags & GPU_USE_CLEAR_RAM_HACK) && gstate.isClearModeColorMask() && (gstate.isClearModeAlphaMask() || gstate.FrameBufFormat() == GE_FORMAT_565)) {
				int scissorX1 = gstate.getScissorX1();
				int scissorY1 = gstate.getScissorY1();
				int scissorX2 = gstate.getScissorX2() + 1;
				int scissorY2 = gstate.getScissorY2() + 1;
				framebufferManager_->ApplyClearToMemory(scissorX1, scissorY1, scissorX2, scissorY2, clearColor);
			}
		}
	}

	gpuStats.numDrawCalls += numDrawCalls;
	gpuStats.numVertsSubmitted += vertexCountInDrawCalls_;

	indexGen.Reset();
	decodedVerts_ = 0;
	numDrawCalls = 0;
	vertexCountInDrawCalls_ = 0;
	decodeCounter_ = 0;
	dcid_ = 0;
	prevPrim_ = GE_PRIM_INVALID;
	gstate_c.vertexFullAlpha = true;
	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

	// Now seems as good a time as any to reset the min/max coords, which we may examine later.
	gstate_c.vertBounds.minU = 512;
	gstate_c.vertBounds.minV = 512;
	gstate_c.vertBounds.maxU = 0;
	gstate_c.vertBounds.maxV = 0;

	{
		PROFILE_THIS_SCOPE("GX2Flush");
		GX2Flush();
	}
#if 0
	// We only support GPU debugging on Windows, and that's the only use case for this.
		GPUDebug::NotifyDraw();
#endif
}

void DrawEngineGX2::TessellationDataTransferGX2::SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) {
#if 0
	// Position
	if (prevSize < size) {
		prevSize = size;
		MEM2_free(data_tex[0].surface.image);
		data_tex[0].surface.width = size;
		data_tex[0].surface.height = 1;
		data_tex[0].surface.depth = 1;
		data_tex[0].surface.dim = GX2_SURFACE_DIM_TEXTURE_1D;
		data_tex[0].surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
		data_tex[0].surface.use = GX2_SURFACE_USE_TEXTURE;
		data_tex[0].viewNumSlices = 1;
		data_tex[0].surface.format = GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32;
		data_tex[0].compMap = GX2_COMP_SEL(_a, _r, _g, _b);
		GX2CalcSurfaceSizeAndAlignment(&data_tex[0].surface);
		GX2InitTextureRegs(&data_tex[0]);
		data_tex[0].surface.image = MEM2_alloc(data_tex[0].surface.imageSize, data_tex[0].surface.alignment);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, data_tex[0].surface.image, data_tex[0].surface.imageSize);

		if (!data_tex[0].surface.image) {
			INFO_LOG(G3D, "Failed to create GX2 texture for HW tessellation");
			return; // TODO: Turn off HW tessellation if texture creation error occured.
		}
		GX2SetVertexTexture(&data_tex[0], 0);
	}
	const u32 *src = (const u32 *)pos;
	u32 *dst = (u32 *)data_tex[0].surface.image;
	while (src < (u32 *)pos + size) {
		*dst++ = __builtin_bswap32(*src++);
	}

	// Texcoords
	if (hasTexCoords) {
		if (prevSizeTex < size) {
			prevSizeTex = size;
			MEM2_free(data_tex[1].surface.image);
			data_tex[1].surface.width = size;
			data_tex[1].surface.height = 1;
			data_tex[1].surface.depth = 1;
			data_tex[1].surface.dim = GX2_SURFACE_DIM_TEXTURE_1D;
			data_tex[1].surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
			data_tex[1].surface.use = GX2_SURFACE_USE_TEXTURE;
			data_tex[1].viewNumSlices = 1;
			data_tex[1].surface.format = GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32;
			data_tex[1].compMap = GX2_COMP_SEL(_a, _r, _g, _b);
			GX2CalcSurfaceSizeAndAlignment(&data_tex[1].surface);
			GX2InitTextureRegs(&data_tex[1]);
			data_tex[1].surface.image = MEM2_alloc(data_tex[1].surface.imageSize, data_tex[1].surface.alignment);
			if (!data_tex[1].surface.image) {
				INFO_LOG(G3D, "Failed to create GX2 texture for HW tessellation");
				return; // TODO: Turn off HW tessellation if texture creation error occured.
			}
			GX2SetVertexTexture(&data_tex[1], 1);
		}
		src = (const u32 *)pos;
		dst = (u32 *)data_tex[1].surface.image;
		while (src < (u32 *)pos + size) {
			*dst++ = __builtin_bswap32(*src++);
		}
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, data_tex[1].surface.image, data_tex[1].surface.imageSize);
	}

	// Color
	int sizeColor = hasColor ? size : 1;
	if (prevSizeCol < sizeColor) {
		prevSizeCol = sizeColor;
		MEM2_free(data_tex[2].surface.image);
		data_tex[2].surface.width = sizeColor;
		data_tex[2].surface.height = 1;
		data_tex[2].surface.depth = 1;
		data_tex[2].surface.dim = GX2_SURFACE_DIM_TEXTURE_1D;
		data_tex[2].surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
		data_tex[2].surface.use = GX2_SURFACE_USE_TEXTURE;
		data_tex[2].viewNumSlices = 1;
		data_tex[2].surface.format = GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32;
		data_tex[2].compMap = GX2_COMP_SEL(_a, _r, _g, _b);
		GX2CalcSurfaceSizeAndAlignment(&data_tex[2].surface);
		GX2InitTextureRegs(&data_tex[2]);
		data_tex[2].surface.image = MEM2_alloc(data_tex[2].surface.imageSize, data_tex[2].surface.alignment);
		if (!data_tex[2].surface.image) {
			INFO_LOG(G3D, "Failed to create GX2 texture for HW tessellation");
			return; // TODO: Turn off HW tessellation if texture creation error occured.
		}
		GX2SetVertexTexture(&data_tex[2], 2);
	}
	src = (const u32 *)col;
	dst = (u32 *)data_tex[2].surface.image;
	while (src < (u32 *)pos + sizeColor) {
		*dst++ = __builtin_bswap32(*src++);
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, data_tex[2].surface.image, data_tex[2].surface.imageSize);
#endif
}
