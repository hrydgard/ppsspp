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
#include <wrl/client.h>

#include "Common/Data/Collections/Hashmaps.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUDebugInterface.h"
#include "GPU/Common/IndexGenerator.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/MiscTypes.h"

struct DecVtxFormat;
struct UVScale;

class VSShader;
class ShaderManagerDX9;
class TextureCacheDX9;
class FramebufferManagerDX9;

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
	~DrawEngineDX9();

	void DeviceLost() override { draw_ = nullptr; }
	void DeviceRestore(Draw::DrawContext *draw) override { draw_ = draw; }

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

protected:
	// Not currently supported.
	bool UpdateUseHWTessellation(bool enable) const override { return false; }

private:
	void Invalidate(InvalidationCallbackFlags flags);
	void DoFlush();

	void ApplyDrawState(int prim);
	void ApplyDrawStateLate();

	HRESULT SetupDecFmtForDraw(const DecVtxFormat &decFmt, u32 pspFmt, IDirect3DVertexDeclaration9 **ppVertexDeclaration);

	LPDIRECT3DDEVICE9 device_ = nullptr;
	Draw::DrawContext *draw_;

	DenseHashMap<u32, Microsoft::WRL::ComPtr<IDirect3DVertexDeclaration9>> vertexDeclMap_;

	// SimpleVertex
	Microsoft::WRL::ComPtr<IDirect3DVertexDeclaration9> transformedVertexDecl_;

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
