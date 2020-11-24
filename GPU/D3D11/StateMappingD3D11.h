#pragma once

#include <cstdint>
#include <d3d11.h>

#include "Common/GPU/thin3d.h"

// TODO: Do this more progressively. No need to compute the entire state if the entire state hasn't changed.

struct D3D11BlendKey {
	union {
		uint64_t value;
		struct {
			// Blend
			unsigned int blendEnable : 1;
			unsigned int srcColor : 5;  // D3D11_BLEND
			unsigned int destColor : 5;  // D3D11_BLEND
			unsigned int srcAlpha : 5;  // D3D11_BLEND
			unsigned int destAlpha : 5;  // D3D11_BLEND
			unsigned int blendOpColor : 3;  // D3D11_BLEND_OP
			unsigned int blendOpAlpha : 3;  // D3D11_BLEND_OP
			unsigned int logicOpEnable : 1;
			unsigned int logicOp : 4;  // D3D11_LOGIC_OP
			unsigned int colorWriteMask : 4;
		};
	};
};

struct D3D11DepthStencilKey {
	union {
		uint64_t value;
		struct {
			// Depth/Stencil
			unsigned int depthTestEnable : 1;
			unsigned int depthWriteEnable : 1;
			unsigned int depthCompareOp : 4;  // D3D11_COMPARISON   (-1 and we could fit it in 3 bits)
			unsigned int stencilTestEnable : 1;
			unsigned int stencilCompareFunc : 4;  // D3D11_COMPARISON
			unsigned int stencilPassOp : 4; // D3D11_STENCIL_OP
			unsigned int stencilFailOp : 4; // D3D11_STENCIL_OP
			unsigned int stencilDepthFailOp : 4;  // D3D11_STENCIL_OP
			unsigned int stencilWriteMask : 8;  // Unfortunately these are baked into the state on D3D11
			unsigned int stencilCompareMask : 8;
		};
	};
};

struct D3D11RasterKey {
	union {
		uint32_t value;
		struct {
			unsigned int cullMode : 2;  // D3D11_CULL_MODE
			unsigned int depthClipEnable : 1;
		};
	};
};

// In D3D11 we cache blend state objects etc, and we simply emit keys, which are then also used to create these objects.
struct D3D11StateKeys {
	D3D11BlendKey blend;
	D3D11DepthStencilKey depthStencil;
	D3D11RasterKey raster;
};

struct D3D11DynamicState {
	int topology;
	bool useBlendColor;
	uint32_t blendColor;
	bool useStencil;
	uint8_t stencilRef;
	Draw::Viewport viewport;
	D3D11_RECT scissor;
};
