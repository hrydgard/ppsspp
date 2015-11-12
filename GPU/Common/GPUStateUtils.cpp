// Copyright (c) 2015- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>

#include "Common/StringUtils.h"
#include "Core/Config.h"
#include "Core/System.h"

#include "GPU/ge_constants.h"
#include "GPU/GPUState.h"
#include "GPU/Common/ShaderId.h"
#include "GPU/Common/VertexDecoderCommon.h"
#include "GPU/Common/FramebufferCommon.h"

#include "GPU/Common/GPUStateUtils.h"


bool CanUseHardwareTransform(int prim) {
	if (!g_Config.bHardwareTransform)
		return false;
	return !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES;
}

// Dest factors where it's safe to eliminate the alpha test under certain conditions
static const bool safeDestFactors[16] = {
	true, // GE_DSTBLEND_SRCCOLOR,
	true, // GE_DSTBLEND_INVSRCCOLOR,
	false, // GE_DSTBLEND_SRCALPHA,
	true, // GE_DSTBLEND_INVSRCALPHA,
	true, // GE_DSTBLEND_DSTALPHA,
	true, // GE_DSTBLEND_INVDSTALPHA,
	false, // GE_DSTBLEND_DOUBLESRCALPHA,
	false, // GE_DSTBLEND_DOUBLEINVSRCALPHA,
	true, // GE_DSTBLEND_DOUBLEDSTALPHA,
	true, // GE_DSTBLEND_DOUBLEINVDSTALPHA,
	true, //GE_DSTBLEND_FIXB,
};

bool IsAlphaTestTriviallyTrue() {
	switch (gstate.getAlphaTestFunction()) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_GEQUAL:
		if (gstate_c.vertexFullAlpha && (gstate_c.textureFullAlpha || !gstate.isTextureAlphaUsed()))
			return true;  // If alpha is full, it doesn't matter what the ref value is.
		return gstate.getAlphaTestRef() == 0;

		// Non-zero check. If we have no depth testing (and thus no depth writing), and an alpha func that will result in no change if zero alpha, get rid of the alpha test.
		// Speeds up Lumines by a LOT on PowerVR.
	case GE_COMP_NOTEQUAL:
		if (gstate.getAlphaTestRef() == 255) {
			// Likely to be rare. Let's just skip the vertexFullAlpha optimization here instead of adding
			// complicated code to discard the draw or whatnot.
			return false;
		}
		// Fallthrough on purpose

	case GE_COMP_GREATER:
	{
#if 0
		// Easy way to check the values in the debugger without ruining && early-out
		bool doTextureAlpha = gstate.isTextureAlphaUsed();
		bool stencilTest = gstate.isStencilTestEnabled();
		bool depthTest = gstate.isDepthTestEnabled();
		GEComparison depthTestFunc = gstate.getDepthTestFunction();
		int alphaRef = gstate.getAlphaTestRef();
		int blendA = gstate.getBlendFuncA();
		bool blendEnabled = gstate.isAlphaBlendEnabled();
		int blendB = gstate.getBlendFuncA();
#endif
		return (gstate_c.vertexFullAlpha && (gstate_c.textureFullAlpha || !gstate.isTextureAlphaUsed())) || (
			(!gstate.isStencilTestEnabled() &&
				!gstate.isDepthTestEnabled() &&
				gstate.getAlphaTestRef() == 0 &&
				gstate.isAlphaBlendEnabled() &&
				gstate.getBlendFuncA() == GE_SRCBLEND_SRCALPHA &&
				safeDestFactors[(int)gstate.getBlendFuncB()]));
	}

	case GE_COMP_LEQUAL:
		return gstate.getAlphaTestRef() == 255;

	case GE_COMP_EQUAL:
	case GE_COMP_LESS:
		return false;

	default:
		return false;
	}
}

bool IsAlphaTestAgainstZero() {
	return gstate.getAlphaTestRef() == 0 && gstate.getAlphaTestMask() == 0xFF;
}

bool IsColorTestAgainstZero() {
	return gstate.getColorTestRef() == 0 && gstate.getColorTestMask() == 0xFFFFFF;
}

bool IsColorTestTriviallyTrue() {
	switch (gstate.getColorTestFunction()) {
	case GE_COMP_NEVER:
		return false;

	case GE_COMP_ALWAYS:
		return true;

	case GE_COMP_EQUAL:
	case GE_COMP_NOTEQUAL:
		return false;
	default:
		return false;
	}
}

const bool nonAlphaSrcFactors[16] = {
	true,  // GE_SRCBLEND_DSTCOLOR,
	true,  // GE_SRCBLEND_INVDSTCOLOR,
	false, // GE_SRCBLEND_SRCALPHA,
	false, // GE_SRCBLEND_INVSRCALPHA,
	true,  // GE_SRCBLEND_DSTALPHA,
	true,  // GE_SRCBLEND_INVDSTALPHA,
	false, // GE_SRCBLEND_DOUBLESRCALPHA,
	false, // GE_SRCBLEND_DOUBLEINVSRCALPHA,
	true,  // GE_SRCBLEND_DOUBLEDSTALPHA,
	true,  // GE_SRCBLEND_DOUBLEINVDSTALPHA,
	true,  // GE_SRCBLEND_FIXA,
};

const bool nonAlphaDestFactors[16] = {
	true,  // GE_DSTBLEND_SRCCOLOR,
	true,  // GE_DSTBLEND_INVSRCCOLOR,
	false, // GE_DSTBLEND_SRCALPHA,
	false, // GE_DSTBLEND_INVSRCALPHA,
	true,  // GE_DSTBLEND_DSTALPHA,
	true,  // GE_DSTBLEND_INVDSTALPHA,
	false, // GE_DSTBLEND_DOUBLESRCALPHA,
	false, // GE_DSTBLEND_DOUBLEINVSRCALPHA,
	true,  // GE_DSTBLEND_DOUBLEDSTALPHA,
	true,  // GE_DSTBLEND_DOUBLEINVDSTALPHA,
	true,  // GE_DSTBLEND_FIXB,
};

ReplaceAlphaType ReplaceAlphaWithStencil(ReplaceBlendType replaceBlend) {
	if (!gstate.isStencilTestEnabled() || gstate.isModeClear()) {
		return REPLACE_ALPHA_NO;
	}

	if (replaceBlend != REPLACE_BLEND_NO && replaceBlend != REPLACE_BLEND_COPY_FBO) {
		if (nonAlphaSrcFactors[gstate.getBlendFuncA()] && nonAlphaDestFactors[gstate.getBlendFuncB()]) {
			return REPLACE_ALPHA_YES;
		} else {
			if (gstate_c.featureFlags & GPU_SUPPORTS_DUALSOURCE_BLEND) {
				return REPLACE_ALPHA_DUALSOURCE;
			} else {
				return REPLACE_ALPHA_NO;
			}
		}
	}

	return REPLACE_ALPHA_YES;
}

StencilValueType ReplaceAlphaWithStencilType() {
	switch (gstate.FrameBufFormat()) {
	case GE_FORMAT_565:
		// There's never a stencil value.  Maybe the right alpha is 1?
		return STENCIL_VALUE_ONE;

	case GE_FORMAT_5551:
		switch (gstate.getStencilOpZPass()) {
			// Technically, this should only ever use zero/one.
		case GE_STENCILOP_REPLACE:
			return (gstate.getStencilTestRef() & 0x80) != 0 ? STENCIL_VALUE_ONE : STENCIL_VALUE_ZERO;

			// Decrementing always zeros, since there's only one bit.
		case GE_STENCILOP_DECR:
		case GE_STENCILOP_ZERO:
			return STENCIL_VALUE_ZERO;

			// Incrementing always fills, since there's only one bit.
		case GE_STENCILOP_INCR:
			return STENCIL_VALUE_ONE;

		case GE_STENCILOP_INVERT:
			return STENCIL_VALUE_INVERT;

		case GE_STENCILOP_KEEP:
			return STENCIL_VALUE_KEEP;
		}
		break;

	case GE_FORMAT_4444:
	case GE_FORMAT_8888:
	case GE_FORMAT_INVALID:
		switch (gstate.getStencilOpZPass()) {
		case GE_STENCILOP_REPLACE:
			// TODO: Could detect zero here and force ZERO - less uniform updates?
			return STENCIL_VALUE_UNIFORM;

		case GE_STENCILOP_ZERO:
			return STENCIL_VALUE_ZERO;

		case GE_STENCILOP_DECR:
			return gstate.FrameBufFormat() == GE_FORMAT_4444 ? STENCIL_VALUE_DECR_4 : STENCIL_VALUE_DECR_8;

		case GE_STENCILOP_INCR:
			return gstate.FrameBufFormat() == GE_FORMAT_4444 ? STENCIL_VALUE_INCR_4 : STENCIL_VALUE_INCR_8;

		case GE_STENCILOP_INVERT:
			return STENCIL_VALUE_INVERT;

		case GE_STENCILOP_KEEP:
			return STENCIL_VALUE_KEEP;
		}
		break;
	}

	return STENCIL_VALUE_KEEP;
}

ReplaceBlendType ReplaceBlendWithShader(bool allowShaderBlend, GEBufferFormat bufferFormat) {
	if (!gstate.isAlphaBlendEnabled() || gstate.isModeClear()) {
		return REPLACE_BLEND_NO;
	}

	GEBlendMode eq = gstate.getBlendEq();
	// Let's get the non-factor modes out of the way first.
	switch (eq) {
	case GE_BLENDMODE_ABSDIFF:
		return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

	case GE_BLENDMODE_MIN:
	case GE_BLENDMODE_MAX:
		if (gstate_c.Supports(GPU_SUPPORTS_BLEND_MINMAX)) {
			return REPLACE_BLEND_STANDARD;
		} else {
			return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;
		}

	default:
		break;
	}

	GEBlendSrcFactor funcA = gstate.getBlendFuncA();
	GEBlendDstFactor funcB = gstate.getBlendFuncB();

	switch (funcA) {
	case GE_SRCBLEND_DOUBLESRCALPHA:
	case GE_SRCBLEND_DOUBLEINVSRCALPHA:
		// 2x alpha in the source function and not in the dest = source color doubling.
		// Even dest alpha is safe, since we're moving the * 2.0 into the src color.
		switch (funcB) {
		case GE_DSTBLEND_SRCCOLOR:
		case GE_DSTBLEND_INVSRCCOLOR:
			// Can't double, we need the source color to be correct.
			return !allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			if (bufferFormat == GE_FORMAT_565) {
				return REPLACE_BLEND_2X_ALPHA;
			}
			return !allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			// We can't technically do this correctly (due to clamping) without reading the dst color.
			// Using a copy isn't accurate either, though, when there's overlap.
			if (gstate_c.featureFlags & GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH)
				return !allowShaderBlend ? REPLACE_BLEND_PRE_SRC_2X_ALPHA : REPLACE_BLEND_COPY_FBO;
			return REPLACE_BLEND_PRE_SRC_2X_ALPHA;

		default:
			// TODO: Could use vertexFullAlpha, but it's not calculated yet.
			return REPLACE_BLEND_PRE_SRC;
		}

	case GE_SRCBLEND_DOUBLEDSTALPHA:
	case GE_SRCBLEND_DOUBLEINVDSTALPHA:
		switch (funcB) {
		case GE_DSTBLEND_SRCCOLOR:
		case GE_DSTBLEND_INVSRCCOLOR:
			if (bufferFormat == GE_FORMAT_565) {
				return REPLACE_BLEND_STANDARD;
			}
			// Can't double, we need the source color to be correct.
			return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			if (bufferFormat == GE_FORMAT_565) {
				return REPLACE_BLEND_STANDARD;
			}
			return !allowShaderBlend ? REPLACE_BLEND_2X_SRC : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			if (bufferFormat == GE_FORMAT_565) {
				return REPLACE_BLEND_2X_ALPHA;
			}
			return !allowShaderBlend ? REPLACE_BLEND_2X_SRC : REPLACE_BLEND_COPY_FBO;

		default:
			if (bufferFormat == GE_FORMAT_565) {
				return REPLACE_BLEND_STANDARD;
			}
			// We can't technically do this correctly (due to clamping) without reading the dst alpha.
			return !allowShaderBlend ? REPLACE_BLEND_2X_SRC : REPLACE_BLEND_COPY_FBO;
		}

	case GE_SRCBLEND_FIXA:
		switch (funcB) {
		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			// Can't safely double alpha, will clamp.
			return !allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			if (bufferFormat == GE_FORMAT_565) {
				return REPLACE_BLEND_STANDARD;
			}
			return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

		case GE_DSTBLEND_FIXB:
			if (gstate.getFixA() == 0xFFFFFF && gstate.getFixB() == 0x000000) {
				// Some games specify this.  Some cards may prefer blending off entirely.
				return REPLACE_BLEND_NO;
			} else if (gstate.getFixA() == 0xFFFFFF || gstate.getFixA() == 0x000000 || gstate.getFixB() == 0xFFFFFF || gstate.getFixB() == 0x000000) {
				return REPLACE_BLEND_STANDARD;
			} else {
				return REPLACE_BLEND_PRE_SRC;
			}

		default:
			return REPLACE_BLEND_STANDARD;
		}

	default:
		switch (funcB) {
		case GE_DSTBLEND_DOUBLESRCALPHA:
		case GE_DSTBLEND_DOUBLEINVSRCALPHA:
			if (funcA == GE_SRCBLEND_SRCALPHA || funcA == GE_SRCBLEND_INVSRCALPHA) {
				// Can't safely double alpha, will clamp.  However, a copy may easily be worse due to overlap.
				if (gstate_c.featureFlags & GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH)
					return !allowShaderBlend ? REPLACE_BLEND_PRE_SRC_2X_ALPHA : REPLACE_BLEND_COPY_FBO;
				return REPLACE_BLEND_PRE_SRC_2X_ALPHA;
			} else {
				// This means dst alpha/color is used in the src factor.
				// Unfortunately, copying here causes overlap problems in Silent Hill games (it seems?)
				// We will just hope that doubling alpha for the dst factor will not clamp too badly.
				if (gstate_c.featureFlags & GPU_SUPPORTS_ANY_FRAMEBUFFER_FETCH)
					return !allowShaderBlend ? REPLACE_BLEND_2X_ALPHA : REPLACE_BLEND_COPY_FBO;
				return REPLACE_BLEND_2X_ALPHA;
			}

		case GE_DSTBLEND_DOUBLEDSTALPHA:
		case GE_DSTBLEND_DOUBLEINVDSTALPHA:
			if (bufferFormat == GE_FORMAT_565) {
				return REPLACE_BLEND_STANDARD;
			}
			return !allowShaderBlend ? REPLACE_BLEND_STANDARD : REPLACE_BLEND_COPY_FBO;

		default:
			return REPLACE_BLEND_STANDARD;
		}
	}
}

LogicOpReplaceType ReplaceLogicOpType() {
	if (!gstate_c.Supports(GPU_SUPPORTS_LOGIC_OP) && gstate.isLogicOpEnabled()) {
		switch (gstate.getLogicOp()) {
		case GE_LOGIC_COPY_INVERTED:
		case GE_LOGIC_AND_INVERTED:
		case GE_LOGIC_OR_INVERTED:
		case GE_LOGIC_NOR:
		case GE_LOGIC_NAND:
		case GE_LOGIC_EQUIV:
			return LOGICOPTYPE_INVERT;
		case GE_LOGIC_INVERTED:
			return LOGICOPTYPE_ONE;
		case GE_LOGIC_SET:
			return LOGICOPTYPE_ONE;
		default:
			return LOGICOPTYPE_NORMAL;
		}
	}
	return LOGICOPTYPE_NORMAL;
}


void ConvertViewportAndScissor(bool useBufferedRendering, float renderWidth, float renderHeight, int bufferWidth, int bufferHeight, ViewportAndScissor &out) {
	bool throughmode = gstate.isModeThrough();
	out.dirtyProj = false;

	float renderWidthFactor, renderHeightFactor;
	float renderX = 0.0f, renderY = 0.0f;
	float displayOffsetX, displayOffsetY;
	if (useBufferedRendering) {
		displayOffsetX = 0.0f;
		displayOffsetY = 0.0f;
		renderWidthFactor = (float)renderWidth / (float)bufferWidth;
		renderHeightFactor = (float)renderHeight / (float)bufferHeight;
	} else {
		float pixelW = PSP_CoreParameter().pixelWidth;
		float pixelH = PSP_CoreParameter().pixelHeight;
		CenterDisplayOutputRect(&displayOffsetX, &displayOffsetY, &renderWidth, &renderHeight, 480, 272, pixelW, pixelH, ROTATION_LOCKED_HORIZONTAL);
		renderWidthFactor = renderWidth / 480.0f;
		renderHeightFactor = renderHeight / 272.0f;
	}

	renderX += gstate_c.curRTOffsetX * renderWidthFactor;

	// Scissor
	int scissorX1 = gstate.getScissorX1();
	int scissorY1 = gstate.getScissorY1();
	int scissorX2 = gstate.getScissorX2() + 1;
	int scissorY2 = gstate.getScissorY2() + 1;

	// This is a bit of a hack as the render buffer isn't always that size
	// We always scissor on non-buffered so that clears don't spill outside the frame.
	if (useBufferedRendering && scissorX1 == 0 && scissorY1 == 0
		&& scissorX2 >= (int)gstate_c.curRTWidth
		&& scissorY2 >= (int)gstate_c.curRTHeight) {
		out.scissorEnable = false;
	} else {
		out.scissorEnable = true;
		out.scissorX = renderX + displayOffsetX + scissorX1 * renderWidthFactor;
		out.scissorY = renderY + displayOffsetY + scissorY1 * renderHeightFactor;
		out.scissorW = (scissorX2 - scissorX1) * renderWidthFactor;
		out.scissorH = (scissorY2 - scissorY1) * renderHeightFactor;
	}

	int curRTWidth = gstate_c.curRTWidth;
	int curRTHeight = gstate_c.curRTHeight;

	float offsetX = gstate.getOffsetX();
	float offsetY = gstate.getOffsetY();

	if (throughmode) {
		// No viewport transform here. Let's experiment with using region.
		out.viewportX = renderX + displayOffsetX;
		out.viewportY = renderY + displayOffsetY;
		out.viewportW = curRTWidth * renderWidthFactor;
		out.viewportH = curRTHeight * renderHeightFactor;
		out.depthRangeMin = 0.0f;
		out.depthRangeMax = 1.0f;
	} else {
		// These we can turn into a glViewport call, offset by offsetX and offsetY. Math after.
		float vpXScale = gstate.getViewportXScale();
		float vpXCenter = gstate.getViewportXCenter();
		float vpYScale = gstate.getViewportYScale();
		float vpYCenter = gstate.getViewportYCenter();

		// The viewport transform appears to go like this:
		// Xscreen = -offsetX + vpXCenter + vpXScale * Xview
		// Yscreen = -offsetY + vpYCenter + vpYScale * Yview
		// Zscreen = vpZCenter + vpZScale * Zview

		// The viewport is normally centered at 2048,2048 but can also be centered at other locations.
		// Offset is subtracted from the viewport center and is also set to values in those ranges, and is set so that the viewport will cover
		// the desired screen area ([0-480)x[0-272)), so 1808,1912.

		// This means that to get the analogue glViewport we must:
		float vpX0 = vpXCenter - offsetX - fabsf(vpXScale);
		float vpY0 = vpYCenter - offsetY - fabsf(vpYScale);
		gstate_c.vpWidth = vpXScale * 2.0f;
		gstate_c.vpHeight = vpYScale * 2.0f;

		float vpWidth = fabsf(gstate_c.vpWidth);
		float vpHeight = fabsf(gstate_c.vpHeight);

		// This multiplication should probably be done after viewport clipping. Would let us very slightly simplify the clipping logic?
		vpX0 *= renderWidthFactor;
		vpY0 *= renderHeightFactor;
		vpWidth *= renderWidthFactor;
		vpHeight *= renderHeightFactor;

		// We used to apply the viewport here via glstate, but there are limits which vary by driver.
		// This may mean some games won't work, or at least won't work at higher render resolutions.
		// So we apply it in the shader instead.
		float left = renderX + vpX0;
		float top = renderY + vpY0;
		float right = left + vpWidth;
		float bottom = top + vpHeight;

		float wScale = 1.0f;
		float xOffset = 0.0f;
		float hScale = 1.0f;
		float yOffset = 0.0f;

		// If we're within the bounds, we want clipping the viewport way.  So leave it be.
		if (left < 0.0f || right > renderWidth) {
			float overageLeft = std::max(-left, 0.0f);
			float overageRight = std::max(right - renderWidth, 0.0f);
			// Our center drifted by the difference in overages.
			float drift = overageRight - overageLeft;

			left += overageLeft;
			right -= overageRight;

			wScale = vpWidth / (right - left);
			xOffset = drift / (right - left);
		}

		if (top < 0.0f || bottom > renderHeight) {
			float overageTop = std::max(-top, 0.0f);
			float overageBottom = std::max(bottom - renderHeight, 0.0f);
			// Our center drifted by the difference in overages.
			float drift = overageBottom - overageTop;

			top += overageTop;
			bottom -= overageBottom;

			hScale = vpHeight / (bottom - top);
			yOffset = drift / (bottom - top);
		}

		bool scaleChanged = gstate_c.vpWidthScale != wScale || gstate_c.vpHeightScale != hScale;
		bool offsetChanged = gstate_c.vpXOffset != xOffset || gstate_c.vpYOffset != yOffset;
		if (scaleChanged || offsetChanged) {
			gstate_c.vpWidthScale = wScale;
			gstate_c.vpHeightScale = hScale;
			gstate_c.vpXOffset = xOffset;
			gstate_c.vpYOffset = yOffset;
			out.dirtyProj = true;
		}

		out.viewportX = left + displayOffsetX;
		out.viewportY = top + displayOffsetY;
		out.viewportW = right - left;
		out.viewportH = bottom - top;

		float zScale = gstate.getViewportZScale();
		float zCenter = gstate.getViewportZCenter();
		float depthRangeMin = zCenter - zScale;
		float depthRangeMax = zCenter + zScale;
		out.depthRangeMin = depthRangeMin * (1.0f / 65535.0f);
		out.depthRangeMax = depthRangeMax * (1.0f / 65535.0f);

#ifndef MOBILE_DEVICE
		float minz = gstate.getDepthRangeMin();
		float maxz = gstate.getDepthRangeMax();
		if ((minz > depthRangeMin && minz > depthRangeMax) || (maxz < depthRangeMin && maxz < depthRangeMax)) {
			WARN_LOG_REPORT_ONCE(minmaxz, G3D, "Unsupported depth range in test - depth range: %f-%f, test: %f-%f", depthRangeMin, depthRangeMax, minz, maxz);
		} else if ((gstate.clipEnable & 1) == 0) {
			// TODO: Need to test whether clipEnable should even affect depth or not.
			if ((minz < depthRangeMin && minz < depthRangeMax) || (maxz > depthRangeMin && maxz > depthRangeMax)) {
				WARN_LOG_REPORT_ONCE(znoclip, G3D, "Unsupported depth range in test without clipping - depth range: %f-%f, test: %f-%f", depthRangeMin, depthRangeMax, minz, maxz);
			}
		}
#endif
	}
}
