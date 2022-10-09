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

#include <d3d9.h>

#include "Common/Data/Collections/Hashmaps.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"

struct DecVtxFormat;
struct UVScale;

class VSShader;
class ShaderManagerDX9;
class TextureCacheDX9;
class FramebufferManagerDX9;

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
class VertexArrayInfoDX9 {
public:
	VertexArrayInfoDX9() {
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
	~VertexArrayInfoDX9();

	enum Status : uint8_t {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	uint64_t hash;
	u32 minihash;

	LPDIRECT3DVERTEXBUFFER9 vbo;
	LPDIRECT3DINDEXBUFFER9 ebo;

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

class TessellationDataTransferDX9 : public TessellationDataTransfer {
public:
	TessellationDataTransferDX9() {}
	~TessellationDataTransferDX9() {}
	void SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) override;
};

// Handles transform, lighting and drawing.
class DrawEngineDX9 : public DrawEngineCommon {
public:
	DrawEngineDX9(Draw::DrawContext *draw);
	virtual ~DrawEngineDX9();

	void SetShaderManager(ShaderManagerDX9 *shaderManager) {
		shaderManager_ = shaderManager;
	}
	void SetTextureCache(TextureCacheDX9 *textureCache) {
		textureCache_ = textureCache;
	}
	void SetFramebufferManager(FramebufferManagerDX9 *fbManager) {
		framebufferManager_ = fbManager;
	}
	void InitDeviceObjects();
	void DestroyDeviceObjects();

	void ClearTrackedVertexArrays() override;

	void BeginFrame();

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

	void DispatchFlush() override { Flush(); }

protected:
	// Not currently supported.
	bool UpdateUseHWTessellation(bool enable) override { return false; }
	void DecimateTrackedVertexArrays();

private:
	void DoFlush();

	void ApplyDrawState(int prim);
	void ApplyDrawStateLate();

	IDirect3DVertexDeclaration9 *SetupDecFmtForDraw(VSShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt);

	void MarkUnreliable(VertexArrayInfoDX9 *vai);

	LPDIRECT3DDEVICE9 device_ = nullptr;
	Draw::DrawContext *draw_;

	PrehashMap<VertexArrayInfoDX9 *, nullptr> vai_;
	DenseHashMap<u32, IDirect3DVertexDeclaration9 *, nullptr> vertexDeclMap_;

	// SimpleVertex
	IDirect3DVertexDeclaration9* transformedVertexDecl_ = nullptr;

	// Other
	ShaderManagerDX9 *shaderManager_ = nullptr;
	TextureCacheDX9 *textureCache_ = nullptr;
	FramebufferManagerDX9 *framebufferManager_ = nullptr;

	// Hardware tessellation
	TessellationDataTransferDX9 *tessDataTransferDX9;

	FBOTexState fboTexBindState_ = FBO_TEX_NONE;

	int lastRenderStepId_ = -1;

	bool fboTexNeedsBind_ = false;
};
