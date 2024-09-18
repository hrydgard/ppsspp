#include <algorithm>
#include <cmath>

#include "ShaderUniforms.h"
#include "Common/System/Display.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/Math/lin/vec3.h"
#include "GPU/GPUState.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Math3D.h"

using namespace Lin;

static void ConvertProjMatrixToVulkan(Matrix4x4 &in) {
	const Vec3 trans(gstate_c.vpXOffset, gstate_c.vpYOffset, gstate_c.vpZOffset * 0.5f + 0.5f);
	const Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
	in.translateAndScale(trans, scale);
}

static void ConvertProjMatrixToD3D11(Matrix4x4 &in) {
	const Vec3 trans(gstate_c.vpXOffset, -gstate_c.vpYOffset, gstate_c.vpZOffset * 0.5f + 0.5f);
	const Vec3 scale(gstate_c.vpWidthScale, -gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
	in.translateAndScale(trans, scale);
}

void CalcCullRange(float minValues[4], float maxValues[4], bool flipViewport, bool hasNegZ) {
	// Account for the projection viewport adjustment when viewport is too large.
	auto reverseViewportX = [](float x) {
		float pspViewport = (x - gstate.getViewportXCenter()) * (1.0f / gstate.getViewportXScale());
		return (pspViewport * gstate_c.vpWidthScale) - gstate_c.vpXOffset;
	};
	auto reverseViewportY = [flipViewport](float y) {
		float heightScale = gstate_c.vpHeightScale;
		float yOffset = gstate_c.vpYOffset;
		if (flipViewport) {
			// For D3D11 and GLES non-buffered.
			heightScale = -heightScale;
			yOffset = -yOffset;
		}
		float pspViewport = (y - gstate.getViewportYCenter()) * (1.0f / gstate.getViewportYScale());
		return (pspViewport * heightScale) - yOffset;
	};
	auto transformZ = [hasNegZ](float z) {
		// Z culling ignores the viewport, so we just redo the projection matrix adjustments.
		if (hasNegZ) {
			return (z * gstate_c.vpDepthScale) + gstate_c.vpZOffset;
		}
		return (z * gstate_c.vpDepthScale * 0.5f) + gstate_c.vpZOffset * 0.5f + 0.5f;
	};
	auto sortPair = [](float a, float b) {
		return a > b ? std::make_pair(b, a) : std::make_pair(a, b);
	};

	// The PSP seems to use 0.12.4 for X and Y, and 0.16.0 for Z.
	// Any vertex outside this range (unless depth clamp enabled) is discarded.
	auto x = sortPair(reverseViewportX(0.0f), reverseViewportX(4096.0f));
	auto y = sortPair(reverseViewportY(0.0f), reverseViewportY(4096.0f));
	auto z = sortPair(transformZ(-1.000030517578125f), transformZ(1.000030517578125f));
	// Since we have space in w, use it to pass the depth clamp flag.  We also pass NAN for w "discard".
	float clampEnable = gstate.isDepthClampEnabled() ? 1.0f : 0.0f;

	minValues[0] = x.first;
	minValues[1] = y.first;
	minValues[2] = z.first;
	minValues[3] = clampEnable;
	maxValues[0] = x.second;
	maxValues[1] = y.second;
	maxValues[2] = z.second;
	maxValues[3] = NAN;
}

void BaseUpdateUniforms(UB_VS_FS_Base *ub, uint64_t dirtyUniforms, bool flipViewport, bool useBufferedRendering) {
	if (dirtyUniforms & DIRTY_TEXENV) {
		Uint8x3ToFloat3(ub->texEnvColor, gstate.texenvcolor);
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORREF) {
		ub->alphaColorRef = gstate.getColorTestRef() | ((gstate.getAlphaTestRef() & gstate.getAlphaTestMask()) << 24);
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORMASK) {
		ub->colorTestMask = gstate.getColorTestMask() | (gstate.getAlphaTestMask() << 24);
	}
	if (dirtyUniforms & DIRTY_FOGCOLOR) {
		Uint8x3ToFloat3(ub->fogColor, gstate.fogcolor);
	}
	if (dirtyUniforms & DIRTY_SHADERBLEND) {
		Uint8x3ToFloat3(ub->blendFixA, gstate.getFixA());
		Uint8x3ToFloat3(ub->blendFixB, gstate.getFixB());
	}
	if (dirtyUniforms & DIRTY_TEXCLAMP) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;

		// First wrap xy, then half texel xy (for clamp.)
		ub->texClamp[0] = widthFactor;
		ub->texClamp[1] = heightFactor;
		ub->texClamp[2] = invW * 0.5f;
		ub->texClamp[3] = invH * 0.5f;
		ub->texClampOffset[0] = gstate_c.curTextureXOffset * invW;
		ub->texClampOffset[1] = gstate_c.curTextureYOffset * invH;
	}

	if (dirtyUniforms & DIRTY_MIPBIAS) {
		float mipBias = (float)gstate.getTexLevelOffset16() * (1.0 / 16.0f);
		ub->mipBias = (mipBias + 0.5f) / (float)(gstate.getTextureMaxLevel() + 1);
	}

	if (dirtyUniforms & DIRTY_PROJMATRIX) {
		Matrix4x4 flippedMatrix;
		memcpy(&flippedMatrix, gstate.projMatrix, 16 * sizeof(float));

		const bool invertedY = gstate_c.vpHeight < 0;
		if (invertedY) {
			flippedMatrix[1] = -flippedMatrix[1];
			flippedMatrix[5] = -flippedMatrix[5];
			flippedMatrix[9] = -flippedMatrix[9];
			flippedMatrix[13] = -flippedMatrix[13];
		}
		const bool invertedX = gstate_c.vpWidth < 0;
		if (invertedX) {
			flippedMatrix[0] = -flippedMatrix[0];
			flippedMatrix[4] = -flippedMatrix[4];
			flippedMatrix[8] = -flippedMatrix[8];
			flippedMatrix[12] = -flippedMatrix[12];
		}
		if (flipViewport) {
			ConvertProjMatrixToD3D11(flippedMatrix);
		} else {
			ConvertProjMatrixToVulkan(flippedMatrix);
		}

		if (!useBufferedRendering && g_display.rotation != DisplayRotation::ROTATE_0) {
			flippedMatrix = flippedMatrix * g_display.rot_matrix;
		}
		CopyMatrix4x4(ub->proj, flippedMatrix.getReadPtr());

		ub->rotation = useBufferedRendering ? 0 : (float)g_display.rotation;
	}

	if (dirtyUniforms & DIRTY_PROJTHROUGHMATRIX) {
		Matrix4x4 proj_through;
		if (flipViewport) {
			proj_through.setOrthoD3D(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0, 0, 1);
		} else {
			proj_through.setOrthoVulkan(0.0f, gstate_c.curRTWidth, 0, gstate_c.curRTHeight, 0, 1);
		}
		if (!useBufferedRendering && g_display.rotation != DisplayRotation::ROTATE_0) {
			proj_through = proj_through * g_display.rot_matrix;
		}

		// Negative RT offsets come from split framebuffers (Killzone)
		if (gstate_c.curRTOffsetX < 0 || gstate_c.curRTOffsetY < 0) {
			proj_through.wx += 2.0f * (float)gstate_c.curRTOffsetX / (float)gstate_c.curRTWidth;
			proj_through.wy += 2.0f * (float)gstate_c.curRTOffsetY / (float)gstate_c.curRTHeight;
		}

		CopyMatrix4x4(ub->proj_through, proj_through.getReadPtr());
	}

	// Transform
	if (dirtyUniforms & DIRTY_WORLDMATRIX) {
		ConvertMatrix4x3To3x4Transposed(ub->world, gstate.worldMatrix);
	}
	if (dirtyUniforms & DIRTY_VIEWMATRIX) {
		ConvertMatrix4x3To3x4Transposed(ub->view, gstate.viewMatrix);
	}
	if (dirtyUniforms & DIRTY_TEXMATRIX) {
		ConvertMatrix4x3To3x4Transposed(ub->tex, gstate.tgenMatrix);
	}

	if (dirtyUniforms & DIRTY_FOGCOEF) {
		float fogcoef[2] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
		};
		// The PSP just ignores infnan here (ignoring IEEE), so take it down to a valid float.
		// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
		if (my_isnanorinf(fogcoef[0])) {
			// Not really sure what a sensible value might be, but let's try 64k.
			fogcoef[0] = std::signbit(fogcoef[0]) ? -65535.0f : 65535.0f;
		}
		if (my_isnanorinf(fogcoef[1])) {
			fogcoef[1] = std::signbit(fogcoef[1]) ? -65535.0f : 65535.0f;
		}
		CopyFloat2(ub->fogCoef, fogcoef);
	}

	if (dirtyUniforms & DIRTY_TEX_ALPHA_MUL) {
		bool doTextureAlpha = gstate.isTextureAlphaUsed();
		if (gstate_c.textureFullAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE) {
			doTextureAlpha = false;
		}
		ub->texNoAlpha = doTextureAlpha ? 0.0f : 1.0f;
		ub->texMul = gstate.isColorDoublingEnabled() ? 2.0f : 1.0f;
	}

	if (dirtyUniforms & DIRTY_STENCILREPLACEVALUE) {
		ub->stencilReplaceValue = (float)gstate.getStencilTestRef() * (1.0 / 255.0);
	}

	// Note - this one is not in lighting but in transformCommon as it has uses beyond lighting
	if (dirtyUniforms & DIRTY_MATAMBIENTALPHA) {
		Uint8x3ToFloat4_AlphaUint8(ub->matAmbient, gstate.materialambient, gstate.getMaterialAmbientA());
	}

	if (dirtyUniforms & DIRTY_COLORWRITEMASK) {
		ub->colorWriteMask = ~((gstate.pmska << 24) | (gstate.pmskc & 0xFFFFFF));
	}

	// Texturing
	if (dirtyUniforms & DIRTY_UVSCALEOFFSET) {
		float widthFactor = 1.0f;
		float heightFactor = 1.0f;
		if (gstate_c.textureIsFramebuffer) {
			const float invW = 1.0f / (float)gstate_c.curTextureWidth;
			const float invH = 1.0f / (float)gstate_c.curTextureHeight;
			const int w = gstate.getTextureWidth(0);
			const int h = gstate.getTextureHeight(0);
			widthFactor = (float)w * invW;
			heightFactor = (float)h * invH;
		}
		if (gstate_c.submitType == SubmitType::HW_BEZIER || gstate_c.submitType == SubmitType::HW_SPLINE) {
			// When we are generating UV coordinates through the bezier/spline, we need to apply the scaling.
			// However, this is missing a check that we're not getting our UV:s supplied for us in the vertices.
			ub->uvScaleOffset[0] = gstate_c.uv.uScale * widthFactor;
			ub->uvScaleOffset[1] = gstate_c.uv.vScale * heightFactor;
			ub->uvScaleOffset[2] = gstate_c.uv.uOff * widthFactor;
			ub->uvScaleOffset[3] = gstate_c.uv.vOff * heightFactor;
		} else {
			ub->uvScaleOffset[0] = widthFactor;
			ub->uvScaleOffset[1] = heightFactor;
			ub->uvScaleOffset[2] = 0.0f;
			ub->uvScaleOffset[3] = 0.0f;
		}
	}

	if (dirtyUniforms & DIRTY_DEPTHRANGE) {
		// Same formulas as D3D9 now. Should work for both Vulkan and D3D11.

		// Depth is [0, 1] mapping to [minz, maxz], not too hard.
		float vpZScale = gstate.getViewportZScale();
		float vpZCenter = gstate.getViewportZCenter();

		// These are just the reverse of the formulas in GPUStateUtils.
		float halfActualZRange = InfToZero(gstate_c.vpDepthScale != 0.0f ? vpZScale / gstate_c.vpDepthScale : 0.0f);
		float inverseDepthScale = InfToZero(gstate_c.vpDepthScale != 0.0f ? 1.0f / gstate_c.vpDepthScale : 0.0f);

		float minz = -((gstate_c.vpZOffset * halfActualZRange) - vpZCenter) - halfActualZRange;
		float viewZScale = halfActualZRange * 2.0f;
		float viewZCenter = minz;

		ub->depthRange[0] = viewZScale;
		ub->depthRange[1] = viewZCenter;
		ub->depthRange[2] = gstate_c.vpZOffset * 0.5f + 0.5f;
		ub->depthRange[3] = 2.0f * inverseDepthScale;
	}

	if (dirtyUniforms & DIRTY_CULLRANGE) {
		CalcCullRange(ub->cullRangeMin, ub->cullRangeMax, flipViewport, false);
	}

	if (dirtyUniforms & DIRTY_BEZIERSPLINE) {
		ub->spline_counts = gstate_c.spline_num_points_u;
	}

	if (dirtyUniforms & DIRTY_DEPAL) {
		int indexMask = gstate.getClutIndexMask();
		int indexShift = gstate.getClutIndexShift();
		int indexOffset = gstate.getClutIndexStartPos() >> 4;
		int format = gstate_c.depalFramebufferFormat;
		uint32_t val = BytesToUint32(indexMask, indexShift, indexOffset, format);
		// Poke in a bilinear filter flag in the top bit.
		if (gstate.isMagnifyFilteringEnabled())
			val |= 0x80000000;
		ub->depal_mask_shift_off_fmt = val;
	}
}

// For "light ubershader" bits.
// TODO: We pack these bits even when not using ubershader lighting. Maybe not bother.
uint32_t PackLightControlBits() {
	// Bit organization
	// Bottom 4 bits are enable bits for each light.
	// Then, for each light, comes 2 bits for "comp" and 2 bits for "type".
	// At the end, at bit 20, we put the three material update bits.

	uint32_t lightControl = 0;
	for (int i = 0; i < 4; i++) {
		if (gstate.isLightChanEnabled(i)) {
			lightControl |= 1 << i;
		}

		u32 computation = (u32)gstate.getLightComputation(i);  // 2 bits
		u32 type = (u32)gstate.getLightType(i);  // 2 bits
		if (type == 3) { type = 0; }  // Don't want to handle this degenerate case in the shader.
		lightControl |= computation << (4 + i * 4);
		lightControl |= type << (4 + i * 4 + 2);
	}

	// Material update is 3 bits.
	lightControl |= gstate.getMaterialUpdate() << 20;
	return lightControl;
}

void LightUpdateUniforms(UB_VS_Lights *ub, uint64_t dirtyUniforms) {
	// Lighting
	if (dirtyUniforms & DIRTY_AMBIENT) {
		Uint8x3ToFloat4_AlphaUint8(ub->ambientColor, gstate.ambientcolor, gstate.getAmbientA());
	}
	if (dirtyUniforms & DIRTY_MATDIFFUSE) {
		Uint8x3ToFloat4(ub->materialDiffuse, gstate.materialdiffuse);
	}
	if (dirtyUniforms & DIRTY_MATSPECULAR) {
		Uint8x3ToFloat4_Alpha(ub->materialSpecular, gstate.materialspecular, std::max(0.0f, getFloat24(gstate.materialspecularcoef)));
	}
	if (dirtyUniforms & DIRTY_MATEMISSIVE) {
		// We're not touching the fourth f32 here, because we store an u32 of control bits in it.
		Uint8x3ToFloat3(ub->materialEmissive, gstate.materialemissive);
	}
	if (dirtyUniforms & DIRTY_LIGHT_CONTROL) {
		ub->lightControl = PackLightControlBits();
	}
	for (int i = 0; i < 4; i++) {
		if (dirtyUniforms & (DIRTY_LIGHT0 << i)) {
			if (gstate.isDirectionalLight(i)) {
				// Prenormalize
				ExpandFloat24x3ToFloat4AndNormalize(ub->lpos[i], &gstate.lpos[i * 3]);
			} else {
				ExpandFloat24x3ToFloat4(ub->lpos[i], &gstate.lpos[i * 3]);
			}
			// ldir is only used for spotlights. Prenormalize it.
			ExpandFloat24x3ToFloat4AndNormalize(ub->ldir[i], &gstate.ldir[i * 3]);
			ExpandFloat24x3ToFloat4(ub->latt[i], &gstate.latt[i * 3]);
			float lightAngle_spotCoef[2] = { getFloat24(gstate.lcutoff[i]), getFloat24(gstate.lconv[i]) };
			CopyFloat2To4(ub->lightAngle_SpotCoef[i], lightAngle_spotCoef);
			Uint8x3ToFloat4(ub->lightAmbient[i], gstate.lcolor[i * 3]);
			Uint8x3ToFloat4(ub->lightDiffuse[i], gstate.lcolor[i * 3 + 1]);
			Uint8x3ToFloat4(ub->lightSpecular[i], gstate.lcolor[i * 3 + 2]);
		}
	}
}

void BoneUpdateUniforms(UB_VS_Bones *ub, uint64_t dirtyUniforms) {
	for (int i = 0; i < 8; i++) {
		if (dirtyUniforms & (DIRTY_BONEMATRIX0 << i)) {
			ConvertMatrix4x3To3x4Transposed(ub->bones[i], gstate.boneMatrix + 12 * i);
		}
	}
}
