#pragma once

#include <cstdint>
#include <wiiu/gx2.h>

#include "Common/GPU/thin3d.h"

// TODO: Do this more progressively. No need to compute the entire state if the entire state hasn't changed.

struct GX2BlendKey {
	union {
		uint64_t value;
		struct {
			// Blend
			bool blendEnable : 1;
			GX2BlendMode srcColor : 5;
			GX2BlendMode destColor : 5;
			GX2BlendMode srcAlpha : 5;
			GX2BlendMode destAlpha : 5;
			GX2BlendCombineMode blendOpColor : 3;
			GX2BlendCombineMode blendOpAlpha : 3;
			bool logicOpEnable : 1;
			GX2LogicOp logicOp : 8;
			GX2ChannelMask colorWriteMask : 4;
		};
	};
};

struct GX2DepthStencilKey {
	union {
		uint64_t value;
		struct {
			// Depth/Stencil
			bool depthTestEnable : 1;
			bool depthWriteEnable : 1;
			GX2CompareFunction depthCompareOp : 4; // GX2_COMPARISON   (-1 and we could fit it in 3 bits)
			bool stencilTestEnable : 1;
			GX2CompareFunction stencilCompareFunc : 4; // GX2_COMPARISON
			GX2StencilFunction stencilPassOp : 4;      // GX2_STENCIL_OP
			GX2StencilFunction stencilFailOp : 4;      // GX2_STENCIL_OP
			GX2StencilFunction stencilDepthFailOp : 4; // GX2_STENCIL_OP
			unsigned int stencilWriteMask : 8;   // Unfortunately these are baked into the state on GX2
			unsigned int stencilCompareMask : 8;
		};
	};
};

struct GX2RasterKey {
	union {
		uint32_t value;
		struct {
			GX2FrontFace frontFace : 1;
			bool cullFront : 1;
			bool cullBack : 1;
		};
	};
};

// In GX2 we cache blend state objects etc, and we simply emit keys, which are then also used to create these objects.
struct GX2StateKeys {
	GX2BlendKey blend;
	GX2DepthStencilKey depthStencil;
	GX2RasterKey raster;
};

struct GX2_RECT {
	int left;
	int top;
	int right;
	int bottom;
};

struct GX2DynamicState {
	int topology;
	bool useBlendColor;
	uint32_t blendColor;
	bool useStencil;
	uint8_t stencilRef;
	Draw::Viewport viewport;
	GX2_RECT scissor;
};
