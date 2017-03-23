#include <string>
#include <sstream>

#include "Common/StringUtils.h"
#include "Core/Config.h"

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexDecoderCommon.h"

std::string VertexShaderDesc(const ShaderID &id) {
	std::stringstream desc;
	desc << StringFromFormat("%08x:%08x ", id.d[1], id.d[0]);
	if (id.Bit(VS_BIT_IS_THROUGH)) desc << "THR ";
	if (id.Bit(VS_BIT_USE_HW_TRANSFORM)) desc << "HWX ";
	if (id.Bit(VS_BIT_HAS_COLOR)) desc << "C ";
	if (id.Bit(VS_BIT_HAS_TEXCOORD)) desc << "T ";
	if (id.Bit(VS_BIT_HAS_NORMAL)) desc << "N ";
	if (id.Bit(VS_BIT_LMODE)) desc << "LM ";
	if (id.Bit(VS_BIT_ENABLE_FOG)) desc << "Fog ";
	if (id.Bit(VS_BIT_NORM_REVERSE)) desc << "RevN ";
	if (id.Bit(VS_BIT_DO_TEXTURE)) desc << "Tex ";
	if (id.Bit(VS_BIT_DO_TEXTURE_TRANSFORM)) desc << "TexProj ";
	int uvgMode = id.Bits(VS_BIT_UVGEN_MODE, 2);
	const char *uvgModes[4] = { "UV ", "UVMtx ", "UVEnv ", "UVUnk " };
	int ls0 = id.Bits(VS_BIT_LS0, 2);
	int ls1 = id.Bits(VS_BIT_LS1, 2);

	if (uvgMode) desc << uvgModes[uvgMode];
	if (id.Bit(VS_BIT_ENABLE_BONES)) desc << "Bones:" << (id.Bits(VS_BIT_BONES, 3) + 1) << " ";
	// Lights
	if (id.Bit(VS_BIT_LIGHTING_ENABLE)) {
		desc << "Light: ";
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
	if (id.Bit(VS_BIT_NORM_REVERSE_TESS)) desc << "TessRevN ";

	// TODO: More...

	return desc.str();
}

void ComputeVertexShaderID(ShaderID *id_out, u32 vertType, bool useHWTransform) {
	bool doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool doTextureTransform = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
	bool doShadeMapping = doTexture && (gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP);
	bool doFlatShading = gstate.getShadeMode() == GE_SHADE_FLAT && !gstate.isModeClear();

	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	bool hasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0;
	bool hasTexcoord = (vertType & GE_VTYPE_TC_MASK) != 0;

	bool doBezier = gstate_c.bezier;
	bool doSpline = gstate_c.spline;
	bool hasColorTess = (gstate.vertType & GE_VTYPE_COL_MASK) != 0 && (doBezier || doSpline);
	bool hasTexcoordTess = (gstate.vertType & GE_VTYPE_TC_MASK) != 0 && (doBezier || doSpline);

	bool enableFog = gstate.isFogEnabled() && !gstate.isModeThrough() && !gstate.isModeClear();
	bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled();
	// lmode: && !isModeThrough!?

	ShaderID id;
	id.SetBit(VS_BIT_LMODE, lmode);
	id.SetBit(VS_BIT_IS_THROUGH, gstate.isModeThrough());
	id.SetBit(VS_BIT_ENABLE_FOG, enableFog);
	id.SetBit(VS_BIT_HAS_COLOR, hasColor);

	if (doTexture) {
		id.SetBit(VS_BIT_DO_TEXTURE);
		id.SetBit(VS_BIT_DO_TEXTURE_TRANSFORM, doTextureTransform);
	}

	if (useHWTransform) {
		id.SetBit(VS_BIT_USE_HW_TRANSFORM);
		id.SetBit(VS_BIT_HAS_NORMAL, hasNormal);

		// UV generation mode. doShadeMapping is implicitly stored here.
		id.SetBits(VS_BIT_UVGEN_MODE, 2, gstate.getUVGenMode());

		// The next bits are used differently depending on UVgen mode
		if (doTextureTransform) {
			id.SetBits(VS_BIT_UVPROJ_MODE, 2, gstate.getUVProjMode());
		} else if (doShadeMapping) {
			id.SetBits(VS_BIT_LS0, 2, gstate.getUVLS0());
			id.SetBits(VS_BIT_LS1, 2, gstate.getUVLS1());
		}

		// Bones.
		bool enableBones = vertTypeIsSkinningEnabled(vertType);
		id.SetBit(VS_BIT_ENABLE_BONES, enableBones);
		if (enableBones) {
			id.SetBits(VS_BIT_BONES, 3, TranslateNumBones(vertTypeGetNumBoneWeights(vertType)) - 1);
			// 2 bits. We should probably send in the weight scalefactor as a uniform instead,
			// or simply preconvert all weights to floats.
			id.SetBits(VS_BIT_WEIGHT_FMTSCALE, 2, (vertType & GE_VTYPE_WEIGHT_MASK) >> GE_VTYPE_WEIGHT_SHIFT);
		}

		// Okay, d[1] coming up. ==============
		if (gstate.isLightingEnabled() || doShadeMapping) {
			// doShadeMapping is stored as UVGenMode, so this is enough for isLightingEnabled.
			if (gstate.isLightingEnabled()) {
				id.SetBits(VS_BIT_MATERIAL_UPDATE, 3, gstate.getMaterialUpdate() & 7);
				id.SetBit(VS_BIT_LIGHTING_ENABLE);
			}
			// Light bits
			for (int i = 0; i < 4; i++) {
				bool chanEnabled = gstate.isLightChanEnabled(i) != 0 && gstate.isLightingEnabled();
				id.SetBit(VS_BIT_LIGHT0_ENABLE + i, chanEnabled);
				if (chanEnabled || (doShadeMapping && (gstate.getUVLS0() == i || gstate.getUVLS1() == i))) {
					id.SetBits(VS_BIT_LIGHT0_COMP + 4 * i, 2, gstate.getLightComputation(i));
					id.SetBits(VS_BIT_LIGHT0_TYPE + 4 * i, 2, gstate.getLightType(i));
				}
			}
		}

		id.SetBit(VS_BIT_NORM_REVERSE, gstate.areNormalsReversed());
		id.SetBit(VS_BIT_HAS_TEXCOORD, hasTexcoord);

		if (g_Config.bHardwareTessellation) {
			id.SetBit(VS_BIT_BEZIER, doBezier);
			id.SetBit(VS_BIT_SPLINE, doSpline);
			id.SetBit(VS_BIT_HAS_COLOR_TESS, hasColorTess);
			id.SetBit(VS_BIT_HAS_TEXCOORD_TESS, hasTexcoordTess);
			id.SetBit(VS_BIT_NORM_REVERSE_TESS, gstate.isPatchNormalsReversed());
		}
	}

	id.SetBit(VS_BIT_FLATSHADE, doFlatShading);

	*id_out = id;
}


static const char *alphaTestFuncs[] = { "NEVER", "ALWAYS", "==", "!=", "<", "<=", ">", ">=" };

static bool MatrixNeedsProjection(const float m[12]) {
	return m[2] != 0.0f || m[5] != 0.0f || m[8] != 0.0f || m[11] != 1.0f;
}

std::string FragmentShaderDesc(const ShaderID &id) {
	std::stringstream desc;
	desc << StringFromFormat("%08x:%08x ", id.d[1], id.d[0]);
	if (id.Bit(FS_BIT_CLEARMODE)) desc << "Clear ";
	if (id.Bit(FS_BIT_DO_TEXTURE)) desc << "Tex ";
	if (id.Bit(FS_BIT_DO_TEXTURE_PROJ)) desc << "TexProj ";
	if (id.Bit(FS_BIT_TEXALPHA)) desc << "TexAlpha ";
	if (id.Bit(FS_BIT_TEXTURE_AT_OFFSET)) desc << "TexOffs ";
	if (id.Bit(FS_BIT_LMODE)) desc << "LM ";
	if (id.Bit(FS_BIT_ENABLE_FOG)) desc << "Fog ";
	if (id.Bit(FS_BIT_COLOR_DOUBLE)) desc << "2x ";
	if (id.Bit(FS_BIT_FLATSHADE)) desc << "Flat ";
	if (id.Bit(FS_BIT_BGRA_TEXTURE)) desc << "BGRA ";
	if (id.Bit(FS_BIT_SHADER_TEX_CLAMP)) {
		desc << "TClamp";
		if (id.Bit(FS_BIT_CLAMP_S)) desc << "S";
		if (id.Bit(FS_BIT_CLAMP_T)) desc << "T";
		desc << " ";
	}
	if (id.Bits(FS_BIT_REPLACE_BLEND, 3)) {
		desc << "ReplaceBlend_" << id.Bits(FS_BIT_REPLACE_BLEND, 3) << "A:" << id.Bits(FS_BIT_BLENDFUNC_A, 4) << "_B:" << id.Bits(FS_BIT_BLENDFUNC_B, 4) << "_Eq:" << id.Bits(FS_BIT_BLENDEQ, 3) << " ";
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
		case STENCIL_VALUE_DECR_8: desc << "StenDecr4 "; break;
		default: desc << "StenUnknown"; break;
		}
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

	return desc.str();
}

// Here we must take all the bits of the gstate that determine what the fragment shader will
// look like, and concatenate them together into an ID.
void ComputeFragmentShaderID(ShaderID *id_out) {
	ShaderID id;
	if (gstate.isModeClear()) {
		// We only need one clear shader, so let's ignore the rest of the bits.
		id.SetBit(FS_BIT_CLEARMODE);
	} else {
		bool isModeThrough = gstate.isModeThrough();
		bool lmode = gstate.isUsingSecondaryColor() && gstate.isLightingEnabled() && !isModeThrough;
		bool enableFog = gstate.isFogEnabled() && !isModeThrough;
		bool enableAlphaTest = gstate.isAlphaTestEnabled() && !IsAlphaTestTriviallyTrue() && !g_Config.bDisableAlphaTest;
		bool enableColorTest = gstate.isColorTestEnabled() && !IsColorTestTriviallyTrue();
		bool enableColorDoubling = gstate.isColorDoublingEnabled() && gstate.isTextureMapEnabled();
		bool doTextureProjection = (gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX && MatrixNeedsProjection(gstate.tgenMatrix));
		bool doTextureAlpha = gstate.isTextureAlphaUsed();
		bool doFlatShading = gstate.getShadeMode() == GE_SHADE_FLAT;

		ReplaceBlendType replaceBlend = ReplaceBlendWithShader(gstate_c.allowShaderBlend, gstate.FrameBufFormat());
		ReplaceAlphaType stencilToAlpha = ReplaceAlphaWithStencil(replaceBlend);

		// All texfuncs except replace are the same for RGB as for RGBA with full alpha.
		// Note that checking this means that we must dirty the fragment shader ID whenever textureFullAlpha changes.
		if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE)
			doTextureAlpha = false;

		if (gstate.isTextureMapEnabled()) {
			id.SetBit(FS_BIT_DO_TEXTURE);
			id.SetBits(FS_BIT_TEXFUNC, 3, gstate.getTextureFunction());
			id.SetBit(FS_BIT_TEXALPHA, doTextureAlpha & 1); // rgb or rgba
			if (gstate_c.needShaderTexClamp) {
				bool textureAtOffset = gstate_c.curTextureXOffset != 0 || gstate_c.curTextureYOffset != 0;
				// 4 bits total.
				id.SetBit(FS_BIT_SHADER_TEX_CLAMP);
				id.SetBit(FS_BIT_CLAMP_S, gstate.isTexCoordClampedS());
				id.SetBit(FS_BIT_CLAMP_T, gstate.isTexCoordClampedT());
				id.SetBit(FS_BIT_TEXTURE_AT_OFFSET, textureAtOffset);
			}
			id.SetBit(FS_BIT_BGRA_TEXTURE, gstate_c.bgraTexture);
		}

		id.SetBit(FS_BIT_LMODE, lmode);
		if (enableAlphaTest) {
			// 5 bits total.
			id.SetBit(FS_BIT_ALPHA_TEST);
			id.SetBits(FS_BIT_ALPHA_TEST_FUNC, 3, gstate.getAlphaTestFunction());
			id.SetBit(FS_BIT_ALPHA_AGAINST_ZERO, IsAlphaTestAgainstZero());
		}
		if (enableColorTest) {
			// 4 bits total.
			id.SetBit(FS_BIT_COLOR_TEST);
			id.SetBits(FS_BIT_COLOR_TEST_FUNC, 2, gstate.getColorTestFunction());
			id.SetBit(FS_BIT_COLOR_AGAINST_ZERO, IsColorTestAgainstZero());
		}

		id.SetBit(FS_BIT_ENABLE_FOG, enableFog);
		id.SetBit(FS_BIT_DO_TEXTURE_PROJ, doTextureProjection);
		id.SetBit(FS_BIT_COLOR_DOUBLE, enableColorDoubling);

		// 2 bits
		id.SetBits(FS_BIT_STENCIL_TO_ALPHA, 2, stencilToAlpha);

		if (stencilToAlpha != REPLACE_ALPHA_NO) {
			// 4 bits
			id.SetBits(FS_BIT_REPLACE_ALPHA_WITH_STENCIL_TYPE, 4, ReplaceAlphaWithStencilType());
		}

		// 2 bits.
		id.SetBits(FS_BIT_REPLACE_LOGIC_OP_TYPE, 2, ReplaceLogicOpType());

		// If replaceBlend == REPLACE_BLEND_STANDARD (or REPLACE_BLEND_NO) nothing is done, so we kill these bits.
		if (replaceBlend > REPLACE_BLEND_STANDARD) {
			// 3 bits.
			id.SetBits(FS_BIT_REPLACE_BLEND, 3, replaceBlend);
			// 11 bits total.
			id.SetBits(FS_BIT_BLENDEQ, 3, gstate.getBlendEq());
			id.SetBits(FS_BIT_BLENDFUNC_A, 4, gstate.getBlendFuncA());
			id.SetBits(FS_BIT_BLENDFUNC_B, 4, gstate.getBlendFuncB());
		}
		id.SetBit(FS_BIT_FLATSHADE, doFlatShading);
	}

	*id_out = id;
}
