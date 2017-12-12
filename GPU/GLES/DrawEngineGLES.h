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
#include "thin3d/GLRenderManager.h"

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
		vbo = nullptr;
		ebo = nullptr;
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

	GLRBuffer *vbo;
	GLRBuffer *ebo;

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

	void DeviceLost();
	void DeviceRestore();

	void ClearTrackedVertexArrays() override;
	void DecimateTrackedVertexArrays();

	void BeginFrame();
	void EndFrame();


	// So that this can be inlined
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	bool IsCodePtrVertexDecoder(const u8 *ptr) const;

	void DispatchFlush() override { Flush(); }
	void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) override {
		SubmitPrim(verts, inds, prim, vertexCount, vertType, bytesRead);
	}

	GLPushBuffer *GetPushVertexBuffer() {
		return frameData_[render_->GetCurFrame()].pushVertex;
	}
	GLPushBuffer *GetPushIndexBuffer() {
		return frameData_[render_->GetCurFrame()].pushIndex;
	}

	void ClearInputLayoutMap();

private:
	void InitDeviceObjects();
	void DestroyDeviceObjects();

	void DoFlush();
	void ApplyDrawState(int prim);
	void ApplyDrawStateLate(bool setStencil, int stencilValue);
	void ResetShaderBlending();

	GLRInputLayout *SetupDecFmtForDraw(LinkedShader *program, const DecVtxFormat &decFmt);

	void DecodeVertsToPushBuffer(GLPushBuffer *push, uint32_t *bindOffset, GLRBuffer **buf);

	void FreeVertexArray(VertexArrayInfo *vai);

	void MarkUnreliable(VertexArrayInfo *vai);

	struct FrameData {
		GLPushBuffer *pushVertex;
		GLPushBuffer *pushIndex;
	};
	FrameData frameData_[GLRenderManager::MAX_INFLIGHT_FRAMES];

	PrehashMap<VertexArrayInfo *, nullptr> vai_;

	DenseHashMap<uint32_t, GLRInputLayout *, nullptr> inputLayoutMap_;

	GLRInputLayout *softwareInputLayout_ = nullptr;
	GLRenderManager *render_;

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
		GLRTexture *data_tex[3]{};
		bool isAllowTexture1D_;
	public:
		TessellationDataTransferGLES(bool isAllowTexture1D) : TessellationDataTransfer(), isAllowTexture1D_(isAllowTexture1D) {	}
		~TessellationDataTransferGLES() {	}
		void SendDataToShader(const float *pos, const float *tex, const float *col, int size, bool hasColor, bool hasTexCoords) override;
	};
};
