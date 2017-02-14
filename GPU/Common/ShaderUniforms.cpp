#include <algorithm>

#include "ShaderUniforms.h"
#include "math/dataconv.h"
#include "math/lin/matrix4x4.h"
#include "math/math_util.h"
#include "math/lin/vec3.h"
#include "GPU/GPUState.h"
#include "GPU/Math3D.h"
#include "Core/Reporting.h"

static void ConvertProjMatrixToVulkan(Matrix4x4 &in) {
	const Vec3 trans(0, 0, gstate_c.vpZOffset * 0.5f + 0.5f);
	const Vec3 scale(gstate_c.vpWidthScale, gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
	in.translateAndScale(trans, scale);
}

static void ConvertProjMatrixToD3D11(Matrix4x4 &in) {
	const Vec3 trans(0, 0, gstate_c.vpZOffset * 0.5f + 0.5f);
	const Vec3 scale(gstate_c.vpWidthScale, -gstate_c.vpHeightScale, gstate_c.vpDepthScale * 0.5f);
	in.translateAndScale(trans, scale);
}

void BaseUpdateUniforms(UB_VS_FS_Base *ub, uint64_t dirtyUniforms, bool flipViewport) {
	if (dirtyUniforms & DIRTY_TEXENV) {
		Uint8x3ToFloat4(ub->texEnvColor, gstate.texenvcolor);
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORREF) {
		Uint8x3ToInt4_Alpha(ub->alphaColorRef, gstate.getColorTestRef(), gstate.getAlphaTestRef() & gstate.getAlphaTestMask());
	}
	if (dirtyUniforms & DIRTY_ALPHACOLORMASK) {
		Uint8x3ToInt4_Alpha(ub->colorTestMask, gstate.getColorTestMask(), gstate.getAlphaTestMask());
	}
	if (dirtyUniforms & DIRTY_FOGCOLOR) {
		Uint8x3ToFloat4(ub->fogColor, gstate.fogcolor);
	}
	if (dirtyUniforms & DIRTY_SHADERBLEND) {
		Uint8x3ToFloat4(ub->blendFixA, gstate.getFixA());
		Uint8x3ToFloat4(ub->blendFixB, gstate.getFixB());
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
		CopyMatrix4x4(ub->proj, flippedMatrix.getReadPtr());
	}

	if (dirtyUniforms & DIRTY_PROJTHROUGHMATRIX) {
		Matrix4x4 proj_through;
		if (flipViewport) {
			proj_through.setOrthoD3D(0.0f, gstate_c.curRTWidth, gstate_c.curRTHeight, 0, 0, 1);
		} else {
			proj_through.setOrthoVulkan(0.0f, gstate_c.curRTWidth, 0, gstate_c.curRTHeight, 0, 1);
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

	// Combined two small uniforms
	if (dirtyUniforms & (DIRTY_FOGCOEF | DIRTY_STENCILREPLACEVALUE)) {
		float fogcoef_stencil[3] = {
			getFloat24(gstate.fog1),
			getFloat24(gstate.fog2),
			(float)gstate.getStencilTestRef()/255.0f
		};
		if (my_isinf(fogcoef_stencil[1])) {
			// not really sure what a sensible value might be.
			fogcoef_stencil[1] = fogcoef_stencil[1] < 0.0f ? -10000.0f : 10000.0f;
		} else if (my_isnan(fogcoef_stencil[1])) {
			// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
			// Just put the fog far away at a large finite distance.
			// Infinities and NaNs are rather unpredictable in shaders on many GPUs
			// so it's best to just make it a sane calculation.
			fogcoef_stencil[0] = 100000.0f;
			fogcoef_stencil[1] = 1.0f;
		}
#ifndef MOBILE_DEVICE
		else if (my_isnanorinf(fogcoef_stencil[1]) || my_isnanorinf(fogcoef_stencil[0])) {
			ERROR_LOG_REPORT_ONCE(fognan, G3D, "Unhandled fog NaN/INF combo: %f %f", fogcoef_stencil[0], fogcoef_stencil[1]);
		}
#endif
		CopyFloat3(ub->fogCoef_stencil, fogcoef_stencil);
	}

	// Note - this one is not in lighting but in transformCommon as it has uses beyond lighting
	if (dirtyUniforms & DIRTY_MATAMBIENTALPHA) {
		Uint8x3ToFloat4_AlphaUint8(ub->matAmbient, gstate.materialambient, gstate.getMaterialAmbientA());
	}

	// Texturing
	if (dirtyUniforms & DIRTY_UVSCALEOFFSET) {
		const float invW = 1.0f / (float)gstate_c.curTextureWidth;
		const float invH = 1.0f / (float)gstate_c.curTextureHeight;
		const int w = gstate.getTextureWidth(0);
		const int h = gstate.getTextureHeight(0);
		const float widthFactor = (float)w * invW;
		const float heightFactor = (float)h * invH;
		ub->uvScaleOffset[0] = widthFactor;
		ub->uvScaleOffset[1] = heightFactor;
		ub->uvScaleOffset[2] = 0.0f;
		ub->uvScaleOffset[3] = 0.0f;
	}

	if (dirtyUniforms & DIRTY_DEPTHRANGE) {
		float viewZScale = gstate.getViewportZScale();
		float viewZCenter = gstate.getViewportZCenter();

		// We had to scale and translate Z to account for our clamped Z range.
		// Therefore, we also need to reverse this to round properly.
		//
		// Example: scale = 65535.0, center = 0.0
		// Resulting range = -65535 to 65535, clamped to [0, 65535]
		// gstate_c.vpDepthScale = 2.0f
		// gstate_c.vpZOffset = -1.0f
		//
		// The projection already accounts for those, so we need to reverse them.
		//
		// Additionally, D3D9 uses a range from [0, 1].  We double and move the center.
		viewZScale *= (1.0f / gstate_c.vpDepthScale) * 2.0f;
		viewZCenter -= 65535.0f * gstate_c.vpZOffset + 32768.5f;

		float viewZInvScale;
		if (viewZScale != 0.0) {
			viewZInvScale = 1.0f / viewZScale;
		} else {
			viewZInvScale = 0.0;
		}

		ub->depthRange[0] = viewZScale;
		ub->depthRange[1] = viewZCenter;
		ub->depthRange[2] = viewZCenter;
		ub->depthRange[3] = viewZInvScale;
	}
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
		Uint8x3ToFloat4(ub->materialEmissive, gstate.materialemissive);
	}

	for (int i = 0; i < 4; i++) {
		if (dirtyUniforms & (DIRTY_LIGHT0 << i)) {
			if (gstate.isDirectionalLight(i)) {
				// Prenormalize
				float x = getFloat24(gstate.lpos[i * 3 + 0]);
				float y = getFloat24(gstate.lpos[i * 3 + 1]);
				float z = getFloat24(gstate.lpos[i * 3 + 2]);
				float len = sqrtf(x*x + y*y + z*z);
				if (len == 0.0f)
					len = 1.0f;
				else
					len = 1.0f / len;
				float vec[3] = { x * len, y * len, z * len };
				CopyFloat3To4(ub->lpos[i], vec);
			} else {
				ExpandFloat24x3ToFloat4(ub->lpos[i], &gstate.lpos[i * 3]);
			}
			ExpandFloat24x3ToFloat4(ub->ldir[i], &gstate.ldir[i * 3]);
			ExpandFloat24x3ToFloat4(ub->latt[i], &gstate.latt[i * 3]);
			CopyFloat1To4(ub->lightAngle[i], getFloat24(gstate.lcutoff[i]));
			CopyFloat1To4(ub->lightSpotCoef[i], getFloat24(gstate.lconv[i]));
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
