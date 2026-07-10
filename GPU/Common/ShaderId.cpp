#include <string>
#include <vector>

#include "Common/GPU/thin3d.h"
#include "Common/StringUtils.h"
#include "Common/Data/Text/StringWriter.h"
#include "Common/BitSet.h"
#include "Core/Config.h"

#include "GPU/ge_constants.h"
#include "GPU/GPU.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/DrawEngineCommon.h"  // Just for ClipInfoFlags

std::string ShaderID::ToDebugString() const {
	return StringFromFormat("%08x:%08x", d >> 32, d & 0xFFFFFFFF);
}

std::string VShaderID::Description() const {
	char buffer[512];
	StringWriter desc(buffer, sizeof(buffer));

	desc.W(ToDebugString()).C(" ");
	if (Bit(VS_BIT_IS_THROUGH)) desc.C("THR ");
	if (Bit(VS_BIT_USE_HW_TRANSFORM)) desc.C("HWX "); else desc.C("SWX ");
	if (Bit(VS_BIT_HAS_NORMAL)) desc.C("N ");
	if (Bit(VS_BIT_HAS_TEXCOORD)) desc.C("T ");
	if (Bit(VS_BIT_HAS_COLOR)) desc.C("C ");
	if (Bit(VS_BIT_LMODE)) desc.C("LM ");
	if (Bit(VS_BIT_NORM_REVERSE)) desc.C("RevN ");
	if (Bit(VS_BIT_FLATSHADE)) desc.C("Flat ");
	if (Bits(VS_BIT_MATERIAL_UPDATE, 3)) desc.C("MatUp:").F("%d", Bits(VS_BIT_MATERIAL_UPDATE, 3)).C(" ");

	int uvgMode = Bits(VS_BIT_UVGEN_MODE, 2);
	static constexpr std::string_view uvgModes[4] = {"UV ", "UVMtx ", "UVEnv ", "UVUnk "};
	if (uvgMode) desc.W(uvgModes[uvgMode]);
	if (uvgMode == GE_TEXMAP_TEXTURE_MATRIX) {
		int uvprojMode = Bits(VS_BIT_UVPROJ_MODE, 2);
		static constexpr std::string_view uvprojModes[4] = { "TexProjPos ", "TexProjUV ", "TexProjNNrm ", "TexProjNrm " };
		desc.W(uvprojModes[uvprojMode]);
	}

	if (Bit(VS_BIT_ENABLE_BONES)) desc.F("Bones:%d ", Bits(VS_BIT_BONES, 3) + 1);
	if (Bits(VS_BIT_WEIGHT_FMTSCALE, 2)) desc.F("WScale:%d ", Bits(VS_BIT_WEIGHT_FMTSCALE, 2));

	int ls0 = Bits(VS_BIT_LS0, 2);
	int ls1 = Bits(VS_BIT_LS1, 2);

	if (Bit(VS_BIT_FS_MINMAX_DISCARD)) desc.C("FSMinMax ");
	if (Bit(VS_BIT_FS_DEPTH_CLAMP)) desc.C("FSDepthClamp ");

	// Lights
	if (Bit(VS_BIT_LIGHTING_ENABLE)) {
		desc.C("Light: ");
	}
	if (Bit(VS_BIT_LIGHT_UBERSHADER)) {
		desc.C("LightUberShader ");
	}
	for (int i = 0; i < 4; i++) {
		bool enabled = Bit(VS_BIT_LIGHT0_ENABLE + i) && Bit(VS_BIT_LIGHTING_ENABLE);
		if (enabled || (uvgMode == GE_TEXMAP_ENVIRONMENT_MAP && (ls0 == i || ls1 == i))) {
			desc.F("%d: ", i);
			desc.F("c:%d t:%d ", Bits(VS_BIT_LIGHT0_COMP + 4 * i, 2), Bits(VS_BIT_LIGHT0_TYPE + 4 * i, 2));
		}
	}

	if (Bit(VS_BIT_SIMPLE_STEREO)) desc.C("SimpleStereo ");
	if (Bit(VS_BIT_VERTEX_RANGE_CULLING)) desc.C("RangeCull ");

	return desc.as_string();
}

void ComputeVertexShaderID(VShaderID *id_out, u32 vertType, bool useHWTransform, bool weightsAsFloat, bool useSkinInDecode, ClipInfoFlags clipInfoFlags) {
	const bool isModeThrough = (vertType & GE_VTYPE_THROUGH) != 0;
	bool doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool doShadeMapping = doTexture && (gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP);
	bool doFlatShading = gstate.getShadeMode() == GE_SHADE_FLAT && !gstate.isModeClear();

	bool vtypeHasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	bool vtypeHasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0;
	bool vtypeHasTexcoord = (vertType & GE_VTYPE_TC_MASK) != 0;

	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled() && !isModeThrough && !gstate.isModeClear();
	bool vertexRangeCulling = gstate_c.Use(GPU_USE_VS_RANGE_CULLING) &&
		!isModeThrough && gstate_c.submitType == SubmitType::DRAW;  // neither hw nor sw spline/bezier. See #11692

	VShaderID id;
	id.SetBit(VS_BIT_LMODE, lmode);
	id.SetBit(VS_BIT_IS_THROUGH, isModeThrough);
	id.SetBit(VS_BIT_VERTEX_RANGE_CULLING, vertexRangeCulling);

	if (!isModeThrough && gstate_c.Use(GPU_USE_SINGLE_PASS_STEREO)) {
		id.SetBit(VS_BIT_SIMPLE_STEREO);
	}

	if (doTexture) {
		// UV generation mode. doShadeMapping is implicitly stored here.
		id.SetBits(VS_BIT_UVGEN_MODE, 2, gstate.getUVGenMode());
	}

	if (useHWTransform) {
		_dbg_assert_(!isModeThrough);
		id.SetBit(VS_BIT_USE_HW_TRANSFORM);

		id.SetBit(VS_BIT_HAS_NORMAL, vtypeHasNormal);
		id.SetBit(VS_BIT_HAS_COLOR, vtypeHasColor);
		id.SetBit(VS_BIT_HAS_TEXCOORD, vtypeHasTexcoord);

		// The next bits are used differently depending on UVgen mode
		if (gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX) {
			id.SetBits(VS_BIT_UVPROJ_MODE, 2, gstate.getUVProjMode());
		} else if (doShadeMapping) {
			id.SetBits(VS_BIT_LS0, 2, gstate.getUVLS0());
			id.SetBits(VS_BIT_LS1, 2, gstate.getUVLS1());
		}

		// Bones.
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
	}

	if (clipInfoFlags & ClipInfoFlags::DepthClampFragment) {
		id.SetBit(VS_BIT_FS_DEPTH_CLAMP);
	}
	if (clipInfoFlags & ClipInfoFlags::MinMaxZDiscard) {
		id.SetBit(VS_BIT_FS_MINMAX_DISCARD);
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

std::string FShaderID::Description() const {
	char buffer[512];
	StringWriter desc(buffer, sizeof(buffer));

	desc.W(ToDebugString()).C(" ");
	if (Bit(FS_BIT_CLEARMODE)) desc.C("Clear ");
	if (Bit(FS_BIT_DO_TEXTURE)) {
		desc.W(Bit(FS_BIT_3D_TEXTURE) ? "Tex3D" : "Tex");
		switch (Bits(FS_BIT_TEXFUNC, 3)) {
		case GE_TEXFUNC_ADD: desc.C("(TFuncAdd) "); break;
		case GE_TEXFUNC_BLEND: desc.C("(TFuncBlend) "); break;
		case GE_TEXFUNC_DECAL: desc.C("(TFuncDecal) "); break;
		case GE_TEXFUNC_MODULATE: desc.C("(TFuncMod) "); break;
		case GE_TEXFUNC_REPLACE: desc.C("(TFuncRepl) "); break;
		default: desc.C("(TFuncUnk) "); break;
		}
	}
	if (Bit(FS_BIT_LMODE)) desc.C("LM ");
	if (Bit(FS_BIT_ENABLE_FOG)) desc.C("Fog ");
	if (Bit(FS_BIT_FLATSHADE)) desc.C("Flat ");
	if (Bit(FS_BIT_DEPTH_TEST_NEVER)) desc.C("DepthNever ");
	if (Bit(FS_BIT_COLOR_WRITEMASK)) desc.C("WriteMask ");
	if (Bit(FS_BIT_SHADER_TEX_CLAMP)) {
		desc.C("TClamp");
		if (Bit(FS_BIT_CLAMP_S)) desc.C("S");
		if (Bit(FS_BIT_CLAMP_T)) desc.C("T");
		desc.C(" ");
	}
	int blendBits = Bits(FS_BIT_REPLACE_BLEND, 3);
	if (blendBits) {
		switch (blendBits) {
		case ReplaceBlendType::REPLACE_BLEND_BLUE_TO_ALPHA:
			desc.C("BlueToAlpha_" "A:").F("%d ", Bits(FS_BIT_BLENDFUNC_A, 4));
			break;
		default:
			desc.C("ReplaceBlend_").F("%d ", Bits(FS_BIT_REPLACE_BLEND, 3))
				 .C("A:").F("%d ", Bits(FS_BIT_BLENDFUNC_A, 4))
				 .C("_B:").F("%d ", Bits(FS_BIT_BLENDFUNC_B, 4))
				 .C("_Eq:").F("%d ", Bits(FS_BIT_BLENDEQ, 3));
			break;
		}
	}

	switch (Bits(FS_BIT_STENCIL_TO_ALPHA, 2)) {
	case REPLACE_ALPHA_NO: break;
	case REPLACE_ALPHA_YES: desc.C("StenToAlpha "); break;
	case REPLACE_ALPHA_DUALSOURCE: desc.C("StenToAlphaDual "); break;
	default: desc.C("StenToAlphaUnknown "); break;  // bad
	}

	if (Bits(FS_BIT_STENCIL_TO_ALPHA, 2) != REPLACE_ALPHA_NO) {
		switch (Bits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4)) {
		case STENCIL_VALUE_UNIFORM: desc.C("StenUniform "); break;
		case STENCIL_VALUE_ZERO: desc.C("Sten0 "); break;
		case STENCIL_VALUE_ONE: desc.C("Sten1 "); break;
		case STENCIL_VALUE_KEEP: desc.C("StenKeep "); break;
		case STENCIL_VALUE_INVERT: desc.C("StenInv "); break;
		case STENCIL_VALUE_INCR_4BIT: desc.C("StenIncr4 "); break;
		case STENCIL_VALUE_INCR_8BIT: desc.C("StenIncr8 "); break;
		case STENCIL_VALUE_DECR_4BIT: desc.C("StenDecr4 "); break;
		case STENCIL_VALUE_DECR_8BIT: desc.C("StenDecr8 "); break;
		default: desc.C("StenUnknown "); break;
		}
	} else if (Bit(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE)) {
		desc.C("StenOff ");
	}

	if (Bit(FS_BIT_ALPHA_AGAINST_ZERO)) desc.C("AlphaTest0 ").W(alphaTestFuncs[Bits(FS_BIT_ALPHA_TEST_FUNC, 3)]).C(" ");
	else if (Bit(FS_BIT_ALPHA_TEST)) desc.C("AlphaTest ").W(alphaTestFuncs[Bits(FS_BIT_ALPHA_TEST_FUNC, 3)]).C(" ");
	if (Bit(FS_BIT_COLOR_AGAINST_ZERO)) desc.C("ColorTest0 ").W(alphaTestFuncs[Bits(FS_BIT_COLOR_TEST_FUNC, 2)]).C(" ");  // first 4 match;
	else if (Bit(FS_BIT_COLOR_TEST)) desc.C("ColorTest ").W(alphaTestFuncs[Bits(FS_BIT_COLOR_TEST_FUNC, 2)]).C(" ");  // first 4 match
	if (Bit(FS_BIT_TEST_DISCARD_TO_ZERO)) desc.C("TestDiscardToZero ");
	if (Bit(FS_BIT_NO_DEPTH_CANNOT_DISCARD_STENCIL)) desc.C("StencilDiscardWorkaround ");
	int logicMode = Bits(FS_BIT_REPLACE_LOGIC_OP, 4);
	if ((logicMode != GE_LOGIC_COPY) && !Bit(FS_BIT_CLEARMODE)) desc.C("RLogic(").W(logicFuncs[logicMode]).C(")");
	if (Bit(FS_BIT_SAMPLE_ARRAY_TEXTURE)) desc.C("TexArray ");
	if (Bit(FS_BIT_STEREO)) desc.C("Stereo ");
	if (Bit(FS_BIT_USE_FRAMEBUFFER_FETCH)) desc.C("(fetch)");
	if (Bit(FS_BIT_MINMAX_DISCARD)) desc.C("FragMinMaxDiscard ");
	if (Bit(FS_BIT_DEPTH_CLAMP)) desc.C("FragDepthClamp ");

	const ShaderDepalMode depalMode = (ShaderDepalMode)Bits(FS_BIT_SHADER_DEPAL_MODE, 2);
	switch (depalMode) {
	case ShaderDepalMode::OFF: break;
	case ShaderDepalMode::NORMAL: desc.C("Depal(");
	{
		const GEBufferFormat shaderDepalFormat = (GEBufferFormat)Bits(FS_BIT_SHADER_DEPAL_FORMAT, 3);
		desc.W(GeBufferFormatToString(shaderDepalFormat)).C(") ");
		break;
	}
	case ShaderDepalMode::SMOOTHED: desc.C("SmoothDepal "); break;
	case ShaderDepalMode::CLUT8_8888: desc.C("CLUT8From8888Depal"); break;
	}

	return desc.as_string();
}

bool FragmentIdNeedsFramebufferRead(const FShaderID &id) {
	return id.Bit(FS_BIT_COLOR_WRITEMASK) ||
		id.Bits(FS_BIT_REPLACE_LOGIC_OP, 4) != GE_LOGIC_COPY ||
		(ReplaceBlendType)id.Bits(FS_BIT_REPLACE_BLEND, 3) == REPLACE_BLEND_READ_FRAMEBUFFER;
}

inline u32 SanitizeBlendMode(GEBlendMode mode) {
	if (mode > GE_BLENDMODE_ABSDIFF)
		return GE_BLENDMODE_MUL_AND_ADD;  // Not sure what the undefined modes are.
	else
		return mode;
}

// Here we must take all the bits of the gstate that determine what the fragment shader will
// look like, and concatenate them together into an ID.
void ComputeFragmentShaderID(FShaderID *id_out, const ComputedPipelineState &pipelineState, const Draw::Bugs &bugs, ClipInfoFlags clipInfoFlags) {
	FShaderID id;
	bool isModeThrough = gstate.isModeThrough();

	// NOTE: This check MUST be identical to the one in ComputeVertexShaderID, otherwise we might get mismatches between VS and FS and end up with no shader at all.
	if (!isModeThrough) {
		if (clipInfoFlags & ClipInfoFlags::DepthClampFragment) {
			id.SetBit(FS_BIT_DEPTH_CLAMP);
		}
		if (clipInfoFlags & ClipInfoFlags::MinMaxZDiscard) {
			id.SetBit(FS_BIT_MINMAX_DISCARD);
		}
	} else {
		_dbg_assert_(0 == (clipInfoFlags & (ClipInfoFlags::DepthClampFragment | ClipInfoFlags::MinMaxZDiscard)));
	}

	if (gstate.isModeClear()) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id.SetBit(FS_BIT_CLEARMODE);
	} else {
		bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled() && !isModeThrough;
		bool enableFog = gstate.isFogEnabled() && !isModeThrough;
		bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue();
		bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue();
		bool enableColorDouble = gstate.isColorDoublingEnabled();
		bool doTextureProjection = (gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX && MatrixNeedsProjection(gstate.tgenMatrix, gstate.getUVProjMode()));
		bool doFlatShading = gstate.getShadeMode() == GE_SHADE_FLAT;

		bool enableTexAlpha = gstate.isTextureAlphaUsed();

		ShaderDepalMode shaderDepalMode = gstate_c.shaderDepalMode;
		GEBufferFormat shaderDepalFormat = {};
		if (shaderDepalMode == ShaderDepalMode::NORMAL) {
			shaderDepalFormat = gstate_c.depalTextureFormat;
		}

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
			id.SetBits(FS_BIT_SHADER_DEPAL_MODE, 2, (int)shaderDepalMode);
			id.SetBits(FS_BIT_SHADER_DEPAL_FORMAT, 3, (int)shaderDepalFormat);
			id.SetBit(FS_BIT_3D_TEXTURE, gstate_c.curTextureIs3D);
			// All framebuffers are array textures in Vulkan now.
			if (gstate_c.textureIsArray && gstate_c.Use(GPU_USE_FRAMEBUFFER_ARRAYS)) {
				id.SetBit(FS_BIT_SAMPLE_ARRAY_TEXTURE);
			}
			id.SetBit(FS_BIT_DO_TEXTURE_PROJ, doTextureProjection);
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
			// This is also set in enableAlphaTest - color test is uncommon, but we can skip discard the same way.
			id.SetBit(FS_BIT_TEST_DISCARD_TO_ZERO, !NeedsTestDiscard());
		}

		id.SetBit(FS_BIT_ENABLE_FOG, enableFog);  // TODO: Will be moved back to the ubershader.

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
			id.SetBits(FS_BIT_BLENDEQ, 3, SanitizeBlendMode(gstate.getBlendEq()));
			id.SetBits(FS_BIT_BLENDFUNC_A, 4, gstate.getBlendFuncA());
			id.SetBits(FS_BIT_BLENDFUNC_B, 4, gstate.getBlendFuncB());
		}
		id.SetBit(FS_BIT_FLATSHADE, doFlatShading);
		id.SetBit(FS_BIT_COLOR_WRITEMASK, colorWriteMask);

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

		// Various conditions that require per-pixel depth manipulation (very expensive!)
		bool needMinMaxClipping = gstate.getDepthRangeMin() != 0 && gstate.getDepthRangeMax() != 0xFFFF && !isModeThrough;

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

std::vector<std::string> ToSortedDebugShaderIdVec(std::vector<uint64_t> ids) {
	// Reverse the bits so that the sort order matches the importance order.
	for (auto &id : ids) {
		id = ReverseBits64(id);
	}
	std::sort(ids.begin(), ids.end());
	// Reverse the bits back to get the original IDs.
	for (auto &id : ids) {
		id = ReverseBits64(id);
	}
	std::vector<std::string> strIds;
	for (auto &id : ids) {
		ShaderID shaderId;
		shaderId.FromUint64(id);
		std::string idStr;
		shaderId.ToString(&idStr);
		strIds.push_back(idStr);
	}
	return strIds;
}
