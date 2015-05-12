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

#include "base/logging.h"
#include "base/timeutil.h"

#include "Common/MemoryUtil.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"

#include "helper/dx_state.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/Directx9/StateMappingDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/TransformPipelineDX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/GPU_DX9.h"

namespace DX9 {

const D3DPRIMITIVETYPE glprim[8] = {
	D3DPT_POINTLIST,
	D3DPT_LINELIST,
	D3DPT_LINESTRIP,
	D3DPT_TRIANGLELIST,
	D3DPT_TRIANGLESTRIP,
	D3DPT_TRIANGLEFAN,
	D3DPT_TRIANGLELIST,	 // With OpenGL ES we have to expand sprites into triangles, tripling the data instead of doubling. sigh. OpenGL ES, Y U NO SUPPORT GL_QUADS?
};

// hrydgard's quick guesses - TODO verify
static const int D3DPRIMITIVEVERTEXCOUNT[8][2] = {
	{0, 0}, // invalid
	{1, 0}, // 1 = D3DPT_POINTLIST,
	{2, 0}, // 2 = D3DPT_LINELIST,
	{2, 1}, // 3 = D3DPT_LINESTRIP,
	{3, 0}, // 4 = D3DPT_TRIANGLELIST,
	{1, 2}, // 5 = D3DPT_TRIANGLESTRIP,
	{1, 2}, // 6 = D3DPT_TRIANGLEFAN,
};

int D3DPrimCount(D3DPRIMITIVETYPE prim, int size) {
	return (size / D3DPRIMITIVEVERTEXCOUNT[prim][0]) - D3DPRIMITIVEVERTEXCOUNT[prim][1];
}

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex)
};

#define VERTEXCACHE_DECIMATION_INTERVAL 17

enum { VAI_KILL_AGE = 120, VAI_UNRELIABLE_KILL_AGE = 240, VAI_UNRELIABLE_KILL_MAX = 4 };

TransformDrawEngineDX9::TransformDrawEngineDX9()
	:	decodedVerts_(0),
		prevPrim_(GE_PRIM_INVALID),
		lastVType_(-1),
		shaderManager_(0),
		textureCache_(0),
		framebufferManager_(0),
		numDrawCalls(0),
		vertexCountInDrawCalls(0),
		decodeCounter_(0),
		dcid_(0),
		uvScale(0),
		fboTexBound_(false) {

	memset(&decOptions_, 0, sizeof(decOptions_));
	decOptions_.expandAllUVtoFloat = true;
	decOptions_.expandAllWeightsToFloat = true;
	decOptions_.expand8BitNormalsToFloat = true;

	decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;
	// Allocate nicely aligned memory. Maybe graphics drivers will
	// appreciate it.
	// All this is a LOT of memory, need to see if we can cut down somehow.
	decoded = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE);
	decIndex = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE);
	splineBuffer = (u8 *)AllocateMemoryPages(SPLINE_BUFFER_SIZE);
	transformed = (TransformedVertex *)AllocateMemoryPages(TRANSFORMED_VERTEX_BUFFER_SIZE);
	transformedExpanded = (TransformedVertex *)AllocateMemoryPages(3 * TRANSFORMED_VERTEX_BUFFER_SIZE);

	if (g_Config.bPrescaleUV) {
		uvScale = new UVScale[MAX_DEFERRED_DRAW_CALLS];
	}
	indexGen.Setup(decIndex);

	InitDeviceObjects();
}

TransformDrawEngineDX9::~TransformDrawEngineDX9() {
	DestroyDeviceObjects();
	FreeMemoryPages(decoded, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex, DECODED_INDEX_BUFFER_SIZE);
	FreeMemoryPages(splineBuffer, SPLINE_BUFFER_SIZE);
	FreeMemoryPages(transformed, TRANSFORMED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(transformedExpanded, 3 * TRANSFORMED_VERTEX_BUFFER_SIZE);
	for (auto decl = vertexDeclMap_.begin(); decl != vertexDeclMap_.end(); ++decl) {
		if (decl->second) {
			decl->second->Release();
		}
	}

	delete [] uvScale;
}

void TransformDrawEngineDX9::InitDeviceObjects() {

}

void TransformDrawEngineDX9::DestroyDeviceObjects() {
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
	// Not supported in regular DX9 so faking, will cause graphics bugs until worked around
	{0,"UNUSED_DEC_U8A_2"}, // DEC_U8A_2,
	{0,"UNUSED_DEC_U16A_2" }, // DEC_U16A_2,
};

static void VertexAttribSetup(D3DVERTEXELEMENT9 * VertexElement, u8 fmt, u8 offset, u8 usage, u8 usage_index = 0) {
	memset(VertexElement, 0, sizeof(D3DVERTEXELEMENT9));
	VertexElement->Offset = offset;
	VertexElement->Type = VComp[fmt].type;
	VertexElement->Usage = usage;
	VertexElement->UsageIndex = usage_index;
}

IDirect3DVertexDeclaration9 *TransformDrawEngineDX9::SetupDecFmtForDraw(VSShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt) {
	auto vertexDeclCached = vertexDeclMap_.find(pspFmt);

	if (vertexDeclCached == vertexDeclMap_.end()) {
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
		HRESULT hr = pD3Ddevice->CreateVertexDeclaration( VertexElements, &pHardwareVertexDecl );
		if (FAILED(hr)) {
			ERROR_LOG(G3D, "Failed to create vertex declaration!");
			pHardwareVertexDecl = nullptr;
		}

		// Add it to map
		vertexDeclMap_[pspFmt] = pHardwareVertexDecl;
		return pHardwareVertexDecl;
	} else {
		// Set it from map
		return vertexDeclCached->second;
	}
}

VertexDecoder *TransformDrawEngineDX9::GetVertexDecoder(u32 vtype) {
	auto iter = decoderMap_.find(vtype);
	if (iter != decoderMap_.end())
		return iter->second;
	VertexDecoder *dec = new VertexDecoder();
	dec->SetVertexType(vtype, decOptions_, decJitCache_);
	decoderMap_[vtype] = dec;
	return dec;
}

void TransformDrawEngineDX9::SetupVertexDecoder(u32 vertType) {
	SetupVertexDecoderInternal(vertType);
}

inline void TransformDrawEngineDX9::SetupVertexDecoderInternal(u32 vertType) {
	// As the decoder depends on the UVGenMode when we use UV prescale, we simply mash it
	// into the top of the verttype where there are unused bits.
	const u32 vertTypeID = (vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24);

	// If vtype has changed, setup the vertex decoder.
	if (vertTypeID != lastVType_) {
		dec_ = GetVertexDecoder(vertTypeID);
		lastVType_ = vertTypeID;
	}
}

void TransformDrawEngineDX9::SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) {
	if (!indexGen.PrimCompatible(prevPrim_, prim) || numDrawCalls >= MAX_DEFERRED_DRAW_CALLS || vertexCountInDrawCalls + vertexCount > VERTEX_BUFFER_MAX)
		Flush();

	// TODO: Is this the right thing to do?
	if (prim == GE_PRIM_KEEP_PREVIOUS) {
		prim = prevPrim_ != GE_PRIM_INVALID ? prevPrim_ : GE_PRIM_POINTS;
	} else {
		prevPrim_ = prim;
	}

	SetupVertexDecoderInternal(vertType);

	*bytesRead = vertexCount * dec_->VertexSize();

	if ((vertexCount < 2 && prim > 0) || (vertexCount < 3 && prim > 2 && prim != GE_PRIM_RECTANGLES))
		return;

	DeferredDrawCall &dc = drawCalls[numDrawCalls];
	dc.verts = verts;
	dc.inds = inds;
	dc.vertType = vertType;
	dc.indexType = (vertType & GE_VTYPE_IDX_MASK) >> GE_VTYPE_IDX_SHIFT;
	dc.prim = prim;
	dc.vertexCount = vertexCount;

	u32 dhash = dcid_;
	dhash ^= (u32)(uintptr_t)verts;
	dhash = __rotl(dhash, 13);
	dhash ^= (u32)(uintptr_t)inds;
	dhash = __rotl(dhash, 13);
	dhash ^= (u32)vertType;
	dhash = __rotl(dhash, 13);
	dhash ^= (u32)vertexCount;
	dhash = __rotl(dhash, 13);
	dhash ^= (u32)prim;
	dcid_ = dhash;

	if (inds) {
		GetIndexBounds(inds, vertexCount, vertType, &dc.indexLowerBound, &dc.indexUpperBound);
	} else {
		dc.indexLowerBound = 0;
		dc.indexUpperBound = vertexCount - 1;
	}

	if (uvScale) {
		uvScale[numDrawCalls] = gstate_c.uv;
	}

	numDrawCalls++;
	vertexCountInDrawCalls += vertexCount;

	if (g_Config.bSoftwareSkinning && (vertType & GE_VTYPE_WEIGHT_MASK)) {
		DecodeVertsStep();
		decodeCounter_++;
	}

	if (prim == GE_PRIM_RECTANGLES && (gstate.getTextureAddress(0) & 0x3FFFFFFF) == (gstate.getFrameBufAddress() & 0x3FFFFFFF)) {
		// Rendertarget == texture?
		if (!g_Config.bDisableSlowFramebufEffects) {
			gstate_c.textureChanged |= TEXCHANGE_PARAMSONLY;
			Flush();
		}
	}
}

void TransformDrawEngineDX9::DecodeVerts() {
	if (uvScale) {
		const UVScale origUV = gstate_c.uv;
		for (; decodeCounter_ < numDrawCalls; decodeCounter_++) {
			gstate_c.uv = uvScale[decodeCounter_];
			DecodeVertsStep();
		}
		gstate_c.uv = origUV;
	} else {
		for (; decodeCounter_ < numDrawCalls; decodeCounter_++) {
			DecodeVertsStep();
		}
	}
	// Sanity check
	if (indexGen.Prim() < 0) {
		ERROR_LOG_REPORT(G3D, "DecodeVerts: Failed to deduce prim: %i", indexGen.Prim());
		// Force to points (0)
		indexGen.AddPrim(GE_PRIM_POINTS, 0);
	}
}

void TransformDrawEngineDX9::DecodeVertsStep() {
	const int i = decodeCounter_;

	const DeferredDrawCall &dc = drawCalls[i];

	indexGen.SetIndex(decodedVerts_);
	int indexLowerBound = dc.indexLowerBound, indexUpperBound = dc.indexUpperBound;

	u32 indexType = dc.indexType;
	void *inds = dc.inds;
	if (indexType == GE_VTYPE_IDX_NONE >> GE_VTYPE_IDX_SHIFT) {
		// Decode the verts and apply morphing. Simple.
		dec_->DecodeVerts(decoded + decodedVerts_ * (int)dec_->GetDecVtxFmt().stride,
			dc.verts, indexLowerBound, indexUpperBound);
		decodedVerts_ += indexUpperBound - indexLowerBound + 1;
		indexGen.AddPrim(dc.prim, dc.vertexCount);
	} else {
		// It's fairly common that games issue long sequences of PRIM calls, with differing
		// inds pointer but the same base vertex pointer. We'd like to reuse vertices between
		// these as much as possible, so we make sure here to combine as many as possible
		// into one nice big drawcall, sharing data.

		// 1. Look ahead to find the max index, only looking as "matching" drawcalls.
		//    Expand the lower and upper bounds as we go.
		int lastMatch = i;
		const int total = numDrawCalls;
		if (uvScale) {
			for (int j = i + 1; j < total; ++j) {
				if (drawCalls[j].verts != dc.verts)
					break;
				if (memcmp(&uvScale[j], &uvScale[i], sizeof(uvScale[0])) != 0)
					break;

				indexLowerBound = std::min(indexLowerBound, (int)drawCalls[j].indexLowerBound);
				indexUpperBound = std::max(indexUpperBound, (int)drawCalls[j].indexUpperBound);
				lastMatch = j;
			}
		} else {
			for (int j = i + 1; j < total; ++j) {
				if (drawCalls[j].verts != dc.verts)
					break;

				indexLowerBound = std::min(indexLowerBound, (int)drawCalls[j].indexLowerBound);
				indexUpperBound = std::max(indexUpperBound, (int)drawCalls[j].indexUpperBound);
				lastMatch = j;
			}
		}

		// 2. Loop through the drawcalls, translating indices as we go.
		switch (indexType) {
		case GE_VTYPE_IDX_8BIT >> GE_VTYPE_IDX_SHIFT:
			for (int j = i; j <= lastMatch; j++) {
				indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u8 *)drawCalls[j].inds, indexLowerBound);
			}
			break;
		case GE_VTYPE_IDX_16BIT >> GE_VTYPE_IDX_SHIFT:
			for (int j = i; j <= lastMatch; j++) {
				indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u16 *)drawCalls[j].inds, indexLowerBound);
			}
			break;
		}

		const int vertexCount = indexUpperBound - indexLowerBound + 1;

		// This check is a workaround for Pangya Fantasy Golf, which sends bogus index data when switching items in "My Room" sometimes.
		if (decodedVerts_ + vertexCount > VERTEX_BUFFER_MAX) {
			return;
		}

		// 3. Decode that range of vertex data.
		dec_->DecodeVerts(decoded + decodedVerts_ * (int)dec_->GetDecVtxFmt().stride,
			dc.verts, indexLowerBound, indexUpperBound);
		decodedVerts_ += vertexCount;

		// 4. Advance indexgen vertex counter.
		indexGen.Advance(vertexCount);
		decodeCounter_ = lastMatch;
	}
}

inline u32 ComputeMiniHashRange(const void *ptr, size_t sz) {
	// Switch to u32 units.
	const u32 *p = (const u32 *)ptr;
	sz >>= 2;

	if (sz > 100) {
		size_t step = sz / 4;
		u32 hash = 0;
		for (size_t i = 0; i < sz; i += step) {
			hash += DoReliableHash32(p + i, 100, 0x3A44B9C4);
		}
		return hash;
	} else {
		return p[0] + p[sz - 1];
	}
}

u32 TransformDrawEngineDX9::ComputeMiniHash() {
	u32 fullhash = 0;
	const int vertexSize = dec_->GetDecVtxFmt().stride;
	const int indexSize = (dec_->VertexType() & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT ? 2 : 1;

	int step;
	if (numDrawCalls < 3) {
		step = 1;
	} else if (numDrawCalls < 8) {
		step = 4;
	} else {
		step = numDrawCalls / 8;
	}
	for (int i = 0; i < numDrawCalls; i += step) {
		const DeferredDrawCall &dc = drawCalls[i];
		if (!dc.inds) {
			fullhash += ComputeMiniHashRange(dc.verts, vertexSize * dc.vertexCount);
		} else {
			int indexLowerBound = dc.indexLowerBound, indexUpperBound = dc.indexUpperBound;
			fullhash += ComputeMiniHashRange((const u8 *)dc.verts + vertexSize * indexLowerBound, vertexSize * (indexUpperBound - indexLowerBound));
			fullhash += ComputeMiniHashRange(dc.inds, indexSize * dc.vertexCount);
		}
	}

	return fullhash;
}

void TransformDrawEngineDX9::MarkUnreliable(VertexArrayInfoDX9 *vai) {
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

ReliableHashType TransformDrawEngineDX9::ComputeHash() {
	ReliableHashType fullhash = 0;
	const int vertexSize = dec_->GetDecVtxFmt().stride;
	const int indexSize = (dec_->VertexType() & GE_VTYPE_IDX_MASK) == GE_VTYPE_IDX_16BIT ? 2 : 1;

	// TODO: Add some caps both for numDrawCalls and num verts to check?
	// It is really very expensive to check all the vertex data so often.
	for (int i = 0; i < numDrawCalls; i++) {
		const DeferredDrawCall &dc = drawCalls[i];
		if (!dc.inds) {
			fullhash += DoReliableHash((const char *)dc.verts, vertexSize * dc.vertexCount, 0x1DE8CAC4);
		} else {
			int indexLowerBound = dc.indexLowerBound, indexUpperBound = dc.indexUpperBound;
			int j = i + 1;
			int lastMatch = i;
			while (j < numDrawCalls) {
				if (drawCalls[j].verts != dc.verts)
					break;
				indexLowerBound = std::min(indexLowerBound, (int)dc.indexLowerBound);
				indexUpperBound = std::max(indexUpperBound, (int)dc.indexUpperBound);
				lastMatch = j;
				j++;
			}
			// This could get seriously expensive with sparse indices. Need to combine hashing ranges the same way
			// we do when drawing.
			fullhash += DoReliableHash((const char *)dc.verts + vertexSize * indexLowerBound,
				vertexSize * (indexUpperBound - indexLowerBound), 0x029F3EE1);
			// Hm, we will miss some indices when combining above, but meh, it should be fine.
			fullhash += DoReliableHash((const char *)dc.inds, indexSize * dc.vertexCount, 0x955FD1CA);
			i = lastMatch;
		}
	}
	if (uvScale) {
		fullhash += DoReliableHash(&uvScale[0], sizeof(uvScale[0]) * numDrawCalls, 0x0123e658);
	}

	return fullhash;
}

void TransformDrawEngineDX9::ClearTrackedVertexArrays() {
	for (auto vai = vai_.begin(); vai != vai_.end(); vai++) {
		delete vai->second;
	}
	vai_.clear();
}

void TransformDrawEngineDX9::DecimateTrackedVertexArrays() {
	if (--decimationCounter_ <= 0) {
		decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	const int threshold = gpuStats.numFlips - VAI_KILL_AGE;
	const int unreliableThreshold = gpuStats.numFlips - VAI_UNRELIABLE_KILL_AGE;
	int unreliableLeft = VAI_UNRELIABLE_KILL_MAX;
	for (auto iter = vai_.begin(); iter != vai_.end(); ) {
		bool kill;
		if (iter->second->status == VertexArrayInfoDX9::VAI_UNRELIABLE) {
			// We limit killing unreliable so we don't rehash too often.
			kill = iter->second->lastFrame < unreliableThreshold && --unreliableLeft >= 0;
		} else {
			kill = iter->second->lastFrame < threshold;
		}
		if (kill) {
			delete iter->second;
			vai_.erase(iter++);
		} else {
			++iter;
		}
	}

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

// The inline wrapper in the header checks for numDrawCalls == 0
void TransformDrawEngineDX9::DoFlush() {
	gpuStats.numFlushes++;
	gpuStats.numTrackedVertexArrays = (int)vai_.size();

	// This is not done on every drawcall, we should collect vertex data
	// until critical state changes. That's when we draw (flush).

	GEPrimitiveType prim = prevPrim_;
	ApplyDrawState(prim);

	VSShader *vshader = shaderManager_->ApplyShader(prim, lastVType_);

	if (vshader->UseHWTransform()) {
		LPDIRECT3DVERTEXBUFFER9 vb_ = NULL;
		LPDIRECT3DINDEXBUFFER9 ib_ = NULL;

		int vertexCount = 0;
		int maxIndex = 0;
		bool useElements = true;

		// Cannot cache vertex data with morph enabled.
		bool useCache = g_Config.bVertexCache && !(lastVType_ & GE_VTYPE_MORPHCOUNT_MASK);
		// Also avoid caching when software skinning.
		if (g_Config.bSoftwareSkinning && (lastVType_ & GE_VTYPE_WEIGHT_MASK))
			useCache = false;

		if (useCache) {
			u32 id = dcid_;
			auto iter = vai_.find(id);
			VertexArrayInfoDX9 *vai;
			if (iter != vai_.end()) {
				// We've seen this before. Could have been a cached draw.
				vai = iter->second;
			} else {
				vai = new VertexArrayInfoDX9();
				vai_[id] = vai;
			}

			switch (vai->status) {
			case VertexArrayInfoDX9::VAI_NEW:
				{
					// Haven't seen this one before.
					ReliableHashType dataHash = ComputeHash();
					vai->hash = dataHash;
					vai->minihash = ComputeMiniHash();
					vai->status = VertexArrayInfoDX9::VAI_HASHING;
					vai->drawsUntilNextFullHash = 0;
					DecodeVerts(); // writes to indexGen
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
						ReliableHashType newHash = vai->hash;
						if (newMiniHash == vai->minihash) {
							newHash = ComputeHash();
						}
						if (newMiniHash != vai->minihash || newHash != vai->hash) {
							MarkUnreliable(vai);
							DecodeVerts();
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
							DecodeVerts();
							goto rotateVBO;
						}
					}

					if (vai->vbo == 0) {
						DecodeVerts();
						vai->numVerts = indexGen.VertexCount();
						vai->prim = indexGen.Prim();
						vai->maxIndex = indexGen.MaxIndex();
						vai->flags = gstate_c.vertexFullAlpha ? VAI_FLAG_VERTEXFULLALPHA : 0;
						useElements = !indexGen.SeenOnlyPurePrims();
						if (!useElements && indexGen.PureCount()) {
							vai->numVerts = indexGen.PureCount();
						}
						void * pVb;
						u32 size = dec_->GetDecVtxFmt().stride * indexGen.MaxIndex();
						pD3Ddevice->CreateVertexBuffer(size, D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT, &vai->vbo, NULL);
						vai->vbo->Lock(0, size, &pVb, 0);
						memcpy(pVb, decoded, size);
						vai->vbo->Unlock();
						if (useElements) {
							void * pIb;
							u32 size = sizeof(short) * indexGen.VertexCount();
							pD3Ddevice->CreateIndexBuffer(size, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &vai->ebo, NULL);
							vai->ebo->Lock(0, size, &pIb, 0);
							memcpy(pIb, decIndex, size);
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
					DecodeVerts();
					goto rotateVBO;
				}
			}

			vai->lastFrame = gpuStats.numFlips;
		} else {
			DecodeVerts();
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

		VERBOSE_LOG(G3D, "Flush prim %i! %i verts in one go", prim, vertexCount);
		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		IDirect3DVertexDeclaration9 *pHardwareVertexDecl = SetupDecFmtForDraw(vshader, dec_->GetDecVtxFmt(), dec_->VertexType());

		if (pHardwareVertexDecl) {
			pD3Ddevice->SetVertexDeclaration(pHardwareVertexDecl);
			if (vb_ == NULL) {
				if (useElements) {
					pD3Ddevice->DrawIndexedPrimitiveUP(glprim[prim], 0, maxIndex + 1, D3DPrimCount(glprim[prim], vertexCount), decIndex, D3DFMT_INDEX16, decoded, dec_->GetDecVtxFmt().stride);
				} else {
					pD3Ddevice->DrawPrimitiveUP(glprim[prim], D3DPrimCount(glprim[prim], vertexCount), decoded, dec_->GetDecVtxFmt().stride);
				}
			} else {
				pD3Ddevice->SetStreamSource(0, vb_, 0, dec_->GetDecVtxFmt().stride);

				if (useElements) {
					pD3Ddevice->SetIndices(ib_);

					pD3Ddevice->DrawIndexedPrimitive(glprim[prim], 0, 0, maxIndex + 1, 0, D3DPrimCount(glprim[prim], vertexCount));
				} else {
					pD3Ddevice->DrawPrimitive(glprim[prim], 0, D3DPrimCount(glprim[prim], vertexCount));
				}
			}
		}
	} else {
		DecodeVerts();
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

		int numTrans = 0;
		bool drawIndexed = false;
		u16 *inds = decIndex;
		TransformedVertex *drawBuffer = NULL;
		SoftwareTransformResult result;
		memset(&result, 0, sizeof(result));

		int maxIndex = indexGen.MaxIndex();
		SoftwareTransform(
			prim, decoded, indexGen.VertexCount(),
			dec_->VertexType(), inds, GE_VTYPE_IDX_16BIT, dec_->GetDecVtxFmt(),
			maxIndex, framebufferManager_, textureCache_, transformed, transformedExpanded, drawBuffer, numTrans, drawIndexed, &result, -1.0f);

		if (result.action == SW_DRAW_PRIMITIVES) {
			if (result.setStencil) {
				dxstate.stencilFunc.set(D3DCMP_ALWAYS, result.stencilValue, 255);
			}

			// TODO: Add a post-transform cache here for multi-RECTANGLES only.
			// Might help for text drawing.

			// these spam the gDebugger log.
			const int vertexSize = sizeof(transformed[0]);

			pD3Ddevice->SetVertexDeclaration(pSoftVertexDecl);
			if (drawIndexed) {
				pD3Ddevice->DrawIndexedPrimitiveUP(glprim[prim], 0, maxIndex, D3DPrimCount(glprim[prim], numTrans), inds, D3DFMT_INDEX16, drawBuffer, sizeof(TransformedVertex));
			} else {
				pD3Ddevice->DrawPrimitiveUP(glprim[prim], D3DPrimCount(glprim[prim], numTrans), drawBuffer, sizeof(TransformedVertex));
			}
		} else if (result.action == SW_CLEAR) {
			u32 clearColor = result.color;
			float clearDepth = result.depth;

			int mask = gstate.isClearModeColorMask() ? D3DCLEAR_TARGET : 0;
			if (gstate.isClearModeAlphaMask()) mask |= D3DCLEAR_STENCIL;
			if (gstate.isClearModeDepthMask()) mask |= D3DCLEAR_ZBUFFER;

			if (mask & D3DCLEAR_ZBUFFER) {
				framebufferManager_->SetDepthUpdated();
			}
			if (mask & D3DCLEAR_TARGET) {
				framebufferManager_->SetColorUpdated();
			}

			dxstate.colorMask.set((mask & D3DCLEAR_TARGET) != 0, (mask & D3DCLEAR_TARGET) != 0, (mask & D3DCLEAR_TARGET) != 0, (mask & D3DCLEAR_STENCIL) != 0);
			pD3Ddevice->Clear(0, NULL, mask, clearColor, clearDepth, clearColor >> 24);
		}
	}

	gpuStats.numDrawCalls += numDrawCalls;
	gpuStats.numVertsSubmitted += vertexCountInDrawCalls;

	indexGen.Reset();
	decodedVerts_ = 0;
	numDrawCalls = 0;
	vertexCountInDrawCalls = 0;
	decodeCounter_ = 0;
	dcid_ = 0;
	prevPrim_ = GE_PRIM_INVALID;
	gstate_c.vertexFullAlpha = true;
	framebufferManager_->SetColorUpdated();

	host->GPUNotifyDraw();
}

void TransformDrawEngineDX9::Resized() {
	decJitCache_->Clear();
	lastVType_ = -1;
	dec_ = NULL;
	for (auto iter = decoderMap_.begin(); iter != decoderMap_.end(); iter++) {
		delete iter->second;
	}
	decoderMap_.clear();

	if (g_Config.bPrescaleUV && !uvScale) {
		uvScale = new UVScale[MAX_DEFERRED_DRAW_CALLS];
	} else if (!g_Config.bPrescaleUV && uvScale) {
		delete uvScale;
		uvScale = 0;
	}
}

bool TransformDrawEngineDX9::IsCodePtrVertexDecoder(const u8 *ptr) const {
	return decJitCache_->IsInSpace(ptr);
}

}  // namespace
