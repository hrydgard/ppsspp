#pragma once

#include "Common/CommonTypes.h"

#include "GPU/ge_constants.h"

// TODO: Replace enums and structs with same from thin3d.h, for convenient mapping.

enum StencilValueType {
	STENCIL_VALUE_UNIFORM,
	STENCIL_VALUE_ZERO,
	STENCIL_VALUE_ONE,
	STENCIL_VALUE_KEEP,
	STENCIL_VALUE_INVERT,
	STENCIL_VALUE_INCR_4,
	STENCIL_VALUE_INCR_8,
	STENCIL_VALUE_DECR_4,
	STENCIL_VALUE_DECR_8,
};

enum ReplaceAlphaType {
	REPLACE_ALPHA_NO = 0,
	REPLACE_ALPHA_YES = 1,
	REPLACE_ALPHA_DUALSOURCE = 2,
};

enum ReplaceBlendType {
	REPLACE_BLEND_NO,
	REPLACE_BLEND_STANDARD,
	REPLACE_BLEND_PRE_SRC,
	REPLACE_BLEND_PRE_SRC_2X_ALPHA,
	REPLACE_BLEND_2X_ALPHA,
	REPLACE_BLEND_2X_SRC,
	REPLACE_BLEND_COPY_FBO,
};

enum LogicOpReplaceType {
	LOGICOPTYPE_NORMAL,
	LOGICOPTYPE_ONE,
	LOGICOPTYPE_INVERT,
};

bool IsAlphaTestTriviallyTrue();
bool IsColorTestAgainstZero();
bool IsColorTestTriviallyTrue();
bool IsAlphaTestAgainstZero();
bool NeedsTestDiscard();
bool IsStencilTestOutputDisabled();

StencilValueType ReplaceAlphaWithStencilType();
ReplaceAlphaType ReplaceAlphaWithStencil(ReplaceBlendType replaceBlend);
ReplaceBlendType ReplaceBlendWithShader(bool allowShaderBlend, GEBufferFormat bufferFormat);

bool CanUseHardwareTransform(int prim);
LogicOpReplaceType ReplaceLogicOpType();


// Common representation, should be able to set this directly with any modern API.
struct ViewportAndScissor {
	bool scissorEnable;
	int scissorX;
	int scissorY;
	int scissorW;
	int scissorH;
	float viewportX;
	float viewportY;
	float viewportW;
	float viewportH;
	float depthRangeMin;
	float depthRangeMax;
	bool dirtyProj;
	bool dirtyDepth;
};
void ConvertViewportAndScissor(bool useBufferedRendering, float renderWidth, float renderHeight, int bufferWidth, int bufferHeight, ViewportAndScissor &out);
float ToScaledDepth(u16 z);
float FromScaledDepth(float z);
float DepthSliceFactor();

// These are common to all modern APIs and can be easily converted with a lookup table.
enum class BlendFactor : uint8_t {
	ZERO,
	ONE,
	SRC_COLOR,
	ONE_MINUS_SRC_COLOR,
	DST_COLOR,
	ONE_MINUS_DST_COLOR,
	SRC_ALPHA,
	ONE_MINUS_SRC_ALPHA,
	DST_ALPHA,
	ONE_MINUS_DST_ALPHA,
	CONSTANT_COLOR,
	ONE_MINUS_CONSTANT_COLOR,
	CONSTANT_ALPHA,
	ONE_MINUS_CONSTANT_ALPHA,
	SRC1_COLOR,
	ONE_MINUS_SRC1_COLOR,
	SRC1_ALPHA,
	ONE_MINUS_SRC1_ALPHA,
	INVALID,
	COUNT,
};

enum class BlendEq : uint8_t {
	ADD,
	SUBTRACT,
	REVERSE_SUBTRACT,
	MIN,
	MAX,
	COUNT
};

struct GenericBlendState {
	bool enabled;
	bool resetShaderBlending;
	bool applyShaderBlending;
	bool dirtyShaderBlend;
	ReplaceAlphaType replaceAlphaWithStencil;

	BlendFactor srcColor;
	BlendFactor dstColor;
	BlendFactor srcAlpha;
	BlendFactor dstAlpha;

	BlendEq eqColor;
	BlendEq eqAlpha;

	bool useBlendColor;
	u32 blendColor;

	void setFactors(BlendFactor srcC, BlendFactor dstC, BlendFactor srcA, BlendFactor dstA) {
		srcColor = srcC;
		dstColor = dstC;
		srcAlpha = srcA;
		dstAlpha = dstA;
	}
	void setEquation(BlendEq eqC, BlendEq eqA) {
		eqColor = eqC;
		eqAlpha = eqA;
	}
	void setBlendColor(uint32_t color, uint8_t alpha) {
		blendColor = color | ((uint32_t)alpha << 24);
		useBlendColor = true;
	}
	void defaultBlendColor(uint8_t alpha) {
		blendColor = 0xFFFFFF | ((uint32_t)alpha << 24);
		useBlendColor = true;
	}
};

void ConvertBlendState(GenericBlendState &blendState, bool allowShaderBlend);
void ApplyStencilReplaceAndLogicOp(ReplaceAlphaType replaceAlphaWithStencil, GenericBlendState &blendState);

struct GenericStencilFuncState {
	bool enabled;
	GEComparison testFunc;
	u8 testRef;
	u8 testMask;
	u8 writeMask;
	GEStencilOp sFail;
	GEStencilOp zFail;
	GEStencilOp zPass;
};

void ConvertStencilFuncState(GenericStencilFuncState &stencilFuncState);
