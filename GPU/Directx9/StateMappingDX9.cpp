// Copyright (c) 2012- PPSSPP Project.

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


#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "GPU/Directx9/StateMappingDX9.h"
#include "GPU/Directx9/GPU_DX9.h"
#include "GPU/Directx9/ShaderManagerDX9.h"
#include "GPU/Directx9/TextureCacheDX9.h"
#include "GPU/Directx9/FramebufferDX9.h"

namespace DX9 {

static const D3DBLEND aLookup[11] = {
	D3DBLEND_DESTCOLOR,
	D3DBLEND_INVDESTCOLOR,
	D3DBLEND_SRCALPHA,
	D3DBLEND_INVSRCALPHA,
	D3DBLEND_DESTALPHA,
	D3DBLEND_INVDESTALPHA,
	D3DBLEND_SRCALPHA,	// should be 2x
	D3DBLEND_INVSRCALPHA,	 // should be 2x
	D3DBLEND_DESTALPHA,	 // should be 2x
	D3DBLEND_INVDESTALPHA,	 // should be 2x	-	and COLOR?
	D3DBLEND_BLENDFACTOR,	// FIXA
};

static const D3DBLEND bLookup[11] = {
	D3DBLEND_SRCCOLOR,
	D3DBLEND_INVSRCCOLOR,
	D3DBLEND_SRCALPHA,
	D3DBLEND_INVSRCALPHA,
	D3DBLEND_DESTALPHA,
	D3DBLEND_INVDESTALPHA,
	D3DBLEND_SRCALPHA,	// should be 2x
	D3DBLEND_INVSRCALPHA,	 // should be 2x
	D3DBLEND_DESTALPHA,	 // should be 2x
	D3DBLEND_INVDESTALPHA,	 // should be 2x
	D3DBLEND_BLENDFACTOR,	// FIXB
};

static const D3DBLENDOP eqLookup[] = {
	D3DBLENDOP_ADD,
	D3DBLENDOP_SUBTRACT,
	D3DBLENDOP_REVSUBTRACT,
	D3DBLENDOP_MIN,
	D3DBLENDOP_MAX,
	D3DBLENDOP_ADD, // should be abs(diff)
};

static const D3DCULL cullingMode[] = {
	D3DCULL_CW,
	D3DCULL_CCW,
};

static const D3DCMPFUNC ztests[] = {
	D3DCMP_NEVER, D3DCMP_ALWAYS, D3DCMP_EQUAL, D3DCMP_NOTEQUAL, 
	D3DCMP_LESS, D3DCMP_LESSEQUAL, D3DCMP_GREATER, D3DCMP_GREATEREQUAL,
};

static const D3DSTENCILOP stencilOps[] = {
	D3DSTENCILOP_KEEP,
	D3DSTENCILOP_ZERO,
	D3DSTENCILOP_REPLACE,
	D3DSTENCILOP_INVERT,
	D3DSTENCILOP_INCR,
	D3DSTENCILOP_DECR,  // don't know if these should be wrap or not
	D3DSTENCILOP_KEEP, // reserved
	D3DSTENCILOP_KEEP, // reserved
};

static u32 blendColor2Func(u32 fix) {
	if (fix == 0xFFFFFF)
		return D3DBLEND_ONE;
	if (fix == 0)
		return D3DBLEND_ZERO;

	Vec3f fix3 = Vec3f::FromRGB(fix);
	if (fix3.x >= 0.99 && fix3.y >= 0.99 && fix3.z >= 0.99)
		return D3DBLEND_ONE;
	else if (fix3.x <= 0.01 && fix3.y <= 0.01 && fix3.z <= 0.01)
		return D3DBLEND_ZERO;
	return D3DBLEND_UNK;
}

static bool blendColorSimilar(Vec3f a, Vec3f b, float margin = 0.1f) {
	Vec3f diff = a - b;
	if (fabsf(diff.x) <= margin && fabsf(diff.y) <= margin && fabsf(diff.z) <= margin)
		return true;
	return false;
}

void TransformDrawEngineDX9::ApplyDrawState(int prim) {
	// TODO: All this setup is soon so expensive that we'll need dirty flags, or simply do it in the command writes where we detect dirty by xoring. Silly to do all this work on every drawcall.

	if (gstate_c.textureChanged) {
		if (gstate.isTextureMapEnabled()) {
			textureCache_->SetTexture();
		}
		gstate_c.textureChanged = false;
	}

	// TODO: The top bit of the alpha channel should be written to the stencil bit somehow. This appears to require very expensive multipass rendering :( Alternatively, one could do a
	// single fullscreen pass that converts alpha to stencil (or 2 passes, to set both the 0 and 1 values) very easily.

	// Set blend
	bool wantBlend = !gstate.isModeClear() && gstate.isAlphaBlendEnabled();
	dxstate.blend.set(wantBlend);
	if (wantBlend) {
		// This can't be done exactly as there are several PSP blend modes that are impossible to do on OpenGL ES 2.0, and some even on regular OpenGL for desktop.
		// HOWEVER - we should be able to approximate the 2x modes in the shader, although they will clip wrongly.

		// Examples of seen unimplementable blend states:
		// Mortal Kombat Unchained: FixA=0000ff FixB=000080 FuncA=10 FuncB=10

		int blendFuncA  = gstate.getBlendFuncA();
		int blendFuncB  = gstate.getBlendFuncB();
		int blendFuncEq = gstate.getBlendEq();
		if (blendFuncA > GE_SRCBLEND_FIXA) blendFuncA = GE_SRCBLEND_FIXA;
		if (blendFuncB > GE_DSTBLEND_FIXB) blendFuncB = GE_DSTBLEND_FIXB;

		// Shortcut by using D3DBLEND_ONE where possible, no need to set blendcolor
		u32 glBlendFuncA = blendFuncA == GE_SRCBLEND_FIXA ? blendColor2Func(gstate.getFixA()) : aLookup[blendFuncA];
		u32 glBlendFuncB = blendFuncB == GE_DSTBLEND_FIXB ? blendColor2Func(gstate.getFixB()) : bLookup[blendFuncB];
		if (blendFuncA == GE_SRCBLEND_FIXA || blendFuncB == GE_DSTBLEND_FIXB) {
			Vec3f fixA = Vec3f::FromRGB(gstate.getFixA());
			Vec3f fixB = Vec3f::FromRGB(gstate.getFixB());
			if (glBlendFuncA == D3DBLEND_UNK && glBlendFuncB != D3DBLEND_UNK) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {fixA.x, fixA.y, fixA.z, 1.0f};
				dxstate.blendColor.set(blendColor);
				glBlendFuncA = D3DBLEND_BLENDFACTOR;
			} else if (glBlendFuncA != D3DBLEND_UNK && glBlendFuncB == D3DBLEND_UNK) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {fixB.x, fixB.y, fixB.z, 1.0f};
				dxstate.blendColor.set(blendColor);
				glBlendFuncB = D3DBLEND_BLENDFACTOR;
			} else if (glBlendFuncA == D3DBLEND_UNK && glBlendFuncB == D3DBLEND_UNK) {
				if (blendColorSimilar(fixA, Vec3f::AssignToAll(1.0f) - fixB)) {
					glBlendFuncA = D3DBLEND_BLENDFACTOR;
					glBlendFuncB = D3DBLEND_INVBLENDFACTOR;
					const float blendColor[4] = {fixA.x, fixA.y, fixA.z, 1.0f};
					dxstate.blendColor.set(blendColor);
				} else if (blendColorSimilar(fixA, fixB)) {
					glBlendFuncA = D3DBLEND_BLENDFACTOR;
					glBlendFuncB = D3DBLEND_BLENDFACTOR;
					const float blendColor[4] = {fixA.x, fixA.y, fixA.z, 1.0f};
					dxstate.blendColor.set(blendColor);
				} else {
					static bool didReportBlend = false;
					if (!didReportBlend)
						Reporting::ReportMessage("ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
					didReportBlend = true;

					DEBUG_LOG(HLE, "ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
					// Let's approximate, at least.  Close is better than totally off.
					const bool nearZeroA = blendColorSimilar(fixA, Vec3f::AssignToAll(0.0f), 0.25f);
					const bool nearZeroB = blendColorSimilar(fixB, Vec3f::AssignToAll(0.0f), 0.25f);
					if (nearZeroA || blendColorSimilar(fixA, Vec3f::AssignToAll(1.0f), 0.25f)) {
						glBlendFuncA = nearZeroA ? D3DBLEND_ZERO : D3DBLEND_ONE;
						glBlendFuncB = D3DBLEND_BLENDFACTOR;
						const float blendColor[4] = {fixB.x, fixB.y, fixB.z, 1.0f};
						dxstate.blendColor.set(blendColor);
					// We need to pick something.  Let's go with A as the fixed color.
					} else {
						glBlendFuncA = D3DBLEND_BLENDFACTOR;
						glBlendFuncB = nearZeroB ? D3DBLEND_ZERO : D3DBLEND_ONE;
						const float blendColor[4] = {fixA.x, fixA.y, fixA.z, 1.0f};
						dxstate.blendColor.set(blendColor);
					}
				}
			}
		}

		// At this point, through all paths above, glBlendFuncA and glBlendFuncB will be set right somehow.
		dxstate.blendFunc.set(glBlendFuncA, glBlendFuncB);
		dxstate.blendEquation.set(eqLookup[blendFuncEq]);
	}

	// Set Dither
	if (gstate.isDitherEnabled()) {
		dxstate.dither.enable();
		dxstate.dither.set(true);
	} else
		dxstate.dither.disable();

	// Set ColorMask/Stencil/Depth
	if (gstate.isModeClear()) {

		// Set Cull 
		dxstate.cullMode.set(false, false);
		
		// Depth Test
		dxstate.depthTest.enable();
		dxstate.depthFunc.set(D3DCMP_ALWAYS);
		dxstate.depthWrite.set(gstate.isClearModeDepthWriteEnabled());
		
		// Color Test
		bool colorMask = (gstate.clearmode >> 8) & 1;
		bool alphaMask = (gstate.clearmode >> 9) & 1;
		dxstate.colorMask.set(colorMask, colorMask, colorMask, alphaMask);

		// Stencil Test
		if (alphaMask) {
			dxstate.stencilTest.enable();
			dxstate.stencilOp.set(D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE, D3DSTENCILOP_REPLACE);
			dxstate.stencilFunc.set(D3DCMP_ALWAYS, 0, 0xFF);
		} else {
			dxstate.depthTest.disable();
		}

	} else {
		
		// Set cull
		bool wantCull = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
		dxstate.cullMode.set(wantCull, gstate.getCullMode());	

		// Depth Test
		if (gstate.isDepthTestEnabled()) {
			dxstate.depthTest.enable();
			dxstate.depthFunc.set(ztests[gstate.getDepthTestFunction()]);
			dxstate.depthWrite.set(gstate.isDepthWriteEnabled());
		} else 
			dxstate.depthTest.disable();

		// PSP color/alpha mask is per bit but we can only support per byte.
		// But let's do that, at least. And let's try a threshold.
		bool rmask = (gstate.pmskc & 0xFF) < 128;
		bool gmask = ((gstate.pmskc >> 8) & 0xFF) < 128;
		bool bmask = ((gstate.pmskc >> 16) & 0xFF) < 128;
		bool amask = (gstate.pmska & 0xFF) < 128;
		dxstate.colorMask.set(rmask, gmask, bmask, amask);
		
		// Stencil Test
		if (gstate.isStencilTestEnabled()) {
			dxstate.stencilTest.enable();
			dxstate.stencilFunc.set(ztests[gstate.getStencilTestFunction()],
				gstate.getStencilTestRef(),
				gstate.getStencilTestMask());
			dxstate.stencilOp.set(stencilOps[gstate.getStencilOpSFail()],  // stencil fail
				stencilOps[gstate.getStencilOpZFail()],  // depth fail
				stencilOps[gstate.getStencilOpZPass()]); // depth pass
		} else {
			dxstate.stencilTest.disable();
		}
	}

#if 1
	// d3d only
	if (wantBlend) {
		GEComparison alphaTestFunc = gstate.getAlphaTestFunction();
		pD3Ddevice->SetRenderState(D3DRS_ALPHAFUNC, ztests[alphaTestFunc]);
		pD3Ddevice->SetRenderState(D3DRS_ALPHAREF, gstate.getAlphaTestRef());
	}
#endif

	float renderWidthFactor, renderHeightFactor;
	float renderWidth, renderHeight;
	float renderX, renderY;
	bool useBufferedRendering = g_Config.iRenderingMode != FB_NON_BUFFERED_MODE;
	if (useBufferedRendering) {
		renderX = 0.0f;
		renderY = 0.0f;
		renderWidth = framebufferManager_->GetRenderWidth();
		renderHeight = framebufferManager_->GetRenderHeight();
		renderWidthFactor = (float)renderWidth / framebufferManager_->GetTargetWidth();
		renderHeightFactor = (float)renderHeight / framebufferManager_->GetTargetHeight();
	} else {
		// TODO: Aspect-ratio aware and centered
		float pixelW = PSP_CoreParameter().pixelWidth;
		float pixelH = PSP_CoreParameter().pixelHeight;
		CenterRect(&renderX, &renderY, &renderWidth, &renderHeight, 480, 272, pixelW, pixelH);
		renderWidthFactor = renderWidth / 480.0f;
		renderHeightFactor = renderHeight / 272.0f;
	}

	bool throughmode = gstate.isModeThrough();

	// Scissor
	int scissorX1 = gstate.getScissorX1();
	int scissorY1 = gstate.getScissorY1();
	int scissorX2 = gstate.getScissorX2() + 1;
	int scissorY2 = gstate.getScissorY2() + 1;

	// This is a bit of a hack as the render buffer isn't always that size
	if (scissorX1 == 0 && scissorY1 == 0 
		&& scissorX2 >= (int) gstate_c.curRTWidth
		&& scissorY2 >= (int) gstate_c.curRTHeight) {
		dxstate.scissorTest.disable();
	} else {
		dxstate.scissorTest.enable();
		dxstate.scissorRect.set(
			renderX + scissorX1 * renderWidthFactor,
			renderY + scissorY1 * renderHeightFactor,
			renderY + scissorX2 * renderWidthFactor,
			renderY + scissorY2 * renderHeightFactor);
	}

	/*
	int regionX1 = gstate.region1 & 0x3FF;
	int regionY1 = (gstate.region1 >> 10) & 0x3FF;
	int regionX2 = (gstate.region2 & 0x3FF) + 1;
	int regionY2 = ((gstate.region2 >> 10) & 0x3FF) + 1;
	*/
	int regionX1 = 0;
	int regionY1 = 0;
	int regionX2 = gstate_c.curRTWidth;
	int regionY2 = gstate_c.curRTHeight;

	float offsetX = (float)(gstate.offsetx & 0xFFFF) / 16.0f;
	float offsetY = (float)(gstate.offsety & 0xFFFF) / 16.0f;

	if (throughmode) {
		// No viewport transform here. Let's experiment with using region.
		dxstate.viewport.set(
			renderX + (0 + regionX1) * renderWidthFactor, 
			renderY + (0 - regionY1) * renderHeightFactor,
			(regionX2 - regionX1) * renderWidthFactor,
			(regionY2 - regionY1) * renderHeightFactor,
			0.f, 1.f);
	} else {
		// These we can turn into a glViewport call, offset by offsetX and offsetY. Math after.
		float vpXa = getFloat24(gstate.viewportx1);
		float vpXb = getFloat24(gstate.viewportx2);
		float vpYa = getFloat24(gstate.viewporty1);
		float vpYb = getFloat24(gstate.viewporty2);

		// The viewport transform appears to go like this: 
		// Xscreen = -offsetX + vpXb + vpXa * Xview
		// Yscreen = -offsetY + vpYb + vpYa * Yview
		// Zscreen = vpZb + vpZa * Zview

		// This means that to get the analogue glViewport we must:
		float vpX0 = vpXb - offsetX - vpXa;
		float vpY0 = vpYb - offsetY + vpYa;   // Need to account for sign of Y
		gstate_c.vpWidth = vpXa * 2.0f;
		gstate_c.vpHeight = -vpYa * 2.0f;

		float vpWidth = fabsf(gstate_c.vpWidth);
		float vpHeight = fabsf(gstate_c.vpHeight);

		vpX0 *= renderWidthFactor;
		vpY0 *= renderHeightFactor;
		vpWidth *= renderWidthFactor;
		vpHeight *= renderHeightFactor;

		vpX0 = (vpXb - offsetX - fabsf(vpXa)) * renderWidthFactor;
		// Flip vpY0 to match the OpenGL coordinate system.
		vpY0 = renderHeight - (vpYb - offsetY + fabsf(vpYa)) * renderHeightFactor;		
		
		// Sadly, as glViewport takes integers, we will not be able to support sub pixel offsets this way. But meh.
		// shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);

		float zScale = getFloat24(gstate.viewportz1) / 65535.0f;
		float zOff = getFloat24(gstate.viewportz2) / 65535.0f;
		float depthRangeMin = zOff - zScale;
		float depthRangeMax = zOff + zScale;

		dxstate.viewport.set(vpX0 + renderX, vpY0 + renderY, vpWidth, vpHeight, depthRangeMin, depthRangeMax);
	}
}

};
