// Copyright (c) 2017- PPSSPP Project.

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

#include <cstdint>
#include <vector>

#include <d3d11.h>

class PushBufferD3D11 {
public:
	PushBufferD3D11(ID3D11Device *device, size_t size, D3D11_BIND_FLAG bindFlags) : size_(size) {
		D3D11_BUFFER_DESC desc{};
		desc.BindFlags = bindFlags;
		desc.ByteWidth = (UINT)size;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		desc.Usage = D3D11_USAGE_DYNAMIC;
		device->CreateBuffer(&desc, nullptr, &buffer_);
	}
	PushBufferD3D11(PushBufferD3D11 &) = delete;
	~PushBufferD3D11() {
		buffer_->Release();
	}
	ID3D11Buffer *Buf() const {
		return buffer_;
	}

	// Should be done each frame
	void Reset() {
		pos_ = 0;
		nextMapDiscard_ = true;
	}

	uint8_t *BeginPush(ID3D11DeviceContext *context, UINT *offset, size_t size, int align = 16) {
		D3D11_MAPPED_SUBRESOURCE map;
		pos_ = (pos_ + align - 1) & ~(align - 1);
		if (pos_ + size > size_) {
			// Wrap! Note that with this method, since we return the same buffer as before, you have to do the draw immediately after,
			// can't defer like in Vulkan. We instead let the driver handle the invalidation etc.
			pos_ = 0;
			nextMapDiscard_ = true;
		}
		context->Map(buffer_, 0, nextMapDiscard_ ? D3D11_MAP_WRITE_DISCARD : D3D11_MAP_WRITE_NO_OVERWRITE, 0, &map);
		nextMapDiscard_ = false;
		*offset = (UINT)pos_;
		uint8_t *retval = (uint8_t *)map.pData + pos_;
		pos_ += size;
		return retval;
	}
	void EndPush(ID3D11DeviceContext *context) {
		context->Unmap(buffer_, 0);
	}

private:
	ID3D11Buffer *buffer_ = nullptr;
	size_t pos_ = 0;
	size_t size_;
	bool nextMapDiscard_ = false;
};

std::vector<uint8_t> CompileShaderToBytecodeD3D11(const char *code, size_t codeSize, const char *target, UINT flags);

ID3D11VertexShader *CreateVertexShaderD3D11(ID3D11Device *device, const char *code, size_t codeSize, std::vector<uint8_t> *byteCodeOut, D3D_FEATURE_LEVEL featureLevel, UINT flags = 0);
ID3D11PixelShader *CreatePixelShaderD3D11(ID3D11Device *device, const char *code, size_t codeSize, D3D_FEATURE_LEVEL featureLevel, UINT flags = 0);
ID3D11ComputeShader *CreateComputeShaderD3D11(ID3D11Device *device, const char *code, size_t codeSize, D3D_FEATURE_LEVEL featureLevel, UINT flags = 0);
ID3D11GeometryShader *CreateGeometryShaderD3D11(ID3D11Device *device, const char *code, size_t codeSize, D3D_FEATURE_LEVEL featureLevel, UINT flags = 0);

class StockObjectsD3D11 {
public:
	void Create(ID3D11Device *device);
	void Destroy();

	ID3D11DepthStencilState *depthStencilDisabled;
	ID3D11DepthStencilState *depthDisabledStencilWrite;
	ID3D11BlendState *blendStateDisabledWithColorMask[16];
	ID3D11RasterizerState *rasterStateNoCull;
	ID3D11SamplerState *samplerPoint2DWrap;
	ID3D11SamplerState *samplerLinear2DWrap;
	ID3D11SamplerState *samplerPoint2DClamp;
	ID3D11SamplerState *samplerLinear2DClamp;
};

#define ASSERT_SUCCESS(x) \
	if (!SUCCEEDED((x))) \
		Crash();

extern StockObjectsD3D11 stockD3D11;
