#pragma once

#include <cstdint>
#include "Common/CommonTypes.h"

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"

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
	REPLACE_BLEND_NO,  // Blend function handled directly with blend states.

	REPLACE_BLEND_STANDARD,

	// SRC part of blend function handled in-shader.
	REPLACE_BLEND_PRE_SRC,
	REPLACE_BLEND_PRE_SRC_2X_ALPHA,
	REPLACE_BLEND_2X_ALPHA,
	REPLACE_BLEND_2X_SRC,

	// Full blend equation runs in shader.
	// We might have to make a copy of the framebuffer target to read from.
	REPLACE_BLEND_COPY_FBO,

	// Color blend mode and color gets copied to alpha blend mode.
	REPLACE_BLEND_BLUE_TO_ALPHA,
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

LogicOpReplaceType ReplaceLogicOpType();

// Common representation, should be able to set this directly with any modern API.
struct ViewportAndScissor {
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
	float widthScale;
	float heightScale;
	float depthScale;
	float xOffset;
	float yOffset;
	float zOffset;
	bool throughMode;
};
void ConvertViewportAndScissor(bool useBufferedRendering, float renderWidth, float renderHeight, int bufferWidth, int bufferHeight, ViewportAndScissor &out);
void UpdateCachedViewportState(const ViewportAndScissor &vpAndScissor);
float ToScaledDepthFromIntegerScale(float z);

struct DepthScaleFactors {
	float offset;
	float scale;

	float Apply(float z) const {
		return (z - offset) * scale;
	}

	float ApplyInverse(float z) const {
		return (z / scale) + offset;
	}
};
DepthScaleFactors GetDepthScaleFactors();

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
	bool resetFramebufferRead;
	bool applyFramebufferRead;
	bool dirtyShaderBlendFixValues;
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

void ConvertBlendState(GenericBlendState &blendState, bool allowShaderBlend, bool forceReplaceBlend);
void ApplyStencilReplaceAndLogicOpIgnoreBlend(ReplaceAlphaType replaceAlphaWithStencil, GenericBlendState &blendState);

struct GenericMaskState {
	bool applyFramebufferRead;
	uint32_t uniformMask;  // For each bit, opposite to the PSP.
	bool rgba[4];  // true = draw, false = don't draw this channel
};

void ConvertMaskState(GenericMaskState &maskState, bool allowFramebufferRead);
bool IsColorWriteMaskComplex(bool allowFramebufferRead);

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

// See issue #15898
inline bool SpongebobDepthInverseConditions(const GenericStencilFuncState &stencilState) {
	// Check that the depth/stencil state matches the conditions exactly
	return gstate.isDepthTestEnabled() && !gstate.isDepthWriteEnabled() &&
		gstate.getDepthTestFunction() == GE_COMP_GEQUAL &&
		stencilState.zFail == GE_STENCILOP_ZERO && stencilState.sFail == GE_STENCILOP_KEEP && stencilState.zPass == GE_STENCILOP_KEEP &&
		stencilState.testFunc == GE_COMP_ALWAYS && stencilState.writeMask == 0xFF &&
		// And also verify no color is written. The game does this through simple alpha blending with a constant zero alpha.
		// We also check for color mask, since it's more natural, in case another game does it.
		((gstate.isAlphaBlendEnabled() &&
			!gstate.isTextureMapEnabled() &&
			gstate.getBlendFuncA() == GE_SRCBLEND_SRCALPHA &&
			gstate.getBlendFuncB() == GE_DSTBLEND_INVSRCALPHA &&
			(
				( // Spongebob
					gstate.getMaterialAmbientA() == 0x0 &&  // our accessor is kinda misnamed here, but material diffuse A is both used as default color and as ambient alpha
					gstate.getMaterialUpdate() == 0
				) ||
				( // MX vs ATV : Reflex
					gstate.getMaterialUpdate() == 1   // Really should also check vertex data, since that's where the zero is :(
				)
			)
		) || gstate.getColorMask() == 0xFFFFFF00);  // note that PSP masks are "inverted"
}
