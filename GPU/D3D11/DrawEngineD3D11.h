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

#include <d3d11.h>
#include <d3d11_1.h>

#include "Common/Data/Collections/Hashmaps.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/D3D11/StateMappingD3D11.h"
#include "GPU/D3D11/D3D11Util.h"

struct DecVtxFormat;
struct UVScale;

class D3D11VertexShader;
class ShaderManagerD3D11;
class TextureCacheD3D11;
class FramebufferManagerD3D11;

class TessellationDataTransferD3D11 : public TessellationDataTransfer {
private:
	ID3D11DeviceContext *context_;
	ID3D11Device *device_;
	ID3D11Buffer *buf[3]{};
	ID3D11ShaderResourceView *view[3]{};
	D3D11_BUFFER_DESC desc{};
	int prevSize = 0;
	int prevSizeWU = 0, prevSizeWV = 0;
public:
	TessellationDataTransferD3D11(ID3D11DeviceContext *context, ID3D11Device *device);
	~TessellationDataTransferD3D11();
	// Send spline/bezier's control points and weights to vertex shader through structured shader buffer.
	void SendDataToShader(const SimpleVertex *const *points, int size_u, int size_v, u32 vertType, const Spline::Weight2D &weights) override;
};

// Handles transform, lighting and drawing.
class DrawEngineD3D11 : public DrawEngineCommon {
public:
	DrawEngineD3D11(Draw::DrawContext *draw, ID3D11Device *device, ID3D11DeviceContext *context);
	~DrawEngineD3D11();

	void DeviceLost() override { draw_ = nullptr;  }
	void DeviceRestore(Draw::DrawContext *draw) override { draw_ = draw; }

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

	void BeginFrame();

	// So that this can be inlined
	void Flush() {
		if (!numDrawVerts_)
			return;
		DoFlush();
	}

	void FinishDeferred() {
		if (!numDrawVerts_)
			return;
		DecodeVerts(decoded_);
	}

	void DispatchFlush() override {
		if (!numDrawVerts_)
			return;
		Flush();
	}

	void NotifyConfigChanged() override;

	void ClearInputLayoutMap();

private:
	void Invalidate(InvalidationCallbackFlags flags);

	void DoFlush();

	void ApplyDrawState(int prim);
	void ApplyDrawStateLate(bool applyStencilRef, uint8_t stencilRef);

	ID3D11InputLayout *SetupDecFmtForDraw(D3D11VertexShader *vshader, const DecVtxFormat &decFmt, u32 pspFmt);

	Draw::DrawContext *draw_;  // Used for framebuffer related things exclusively.
	ID3D11Device *device_;
	ID3D11Device1 *device1_;
	ID3D11DeviceContext *context_;
	ID3D11DeviceContext1 *context1_;

	struct InputLayoutKey {
		D3D11VertexShader *vshader;
		u32 decFmtId;
		bool operator <(const InputLayoutKey &other) const {
			if (decFmtId < other.decFmtId)
				return true;
			if (decFmtId > other.decFmtId)
				return false;
			return vshader < other.vshader;
		}
	};

	DenseHashMap<InputLayoutKey, ID3D11InputLayout *> inputLayoutMap_;

	// Other
	ShaderManagerD3D11 *shaderManager_ = nullptr;
	TextureCacheD3D11 *textureCache_ = nullptr;
	FramebufferManagerD3D11 *framebufferManager_ = nullptr;

	// Pushbuffers
	PushBufferD3D11 *pushVerts_;
	PushBufferD3D11 *pushInds_;

	// D3D11 state object caches.
	DenseHashMap<uint64_t, ID3D11BlendState *> blendCache_;
	DenseHashMap<uint64_t, ID3D11BlendState1 *> blendCache1_;
	DenseHashMap<uint64_t, ID3D11DepthStencilState *> depthStencilCache_;
	DenseHashMap<uint32_t, ID3D11RasterizerState *> rasterCache_;

	// Keep the depth state between ApplyDrawState and ApplyDrawStateLate
	ID3D11RasterizerState *rasterState_ = nullptr;
	ID3D11BlendState *blendState_ = nullptr;
	ID3D11BlendState1 *blendState1_ = nullptr;
	ID3D11DepthStencilState *depthStencilState_ = nullptr;

	// State keys
	D3D11StateKeys keys_{};
	D3D11DynamicState dynState_{};

	// Hardware tessellation
	TessellationDataTransferD3D11 *tessDataTransferD3D11;

	int lastRenderStepId_ = -1;
};
