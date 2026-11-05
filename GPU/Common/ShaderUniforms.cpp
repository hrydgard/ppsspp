#include <algorithm>
#include <cmath>

#include "ShaderUniforms.h"
#include "Common/System/Display.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Math/lin/matrix4x4.h"
#include "Common/Math/math_util.h"
#include "Common/Math/CrossSIMD.h"
#include "Common/Math/lin/vec3.h"
#include "Common/TimeUtil.h"
#include "Common/VR/PPSSPPVR.h"
#include "Core/Config.h"
#include "GPU/GPUState.h"
#include "GPU/Common/FramebufferManagerCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "GPU/Common/TransformCommon.h"
#include "GPU/Math3D.h"

using namespace Lin;

void UpdateRotation(float rotMatrix[4], bool useBufferedRendering) {
	if (!gstate_c.Use(GPU_USE_PRE_ROTATION) || useBufferedRendering) {
		rotMatrix[0] = 1.0f;
		rotMatrix[1] = 0.0f;
		rotMatrix[2] = 0.0f;
		rotMatrix[3] = 1.0f;
	} else {
		const DisplayRotation rotation = g_display.rotation;
		// The others are ROTATE_90 and so on.
		rotMatrix[0] = rotation == DisplayRotation::ROTATE_0 ? 1.0 : (rotation == DisplayRotation::ROTATE_180 ? -1.0 : 0.0);
		rotMatrix[1] = rotation == DisplayRotation::ROTATE_90 ? 1.0 : (rotation == DisplayRotation::ROTATE_270 ? -1.0 : 0.0);
		rotMatrix[2] = rotation == DisplayRotation::ROTATE_270 ? 1.0 : (rotation == DisplayRotation::ROTATE_90 ? -1.0 : 0.0);
		rotMatrix[3] = rotation == DisplayRotation::ROTATE_0 ? 1.0 : (rotation == DisplayRotation::ROTATE_180 ? -1.0 : 0.0);
	}
}

// The part of a video texture the frame fills: as much as was written there (see
// TextureCacheCommon::ApplyTextureFinish), or 480x272 if that isn't known. The VSH's UMD Video
// player converts 480 rows, and its strips wrap around a texture they fill the width of.
int VideoFrameWidth() {
	return gstate_c.videoColumns > 0 ? gstate_c.videoColumns : 480;
}

int VideoFrameHeight() {
	return gstate_c.videoRows > 0 ? gstate_c.videoRows : 272;
}

bool VideoClampsS() {
	return gstate.getTextureWidth(0) > VideoFrameWidth();
}

bool VideoClampsT() {
	return gstate.getTextureHeight(0) > VideoFrameHeight();
}

void CalcTexClamp(float texClamp[4], float texClampOffset[2]) {
	const float invW = 1.0f / (float)gstate_c.curTextureWidth;
	const float invH = 1.0f / (float)gstate_c.curTextureHeight;
	int w = gstate.getTextureWidth(0);
	int h = gstate.getTextureHeight(0);
	if (gstate_c.textureIsVideo) {
		// Only the video frame itself, whatever is in memory around it may be garbage.
		w = std::min(w, VideoFrameWidth());
		h = std::min(h, VideoFrameHeight());
	}

	// First wrap xy, then half texel xy (for clamp.)
	texClamp[0] = (float)w * invW;
	texClamp[1] = (float)h * invH;
	texClamp[2] = invW * 0.5f;
	texClamp[3] = invH * 0.5f;
	texClampOffset[0] = gstate_c.curTextureXOffset * invW;
	texClampOffset[1] = gstate_c.curTextureYOffset * invH;
}

void BaseUpdateUniforms(UB_VS_FS_Base *ub, uint64_t dirtyUniforms, bool useBufferedRendering, bool pixelMapped) {
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
		CalcTexClamp(ub->texClamp, ub->texClampOffset);
	}

	if (dirtyUniforms & DIRTY_MIPBIAS) {
		float mipBias = (float)gstate.getTexLevelOffset16() * (1.0 / 16.0f);
		ub->mipBias = (mipBias + 0.5f) / (float)(gstate.getTextureMaxLevel() + 1);
		// For GPU_USE_SHADER_LOD_BIAS. The sampler applies the bias only in auto mode, see GetSamplingParams.
		ub->texLodBias = gstate.getTexLevelMode() == GE_TEXLEVEL_MODE_AUTO ? mipBias : 0.0f;
	}

	if (dirtyUniforms & DIRTY_PROJMATRIX) {
		CopyMatrix4x4(ub->proj, gstate.projMatrix);
		ub->rotation = useBufferedRendering ? 0 : (float)g_display.rotation;
	}

	if (dirtyUniforms & DIRTY_FRAMEBUFFER_DIM) {
		ub->xywh[0] = (float)gstate_c.curRTOffsetX;
		ub->xywh[1] = (float)gstate_c.curRTOffsetY;
		ub->xywh[2] = (float)(2.0 / gstate_c.curRTWidth);  // intentionally do double precision here.
		ub->xywh[3] = (float)(2.0 / gstate_c.curRTHeight);

		ub->rotation = useBufferedRendering ? 0 : (float)g_display.rotation;
	}

	if (dirtyUniforms & DIRTY_RASTER_OFFSET) {
		ub->rasterOffset[0] = gstate.getOffsetX();
		ub->rasterOffset[1] = gstate.getOffsetY();
		ub->minZmaxZ[0] = (float)gstate.getDepthRangeMin();
		ub->minZmaxZ[1] = (float)gstate.getDepthRangeMax();
		// test sine wave
		// ub->minZmaxZ[0] = (sin(time_now_d()) * 0.5f + 0.5f) * 65536.0;
	}

	if (dirtyUniforms & DIRTY_VIEWPORT_UNIFORMS) {
		Vec4F32 vpScale = Vec4F32::LoadF24x4(&gstate.viewportxscale);
		Vec4F32 vpOffset = Vec4F32::LoadF24x4(&gstate.viewportxcenter);
		vpScale.Store(ub->vpScale);
		vpOffset.Store(ub->vpOffset);
		ub->NaN = std::numeric_limits<float>::quiet_NaN();  // Used in the shader for range culling.
	}

	// Transform
	if (dirtyUniforms & DIRTY_WORLDMATRIX) {
		// TODO: We could change the shader to directly read these "malformed" matrices, but we'd
		// be doing the matrix multiplication manually.
		ConvertMatrix4x3To3x4Transposed(ub->world, gstate.worldMatrix);
	}
	if (dirtyUniforms & DIRTY_VIEWMATRIX) {
		ConvertMatrix4x3To3x4Transposed(ub->view, gstate.viewMatrix);
	}
	if (dirtyUniforms & DIRTY_TEXMATRIX) {
		ConvertMatrix4x3To3x4Transposed(ub->tex, gstate.tgenMatrix);
	}

	if (dirtyUniforms & DIRTY_FOGCOEF) {
		UpdateFogCoef(gstate, ub->fogCoef);
	}

	if (dirtyUniforms & DIRTY_TEX_ALPHA_MUL) {
		bool doTextureAlpha = gstate.isTextureAlphaUsed();
		if (gstate_c.textureSolidAlpha && gstate.getTextureFunction() != GE_TEXFUNC_REPLACE) {
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
		UpdateUVScaleOff(gstate, ub->uvScaleOffset);
	}

	if (dirtyUniforms & DIRTY_DEPAL) {
		ub->depal_mask_shift_off_fmt = PackDepalBits(pixelMapped);
	}
}

bool GuessVRDrawingHUD(bool is2D, bool flatScreen) {

	bool hud = true;
	//HUD shouldn't be modified in nonVR mode
	if (IsBigScreenVRMode()) hud = false;
	//HUD can be disabled in settings
	else if (!g_Config.bRescaleHUD) hud = false;
	//HUD cannot be rendered in flatscreen
	else if (flatScreen) hud = false;
	//HUD has to be 2D
	else if (!is2D) hud = false;
	//HUD has to be blended
	else if (!gstate.isAlphaBlendEnabled()) hud = false;
#ifdef __LIBRETRO__
	// Untextured transition fades must cover the view, not be scaled with the HUD.
	else if (!gstate.isTextureMapEnabled()) hud = false;
#endif
	//HUD cannot be rendered with clear color mask
	else if (gstate.isClearModeColorMask()) hud = false;
	//HUD cannot be rendered with depth color mask
	else if (gstate.isClearModeDepthMask()) hud = false;
	//HUD texture has to contain alpha channel
	else if (!gstate.isTextureAlphaUsed()) hud = false;
	//HUD texture cannot be in 5551 format
	else if (gstate.getTextureFormat() == GETextureFormat::GE_TFMT_5551) hud = false;
	//HUD texture cannot be in CLUT16 format
	else if (gstate.getTextureFormat() == GETextureFormat::GE_TFMT_CLUT16) hud = false;
	//HUD texture cannot be in CLUT32 format
	else if (gstate.getTextureFormat() == GETextureFormat::GE_TFMT_CLUT32) hud = false;
	//HUD cannot have full texture alpha
	else if (gstate_c.textureSolidAlpha && gstate.getTextureFormat() != GETextureFormat::GE_TFMT_CLUT4) hud = false;
	//HUD must have full vertex alpha
	else if (!gstate_c.vertexFullAlpha && gstate.getDepthTestFunction() == GE_COMP_NEVER) hud = false;
	//HUD cannot render FB screenshot
	else if (gstate_c.curTextureHeight % 68 <= 1) hud = false;
	//HUD cannot be rendered with add function
	else if (gstate.getTextureFunction() == GETexFunc::GE_TEXFUNC_ADD) hud = false;
	//HUD cannot be rendered with replace function
	else if (gstate.getTextureFunction() == GETexFunc::GE_TEXFUNC_REPLACE) hud = false;
	//HUD cannot be rendered with full clear color mask
	else if ((gstate.getClearModeColorMask() == 0xFFFFFF) && (gstate.getColorMask() == 0xFFFFFF)) hud = false;

	return hud;
}

void VRUpdateUniforms(UB_VS_FS_Base *base, UB_VS_VR *vr, uint64_t dirtyUniforms) {
	// Also counts the 3D draws that decide whether the scene is 3D, so once per draw.
	const bool is2D = Is2DVRObject(gstate.projMatrix, gstate.isModeThrough());
	const bool flatScreen = IsFlatVRScene();

	if (GuessVRDrawingHUD(is2D, flatScreen)) {
		float aspect = 480.0f / 272.0f * (IsImmersiveVRMode() ? 0.5f : 1.0f);
		vr->scaleX = g_Config.fHeadUpDisplayScale * aspect;
		vr->scaleY = g_Config.fHeadUpDisplayScale;
	} else {
		vr->scaleX = 1.0f;
		vr->scaleY = 1.0f;
	}

	if (dirtyUniforms & DIRTY_FOGCOLOR) {
		// For the skyplane hack's clear.
		SetVRCompat(VR_COMPAT_FOG_COLOR, gstate.fogcolor);
	}

	// Every draw, not only when the game changes the matrices: the result also depends on whether this
	// draw is 2D and on the head pose. GL gets away with dirty flags because its uniforms are per program
	// (2D and 3D draws rarely share one), while this buffer is shared by all draws.
	{
		if (flatScreen || is2D) {
			memcpy(vr->projLens, gstate.projMatrix, sizeof(vr->projLens));
		} else {
			UpdateVRProjection(gstate.projMatrix, vr->projLens);
		}
		UpdateVRParams(gstate.projMatrix);
	}

	{
		float leftEyeView[16];
		float rightEyeView[16];
		ConvertMatrix4x3To4x4Transposed(leftEyeView, gstate.viewMatrix);
		ConvertMatrix4x3To4x4Transposed(rightEyeView, gstate.viewMatrix);
		if (!is2D) {
			UpdateVRView(leftEyeView, rightEyeView);
		}
		// The head pose is a rotation and a translation, so the last row stays 0 0 0 1 and the
		// first three are the 3x4 the shaders take.
		memcpy(base->view, leftEyeView, sizeof(base->view));
		memcpy(vr->viewRight, rightEyeView, sizeof(vr->viewRight));
	}
}

void DirtyDepalIfPixelMappedChanged(bool pixelMapped) {
	if (gstate_c.shaderDepalMode != ShaderDepalMode::OFF && pixelMapped != gstate_c.depalPixelMapped) {
		gstate_c.Dirty(DIRTY_DEPAL);
	}
}

uint32_t PackDepalBits(bool pixelMapped) {
	gstate_c.depalPixelMapped = pixelMapped;
	const int indexMask = gstate.getClutIndexMask();
	const int indexShift = gstate.getClutIndexShift();
	const int indexOffset = gstate.getClutIndexStartPos() >> 4;
	const int format = gstate_c.depalTextureFormat;
	uint32_t val = BytesToUint32(indexMask, indexShift, indexOffset, format);
	// NOTE: This must follow similar logic to TextureCacheCommon::GetSamplingParams -
	// maybe we can share it somehow.
	// TODO: Handle replaced textures.
	bool bilinear = gstate.isMagnifyFilteringEnabled() && !pixelMapped;
	switch (g_Config.iTexFiltering) {
	case TEX_FILTER_FORCE_NEAREST:
		bilinear = false;
		break;
	case TEX_FILTER_FORCE_LINEAR:
		if (CanForceBilinear(gstate)) {
			bilinear = true;
		}
		break;
	default:
		break;
	}
	if (bilinear) {
		// Poke in a bilinear filter flag in the top bit.
		val |= 0x80000000;
	}
	return val;
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
		Uint8x3ToFloat4_Alpha(ub->materialSpecular, gstate.materialspecular, std::max(0.0f, PSPLightExponent(getFloat24(gstate.materialspecularcoef))));
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
			float lightAngle_spotCoef[2] = { getFloat24(gstate.lcutoff[i]), PSPLightExponent(getFloat24(gstate.lconv[i])) };
			CopyFloat2To4(ub->lightAngle_SpotCoef[i], lightAngle_spotCoef);
			Uint8x3ToFloat4(ub->lightAmbient[i], gstate.lcolor[i * 3]);
			Uint8x3ToFloat4(ub->lightDiffuse[i], gstate.lcolor[i * 3 + 1]);
			Uint8x3ToFloat4(ub->lightSpecular[i], gstate.lcolor[i * 3 + 2]);
		}
	}
}

void UpdateFogCoef(const GEState &state, float fogCoef[2]) {
	fogCoef[0] = getFloat24(gstate.fog1);
	fogCoef[1] = getFloat24(gstate.fog2);
	// The PSP just ignores infnan here (ignoring IEEE), so take it down to a valid float.
	// Workaround for https://github.com/hrydgard/ppsspp/issues/5384#issuecomment-38365988
	if (my_isnanorinf(fogCoef[0])) {
		// Not really sure what a sensible value might be, but let's try 64k.
		fogCoef[0] = std::signbit(fogCoef[0]) ? -65535.0f : 65535.0f;
	}
	if (my_isnanorinf(fogCoef[1])) {
		fogCoef[1] = std::signbit(fogCoef[1]) ? -65535.0f : 65535.0f;
	}
}
