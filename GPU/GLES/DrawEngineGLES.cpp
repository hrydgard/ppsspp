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

// Ideas for speeding things up on mobile OpenGL ES implementations
//
// Use superbuffers! Yes I just invented that name.
//
// The idea is to avoid respecifying the vertex format between every draw call (multiple glVertexAttribPointer ...)
// by combining the contents of multiple draw calls into one buffer, as long as
// they have exactly the same output vertex format. (different input formats is fine! This way
// we can combine the data for multiple draws with different numbers of bones, as we consider numbones < 4 to be = 4)
// into one VBO.
//
// This will likely be a win because I believe that between every change of VBO + glVertexAttribPointer*N, the driver will
// perform a lot of validation, probably at draw call time, while all the validation can be skipped if the only thing
// that changes between two draw calls is simple state or texture or a matrix etc, not anything vertex related.
// Also the driver will have to manage hundreds instead of thousands of VBOs in games like GTA.
//
// * Every 10 frames or something, do the following:
//   - Frame 1:
//		 + Mark all drawn buffers with in-frame sequence numbers (alternatively,
//		   just log them in an array)
//	 - Frame 2 (beginning?):
//	   + Take adjacent buffers that have the same output vertex format, and add them
//	     to a list of buffers to combine. Create said buffers with appropriate sizes
//	     and precompute the offsets that the draws should be written into.
//	 - Frame 2 (end):
//	   + Actually do the work of combining the buffers. This probably means re-decoding
//	     the vertices into a new one. Will also have to apply index offsets.
//
// Also need to change the drawing code so that we don't glBindBuffer and respecify glVAP if
// two subsequent drawcalls come from the same superbuffer.
//
// Or we ignore all of this including vertex caching and simply find a way to do highly optimized vertex streaming,
// like Dolphin is trying to. That will likely never be able to reach the same speed as perfectly optimized
// superbuffers though. For this we will have to JIT the vertex decoder but that's not too hard.
//
// Now, when do we delete superbuffers? Maybe when half the buffers within have been killed?
//
// Another idea for GTA which switches textures a lot while not changing much other state is to use ES 3 Array
// textures, if they are the same size (even if they aren't, might be okay to simply resize the textures to match
// if they're just a multiple of 2 away) or something. Then we'd have to add a W texture coordinate to choose the
// texture within the bound texture array to the vertex data when merging into superbuffers.
//
// There are even more things to try. For games that do matrix palette skinning by quickly switching bones and
// just drawing a few triangles per call (NBA, FF:CC, Tekken 6 etc) we could even collect matrices, upload them
// all at once, writing matrix indices into the vertices in addition to the weights, and then doing a single
// draw call with specially generated shader to draw the whole mesh. This code will be seriously complex though.

#include "base/logging.h"
#include "base/timeutil.h"

#include "Common/MemoryUtil.h"
#include "Core/MemMap.h"
#include "Core/Host.h"
#include "Core/System.h"
#include "Core/Reporting.h"
#include "Core/Config.h"
#include "Core/CoreTiming.h"

#include "profiler/profiler.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/GLES/GLStateCache.h"
#include "GPU/GLES/FragmentTestCache.h"
#include "GPU/GLES/StateMapping.h"
#include "GPU/GLES/TextureCache.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/ShaderManager.h"
#include "GPU/GLES/GPU_GLES.h"

extern const GLuint glprim[8] = {
	GL_POINTS,
	GL_LINES,
	GL_LINE_STRIP,
	GL_TRIANGLES,
	GL_TRIANGLE_STRIP,
	GL_TRIANGLE_FAN,
	GL_TRIANGLES,
	// Rectangles need to be expanded into triangles.
};

enum {
	TRANSFORMED_VERTEX_BUFFER_SIZE = VERTEX_BUFFER_MAX * sizeof(TransformedVertex)
};

#define VERTEXCACHE_DECIMATION_INTERVAL 17
#define VERTEXCACHE_NAME_DECIMATION_INTERVAL 41
#define VERTEXCACHE_NAME_DECIMATION_MAX 100
#define VERTEXCACHE_NAME_CACHE_SIZE 64
#define VERTEXCACHE_NAME_CACHE_FULL_BYTES (1024 * 1024)
#define VERTEXCACHE_NAME_CACHE_MAX_AGE 120

enum { VAI_KILL_AGE = 120, VAI_UNRELIABLE_KILL_AGE = 240, VAI_UNRELIABLE_KILL_MAX = 4 };


DrawEngineGLES::DrawEngineGLES()
	: decodedVerts_(0),
		prevPrim_(GE_PRIM_INVALID),
		lastVType_(-1),
		shaderManager_(nullptr),
		textureCache_(nullptr),
		framebufferManager_(nullptr),
		numDrawCalls(0),
		vertexCountInDrawCalls(0),
		decodeCounter_(0),
		dcid_(0),
		uvScale(nullptr),
		fboTexNeedBind_(false),
		fboTexBound_(false) {
	decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;
	bufferDecimationCounter_ = VERTEXCACHE_NAME_DECIMATION_INTERVAL;
	memset(&decOptions_, 0, sizeof(decOptions_));
	decOptions_.expandAllUVtoFloat = false;
	// Allocate nicely aligned memory. Maybe graphics drivers will
	// appreciate it.
	// All this is a LOT of memory, need to see if we can cut down somehow.
	decoded = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decIndex = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	splineBuffer = (u8 *)AllocateMemoryPages(SPLINE_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	transformed = (TransformedVertex *)AllocateMemoryPages(TRANSFORMED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	transformedExpanded = (TransformedVertex *)AllocateMemoryPages(3 * TRANSFORMED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);

	if (g_Config.bPrescaleUV) {
		uvScale = new UVScale[MAX_DEFERRED_DRAW_CALLS];
	}
	indexGen.Setup(decIndex);

	InitDeviceObjects();
	register_gl_resource_holder(this);
}

DrawEngineGLES::~DrawEngineGLES() {
	DestroyDeviceObjects();
	FreeMemoryPages(decoded, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex, DECODED_INDEX_BUFFER_SIZE);
	FreeMemoryPages(splineBuffer, SPLINE_BUFFER_SIZE);
	FreeMemoryPages(transformed, TRANSFORMED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(transformedExpanded, 3 * TRANSFORMED_VERTEX_BUFFER_SIZE);

	unregister_gl_resource_holder(this);
	delete [] uvScale;
}

void DrawEngineGLES::RestoreVAO() {
	if (sharedVao_ != 0) {
		glBindVertexArray(sharedVao_);
	} else if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
		// Note: this is here because, InitDeviceObjects() is called before GPU_SUPPORTS_VAO is setup.
		// So, this establishes it if Supports() returns true and there isn't one yet.
		glGenVertexArrays(1, &sharedVao_);
		glBindVertexArray(sharedVao_);
	}
}

void DrawEngineGLES::InitDeviceObjects() {
	if (bufferNameCache_.empty()) {
		bufferNameCache_.resize(VERTEXCACHE_NAME_CACHE_SIZE);
		glGenBuffers(VERTEXCACHE_NAME_CACHE_SIZE, &bufferNameCache_[0]);
		bufferNameCacheSize_ = 0;

		if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
			glGenVertexArrays(1, &sharedVao_);
		} else {
			sharedVao_ = 0;
		}
	} else {
		ERROR_LOG(G3D, "Device objects already initialized!");
	}
}

void DrawEngineGLES::DestroyDeviceObjects() {
	ClearTrackedVertexArrays();
	if (!bufferNameCache_.empty()) {
		glstate.arrayBuffer.unbind();
		glstate.elementArrayBuffer.unbind();
		glDeleteBuffers((GLsizei)bufferNameCache_.size(), &bufferNameCache_[0]);
		bufferNameCache_.clear();
		bufferNameInfo_.clear();
		freeSizedBuffers_.clear();
		bufferNameCacheSize_ = 0;

		if (sharedVao_ != 0) {
			glDeleteVertexArrays(1, &sharedVao_);
		}
	}
}

void DrawEngineGLES::GLLost() {
	ILOG("TransformDrawEngine::GLLost()");
	// The objects have already been deleted.
	bufferNameCache_.clear();
	bufferNameInfo_.clear();
	freeSizedBuffers_.clear();
	bufferNameCacheSize_ = 0;
	ClearTrackedVertexArrays();
}

void DrawEngineGLES::GLRestore() {
	ILOG("TransformDrawEngine::GLRestore()");
	InitDeviceObjects();
}

struct GlTypeInfo {
	u16 type;
	u8 count;
	u8 normalized;
};

static const GlTypeInfo GLComp[] = {
	{0}, // 	DEC_NONE,
	{GL_FLOAT, 1, GL_FALSE}, // 	DEC_FLOAT_1,
	{GL_FLOAT, 2, GL_FALSE}, // 	DEC_FLOAT_2,
	{GL_FLOAT, 3, GL_FALSE}, // 	DEC_FLOAT_3,
	{GL_FLOAT, 4, GL_FALSE}, // 	DEC_FLOAT_4,
	{GL_BYTE, 4, GL_TRUE}, // 	DEC_S8_3,
	{GL_SHORT, 4, GL_TRUE},// 	DEC_S16_3,
	{GL_UNSIGNED_BYTE, 1, GL_TRUE},// 	DEC_U8_1,
	{GL_UNSIGNED_BYTE, 2, GL_TRUE},// 	DEC_U8_2,
	{GL_UNSIGNED_BYTE, 3, GL_TRUE},// 	DEC_U8_3,
	{GL_UNSIGNED_BYTE, 4, GL_TRUE},// 	DEC_U8_4,
	{GL_UNSIGNED_SHORT, 1, GL_TRUE},// 	DEC_U16_1,
	{GL_UNSIGNED_SHORT, 2, GL_TRUE},// 	DEC_U16_2,
	{GL_UNSIGNED_SHORT, 3, GL_TRUE},// 	DEC_U16_3,
	{GL_UNSIGNED_SHORT, 4, GL_TRUE},// 	DEC_U16_4,
	{GL_UNSIGNED_BYTE,  2, GL_FALSE},// 	DEC_U8A_2,
	{GL_UNSIGNED_SHORT, 2, GL_FALSE},// 	DEC_U16A_2,
};

static inline void VertexAttribSetup(int attrib, int fmt, int stride, u8 *ptr) {
	if (fmt) {
		const GlTypeInfo &type = GLComp[fmt];
		glVertexAttribPointer(attrib, type.count, type.type, type.normalized, stride, ptr);
	}
}

// TODO: Use VBO and get rid of the vertexData pointers - with that, we will supply only offsets
static void SetupDecFmtForDraw(LinkedShader *program, const DecVtxFormat &decFmt, u8 *vertexData) {
	VertexAttribSetup(ATTR_W1, decFmt.w0fmt, decFmt.stride, vertexData + decFmt.w0off);
	VertexAttribSetup(ATTR_W2, decFmt.w1fmt, decFmt.stride, vertexData + decFmt.w1off);
	VertexAttribSetup(ATTR_TEXCOORD, decFmt.uvfmt, decFmt.stride, vertexData + decFmt.uvoff);
	VertexAttribSetup(ATTR_COLOR0, decFmt.c0fmt, decFmt.stride, vertexData + decFmt.c0off);
	VertexAttribSetup(ATTR_COLOR1, decFmt.c1fmt, decFmt.stride, vertexData + decFmt.c1off);
	VertexAttribSetup(ATTR_NORMAL, decFmt.nrmfmt, decFmt.stride, vertexData + decFmt.nrmoff);
	VertexAttribSetup(ATTR_POSITION, decFmt.posfmt, decFmt.stride, vertexData + decFmt.posoff);
}

void DrawEngineGLES::SetupVertexDecoder(u32 vertType) {
	SetupVertexDecoderInternal(vertType);
}

inline void DrawEngineGLES::SetupVertexDecoderInternal(u32 vertType) {
	// As the decoder depends on the UVGenMode when we use UV prescale, we simply mash it
	// into the top of the verttype where there are unused bits.
	const u32 vertTypeID = (vertType & 0xFFFFFF) | (gstate.getUVGenMode() << 24);

	// If vtype has changed, setup the vertex decoder.
	if (vertTypeID != lastVType_) {
		dec_ = GetVertexDecoder(vertTypeID);
		lastVType_ = vertTypeID;
	}
}

void DrawEngineGLES::SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) {
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

void DrawEngineGLES::DecodeVerts() {
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

void DrawEngineGLES::DecodeVertsStep() {
	PROFILE_THIS_SCOPE("vertdec");

	const int i = decodeCounter_;

	const DeferredDrawCall &dc = drawCalls[i];

	indexGen.SetIndex(decodedVerts_);
	int indexLowerBound = dc.indexLowerBound, indexUpperBound = dc.indexUpperBound;

	u32 indexType = dc.indexType;
	if (indexType == (GE_VTYPE_IDX_NONE >> GE_VTYPE_IDX_SHIFT)) {
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
				indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u16_le *)drawCalls[j].inds, indexLowerBound);
			}
			break;
		case GE_VTYPE_IDX_32BIT >> GE_VTYPE_IDX_SHIFT:
			for (int j = i; j <= lastMatch; j++) {
				indexGen.TranslatePrim(drawCalls[j].prim, drawCalls[j].vertexCount, (const u32_le *)drawCalls[j].inds, indexLowerBound);
			}
			break;
		}

		const int vertexCount = indexUpperBound - indexLowerBound + 1;

		// This check is a workaround for Pangya Fantasy Golf, which sends bogus index data when switching items in "My Room" sometimes.
		if (decodedVerts_ + vertexCount > VERTEX_BUFFER_MAX) {
			return;
		}

		// 3. Decode that range of vertex data.
		int stride = (int)dec_->GetDecVtxFmt().stride;
		dec_->DecodeVerts(decoded + decodedVerts_ * stride, dc.verts, indexLowerBound, indexUpperBound);
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

u32 DrawEngineGLES::ComputeMiniHash() {
	u32 fullhash = 0;
	const int vertexSize = dec_->GetDecVtxFmt().stride;
	const int indexSize = IndexSize(dec_->VertexType());

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

void DrawEngineGLES::MarkUnreliable(VertexArrayInfo *vai) {
	vai->status = VertexArrayInfo::VAI_UNRELIABLE;
	if (vai->vbo) {
		FreeBuffer(vai->vbo);
		vai->vbo = 0;
	}
	if (vai->ebo) {
		FreeBuffer(vai->ebo);
		vai->ebo = 0;
	}
}

ReliableHashType DrawEngineGLES::ComputeHash() {
	ReliableHashType fullhash = 0;
	const int vertexSize = dec_->GetDecVtxFmt().stride;
	const int indexSize = IndexSize(dec_->VertexType());

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

void DrawEngineGLES::ClearTrackedVertexArrays() {
	for (auto vai = vai_.begin(); vai != vai_.end(); vai++) {
		FreeVertexArray(vai->second);
		delete vai->second;
	}
	vai_.clear();
}

void DrawEngineGLES::DecimateTrackedVertexArrays() {
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
		if (iter->second->status == VertexArrayInfo::VAI_UNRELIABLE) {
			// We limit killing unreliable so we don't rehash too often.
			kill = iter->second->lastFrame < unreliableThreshold && --unreliableLeft >= 0;
		} else {
			kill = iter->second->lastFrame < threshold;
		}
		if (kill) {
			FreeVertexArray(iter->second);
			delete iter->second;
			vai_.erase(iter++);
		} else {
			++iter;
		}
	}
}

GLuint DrawEngineGLES::AllocateBuffer(size_t sz) {
	GLuint unused = 0;

	auto freeMatch = freeSizedBuffers_.find(sz);
	if (freeMatch != freeSizedBuffers_.end()) {
		unused = freeMatch->second;
		_assert_(!bufferNameInfo_[unused].used);

		freeSizedBuffers_.erase(freeMatch);
	} else {
		for (GLuint buf : bufferNameCache_) {
			const BufferNameInfo &info = bufferNameInfo_[buf];
			if (info.used) {
				continue;
			}

			// Just pick the first unused one, we'll have to resize it.
			unused = buf;

			// Let's also remove from the free list, if it's there.
			if (info.sz != 0) {
				auto range = freeSizedBuffers_.equal_range(info.sz);
				for (auto it = range.first; it != range.second; ++it) {
					if (it->second == buf) {
						// It will only be once, so remove and bail.
						freeSizedBuffers_.erase(it);
						break;
					}
				}
			}
			break;
		}
	}

	if (unused == 0) {
		size_t oldSize = bufferNameCache_.size();
		bufferNameCache_.resize(oldSize + VERTEXCACHE_NAME_CACHE_SIZE);
		glGenBuffers(VERTEXCACHE_NAME_CACHE_SIZE, &bufferNameCache_[oldSize]);

		unused = bufferNameCache_[oldSize];
	}

	BufferNameInfo &info = bufferNameInfo_[unused];

	// Record the change in size.
	bufferNameCacheSize_ += sz - info.sz;
	info.sz = sz;
	info.used = true;
	return unused;
}

void DrawEngineGLES::FreeBuffer(GLuint buf) {
	// We can reuse buffers by setting new data on them, so let's actually keep it.
	auto it = bufferNameInfo_.find(buf);
	if (it != bufferNameInfo_.end()) {
		it->second.used = false;
		it->second.lastFrame = gpuStats.numFlips;

		if (it->second.sz != 0) {
			freeSizedBuffers_.insert(std::make_pair(it->second.sz, buf));
		}
	} else {
		ERROR_LOG(G3D, "Unexpected buffer freed (%d) but not tracked", buf);
	}
}

void DrawEngineGLES::FreeVertexArray(VertexArrayInfo *vai) {
	if (vai->vbo) {
		FreeBuffer(vai->vbo);
		vai->vbo = 0;
	}
	if (vai->ebo) {
		FreeBuffer(vai->ebo);
		vai->ebo = 0;
	}
}

void DrawEngineGLES::DoFlush() {
	PROFILE_THIS_SCOPE("flush");
	gpuStats.numFlushes++;
	gpuStats.numTrackedVertexArrays = (int)vai_.size();

	// This is not done on every drawcall, we should collect vertex data
	// until critical state changes. That's when we draw (flush).

	GEPrimitiveType prim = prevPrim_;
	ApplyDrawState(prim);

	ShaderID vsid;
	Shader *vshader = shaderManager_->ApplyVertexShader(prim, lastVType_, &vsid);

	if (vshader->UseHWTransform()) {
		GLuint vbo = 0, ebo = 0;
		int vertexCount = 0;
		bool useElements = true;

		// Cannot cache vertex data with morph enabled.
		bool useCache = g_Config.bVertexCache && !(lastVType_ & GE_VTYPE_MORPHCOUNT_MASK);
		// Also avoid caching when software skinning.
		if (g_Config.bSoftwareSkinning && (lastVType_ & GE_VTYPE_WEIGHT_MASK))
			useCache = false;

		if (useCache) {
			u32 id = dcid_;
			auto iter = vai_.find(id);
			VertexArrayInfo *vai;
			if (iter != vai_.end()) {
				// We've seen this before. Could have been a cached draw.
				vai = iter->second;
			} else {
				vai = new VertexArrayInfo();
				vai_[id] = vai;
			}

			switch (vai->status) {
			case VertexArrayInfo::VAI_NEW:
				{
					// Haven't seen this one before.
					ReliableHashType dataHash = ComputeHash();
					vai->hash = dataHash;
					vai->minihash = ComputeMiniHash();
					vai->status = VertexArrayInfo::VAI_HASHING;
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
			case VertexArrayInfo::VAI_HASHING:
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
							// exponential backoff up to 16 draws, then every 32
							vai->drawsUntilNextFullHash = std::min(32, vai->numFrames);
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

						_dbg_assert_msg_(G3D, gstate_c.vertBounds.minV >= gstate_c.vertBounds.maxV, "Should not have checked UVs when caching.");

						size_t vsz = dec_->GetDecVtxFmt().stride * indexGen.MaxIndex();
						vai->vbo = AllocateBuffer(vsz);
						glstate.arrayBuffer.bind(vai->vbo);
						glBufferData(GL_ARRAY_BUFFER, vsz, decoded, GL_STATIC_DRAW);
						// If there's only been one primitive type, and it's either TRIANGLES, LINES or POINTS,
						// there is no need for the index buffer we built. We can then use glDrawArrays instead
						// for a very minor speed boost.
						if (useElements) {
							size_t esz = sizeof(short) * indexGen.VertexCount();
							vai->ebo = AllocateBuffer(esz);
							glstate.elementArrayBuffer.bind(vai->ebo);
							glBufferData(GL_ELEMENT_ARRAY_BUFFER, esz, (GLvoid *)decIndex, GL_STATIC_DRAW);
						} else {
							vai->ebo = 0;
							glstate.elementArrayBuffer.bind(vai->ebo);
						}
					} else {
						gpuStats.numCachedDrawCalls++;
						glstate.arrayBuffer.bind(vai->vbo);
						glstate.elementArrayBuffer.bind(vai->ebo);
						useElements = vai->ebo ? true : false;
						gpuStats.numCachedVertsDrawn += vai->numVerts;
						gstate_c.vertexFullAlpha = vai->flags & VAI_FLAG_VERTEXFULLALPHA;
					}
					vbo = vai->vbo;
					ebo = vai->ebo;
					vertexCount = vai->numVerts;
					prim = static_cast<GEPrimitiveType>(vai->prim);
					break;
				}

				// Reliable - we don't even bother hashing anymore. Right now we don't go here until after a very long time.
			case VertexArrayInfo::VAI_RELIABLE:
				{
					vai->numDraws++;
					if (vai->lastFrame != gpuStats.numFlips) {
						vai->numFrames++;
					}
					gpuStats.numCachedDrawCalls++;
					gpuStats.numCachedVertsDrawn += vai->numVerts;
					vbo = vai->vbo;
					ebo = vai->ebo;
					glstate.arrayBuffer.bind(vbo);
					glstate.elementArrayBuffer.bind(ebo);
					vertexCount = vai->numVerts;
					prim = static_cast<GEPrimitiveType>(vai->prim);

					gstate_c.vertexFullAlpha = vai->flags & VAI_FLAG_VERTEXFULLALPHA;
					break;
				}

			case VertexArrayInfo::VAI_UNRELIABLE:
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
			if (!useElements && indexGen.PureCount()) {
				vertexCount = indexGen.PureCount();
			}
			glstate.arrayBuffer.unbind();
			glstate.elementArrayBuffer.unbind();

			prim = indexGen.Prim();
		}

		VERBOSE_LOG(G3D, "Flush prim %i! %i verts in one go", prim, vertexCount);
		bool hasColor = (lastVType_ & GE_VTYPE_COL_MASK) != GE_VTYPE_COL_NONE;
		if (gstate.isModeThrough()) {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && (hasColor || gstate.getMaterialAmbientA() == 255);
		} else {
			gstate_c.vertexFullAlpha = gstate_c.vertexFullAlpha && ((hasColor && (gstate.materialupdate & 1)) || gstate.getMaterialAmbientA() == 255) && (!gstate.isLightingEnabled() || gstate.getAmbientA() == 255);
		}

		ApplyDrawStateLate();

		if (gstate_c.Supports(GPU_SUPPORTS_VAO) && vbo == 0) {
			vbo = BindBuffer(decoded, dec_->GetDecVtxFmt().stride * indexGen.MaxIndex());
			if (useElements) {
				ebo = BindElementBuffer(decIndex, sizeof(short) * indexGen.VertexCount());
			}
		}

		LinkedShader *program = shaderManager_->ApplyFragmentShader(vsid, vshader, lastVType_, prim);
		SetupDecFmtForDraw(program, dec_->GetDecVtxFmt(), vbo ? 0 : decoded);

		if (useElements) {
			glDrawElements(glprim[prim], vertexCount, GL_UNSIGNED_SHORT, ebo ? 0 : (GLvoid*)decIndex);
		} else {
			glDrawArrays(glprim[prim], 0, vertexCount);
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

		TransformedVertex *drawBuffer = NULL;
		int numTrans;
		bool drawIndexed = false;
		u16 *inds = decIndex;
		SoftwareTransformResult result;
		memset(&result, 0, sizeof(result));

		// TODO: Keep this static?  Faster than repopulating?
		SoftwareTransformParams params;
		memset(&params, 0, sizeof(params));
		params.decoded = decoded;
		params.transformed = transformed;
		params.transformedExpanded = transformedExpanded;
		params.fbman = framebufferManager_;
		params.texCache = textureCache_;
		params.allowSeparateAlphaClear = true;

		int maxIndex = indexGen.MaxIndex();
		SoftwareTransform(
			prim, indexGen.VertexCount(),
			dec_->VertexType(), inds, GE_VTYPE_IDX_16BIT, dec_->GetDecVtxFmt(),
			maxIndex, drawBuffer, numTrans, drawIndexed, &params, &result);
		ApplyDrawStateLate();

		LinkedShader *program = shaderManager_->ApplyFragmentShader(vsid, vshader, lastVType_, prim);

		if (result.action == SW_DRAW_PRIMITIVES) {
			if (result.setStencil) {
				glstate.stencilFunc.set(GL_ALWAYS, result.stencilValue, 255);
			}
			const int vertexSize = sizeof(transformed[0]);

			bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;

			const uint8_t *bufferStart = (const uint8_t *)drawBuffer;
			if (gstate_c.Supports(GPU_SUPPORTS_VAO)) {
				bufferStart = 0;
				BindBuffer(drawBuffer, vertexSize * maxIndex);
				if (drawIndexed) {
					BindElementBuffer(inds, sizeof(short) * numTrans);
					inds = 0;
				}
			} else {
				glstate.arrayBuffer.unbind();
				glstate.elementArrayBuffer.unbind();
			}

			glVertexAttribPointer(ATTR_POSITION, 4, GL_FLOAT, GL_FALSE, vertexSize, bufferStart);
			int attrMask = program->attrMask;
			if (attrMask & (1 << ATTR_TEXCOORD)) glVertexAttribPointer(ATTR_TEXCOORD, doTextureProjection ? 3 : 2, GL_FLOAT, GL_FALSE, vertexSize, bufferStart + offsetof(TransformedVertex, u));
			if (attrMask & (1 << ATTR_COLOR0)) glVertexAttribPointer(ATTR_COLOR0, 4, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, bufferStart + offsetof(TransformedVertex, color0));
			if (attrMask & (1 << ATTR_COLOR1)) glVertexAttribPointer(ATTR_COLOR1, 3, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, bufferStart + offsetof(TransformedVertex, color1));
			if (drawIndexed) {
				glDrawElements(glprim[prim], numTrans, GL_UNSIGNED_SHORT, inds);
			} else {
				glDrawArrays(glprim[prim], 0, numTrans);
			}
		} else if (result.action == SW_CLEAR) {
			u32 clearColor = result.color;
			float clearDepth = result.depth;
			const float col[4] = {
				((clearColor & 0xFF)) / 255.0f,
				((clearColor & 0xFF00) >> 8) / 255.0f,
				((clearColor & 0xFF0000) >> 16) / 255.0f,
				((clearColor & 0xFF000000) >> 24) / 255.0f,
			};

			bool colorMask = gstate.isClearModeColorMask();
			bool alphaMask = gstate.isClearModeAlphaMask();
			bool depthMask = gstate.isClearModeDepthMask();
			if (depthMask) {
				framebufferManager_->SetDepthUpdated();
			}

			// Note that scissor may still apply while clearing.  Turn off other tests for the clear.
			glstate.stencilTest.disable();
			glstate.stencilMask.set(0xFF);
			glstate.depthTest.disable();

			GLbitfield target = 0;
			if (colorMask || alphaMask) target |= GL_COLOR_BUFFER_BIT;
			if (alphaMask) target |= GL_STENCIL_BUFFER_BIT;
			if (depthMask) target |= GL_DEPTH_BUFFER_BIT;

			glstate.colorMask.set(colorMask, colorMask, colorMask, alphaMask);
			glClearColor(col[0], col[1], col[2], col[3]);
#ifdef USING_GLES2
			glClearDepthf(clearDepth);
#else
			glClearDepth(clearDepth);
#endif
			// Stencil takes alpha.
			glClearStencil(clearColor >> 24);
			glClear(target);
			framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

			int scissorX1 = gstate.getScissorX1();
			int scissorY1 = gstate.getScissorY1();
			int scissorX2 = gstate.getScissorX2() + 1;
			int scissorY2 = gstate.getScissorY2() + 1;
			framebufferManager_->SetSafeSize(scissorX2, scissorY2);

			if (g_Config.bBlockTransferGPU && colorMask && (alphaMask || gstate.FrameBufFormat() == GE_FORMAT_565)) {
				ApplyClearToMemory(scissorX1, scissorY1, scissorX2, scissorY2, clearColor);
			}
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
	framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

	// Now seems as good a time as any to reset the min/max coords, which we may examine later.
	gstate_c.vertBounds.minU = 512;
	gstate_c.vertBounds.minV = 512;
	gstate_c.vertBounds.maxU = 0;
	gstate_c.vertBounds.maxV = 0;

#ifndef MOBILE_DEVICE
	host->GPUNotifyDraw();
#endif
}

void DrawEngineGLES::Resized() {
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

GLuint DrawEngineGLES::BindBuffer(const void *p, size_t sz) {
	// Get a new buffer each time we need one.
	GLuint buf = AllocateBuffer(sz);
	glstate.arrayBuffer.bind(buf);

	// These aren't used more than once per frame, so let's use GL_STREAM_DRAW.
	glBufferData(GL_ARRAY_BUFFER, sz, p, GL_STREAM_DRAW);
	buffersThisFrame_.push_back(buf);

	return buf;
}

GLuint DrawEngineGLES::BindBuffer(const void *p1, size_t sz1, const void *p2, size_t sz2) {
	GLuint buf = AllocateBuffer(sz1 + sz2);
	glstate.arrayBuffer.bind(buf);

	glBufferData(GL_ARRAY_BUFFER, sz1 + sz2, nullptr, GL_STREAM_DRAW);
	glBufferSubData(GL_ARRAY_BUFFER, 0, sz1, p1);
	glBufferSubData(GL_ARRAY_BUFFER, sz1, sz2, p2);
	buffersThisFrame_.push_back(buf);

	return buf;
}

GLuint DrawEngineGLES::BindElementBuffer(const void *p, size_t sz) {
	GLuint buf = AllocateBuffer(sz);
	glstate.elementArrayBuffer.bind(buf);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sz, p, GL_STREAM_DRAW);
	buffersThisFrame_.push_back(buf);

	return buf;
}

void DrawEngineGLES::DecimateBuffers() {
	for (GLuint buf : buffersThisFrame_) {
		FreeBuffer(buf);
	}
	buffersThisFrame_.clear();

	if (--bufferDecimationCounter_ <= 0) {
		bufferDecimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;
	} else {
		return;
	}

	// Let's not keep too many around, will eat up memory.
	// First check if there's any to free, and only check if it seems somewhat full.
	bool hasOld = false;
	if (bufferNameCacheSize_ > VERTEXCACHE_NAME_CACHE_FULL_BYTES) {
		for (GLuint buf : bufferNameCache_) {
			const BufferNameInfo &info = bufferNameInfo_[buf];
			const int age = gpuStats.numFlips - info.lastFrame;
			if (!info.used && age > VERTEXCACHE_NAME_CACHE_MAX_AGE) {
				hasOld = true;
				break;
			}
		}
	}

	if (hasOld) {
		// Okay, it is.  Let's rebuild the array.
		std::vector<GLuint> toFree;
		std::vector<GLuint> toKeep;

		toKeep.reserve(bufferNameCache_.size());

		for (size_t i = 0, n = bufferNameCache_.size(); i < n; ++i)  {
			const GLuint buf = bufferNameCache_[i];
			const BufferNameInfo &info = bufferNameInfo_[buf];
			const int age = gpuStats.numFlips - info.lastFrame;
			if (!info.used && age > VERTEXCACHE_NAME_CACHE_MAX_AGE) {
				toFree.push_back(buf);
				bufferNameCacheSize_ -= bufferNameInfo_[buf].sz;
				bufferNameInfo_.erase(buf);

				// If we've removed all we want to this round, keep the rest and abort.
				if (toFree.size() >= VERTEXCACHE_NAME_DECIMATION_MAX && i + 1 < bufferNameCache_.size()) {
					toKeep.insert(toKeep.end(), bufferNameCache_.begin() + i + 1, bufferNameCache_.end());
					break;
				}
			} else {
				toKeep.push_back(buf);
			}
		}

		if (!toFree.empty()) {
			bufferNameCache_ = toKeep;
			// TODO: Rebuild?
			freeSizedBuffers_.clear();

			glstate.arrayBuffer.unbind();
			glstate.elementArrayBuffer.unbind();
			glDeleteBuffers((GLsizei)toFree.size(), &toFree[0]);
		}
	}
}

bool DrawEngineGLES::IsCodePtrVertexDecoder(const u8 *ptr) const {
	return decJitCache_->IsInSpace(ptr);
}
