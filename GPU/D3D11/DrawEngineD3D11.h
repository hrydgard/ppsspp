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

#include <unordered_map>

#include <d3d11.h>
#include <d3d11_1.h>

#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/D3D11/FragmentShaderGeneratorD3D11.h"
#include "GPU/D3D11/StateMappingD3D11.h"
#include "GPU/D3D11/D3D11Util.h"

struct DecVtxFormat;
struct UVScale;

class D3D11VertexShader;
class ShaderManagerD3D11;
class TextureCacheD3D11;
class FramebufferManagerD3D11;

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
	VAI11_FLAG_VERTEXFULLALPHA = 1,
};

// Avoiding the full include of TextureDecoder.h.
#if (defined(_M_SSE) && defined(_M_X64)) || defined(ARM64)
typedef u64 ReliableHashType;
#else
typedef u32 ReliableHashType;
#endif

// Try to keep this POD.
class VertexArrayInfoD3D11 {
public:
	VertexArrayInfoD3D11() {
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
	~VertexArrayInfoD3D11();

	enum Status {
		VAI_NEW,
		VAI_HASHING,
		VAI_RELIABLE,  // cache, don't hash
		VAI_UNRELIABLE,  // never cache
	};

	ReliableHashType hash;
	u32 minihash;

	Status status;

	ID3D11Buffer *vbo;
	ID3D11Buffer *ebo;

	// Precalculated parameter for drawRangeElements
	u16 numVerts;
	u16 maxIndex;
	s8 prim;

	// ID information
	int numDraws;
	int numFrames;
	int lastFrame;  // So that we can forget.
	u16 drawsUntilNextFullHash;
	u8 flags;
};

// Handles transform, lighting and drawing.
class DrawEngineD3D11 : public DrawEngineCommon {
public:
	DrawEngineD3D11(Draw::DrawContext *draw, ID3D11Device *device, ID3D11DeviceContext *context);
	virtual ~DrawEngineD3D11();

	void SubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead);

	void SetShaderManager(ShaderManagerD3D11 *shaderManager) {
		shaderManager_ = shaderManager;
	}
	void SetTextureCache(TextureCacheD3D11 *textureCache) {
		textureCache_ = textureCache;
	}
	void SetFramebufferManager(FramebufferManagerD3D11 *fbManager) {
		framebufferManager_ = fbManager;
	}
	void InitDeviceObjects();
	void DestroyDeviceObjects();
	void GLLost() {};

	void BeginFrame();

	void SetupVertexDecoder(u32 vertType);
	void SetupVertexDecoderInternal(u32 vertType);

	// So that this can be inlined
	void Flush() {
		if (!numDrawCalls)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawCalls)
			return;
		DecodeVerts();
	}

	bool IsCodePtrVertexDecoder(const u8 *ptr) const;

	void DispatchFlush() override { Flush(); }
	void DispatchSubmitPrim(void *verts, void *inds, GEPrimitiveType prim, int vertexCount, u32 vertType, int *bytesRead) override {
		SubmitPrim(verts, inds, prim, vertexCount, vertType, bytesRead);
	}

	void ClearTrackedVertexArrays() override;

	void Resized() override;

private:
	void DecodeVerts();
	void DecodeVertsStep();
	void DoFlush();

	void ApplyDrawState();
	void ApplyDrawStateLate(int prim, bool applyStencilRef, uint8_t stencilRef);
	void ResetShaderBlending();

	void ClearInputLayoutMap();

	ID3D11InputLayout *SetupDecFmtForDraw(D3D11VertexShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt);

	u32 ComputeMiniHash();
	ReliableHashType ComputeHash();  // Reads deferred vertex data.
	void MarkUnreliable(VertexArrayInfoD3D11 *vai);

	Draw::DrawContext *draw_;  // Used for framebuffer related things exclusively.
	ID3D11Device *device_;
	ID3D11Device1 *device1_;
	ID3D11DeviceContext *context_;
	ID3D11DeviceContext1 *context1_;

	// Defer all vertex decoding to a Flush, so that we can hash and cache the
	// generated buffers without having to redecode them every time.
	struct DeferredDrawCall {
		void *verts;
		void *inds;
		u32 vertType;
		u8 indexType;
		s8 prim;
		u32 vertexCount;
		u16 indexLowerBound;
		u16 indexUpperBound;
	};

	// Vertex collector state
	IndexGenerator indexGen;
	int decodedVerts_;
	GEPrimitiveType prevPrim_;
	
	TransformedVertex *transformed;
	TransformedVertex *transformedExpanded;

	std::unordered_map<u32, VertexArrayInfoD3D11 *> vai_;

	struct InputLayoutKey {
		u32 vertType;
		D3D11VertexShader *vshader;
		bool operator <(const InputLayoutKey &other) const {
			if (vertType < other.vertType)
				return true;
			if (vertType > other.vertType)
				return false;
			return vshader < other.vshader;
		}
	};

	std::map<InputLayoutKey, ID3D11InputLayout *> inputLayoutMap_;

	// Other
	ShaderManagerD3D11 *shaderManager_;
	TextureCacheD3D11 *textureCache_;
	FramebufferManagerD3D11 *framebufferManager_;

	// Pushbuffers
	PushBufferD3D11 *pushVerts_;
	PushBufferD3D11 *pushInds_;

	enum { MAX_DEFERRED_DRAW_CALLS = 128 };

	DeferredDrawCall drawCalls[MAX_DEFERRED_DRAW_CALLS];
	int numDrawCalls;
	int vertexCountInDrawCalls;

	int decimationCounter_;
	int decodeCounter_;
	u32 dcid_;

	UVScale uvScale[MAX_DEFERRED_DRAW_CALLS];

	// D3D11 state object caches
	std::map<uint64_t, ID3D11BlendState *> blendCache_;
	std::map<uint64_t, ID3D11BlendState1 *> blendCache1_;
	std::map<uint64_t, ID3D11DepthStencilState *> depthStencilCache_;
	std::map<uint32_t, ID3D11RasterizerState *> rasterCache_;

	// State keys
	D3D11StateKeys keys_{};
	D3D11DynamicState dynState_{};

	// Hardware tessellation
	class TessellationDataTransferD3D11 : public TessellationDataTransfer {
	private:
		ID3D11DeviceContext *context_;
		ID3D11Device *device_;
		ID3D11Texture1D *data_tex[3];
		ID3D11ShaderResourceView *view[3];
		D3D11_TEXTURE1D_DESC desc;
		D3D11_BOX dstBox;
	public:
		TessellationDataTransferD3D11(ID3D11DeviceContext *context_, ID3D11Device *device_)
			: TessellationDataTransfer(), context_(context_), device_(device_), data_tex(), view(), desc(), dstBox{0, 0, 0, 1, 1, 1} {
			desc.CPUAccessFlags = 0;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.ArraySize = 1;
			desc.MipLevels = 1;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		}
		~TessellationDataTransferD3D11() {
			for (int i = 0; i < 3; i++) {
				if (data_tex[i]) {
					data_tex[i]->Release();
					view[i]->Release();
				}
			}
		}
		void SendDataToShader(const float *pos, const float *tex, const float *col, int size, bool hasColor, bool hasTexCoords) override;
	};
};
