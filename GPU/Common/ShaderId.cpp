#include <string>
#include <sstream>
#include <array>

#include "Common/GPU/thin3d.h"
#include "Common/StringUtils.h"
#include "Core/System.h"
#include "Core/Config.h"

#include "GPU/ge_constants.h"
#include "GPU/GPU.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexDecoderCommon.h"

std::string VertexShaderDesc(const VShaderID &id) {
	std::stringstream desc;
	desc << StringFromFormat("%08x:%08x ", id.d[1], id.d[0]);
	if (id.Bit(VS_BIT_IS_THROUGH)) desc << "THR ";
	if (id.Bit(VS_BIT_USE_HW_TRANSFORM)) desc << "HWX ";
	if (id.Bit(VS_BIT_HAS_COLOR)) desc << "C ";
	if (id.Bit(VS_BIT_HAS_TEXCOORD)) desc << "T ";
	if (id.Bit(VS_BIT_HAS_NORMAL)) desc << "N ";
	if (id.Bit(VS_BIT_LMODE)) desc << "LM ";
	if (id.Bit(VS_BIT_NORM_REVERSE)) desc << "RevN ";
	int uvgMode = id.Bits(VS_BIT_UVGEN_MODE, 2);
	if (uvgMode == GE_TEXMAP_TEXTURE_MATRIX) {
		int uvprojMode = id.Bits(VS_BIT_UVPROJ_MODE, 2);
		const char *uvprojModes[4] = { "TexProjPos ", "TexProjUV ", "TexProjNNrm ", "TexProjNrm " };
		desc << uvprojModes[uvprojMode];
	}
	static constexpr std::array<const char*, 4> uvgModes = { "UV ", "UVMtx ", "UVEnv ", "UVUnk " };
	int ls0 = id.Bits(VS_BIT_LS0, 2);
	int ls1 = id.Bits(VS_BIT_LS1, 2);

	if (uvgMode) desc << uvgModes[uvgMode];
	if (id.Bit(VS_BIT_ENABLE_BONES)) desc << "Bones:" << (id.Bits(VS_BIT_BONES, 3) + 1) << " ";
	// Lights
	if (id.Bit(VS_BIT_LIGHTING_ENABLE)) {
		desc << "Light: ";
	}
	if (id.Bit(VS_BIT_LIGHT_UBERSHADER)) {
		desc << "LightUberShader ";
	}
	for (int i = 0; i < 4; i++) {
		bool enabled = id.Bit(VS_BIT_LIGHT0_ENABLE + i) && id.Bit(VS_BIT_LIGHTING_ENABLE);
		if (enabled || (uvgMode == GE_TEXMAP_ENVIRONMENT_MAP && (ls0 == i || ls1 == i))) {
			desc << i << ": ";
			desc << "c:" << id.Bits(VS_BIT_LIGHT0_COMP + 4 * i, 2) << " t:" << id.Bits(VS_BIT_LIGHT0_TYPE + 4 * i, 2) << " ";
		}
	}
	if (id.Bits(VS_BIT_MATERIAL_UPDATE, 3)) desc << "MatUp:" << id.Bits(VS_BIT_MATERIAL_UPDATE, 3) << " ";
	if (id.Bits(VS_BIT_WEIGHT_FMTSCALE, 2)) desc << "WScale " << id.Bits(VS_BIT_WEIGHT_FMTSCALE, 2) << " ";
	if (id.Bit(VS_BIT_FLATSHADE)) desc << "Flat ";

	if (id.Bit(VS_BIT_BEZIER)) desc << "Bezier ";
	if (id.Bit(VS_BIT_SPLINE)) desc << "Spline ";
	if (id.Bit(VS_BIT_HAS_COLOR_TESS)) desc << "TessC ";
	if (id.Bit(VS_BIT_HAS_TEXCOORD_TESS)) desc << "TessT ";
	if (id.Bit(VS_BIT_HAS_NORMAL_TESS)) desc << "TessN ";
	if (id.Bit(VS_BIT_NORM_REVERSE_TESS)) desc << "TessRevN ";
	if (id.Bit(VS_BIT_VERTEX_RANGE_CULLING)) desc << "Cull ";

	if (id.Bit(VS_BIT_SIMPLE_STEREO)) desc << "SimpleStereo ";

	return desc.str();
}

void ComputeVertexShaderID(VShaderID *id_out, VertexDecoder *vertexDecoder, bool useHWTransform, bool useHWTessellation, bool weightsAsFloat, bool useSkinInDecode) {
	u32 vertType = vertexDecoder->VertexType();

	bool isModeThrough = (vertType & GE_VTYPE_THROUGH) != 0;
	bool doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool doShadeMapping = doTexture && (gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP);
	bool doFlatShading = gstate.getShadeMode() == GE_SHADE_FLAT && !gstate.isModeClear();

	bool vtypeHasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	bool vtypeHasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0;
	bool vtypeHasTexcoord = (vertType & GE_VTYPE_TC_MASK) != 0;

	bool doBezier = gstate_c.submitType == SubmitType::HW_BEZIER;
	bool doSpline = gstate_c.submitType == SubmitType::HW_SPLINE;

	if (doBezier || doSpline) {
		_assert_(vtypeHasNormal);
	}

	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled() && !isModeThrough && !gstate.isModeClear();
	bool vertexRangeCulling = gstate_c.Use(GPU_USE_VS_RANGE_CULLING) &&
		!isModeThrough && gstate_c.submitType == SubmitType::DRAW;  // neither hw nor sw spline/bezier. See #11692

	VShaderID id;
	id.SetBit(VS_BIT_LMODE, lmode);
	id.SetBit(VS_BIT_IS_THROUGH, isModeThrough);
	id.SetBit(VS_BIT_HAS_COLOR, vtypeHasColor);
	id.SetBit(VS_BIT_VERTEX_RANGE_CULLING, vertexRangeCulling);

	if (!isModeThrough && gstate_c.Use(GPU_USE_SINGLE_PASS_STEREO)) {
		id.SetBit(VS_BIT_SIMPLE_STEREO);
	}

	if (doTexture) {
		// UV generation mode. doShadeMapping is implicitly stored here.
		id.SetBits(VS_BIT_UVGEN_MODE, 2, gstate.getUVGenMode());
	}

	if (useHWTransform) {
		id.SetBit(VS_BIT_USE_HW_TRANSFORM);
		id.SetBit(VS_BIT_HAS_NORMAL, vtypeHasNormal);

		// The next bits are used differently depending on UVgen mode
		if (gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX) {
			id.SetBits(VS_BIT_UVPROJ_MODE, 2, gstate.getUVProjMode());
		} else if (doShadeMapping) {
			id.SetBits(VS_BIT_LS0, 2, gstate.getUVLS0());
			id.SetBits(VS_BIT_LS1, 2, gstate.getUVLS1());
		}

		// Bones.
		u32 vertType = vertexDecoder->VertexType();
		bool enableBones = !useSkinInDecode && vertTypeIsSkinningEnabled(vertType);
		id.SetBit(VS_BIT_ENABLE_BONES, enableBones);
		if (enableBones) {
			id.SetBits(VS_BIT_BONES, 3, TranslateNumBones(vertTypeGetNumBoneWeights(vertType)) - 1);
			// 2 bits. We should probably send in the weight scalefactor as a uniform instead,
			// or simply preconvert all weights to floats.
			id.SetBits(VS_BIT_WEIGHT_FMTSCALE, 2, weightsAsFloat ? 0 : (vertType & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT);
		}

		if (gstate.isLightingEnabled()) {
			// doShadeMapping is stored as UVGenMode, and light type doesn't matter for shade mapping.
			id.SetBit(VS_BIT_LIGHTING_ENABLE);
			if (gstate_c.Use(GPU_USE_LIGHT_UBERSHADER)) {
				id.SetBit(VS_BIT_LIGHT_UBERSHADER);
			} else {
				id.SetBits(VS_BIT_MATERIAL_UPDATE, 3, gstate.getMaterialUpdate());
				// Light bits
				for (int i = 0; i < 4; i++) {
					bool chanEnabled = gstate.isLightChanEnabled(i) != 0;
					id.SetBit(VS_BIT_LIGHT0_ENABLE + i, chanEnabled);
					if (chanEnabled) {
						id.SetBits(VS_BIT_LIGHT0_COMP + 4 * i, 2, gstate.getLightComputation(i));
						id.SetBits(VS_BIT_LIGHT0_TYPE + 4 * i, 2, gstate.getLightType(i));
					}
				}
			}
		}

		id.SetBit(VS_BIT_NORM_REVERSE, gstate.areNormalsReversed());
		id.SetBit(VS_BIT_HAS_TEXCOORD, vtypeHasTexcoord);

		if (useHWTessellation) {
			id.SetBit(VS_BIT_BEZIER, doBezier);
			id.SetBit(VS_BIT_SPLINE, doSpline);
			if (doBezier || doSpline) {
				// These are the original vertType's values (normalized will always have colors, etc.)
				id.SetBit(VS_BIT_HAS_COLOR_TESS, (gstate.vertType & GE_VTYPE_COL_MASK) != 0);
				id.SetBit(VS_BIT_HAS_TEXCOORD_TESS, (gstate.vertType & GE_VTYPE_TC_MASK) != 0);
				id.SetBit(VS_BIT_HAS_NORMAL_TESS, (gstate.vertType & GE_VTYPE_NRM_MASK) != 0 || gstate.isLightingEnabled());
			}
			id.SetBit(VS_BIT_NORM_REVERSE_TESS, gstate.isPatchNormalsReversed());
		}
	}

	id.SetBit(VS_BIT_FLATSHADE, doFlatShading);

	// These two bits cannot be combined, otherwise havoc occurs. We get reports that indicate this happened somehow... "ERROR: 0:14: 'u_proj' : undeclared identifier"
	_dbg_assert_msg_(!id.Bit(VS_BIT_USE_HW_TRANSFORM) || !id.Bit(VS_BIT_IS_THROUGH), "Can't have both THROUGH and USE_HW_TRANSFORM together!");

	*id_out = id;
}


static const char * const alphaTestFuncs[] = { "NEVER", "ALWAYS", "==", "!=", "<", "<=", ">", ">=" };
static const char * const logicFuncs[] = {
	"CLEAR", "AND", "AND_REV", "COPY", "AND_INV", "NOOP", "XOR", "OR",
	"NOR", "EQUIV", "INVERTED", "OR_REV", "COPY_INV", "OR_INV", "NAND", "SET",
};

static bool MatrixNeedsProjection(const float m[12], GETexProjMapMode mode) {
	// For GE_PROJMAP_UV, we can ignore m[8] since it multiplies to zero.
	return m[2] != 0.0f || m[5] != 0.0f || (m[8] != 0.0f && mode != GE_PROJMAP_UV) || m[11] != 1.0f;
}

std::string FragmentShaderDesc(const FShaderID &id) {
	std::stringstream desc;
	desc << StringFromFormat("%08x:%08x ", id.d[1], id.d[0]);
	if (id.Bit(FS_BIT_CLEARMODE)) desc << "Clear ";
	if (id.Bit(FS_BIT_DO_TEXTURE)) desc << (id.Bit(FS_BIT_3D_TEXTURE) ? "Tex3D " : "Tex ");
	if (id.Bit(FS_BIT_DO_TEXTURE_PROJ)) desc << "TexProj ";
	if (id.Bit(FS_BIT_ENABLE_FOG)) desc << "Fog ";
	if (id.Bit(FS_BIT_LMODE)) desc << "LM ";
	if (id.Bit(FS_BIT_TEXALPHA)) desc << "TexAlpha ";
	if (id.Bit(FS_BIT_DOUBLE_COLOR)) desc << "Double ";
	if (id.Bit(FS_BIT_FLATSHADE)) desc << "Flat ";
	if (id.Bit(FS_BIT_BGRA_TEXTURE)) desc << "BGRA ";
	if (id.Bit(FS_BIT_UBERSHADER)) desc << "FragUber ";
	if (id.Bit(FS_BIT_DEPTH_TEST_NEVER)) desc << "DepthNever ";
	switch ((ShaderDepalMode)id.Bits(FS_BIT_SHADER_DEPAL_MODE, 2)) {
	case ShaderDepalMode::OFF: break;
	case ShaderDepalMode::NORMAL: desc << "Depal ";  break;
	case ShaderDepalMode::SMOOTHED: desc << "SmoothDepal "; break;
	case ShaderDepalMode::CLUT8_8888: desc << "CLUT8From8888Depal"; break;
	}
	if (id.Bit(FS_BIT_COLOR_WRITEMASK)) desc << "WriteMask ";
	if (id.Bit(FS_BIT_SHADER_TEX_CLAMP)) {
		desc << "TClamp";
		if (id.Bit(FS_BIT_CLAMP_S)) desc << "S";
		if (id.Bit(FS_BIT_CLAMP_T)) desc << "T";
		desc << " ";
	}
	int blendBits = id.Bits(FS_BIT_REPLACE_BLEND, 3);
	if (blendBits) {
		switch (blendBits) {
		case ReplaceBlendType::REPLACE_BLEND_BLUE_TO_ALPHA:
			desc << "BlueToAlpha_" << "A:" << id.Bits(FS_BIT_BLENDFUNC_A, 4);
			break;
		default:
			desc << "ReplaceBlend_" << id.Bits(FS_BIT_REPLACE_BLEND, 3)
				 << "A:" << id.Bits(FS_BIT_BLENDFUNC_A, 4)
				 << "_B:" << id.Bits(FS_BIT_BLENDFUNC_B, 4)
				 << "_Eq:" << id.Bits(FS_BIT_BLENDEQ, 3) << " ";
			break;
		}
	}

	switch (id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2)) {
	case REPLACE_ALPHA_NO: break;
	case REPLACE_ALPHA_YES: desc << "StenToAlpha "; break;
	case REPLACE_ALPHA_DUALSOURCE: desc << "StenToAlphaDual "; break;
	}
	if (id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2) != REPLACE_ALPHA_NO) {
		switch (id.Bits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4)) {
		case STENCIL_VALUE_UNIFORM: desc << "StenUniform "; break;
		case STENCIL_VALUE_ZERO: desc << "Sten0 "; break;
		case STENCIL_VALUE_ONE: desc << "Sten1 "; break;
		case STENCIL_VALUE_KEEP: desc << "StenKeep "; break;
		case STENCIL_VALUE_INVERT: desc << "StenInv "; break;
		case STENCIL_VALUE_INCR_4: desc << "StenIncr4 "; break;
		case STENCIL_VALUE_INCR_8: desc << "StenIncr8 "; break;
		case STENCIL_VALUE_DECR_4: desc << "StenDecr4 "; break;
		case STENCIL_VALUE_DECR_8: desc << "StenDecr8 "; break;
		default: desc << "StenUnknown "; break;
		}
	} else if (id.Bit(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE)) {
		desc << "StenOff ";
	}
	if (id.Bit(FS_BIT_DO_TEXTURE)) {
		switch (id.Bits(FS_BIT_TEXFUNC, 3)) {
		case GE_TEXFUNC_ADD: desc << "TFuncAdd "; break;
		case GE_TEXFUNC_BLEND: desc << "TFuncBlend "; break;
		case GE_TEXFUNC_DECAL: desc << "TFuncDecal "; break;
		case GE_TEXFUNC_MODULATE: desc << "TFuncMod "; break;
		case GE_TEXFUNC_REPLACE: desc << "TFuncRepl "; break;
		default: desc << "TFuncUnk "; break;
		}
	}

	if (id.Bit(FS_BIT_ALPHA_AGAINST_ZERO)) desc << "AlphaTest0 " << alphaTestFuncs[id.Bits(FS_BIT_ALPHA_TEST_FUNC, 3)] << " ";
	else if (id.Bit(FS_BIT_ALPHA_TEST)) desc << "AlphaTest " << alphaTestFuncs[id.Bits(FS_BIT_ALPHA_TEST_FUNC, 3)] << " ";
	if (id.Bit(FS_BIT_COLOR_AGAINST_ZERO)) desc << "ColorTest0 " << alphaTestFuncs[id.Bits(FS_BIT_COLOR_TEST_FUNC, 2)] << " ";  // first 4 match;
	else if (id.Bit(FS_BIT_COLOR_TEST)) desc << "ColorTest " << alphaTestFuncs[id.Bits(FS_BIT_COLOR_TEST_FUNC, 2)] << " ";  // first 4 match
	if (id.Bit(FS_BIT_TEST_DISCARD_TO_ZERO)) desc << "TestDiscardToZero ";
	if (id.Bit(FS_BIT_NO_DEPTH_CANNOT_DISCARD_STENCIL)) desc << "StencilDiscardWorkaround ";
	int logicMode = id.Bits(FS_BIT_REPLACE_LOGIC_OP, 4);
	if ((logicMode != GE_LOGIC_COPY) && !id.Bit(FS_BIT_CLEARMODE)) desc << "RLogic(" << logicFuncs[logicMode] << ")";
	if (id.Bit(FS_BIT_SAMPLE_ARRAY_TEXTURE)) desc << "TexArray ";
	if (id.Bit(FS_BIT_STEREO)) desc << "Stereo ";
	if (id.Bit(FS_BIT_USE_FRAMEBUFFER_FETCH)) desc << "(fetch)";
	return desc.str();
}

bool FragmentIdNeedsFramebufferRead(const FShaderID &id) {
	return id.Bit(FS_BIT_COLOR_WRITEMASK) ||
		id.Bits(FS_BIT_REPLACE_LOGIC_OP, 4) != GE_LOGIC_COPY ||
		(ReplaceBlendType)id.Bits(FS_BIT_REPLACE_BLEND, 3) == REPLACE_BLEND_READ_FRAMEBUFFER;
}

// Here we must take all the bits of the gstate that determine what the fragment shader will
// look like, and concatenate them together into an ID.
void ComputeFragmentShaderID(FShaderID *id_out, const ComputedPipelineState &pipelineState, const Draw::Bugs &bugs) {
	FShaderID id;
	if (gstate.isModeClear()) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id.SetBit(FS_BIT_CLEARMODE);
	} else {
		bool isModeThrough = gstate.isModeThrough();
		bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled() && !isModeThrough;
		bool enableFog = gstate.isFogEnabled() && !isModeThrough;
		bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue();
		bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue();
		bool enableColorDouble = gstate.isColorDoublingEnabled();
		bool doTextureProjection = (gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX && MatrixNeedsProjection(gstate.tgenMatrix, gstate.getUVProjMode()));
		bool doFlatShading = gstate.getShadeMode() == GE_SHADE_FLAT;

		bool enableTexAlpha = gstate.isTextureAlphaUsed();

		bool uberShader = gstate_c.Use(GPU_USE_FRAGMENT_UBERSHADER);

		ShaderDepalMode shaderDepalMode = gstate_c.shaderDepalMode;

		bool colorWriteMask = pipelineState.maskState.applyFramebufferRead;
		ReplaceBlendType replaceBlend = pipelineState.blendState.replaceBlend;
		GELogicOp replaceLogicOpType = pipelineState.logicState.applyFramebufferRead ? pipelineState.logicState.logicOp : GE_LOGIC_COPY;

		SimulateLogicOpType simulateLogicOpType = pipelineState.blendState.simulateLogicOpType;
		ReplaceAlphaType stencilToAlpha = pipelineState.blendState.replaceAlphaWithStencil;

		if (gstate.isTextureMapEnabled()) {
			id.SetBit(FS_BIT_DO_TEXTURE);
			id.SetBits(FS_BIT_TEXFUNC, 3, gstate.getTextureFunction());
			if (gstate_c.needShaderTexClamp) {
				// 4 bits total.
				id.SetBit(FS_BIT_SHADER_TEX_CLAMP);
				id.SetBit(FS_BIT_CLAMP_S, gstate.isTexCoordClampedS());
				id.SetBit(FS_BIT_CLAMP_T, gstate.isTexCoordClampedT());
			}
			id.SetBit(FS_BIT_BGRA_TEXTURE, gstate_c.bgraTexture);
			id.SetBits(FS_BIT_SHADER_DEPAL_MODE, 2, (int)shaderDepalMode);
			id.SetBit(FS_BIT_3D_TEXTURE, gstate_c.curTextureIs3D);
		}

		id.SetBit(FS_BIT_LMODE, lmode);

		if (enableAlphaTest) {
			// 5 bits total.
			id.SetBit(FS_BIT_ALPHA_TEST);
			id.SetBits(FS_BIT_ALPHA_TEST_FUNC, 3, gstate.getAlphaTestFunction());
			id.SetBit(FS_BIT_ALPHA_AGAINST_ZERO, IsAlphaTestAgainstZero());
			id.SetBit(FS_BIT_TEST_DISCARD_TO_ZERO, !NeedsTestDiscard());
		}
		if (enableColorTest) {
			// 4 bits total.
			id.SetBit(FS_BIT_COLOR_TEST);
			id.SetBits(FS_BIT_COLOR_TEST_FUNC, 2, gstate.getColorTestFunction());
			id.SetBit(FS_BIT_COLOR_AGAINST_ZERO, IsColorTestAgainstZero());
			// This is alos set in enableAlphaTest - color test is uncommon, but we can skip discard the same way.
			id.SetBit(FS_BIT_TEST_DISCARD_TO_ZERO, !NeedsTestDiscard());
		}

		id.SetBit(FS_BIT_ENABLE_FOG, enableFog);  // TODO: Will be moved back to the ubershader.

		id.SetBit(FS_BIT_UBERSHADER, uberShader);
		if (!uberShader) {
			id.SetBit(FS_BIT_TEXALPHA, enableTexAlpha);
			id.SetBit(FS_BIT_DOUBLE_COLOR, enableColorDouble);
		}

		id.SetBit(FS_BIT_DO_TEXTURE_PROJ, doTextureProjection);

		// 2 bits
		id.SetBits(FS_BIT_STENCIL_TO_ALPHA, 2, stencilToAlpha);

		if (stencilToAlpha != REPLACE_ALPHA_NO) {
			// 4 bits
			id.SetBits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4, ReplaceAlphaWithStencilType());
		}

		// 2 bits.
		id.SetBits(FS_BIT_SIMULATE_LOGIC_OP_TYPE, 2, simulateLogicOpType);

		// 4 bits. Set to GE_LOGIC_COPY if not used, which does nothing in the shader generator.
		id.SetBits(FS_BIT_REPLACE_LOGIC_OP, 4, (int)replaceLogicOpType);

		// If replaceBlend == REPLACE_BLEND_STANDARD (or REPLACE_BLEND_NO) nothing is done, so we kill these bits.
		if (replaceBlend == REPLACE_BLEND_BLUE_TO_ALPHA) {
			id.SetBits(FS_BIT_REPLACE_BLEND, 3, replaceBlend);
			id.SetBits(FS_BIT_BLENDFUNC_A, 4, gstate.getBlendFuncA());
		} else if (replaceBlend > REPLACE_BLEND_STANDARD) {
			// 3 bits.
			id.SetBits(FS_BIT_REPLACE_BLEND, 3, replaceBlend);
			// 11 bits total.
			id.SetBits(FS_BIT_BLENDEQ, 3, gstate.getBlendEq());
			id.SetBits(FS_BIT_BLENDFUNC_A, 4, gstate.getBlendFuncA());
			id.SetBits(FS_BIT_BLENDFUNC_B, 4, gstate.getBlendFuncB());
		}
		id.SetBit(FS_BIT_FLATSHADE, doFlatShading);
		id.SetBit(FS_BIT_COLOR_WRITEMASK, colorWriteMask);

		// All framebuffers are array textures in Vulkan now.
		if (gstate_c.textureIsArray && gstate_c.Use(GPU_USE_FRAMEBUFFER_ARRAYS)) {
			id.SetBit(FS_BIT_SAMPLE_ARRAY_TEXTURE);
		}

		// Stereo support
		if (gstate_c.Use(GPU_USE_SINGLE_PASS_STEREO)) {
			id.SetBit(FS_BIT_STEREO);
		}

		if (g_Config.bVendorBugChecksEnabled) {
			if (bugs.Has(Draw::Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL_ADRENO) || bugs.Has(Draw::Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL_MALI)) {
				// On Adreno, the workaround is safe, so we do simple checks.
				bool stencilWithoutDepth = (!gstate.isDepthTestEnabled() || !gstate.isDepthWriteEnabled()) && !IsStencilTestOutputDisabled();
				if (stencilWithoutDepth) {
					id.SetBit(FS_BIT_NO_DEPTH_CANNOT_DISCARD_STENCIL, stencilWithoutDepth);
				}
			}
		}

		// Forcibly disable NEVER + depth-write on Mali.
		// TODO: Take this from computed depth test instead of directly from the gstate.
		// That will take more refactoring though.
		if (bugs.Has(Draw::Bugs::NO_DEPTH_CANNOT_DISCARD_STENCIL_MALI) &&
			gstate.getDepthTestFunction() == GE_COMP_NEVER && gstate.isDepthTestEnabled()) {
			id.SetBit(FS_BIT_DEPTH_TEST_NEVER);
		}

		// In case the USE flag changes (for example, in multisampling we might disable input attachments),
		// we don't want to accidentally use the wrong cached shader here. So moved it to a bit.
		if (FragmentIdNeedsFramebufferRead(id)) {
			if (gstate_c.Use(GPU_USE_FRAMEBUFFER_FETCH)) {
				id.SetBit(FS_BIT_USE_FRAMEBUFFER_FETCH);
			}
		}
	}

	*id_out = id;
}

std::string GeometryShaderDesc(const GShaderID &id) {
	std::stringstream desc;
	desc << StringFromFormat("%08x:%08x ", id.d[1], id.d[0]);
	if (id.Bit(GS_BIT_ENABLED)) desc << "ENABLED ";
	if (id.Bit(GS_BIT_DO_TEXTURE)) desc << "TEX ";
	if (id.Bit(GS_BIT_LMODE)) desc << "LM ";
	return desc.str();
}

void ComputeGeometryShaderID(GShaderID *id_out, const Draw::Bugs &bugs, int prim) {
	GShaderID id;
	// Early out.
	if (!gstate_c.Use(GPU_USE_GS_CULLING)) {
		*id_out = id;
		return;
	}

	bool isModeThrough = gstate.isModeThrough();
	bool isCurve = gstate_c.submitType != SubmitType::DRAW;
	bool isTriangle = prim == GE_PRIM_TRIANGLES || prim == GE_PRIM_TRIANGLE_FAN || prim == GE_PRIM_TRIANGLE_STRIP;

	bool vertexRangeCulling = !isCurve;
	bool clipClampedDepth = gstate_c.Use(GPU_USE_DEPTH_CLAMP) && !gstate_c.Use(GPU_USE_CLIP_DISTANCE);

	// Only use this for triangle primitives, and if we actually need it.
	if ((!vertexRangeCulling && !clipClampedDepth) || isModeThrough || !isTriangle) {
		*id_out = id;
		return;
	}

	id.SetBit(GS_BIT_ENABLED, true);
	// Vertex range culling doesn't seem tno happen for spline/bezier, see #11692.
	id.SetBit(GS_BIT_CURVE, isCurve);

	if (gstate.isModeClear()) {
		// No attribute bits.
	} else {
		bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled() && !isModeThrough;

		id.SetBit(GS_BIT_LMODE, lmode);
		if (gstate.isTextureMapEnabled()) {
			id.SetBit(GS_BIT_DO_TEXTURE);
		}
	}

	*id_out = id;
}
