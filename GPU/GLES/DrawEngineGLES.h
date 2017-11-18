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

#pragma once

#include <Common/Hashmaps.h>
#include <unordered_map>

#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/GLES/FragmentShaderGeneratorGLES.h"
#include "gfx/gl_common.h"

class LinkedShader;
class ShaderManagerGLES;
class TextureCacheGLES;
class FramebufferManagerGLES;
class FramebufferManagerCommon;
class TextureCacheCommon;
class FragmentTestCacheGLES;
struct TransformedVertex;

struct DecVtxFormat;

// States transitions:
// On creation: DRAWN_NEW
// DRAWN_NEW -> DRAWN_HASHING
// DRAWN_HASHING -> DRAWN_RELIABLE
// DRAWN_HASHING -> DRAWN_UNRELIABLE
// DRAWN_ONCE -> UNRELIABLE
// DRAWN_RELIABLE -> DRAWN_SAFE
// UNRELIABLE -> death
// DRAWN_ONCE -> death
// DRAWN_RELIABLE -> death

enum {
	VAI_FLAG_VERTEXFULLALPHA = 1,
};

// Try to keep this POD.
class VertexArrayInfo {
public:
	VertexArrayInfo() {
		status = VAI_NEW;
		vbo = 0;
		ebo = 0;
		prim = GE_PRIM_INVALID;
		numDraws = 0;
		numFrames = 0;
		lastFrame = gpuStats.numFlips;
		numVerts = 0;
		drawsUntilNextFullHash = 0;
		flags = 0;
	}

	enum Status : uint8_t {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	ReliableHashType hash;
	u32 minihash;

	u32 vbo;
	u32 ebo;

	// Precalculated parameter for drawRangeElements
	u16 numVerts;
	u16 maxIndex;
	s8 prim;
	Status status;

	// ID information
	int numDraws;
	int numFrames;
	int lastFrame;  // So that we can forget.
	u16 drawsUntilNextFullHash;
	u8 flags;
};

// Handles transform, lighting and drawing.
class DrawEngineGLES : public DrawEngineCommon {
public:
	DrawEngineGLES(Draw::DrawContext *draw);
	virtual ~DrawEngineGLES();

	void SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead);

	void SetShaderManager(ShaderManagerGLES *shaderManager) {
		shaderManager_ = shaderManager;
	}
	void SetTextureCache(TextureCacheGLES *textureCache) {
		textureCache_ = textureCache;
	}
	void SetFramebufferManager(FramebufferManagerGLES *fbManager) {
		framebufferManager_ = fbManager;
	}
	void SetFragmentTestCache(FragmentTestCacheGLES *testCache) {
		fragmentTestCache_ = testCache;
	}
	void RestoreVAO();

	void DeviceLost();
	void DeviceRestore();

	void ClearTrackedVertexArrays() override;
	void DecimateTrackedVertexArrays();

	// So that this can be inlined
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawCalls)
			return;
		DecodeVerts(decoded);
	}

	bool IsCodePtrVertexDecoder(const u8 *ptr) const;

	void DispatchFlush() override { Flush(); }
	void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) override {
		SubmitPrim(verts, inds, prim, vertexCount, vertType, bytesRead);
	}

	GLuint BindBuffer(const void *p, size_t sz);
	GLuint BindBuffer(const void *p1, size_t sz1, const void *p2, size_t sz2);
	GLuint BindElementBuffer(const void *p, size_t sz);
	void DecimateBuffers();

private:
	void InitDeviceObjects();
	void DestroyDeviceObjects();

	void DoFlush();
	void ApplyDrawState(int prim);
	void ApplyDrawStateLate();
	void ResetShaderBlending();

	GLuint AllocateBuffer(size_t sz);
	void FreeBuffer(GLuint buf);
	void FreeVertexArray(VertexArrayInfo *vai);

	void MarkUnreliable(VertexArrayInfo *vai);

	PrehashMap<VertexArrayInfo *, nullptr> vai_;

	// Vertex buffer objects
	// Element buffer objects
	struct BufferNameInfo {
		BufferNameInfo() : sz(0), used(false), lastFrame(0) {}

		size_t sz;
		bool used;
		int lastFrame;
	};
	std::vector<GLuint> bufferNameCache_;
	std::multimap<size_t, GLuint> freeSizedBuffers_;
	std::unordered_map<GLuint, BufferNameInfo> bufferNameInfo_;
	std::vector<GLuint> buffersThisFrame_;
	size_t bufferNameCacheSize_ = 0;
	GLuint sharedVao_ = 0;

	// Other
	ShaderManagerGLES *shaderManager_ = nullptr;
	TextureCacheGLES *textureCache_ = nullptr;
	FramebufferManagerGLES *framebufferManager_ = nullptr;
	FragmentTestCacheGLES *fragmentTestCache_ = nullptr;
	Draw::DrawContext *draw_;

	int bufferDecimationCounter_ = 0;

	// Hardware tessellation
	class TessellationDataTransferGLES : public TessellationDataTransfer {
	private:
		int data_tex[3];
		bool isAllowTexture1D_;
	public:
		TessellationDataTransferGLES(bool isAllowTexture1D) : TessellationDataTransfer(), data_tex(), isAllowTexture1D_(isAllowTexture1D) {
			glGenTextures(3, (GLuint*)data_tex);
		}
		~TessellationDataTransferGLES() {
			glDeleteTextures(3, (GLuint*)data_tex); 
		}
		void SendDataToShader(const float *pos, const float *tex, const float *col, int size, bool hasColor, bool hasTexCoords) override;
	};
};
