#include <string>
#include <sstream>

#include "Common/StringUtils.h"
#include "Core/Config.h"

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
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
	if (id.Bit(VS_BIT_DO_TEXTURE_PROJ)) desc << "TexProj ";
	if (id.Bit(VS_BIT_FLIP_TEXTURE)) desc << "Flip ";
	int uvgMode = id.Bits(VS_BIT_UVGEN_MODE, 2);
	const char *uvgModes[4] = { "UV ", "UVMtx ", "UVEnv ", "UVUnk " };
	int ls0 = id.Bits(VS_BIT_LS0, 2);
	int ls1 = id.Bits(VS_BIT_LS1, 2);

	if (uvgMode) desc << uvgModes[uvgMode];
	if (id.Bit(VS_BIT_ENABLE_BONES)) desc << "Bones:" << (id.Bits(VS_BIT_BONES, 3) + 1) << " ";
	// Lights
	if (id.Bit(VS_BIT_LIGHTING_ENABLE)) {
		desc << "Light: ";
		for (int i = 0; i < 4; i++) {
			if (id.Bit(VS_BIT_LIGHT0_ENABLE + i) || (uvgMode == GE_TEXMAP_ENVIRONMENT_MAP && (ls0 == i || ls1 == i))) {
				desc << i << ": ";
				desc << "c:" << id.Bits(VS_BIT_LIGHT0_COMP + 4 * i, 2) << " t:" << id.Bits(VS_BIT_LIGHT0_TYPE + 4 * i, 2) << " ";
			}
		}
	}
	if (id.Bits(VS_BIT_MATERIAL_UPDATE, 3)) desc << "MatUp:" << id.Bits(VS_BIT_MATERIAL_UPDATE, 3) << " ";
	if (id.Bits(VS_BIT_WEIGHT_FMTSCALE, 2)) desc << "WScale " << id.Bits(VS_BIT_WEIGHT_FMTSCALE, 2) << " ";
	if (id.Bits(VS_BIT_TEXCOORD_FMTSCALE, 2)) desc << "TCScale " << id.Bits(VS_BIT_TEXCOORD_FMTSCALE, 2) << " ";
	if (id.Bit(VS_BIT_FLATSHADE)) desc << "Flat ";

	// TODO: More...

	return desc.str();
}

bool CanUseHardwareTransform(int prim) {
	if (!g_Config.bHardwareTransform)
		return false;
	return !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES;
}

void ComputeVertexShaderID(ShaderID *id_out, u32 vertType, bool useHWTransform) {
	bool doTexture = gstate.isTextureMapEnabled() && !gstate.isModeClear();
	bool doTextureProjection = gstate.getUVGenMode() == GE_TEXMAP_TEXTURE_MATRIX;
	bool doShadeMapping = doTexture && (gstate.getUVGenMode() == GE_TEXMAP_ENVIRONMENT_MAP);
	bool doFlatShading = gstate.getShadeMode() == GE_SHADE_FLAT && !gstate.isModeClear();

	bool hasColor = (vertType & GE_VTYPE_COL_MASK) != 0;
	bool hasNormal = (vertType & GE_VTYPE_NRM_MASK) != 0;
	bool hasTexcoord = (vertType & GE_VTYPE_TC_MASK) != 0;
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
		id.SetBit(VS_BIT_FLIP_TEXTURE, gstate_c.flipTexture);
		id.SetBit(VS_BIT_DO_TEXTURE_PROJ, doTextureProjection);
	}

	if (useHWTransform) {
		id.SetBit(VS_BIT_USE_HW_TRANSFORM);
		id.SetBit(VS_BIT_HAS_NORMAL, hasNormal);

		// UV generation mode. doShadeMapping is implicitly stored here.
		id.SetBits(VS_BIT_UVGEN_MODE, 2, gstate.getUVGenMode());

		// The next bits are used differently depending on UVgen mode
		if (doTextureProjection) {
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
			if (gstate.isLightingEnabled())
				id.SetBit(VS_BIT_LIGHTING_ENABLE);
			// Light bits
			for (int i = 0; i < 4; i++) {
				id.SetBit(VS_BIT_LIGHT0_ENABLE + i, gstate.isLightChanEnabled(i) != 0);
				if (gstate.isLightChanEnabled(i) || (doShadeMapping && (gstate.getUVLS0() == i || gstate.getUVLS1() == i))) {
					id.SetBits(VS_BIT_LIGHT0_COMP + 4 * i, 2, gstate.getLightComputation(i));
					id.SetBits(VS_BIT_LIGHT0_TYPE + 4 * i, 2, gstate.getLightType(i));
				}
			}
			id.SetBits(VS_BIT_MATERIAL_UPDATE, 3, gstate.getMaterialUpdate() & 7);
		}

		id.SetBit(VS_BIT_NORM_REVERSE, gstate.areNormalsReversed());
		if (doTextureProjection && gstate.getUVProjMode() == GE_PROJMAP_UV) {
			id.SetBits(VS_BIT_TEXCOORD_FMTSCALE, 2, (vertType & GE_VTYPE_TC_MASK) >> GE_VTYPE_TC_SHIFT);  // two bits
		} else {
			id.SetBit(VS_BIT_HAS_TEXCOORD, hasTexcoord);
		}
	}

	id.SetBit(VS_BIT_FLATSHADE, doFlatShading);

	*id_out = id;
}
