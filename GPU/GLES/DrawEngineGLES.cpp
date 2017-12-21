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

#include "gfx/gl_debug_log.h"
#include "profiler/profiler.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"

#include "GPU/Common/TextureDecoder.h"
#include "GPU/Common/SplineCommon.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/SoftwareTransformCommon.h"
#include "GPU/GLES/FragmentTestCacheGLES.h"
#include "GPU/GLES/StateMappingGLES.h"
#include "GPU/GLES/TextureCacheGLES.h"
#include "GPU/GLES/DrawEngineGLES.h"
#include "GPU/GLES/ShaderManagerGLES.h"
#include "GPU/GLES/GPU_GLES.h"

const GLuint glprim[8] = {
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

DrawEngineGLES::DrawEngineGLES(Draw::DrawContext *draw) : vai_(256), draw_(draw), inputLayoutMap_(16) {
	render_ = (GLRenderManager *)draw_->GetNativeObject(Draw::NativeObject::RENDER_MANAGER);

	decOptions_.expandAllWeightsToFloat = false;
	decOptions_.expand8BitNormalsToFloat = false;

	decimationCounter_ = VERTEXCACHE_DECIMATION_INTERVAL;
	bufferDecimationCounter_ = VERTEXCACHE_NAME_DECIMATION_INTERVAL;
	// Allocate nicely aligned memory. Maybe graphics drivers will
	// appreciate it.
	// All this is a LOT of memory, need to see if we can cut down somehow.
	decoded = (u8 *)AllocateMemoryPages(DECODED_VERTEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	decIndex = (u16 *)AllocateMemoryPages(DECODED_INDEX_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);
	splineBuffer = (u8 *)AllocateMemoryPages(SPLINE_BUFFER_SIZE, MEM_PROT_READ | MEM_PROT_WRITE);

	indexGen.Setup(decIndex);

	InitDeviceObjects();

	tessDataTransfer = new TessellationDataTransferGLES(gl_extensions.VersionGEThan(3, 0, 0));
}

DrawEngineGLES::~DrawEngineGLES() {
	DestroyDeviceObjects();
	FreeMemoryPages(decoded, DECODED_VERTEX_BUFFER_SIZE);
	FreeMemoryPages(decIndex, DECODED_INDEX_BUFFER_SIZE);
	FreeMemoryPages(splineBuffer, SPLINE_BUFFER_SIZE);

	delete tessDataTransfer;
}

void DrawEngineGLES::DeviceLost() {
	DestroyDeviceObjects();
}

void DrawEngineGLES::DeviceRestore() {
	InitDeviceObjects();
}

void DrawEngineGLES::InitDeviceObjects() {
	for (int i = 0; i < GLRenderManager::MAX_INFLIGHT_FRAMES; i++) {
		frameData_[i].pushVertex = new GLPushBuffer(render_, GL_ARRAY_BUFFER, 1024 * 1024);
		frameData_[i].pushIndex = new GLPushBuffer(render_, GL_ELEMENT_ARRAY_BUFFER, 256 * 1024);
	}

	int vertexSize = sizeof(TransformedVertex);
	std::vector<GLRInputLayout::Entry> entries;
	entries.push_back({ ATTR_POSITION, 4, GL_FLOAT, GL_FALSE, vertexSize, 0 });
	entries.push_back({ ATTR_TEXCOORD, 3, GL_FLOAT, GL_FALSE, vertexSize, offsetof(TransformedVertex, u) });
	entries.push_back({ ATTR_COLOR0, 4, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, offsetof(TransformedVertex, color0) });
	entries.push_back({ ATTR_COLOR1, 3, GL_UNSIGNED_BYTE, GL_TRUE, vertexSize, offsetof(TransformedVertex, color1) });
	softwareInputLayout_ = render_->CreateInputLayout(entries);
}

void DrawEngineGLES::DestroyDeviceObjects() {
	for (int i = 0; i < GLRenderManager::MAX_INFLIGHT_FRAMES; i++) {
		frameData_[i].pushVertex->Destroy();
		frameData_[i].pushIndex->Destroy();
		delete frameData_[i].pushVertex;
		delete frameData_[i].pushIndex;
	}

	ClearTrackedVertexArrays();

	render_->DeleteInputLayout(softwareInputLayout_);
}

void DrawEngineGLES::ClearInputLayoutMap() {
	inputLayoutMap_.Iterate([&](const uint32_t &key, GLRInputLayout *il) {
		render_->DeleteInputLayout(il);
	});
	inputLayoutMap_.Clear();
}

void DrawEngineGLES::BeginFrame() {
	FrameData &frameData = frameData_[render_->GetCurFrame()];
	frameData.pushIndex->Begin();
	frameData.pushVertex->Begin();
}

void DrawEngineGLES::EndFrame() {
	FrameData &frameData = frameData_[render_->GetCurFrame()];
	frameData.pushIndex->End();
	frameData.pushVertex->End();
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
};

static inline void VertexAttribSetup(int attrib, int fmt, int stride, int offset, std::vector<GLRInputLayout::Entry> &entries) {
	if (fmt) {
		const GlTypeInfo &type = GLComp[fmt];
		GLRInputLayout::Entry entry;
		entry.offset = offset;
		entry.location = attrib;
		entry.normalized = type.normalized;
		entry.type = type.type;
		entry.stride = stride;
		entry.count = type.count;
		entries.push_back(entry);
	}
}

// TODO: Use VBO and get rid of the vertexData pointers - with that, we will supply only offsets
GLRInputLayout *DrawEngineGLES::SetupDecFmtForDraw(LinkedShader *program, const DecVtxFormat &decFmt) {
	uint32_t key = decFmt.id;
	GLRInputLayout *inputLayout = inputLayoutMap_.Get(key);
	if (inputLayout) {
		return inputLayout;
	}

	std::vector<GLRInputLayout::Entry> entries;
	VertexAttribSetup(ATTR_W1, decFmt.w0fmt, decFmt.stride, decFmt.w0off, entries);
	VertexAttribSetup(ATTR_W2, decFmt.w1fmt, decFmt.stride, decFmt.w1off, entries);
	VertexAttribSetup(ATTR_TEXCOORD, decFmt.uvfmt, decFmt.stride, decFmt.uvoff, entries);
	VertexAttribSetup(ATTR_COLOR0, decFmt.c0fmt, decFmt.stride, decFmt.c0off, entries);
	VertexAttribSetup(ATTR_COLOR1, decFmt.c1fmt, decFmt.stride, decFmt.c1off, entries);
	VertexAttribSetup(ATTR_NORMAL, decFmt.nrmfmt, decFmt.stride, decFmt.nrmoff, entries);
	VertexAttribSetup(ATTR_POSITION, decFmt.posfmt, decFmt.stride, decFmt.posoff, entries);

	inputLayout = render_->CreateInputLayout(entries);
	inputLayoutMap_.Insert(key, inputLayout);
	return inputLayout;
}

void DrawEngineGLES::SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) {
	if (!indexGen.PrimCompatible(prevPrim_, prim) || numDrawCalls >= MAX_DEFERRED_DRAW_CALLS || vertexCountInDrawCalls_ + vertexCount > VERTEX_BUFFER_MAX)
		Flush();

	// TODO: Is this the right thing to do?
	if (prim == GE_PRIM_KEEP_PREVIOUS) {
		prim = prevPrim_ != GE_PRIM_INVALID ? prevPrim_ : GE_PRIM_POINTS;
	} else {
		prevPrim_ = prim;
	}

	SetupVertexDecoder(vertType);

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

	uvScale[numDrawCalls] = gstate_c.uv;

	numDrawCalls++;
	vertexCountInDrawCalls_ += vertexCount;

	if (g_Config.bSoftwareSkinning && (vertType & GE_VTYPE_WEIGHT_MASK)) {
		DecodeVertsStep(decoded, decodeCounter_, decodedVerts_);
		decodeCounter_++;
	}

	if (prim == GE_PRIM_RECTANGLES && (gstate.getTextureAddress(0) & 0x3FFFFFFF) == (gstate.getFrameBufAddress() & 0x3FFFFFFF)) {
		// Rendertarget == texture?
		if (!g_Config.bDisableSlowFramebufEffects) {
			gstate_c.Dirty(DIRTY_TEXTURE_PARAMS);
			Flush();
		}
	}
}

void DrawEngineGLES::DecodeVertsToPushBuffer(GLPushBuffer *push, uint32_t *bindOffset, GLRBuffer **buf) {
	u8 *dest = decoded;

	// Figure out how much pushbuffer space we need to allocate.
	if (push) {
		int vertsToDecode = ComputeNumVertsToDecode();
		dest = (u8 *)push->Push(vertsToDecode * dec_->GetDecVtxFmt().stride, bindOffset, buf);
	}
	DecodeVerts(dest);
}

void DrawEngineGLES::MarkUnreliable(VertexArrayInfo *vai) {
	vai->status = VertexArrayInfo::VAI_UNRELIABLE;
	if (vai->vbo) {
		render_->DeleteBuffer(vai->vbo);
		vai->vbo = 0;
	}
	if (vai->ebo) {
		render_->DeleteBuffer(vai->ebo);
		vai->ebo = 0;
	}
}

void DrawEngineGLES::ClearTrackedVertexArrays() {
	vai_.Iterate([&](uint32_t hash, VertexArrayInfo *vai){
		FreeVertexArray(vai);
		delete vai;
	});
	vai_.Clear();
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
	vai_.Iterate([&](uint32_t hash, VertexArrayInfo *vai) {
		bool kill;
		if (vai->status == VertexArrayInfo::VAI_UNRELIABLE) {
			// We limit killing unreliable so we don't rehash too often.
			kill = vai->lastFrame < unreliableThreshold && --unreliableLeft >= 0;
		} else {
			kill = vai->lastFrame < threshold;
		}
		if (kill) {
			FreeVertexArray(vai);
			delete vai;
			vai_.Remove(hash);
		}
	});
	vai_.Maintain();
}

void DrawEngineGLES::FreeVertexArray(VertexArrayInfo *vai) {
	if (vai->vbo) {
		render_->DeleteBuffer(vai->vbo);
		vai->vbo = nullptr;
	}
	if (vai->ebo) {
		render_->DeleteBuffer(vai->ebo);
		vai->ebo = nullptr;
	}
}

void DrawEngineGLES::DoFlush() {
	PROFILE_THIS_SCOPE("flush");

	FrameData &frameData = frameData_[render_->GetCurFrame()];
	
	gpuStats.numFlushes++;
	gpuStats.numTrackedVertexArrays = (int)vai_.size();

	bool textureNeedsApply = false;
	if (gstate_c.IsDirty(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS) && !gstate.isModeClear() && gstate.isTextureMapEnabled()) {
		textureCache_->SetTexture();
		gstate_c.Clean(DIRTY_TEXTURE_IMAGE | DIRTY_TEXTURE_PARAMS);
		textureNeedsApply = true;
	}

	GEPrimitiveType prim = prevPrim_;

	VShaderID vsid;
	Shader *vshader = shaderManager_->ApplyVertexShader(prim, lastVType_, &vsid);

	GLRBuffer *vertexBuffer = nullptr;
	GLRBuffer *indexBuffer = nullptr;
	uint32_t vertexBufferOffset = 0;
	uint32_t indexBufferOffset = 0;

	if (vshader->UseHWTransform()) {
		int vertexCount = 0;
		bool useElements = true;

		// Cannot cache vertex data with morph enabled.
		bool useCache = g_Config.bVertexCache && !(lastVType_ & GE_VTYPE_MORPHCOUNT_MASK);
		// Also avoid caching when software skinning.
		if (g_Config.bSoftwareSkinning && (lastVType_ & GE_VTYPE_WEIGHT_MASK))
			useCache = false;

		// TEMPORARY
		useCache = false;

		if (useCache) {
			u32 id = dcid_ ^ gstate.getUVGenMode();  // This can have an effect on which UV decoder we need to use! And hence what the decoded data will look like. See #9263
			VertexArrayInfo *vai = vai_.Get(id);
			if (!vai) {
				vai = new VertexArrayInfo();
				vai_.Insert(id, vai);
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
					DecodeVerts(decoded); // writes to indexGen
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
							DecodeVerts(decoded);
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
							DecodeVerts(decoded);
							goto rotateVBO;
						}
					}

					if (vai->vbo == 0) {
						DecodeVerts(decoded);
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
						vai->vbo = render_->CreateBuffer(GL_ARRAY_BUFFER, vsz, GL_STATIC_DRAW);
						render_->BufferSubdata(vai->vbo, 0, vsz, decoded);
						// If there's only been one primitive type, and it's either TRIANGLES, LINES or POINTS,
						// there is no need for the index buffer we built. We can then use glDrawArrays instead
						// for a very minor speed boost.
						if (useElements) {
							size_t esz = sizeof(short) * indexGen.VertexCount();
							vai->ebo = render_->CreateBuffer(GL_ARRAY_BUFFER, esz, GL_STATIC_DRAW);
							render_->BufferSubdata(vai->ebo, 0, esz, (uint8_t *)decIndex, false);
						} else {
							vai->ebo = 0;
							render_->BindIndexBuffer(vai->ebo);
						}
					} else {
						gpuStats.numCachedDrawCalls++;
						useElements = vai->ebo ? true : false;
						gpuStats.numCachedVertsDrawn += vai->numVerts;
						gstate_c.vertexFullAlpha = vai->flags & VAI_FLAG_VERTEXFULLALPHA;
					}
					vertexBuffer = vai->vbo;
					indexBuffer = vai->ebo;
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
					vertexBuffer = vai->vbo;
					indexBuffer = vai->ebo;
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
					DecodeVerts(decoded);
					goto rotateVBO;
				}
			}

			vai->lastFrame = gpuStats.numFlips;
		} else {
			if (g_Config.bSoftwareSkinning && (lastVType_ & GE_VTYPE_WEIGHT_MASK)) {
				// If software skinning, we've already predecoded into "decoded". So push that content.
				size_t size = decodedVerts_ * dec_->GetDecVtxFmt().stride;
				u8 *dest = (u8 *)frameData.pushVertex->Push(size, &vertexBufferOffset, &vertexBuffer);
				memcpy(dest, decoded, size);
			} else {
				// Decode directly into the pushbuffer
				DecodeVertsToPushBuffer(frameData.pushVertex, &vertexBufferOffset, &vertexBuffer);
			}

rotateVBO:
			gpuStats.numUncachedVertsDrawn += indexGen.VertexCount();
			useElements = !indexGen.SeenOnlyPurePrims();
			vertexCount = indexGen.VertexCount();
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

		if (textureNeedsApply)
			textureCache_->ApplyTexture();

		// Need to ApplyDrawState after ApplyTexture because depal can launch a render pass and that wrecks the state.
		ApplyDrawState(prim);
		ApplyDrawStateLate(false, 0);
		
		LinkedShader *program = shaderManager_->ApplyFragmentShader(vsid, vshader, lastVType_, prim);
		GLRInputLayout *inputLayout = SetupDecFmtForDraw(program, dec_->GetDecVtxFmt());
		render_->BindVertexBuffer(inputLayout, vertexBuffer, vertexBufferOffset);
		if (useElements) {
			if (!indexBuffer) {
				indexBufferOffset = (uint32_t)frameData.pushIndex->Push(decIndex, sizeof(uint16_t) * indexGen.VertexCount(), &indexBuffer);
				render_->BindIndexBuffer(indexBuffer);
			}
			if (gstate_c.bezier || gstate_c.spline)
				render_->DrawIndexed(glprim[prim], vertexCount, GL_UNSIGNED_SHORT, (GLvoid*)(intptr_t)indexBufferOffset, numPatches);
			else
				render_->DrawIndexed(glprim[prim], vertexCount, GL_UNSIGNED_SHORT, (GLvoid*)(intptr_t)indexBufferOffset);
		} else {
			render_->Draw(glprim[prim], 0, vertexCount);
		}
	} else {
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

		TransformedVertex *drawBuffer = NULL;
		int numTrans;
		bool drawIndexed = false;
		u16 *inds = decIndex;
		SoftwareTransformResult result{};
		// TODO: Keep this static?  Faster than repopulating?
		SoftwareTransformParams params{};
		params.decoded = decoded;
		params.transformed = transformed;
		params.transformedExpanded = transformedExpanded;
		params.fbman = framebufferManager_;
		params.texCache = textureCache_;
		params.allowClear = true;
		params.allowSeparateAlphaClear = true;

		int maxIndex = indexGen.MaxIndex();
		int vertexCount = indexGen.VertexCount();

		// TODO: Split up into multiple draw calls for GLES 2.0 where you can't guarantee support for more than 0x10000 verts.
#if defined(MOBILE_DEVICE)
		if (vertexCount > 0x10000 / 3)
			vertexCount = 0x10000 / 3;
#endif
		SoftwareTransform(
			prim, vertexCount,
			dec_->VertexType(), inds, GE_VTYPE_IDX_16BIT, dec_->GetDecVtxFmt(),
			maxIndex, drawBuffer, numTrans, drawIndexed, &params, &result);

		if (textureNeedsApply)
			textureCache_->ApplyTexture();

		ApplyDrawState(prim);
		ApplyDrawStateLate(result.setStencil, result.stencilValue);

		LinkedShader *program = shaderManager_->ApplyFragmentShader(vsid, vshader, lastVType_, prim);

		if (result.action == SW_DRAW_PRIMITIVES) {
			const int vertexSize = sizeof(transformed[0]);

			bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;

			if (drawIndexed) {
				vertexBufferOffset = (uint32_t)frameData.pushVertex->Push(drawBuffer, maxIndex * sizeof(TransformedVertex), &vertexBuffer);
				indexBufferOffset = (uint32_t)frameData.pushIndex->Push(inds, sizeof(uint16_t) * numTrans, &indexBuffer);
				render_->BindVertexBuffer(softwareInputLayout_, vertexBuffer, vertexBufferOffset);
				render_->BindIndexBuffer(indexBuffer);
				render_->DrawIndexed(glprim[prim], numTrans, GL_UNSIGNED_SHORT, (void *)(intptr_t)indexBufferOffset);
			} else {
				vertexBufferOffset = (uint32_t)frameData.pushVertex->Push(drawBuffer, numTrans * sizeof(TransformedVertex), &vertexBuffer);
				render_->BindVertexBuffer(softwareInputLayout_, vertexBuffer, vertexBufferOffset);
				render_->Draw(glprim[prim], 0, numTrans);
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

			GLbitfield target = 0;
			if (colorMask || alphaMask) target |= GL_COLOR_BUFFER_BIT;
			if (alphaMask) target |= GL_STENCIL_BUFFER_BIT;
			if (depthMask) target |= GL_DEPTH_BUFFER_BIT;

			render_->Clear(clearColor, clearDepth, clearColor >> 24, target);
			framebufferManager_->SetColorUpdated(gstate_c.skipDrawReason);

			int scissorX1 = gstate.getScissorX1();
			int scissorY1 = gstate.getScissorY1();
			int scissorX2 = gstate.getScissorX2() + 1;
			int scissorY2 = gstate.getScissorY2() + 1;
			framebufferManager_->SetSafeSize(scissorX2, scissorY2);

			if (g_Config.bBlockTransferGPU && (gstate_c.featureFlags & GPU_USE_CLEAR_RAM_HACK) && colorMask && (alphaMask || gstate.FrameBufFormat() == GE_FORMAT_565)) {
				framebufferManager_->ApplyClearToMemory(scissorX1, scissorY1, scissorX2, scissorY2, clearColor);
			}
			gstate_c.Dirty(DIRTY_BLEND_STATE);  // Make sure the color mask gets re-applied.
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

#ifndef MOBILE_DEVICE
	host->GPUNotifyDraw();
#endif
	CHECK_GL_ERROR_IF_DEBUG();
}

bool DrawEngineGLES::IsCodePtrVertexDecoder(const u8 *ptr) const {
	return decJitCache_->IsInSpace(ptr);
}

void DrawEngineGLES::TessellationDataTransferGLES::SendDataToShader(const float *pos, const float *tex, const float *col, int size, bool hasColor, bool hasTexCoords) {
	/*
#ifndef USING_GLES2
	if (isAllowTexture1D_) {
		// Position
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_1D, data_tex[0]);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (prevSize < size) {
			glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, size, 0, GL_RGBA, GL_FLOAT, (GLfloat*)pos);
			prevSize = size;
		} else {
			glTexSubImage1D(GL_TEXTURE_1D, 0, 0, size, GL_RGBA, GL_FLOAT, (GLfloat*)pos);
		}

		// Texcoords
		if (hasTexCoords) {
			glActiveTexture(GL_TEXTURE5);
			glBindTexture(GL_TEXTURE_1D, data_tex[1]);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			if (prevSizeTex < size) {
				glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, size, 0, GL_RGBA, GL_FLOAT, (GLfloat*)tex);
				prevSizeTex = size;
			} else {
				glTexSubImage1D(GL_TEXTURE_1D, 0, 0, size, GL_RGBA, GL_FLOAT, (GLfloat*)tex);
			}
		}

		// Color
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_1D, data_tex[2]);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		int sizeColor = hasColor ? size : 1;
		if (prevSizeCol < sizeColor) {
			glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA32F, sizeColor, 0, GL_RGBA, GL_FLOAT, (GLfloat*)col);
			prevSizeCol = sizeColor;
		} else {
			glTexSubImage1D(GL_TEXTURE_1D, 0, 0, sizeColor, GL_RGBA, GL_FLOAT, (GLfloat*)col);
		}
	} else 
#endif
	{
		// Position
		glActiveTexture(GL_TEXTURE4);
		glBindTexture(GL_TEXTURE_2D, data_tex[0]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		if (prevSize < size) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, size, 1, 0, GL_RGBA, GL_FLOAT, (GLfloat*)pos);
			prevSize = size;
		} else {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, 1, GL_RGBA, GL_FLOAT, (GLfloat*)pos);
		}

		// Texcoords
		if (hasTexCoords) {
			glActiveTexture(GL_TEXTURE5);
			glBindTexture(GL_TEXTURE_2D, data_tex[1]);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			if (prevSizeTex < size) {
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, size, 1, 0, GL_RGBA, GL_FLOAT, (GLfloat*)tex);
				prevSizeTex = size;
			} else {
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size, 1, GL_RGBA, GL_FLOAT, (GLfloat*)tex);
			}
		}

		// Color
		glActiveTexture(GL_TEXTURE6);
		glBindTexture(GL_TEXTURE_2D, data_tex[2]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		int sizeColor = hasColor ? size : 1;
		if (prevSizeCol < sizeColor) {
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, sizeColor, 1, 0, GL_RGBA, GL_FLOAT, (GLfloat*)col);
			prevSizeCol = sizeColor;
		} else {
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, sizeColor, 1, GL_RGBA, GL_FLOAT, (GLfloat*)col);
		}
	}
	glActiveTexture(GL_TEXTURE0);
	CHECK_GL_ERROR_IF_DEBUG();
	*/
}
