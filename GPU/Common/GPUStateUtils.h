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
	REPLACE_BLEND_READ_FRAMEBUFFER,

	// Color blend mode and color gets copied to alpha blend mode.
	REPLACE_BLEND_BLUE_TO_ALPHA,
};

enum SimulateLogicOpType {
	LOGICOPTYPE_NORMAL,
	LOGICOPTYPE_ONE,
	LOGICOPTYPE_INVERT,
};

bool IsAlphaTestTriviallyTrue();
bool IsColorTestAgainstZero();
bool IsColorTestTriviallyTrue();
bool IsAlphaTestAgainstZero();
bool NeedsTestDiscard();
bool IsDepthTestEffectivelyDisabled();
bool IsStencilTestOutputDisabled();

StencilValueType ReplaceAlphaWithStencilType();
ReplaceAlphaType ReplaceAlphaWithStencil(ReplaceBlendType replaceBlend);
ReplaceBlendType ReplaceBlendWithShader(GEBufferFormat bufferFormat);

// This is for the fallback path if real logic ops are not available.
SimulateLogicOpType SimulateLogicOpShaderTypeIfNeeded();

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

// NOTE: See the .cpp file for detailed comment about how the use flags are interpreted.
class DepthScaleFactors {
public:
	// This should only be used from GetDepthScaleFactors.
	DepthScaleFactors(double offset, double scale) : offset_(offset), scale_(scale) {}

	// Decodes a value from a depth buffer to a value of range 0..65536
	float DecodeToU16(float z) const {
		return (float)((z - offset_) * scale_);
	}

	// Encodes a value from the range 0..65536 to a normalized depth value (0-1), in the
	// range that we write to the depth buffer.
	float EncodeFromU16(float z_u16) const {
		return (float)(((double)z_u16 / scale_) + offset_);
	}

	float Offset() const { return (float)offset_; }

	float ScaleU16() const { return (float)scale_; }
	float Scale() const { return (float)(scale_ / 65535.0); }

private:
	// Doubles hardly cost anything these days, and precision matters here.
	double offset_;
	double scale_;
};

DepthScaleFactors GetDepthScaleFactors(u32 useFlags);

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

// Computed blend setup, including shader stuff.
struct GenericBlendState {
	bool applyFramebufferRead;
	bool dirtyShaderBlendFixValues;

	// Shader generation state
	ReplaceAlphaType replaceAlphaWithStencil;
	ReplaceBlendType replaceBlend;
	SimulateLogicOpType simulateLogicOpType;

	// Resulting hardware blend state
	bool blendEnabled;

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

	void Log();
};

void ApplyStencilReplaceAndLogicOpIgnoreBlend(ReplaceAlphaType replaceAlphaWithStencil, GenericBlendState &blendState);

struct GenericMaskState {
	bool applyFramebufferRead;
	uint32_t uniformMask;  // For each bit, opposite to the PSP.

	// The hardware channel masks, 1 bit per color component. From bit 0, order is RGBA like in all APIs!
	uint8_t channelMask;

	void ConvertToShaderBlend() {
		// If we have to do it in the shader, we simply pass through all channels but mask only in the shader instead.
		// Some GPUs have minor penalties for masks that are not all-channels-on or all-channels-off.
		channelMask = 0xF;
		applyFramebufferRead = true;
	}

	void Log();
};

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

struct GenericLogicState {
	// If set, logic op is applied in the shader INSTEAD of in hardware.
	// In this case, simulateLogicOpType and all that should be off.
	bool applyFramebufferRead;

	// Hardware
	bool logicOpEnabled;

	// Hardware and shader generation
	GELogicOp logicOp;

	void ApplyToBlendState(GenericBlendState &blendState);
	void ConvertToShaderBlend() {
		if (logicOp != GE_LOGIC_COPY) {
			logicOpEnabled = false;
			applyFramebufferRead = true;
			// Same logicOp is kept.
		}
	}
};

struct ComputedPipelineState {
	GenericBlendState blendState;
	GenericMaskState maskState;
	GenericLogicState logicState;

	void Convert(bool shaderBitOpsSupported);

	bool FramebufferRead() const {
		// If blending is off, its applyFramebufferRead can be false even after state propagation.
		// So it's not enough to check just that one.
		return blendState.applyFramebufferRead || maskState.applyFramebufferRead || logicState.applyFramebufferRead;
	}
};

// See issue #15898
inline bool SpongebobDepthInverseConditions(const GenericStencilFuncState &stencilState) {
	// Check that the depth/stencil state matches the conditions exactly.
	// Always with a depth test that's not writing to the depth buffer (only stencil.)
	if (!gstate.isDepthTestEnabled() || gstate.isDepthWriteEnabled())
		return false;
	// Always GREATER_EQUAL, which we flip to LESS.
	if (gstate.getDepthTestFunction() != GE_COMP_GEQUAL)
		return false;

	// The whole purpose here is a depth fail that we need to write to alpha.
	if (stencilState.zFail != GE_STENCILOP_ZERO || stencilState.sFail != GE_STENCILOP_KEEP || stencilState.zPass != GE_STENCILOP_KEEP)
		return false;
	if (stencilState.testFunc != GE_COMP_ALWAYS || stencilState.writeMask != 0xFF)
		return false;

	// Lastly, verify no color is written.  Natural way is a mask, in case another game uses it.
	// Note that the PSP masks are reversed compared to typical APIs.
	if (gstate.getColorMask() == 0xFFFFFF00)
		return true;

	// These games specifically use simple alpha blending with a constant zero alpha.
	if (!gstate.isAlphaBlendEnabled() || gstate.getBlendFuncA() != GE_SRCBLEND_SRCALPHA || gstate.getBlendFuncB() != GE_DSTBLEND_INVSRCALPHA)
		return false;

	// Also make sure there's no texture, in case its alpha gets involved.
	if (gstate.isTextureMapEnabled())
		return false;

	// Spongebob uses material alpha.
	if (gstate.getMaterialAmbientA() == 0x00 && gstate.getMaterialUpdate() == 0)
		return true;
	// MX vs ATV : Reflex uses vertex colors, should really check them...
	if (gstate.getMaterialUpdate() == 1)
		return true;

	// Okay, color is most likely being used if we didn't hit the above.
	return false;
}
