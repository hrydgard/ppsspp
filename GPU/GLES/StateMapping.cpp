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

#include "StateMapping.h"
#include "native/gfx_es2/gl_state.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "Core/Reporting.h"
#include "DisplayListInterpreter.h"
#include "ShaderManager.h"
#include "TextureCache.h"
#include "Framebuffer.h"

static const GLushort aLookup[11] = {
	GL_DST_COLOR,
	GL_ONE_MINUS_DST_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_SRC_ALPHA,	// should be 2x
	GL_ONE_MINUS_SRC_ALPHA,	 // should be 2x
	GL_DST_ALPHA,	 // should be 2x
	GL_ONE_MINUS_DST_ALPHA,	 // should be 2x	-	and COLOR?
	GL_CONSTANT_COLOR,	// FIXA
};

static const GLushort bLookup[11] = {
	GL_SRC_COLOR,
	GL_ONE_MINUS_SRC_COLOR,
	GL_SRC_ALPHA,
	GL_ONE_MINUS_SRC_ALPHA,
	GL_DST_ALPHA,
	GL_ONE_MINUS_DST_ALPHA,
	GL_SRC_ALPHA,	// should be 2x
	GL_ONE_MINUS_SRC_ALPHA,	 // should be 2x
	GL_DST_ALPHA,	 // should be 2x
	GL_ONE_MINUS_DST_ALPHA,	 // should be 2x
	GL_CONSTANT_COLOR,	// FIXB
};

static const GLushort eqLookup[] = {
	GL_FUNC_ADD,
	GL_FUNC_SUBTRACT,
	GL_FUNC_REVERSE_SUBTRACT,
#if defined(USING_GLES2)
	GL_FUNC_ADD,
	GL_FUNC_ADD,
#else
	GL_MIN,
	GL_MAX,
#endif
	GL_FUNC_ADD, // should be abs(diff)
};

static const GLushort cullingMode[] = {
	GL_BACK,
	GL_FRONT,
};

static const GLushort ztests[] = {
	GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, 
	GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL,
};

static const GLushort stencilOps[] = {
	GL_KEEP,
	GL_ZERO,
	GL_REPLACE,
	GL_INVERT,
	GL_INCR_WRAP,
	GL_DECR_WRAP,  // don't know if these should be wrap or not
	GL_KEEP, // reserved
	GL_KEEP, // reserved
};

static GLenum blendColor2Func(u32 fix) {
	if (fix == 0xFFFFFF)
		return GL_ONE;
	if (fix == 0)
		return GL_ZERO;

	Vec3 fix3 = Vec3(fix);
	if (fix3.x >= 0.99 && fix3.y >= 0.99 && fix3.z >= 0.99)
		return GL_ONE;
	else if (fix3.x <= 0.01 && fix3.y <= 0.01 && fix3.z <= 0.01)
		return GL_ZERO;
	return GL_INVALID_ENUM;
}

static bool blendColorSimilar(Vec3 a, Vec3 b, float margin = 0.1f) {
	Vec3 diff = a - b;
	if (fabsf(diff.x) <= margin && fabsf(diff.y) <= margin && fabsf(diff.z) <= margin)
		return true;
	return false;
}

void TransformDrawEngine::ApplyDrawState(int prim) {
	// TODO: All this setup is soon so expensive that we'll need dirty flags, or simply do it in the command writes where we detect dirty by xoring. Silly to do all this work on every drawcall.

	if (gstate_c.textureChanged) {
		if (gstate.textureMapEnable & 1) {
			textureCache_->SetTexture();
		}
		gstate_c.textureChanged = false;
	}

	// Set cull
	bool wantCull = !gstate.isModeClear() && !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
	glstate.cullFace.set(wantCull);
	if (wantCull) 
		glstate.cullFaceMode.set(cullingMode[gstate.getCullMode()]);

	// TODO: The top bit of the alpha channel should be written to the stencil bit somehow. This appears to require very expensive multipass rendering :( Alternatively, one could do a
	// single fullscreen pass that converts alpha to stencil (or 2 passes, to set both the 0 and 1 values) very easily.

	// Set blend
	bool wantBlend = !gstate.isModeClear() && gstate.isAlphaBlendEnabled();
	glstate.blend.set(wantBlend);
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

		// Shortcut by using GL_ONE where possible, no need to set blendcolor
		GLuint glBlendFuncA = blendFuncA == GE_SRCBLEND_FIXA ? blendColor2Func(gstate.getFixA()) : aLookup[blendFuncA];
		GLuint glBlendFuncB = blendFuncB == GE_DSTBLEND_FIXB ? blendColor2Func(gstate.getFixB()) : bLookup[blendFuncB];
		if (blendFuncA == GE_SRCBLEND_FIXA || blendFuncB == GE_DSTBLEND_FIXB) {
			Vec3 fixA = Vec3(gstate.getFixA());
			Vec3 fixB = Vec3(gstate.getFixB());
			if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB != GL_INVALID_ENUM) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {fixA.x, fixA.y, fixA.z, 1.0f};
				glstate.blendColor.set(blendColor);
				glBlendFuncA = GL_CONSTANT_COLOR;
			} else if (glBlendFuncA != GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {fixB.x, fixB.y, fixB.z, 1.0f};
				glstate.blendColor.set(blendColor);
				glBlendFuncB = GL_CONSTANT_COLOR;
			} else if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {
				if (blendColorSimilar(fixA, Vec3(1.0f) - fixB)) {
					glBlendFuncA = GL_CONSTANT_COLOR;
					glBlendFuncB = GL_ONE_MINUS_CONSTANT_COLOR;
					const float blendColor[4] = {fixA.x, fixA.y, fixA.z, 1.0f};
					glstate.blendColor.set(blendColor);
				} else if (blendColorSimilar(fixA, fixB)) {
					glBlendFuncA = GL_CONSTANT_COLOR;
					glBlendFuncB = GL_CONSTANT_COLOR;
					const float blendColor[4] = {fixA.x, fixA.y, fixA.z, 1.0f};
					glstate.blendColor.set(blendColor);
				} else {
					static bool didReportBlend = false;
					if (!didReportBlend)
						Reporting::ReportMessage("ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
					didReportBlend = true;

					DEBUG_LOG(HLE, "ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
					// Let's approximate, at least.  Close is better than totally off.
					const bool nearZeroA = blendColorSimilar(fixA, 0.0f, 0.25f);
					const bool nearZeroB = blendColorSimilar(fixB, 0.0f, 0.25f);
					if (nearZeroA || blendColorSimilar(fixA, 1.0f, 0.25f)) {
						glBlendFuncA = nearZeroA ? GL_ZERO : GL_ONE;
						glBlendFuncB = GL_CONSTANT_COLOR;
						const float blendColor[4] = {fixB.x, fixB.y, fixB.z, 1.0f};
						glstate.blendColor.set(blendColor);
					// We need to pick something.  Let's go with A as the fixed color.
					} else {
						glBlendFuncA = GL_CONSTANT_COLOR;
						glBlendFuncB = nearZeroB ? GL_ZERO : GL_ONE;
						const float blendColor[4] = {fixA.x, fixA.y, fixA.z, 1.0f};
						glstate.blendColor.set(blendColor);
					}
				}
			}
		}

		// At this point, through all paths above, glBlendFuncA and glBlendFuncB will be set right somehow.
		glstate.blendFunc.set(glBlendFuncA, glBlendFuncB);
		glstate.blendEquation.set(eqLookup[blendFuncEq]);
	}

	// Set Dither
	if (gstate.isDitherEnabled()) {
		glstate.dither.enable();
		glstate.dither.set(GL_TRUE);
	} else
		glstate.dither.disable();

	// Set ColorMask/Stencil/Depth
	if (gstate.isModeClear()) {
		bool colorMask = (gstate.clearmode >> 8) & 1;
		bool alphaMask = (gstate.clearmode >> 9) & 1;
		bool depthMask = (gstate.clearmode >> 10) & 1;
		glstate.colorMask.set(colorMask, colorMask, colorMask, alphaMask);

		glstate.stencilTest.enable();
		glstate.stencilOp.set(GL_REPLACE, GL_REPLACE, GL_REPLACE);
		glstate.stencilFunc.set(GL_ALWAYS, 0, 0xFF);

		glstate.depthTest.enable();
		glstate.depthFunc.set(GL_ALWAYS);
		glstate.depthWrite.set(depthMask ? GL_TRUE : GL_FALSE);

	} else {
		if (gstate.isDepthTestEnabled()) {
			glstate.depthTest.enable();
			glstate.depthFunc.set(ztests[gstate.getDepthTestFunc()]);
		} else {
			glstate.depthTest.disable();
		}

		glstate.depthWrite.set(gstate.isDepthWriteEnabled() ? GL_TRUE : GL_FALSE);

		// PSP color/alpha mask is per bit but we can only support per byte.
		// But let's do that, at least. And let's try a threshold.
		bool rmask = (gstate.pmskc & 0xFF) < 128;
		bool gmask = ((gstate.pmskc >> 8) & 0xFF) < 128;
		bool bmask = ((gstate.pmskc >> 16) & 0xFF) < 128;
		bool amask = (gstate.pmska & 0xFF) < 128;
		glstate.colorMask.set(rmask, gmask, bmask, amask);
		
		if (gstate.isStencilTestEnabled()) {
			glstate.stencilTest.enable();
			glstate.stencilFunc.set(ztests[gstate.stenciltest & 0x7],  // func
				(gstate.stenciltest >> 8) & 0xFF,  // ref
				(gstate.stenciltest >> 16) & 0xFF);  // mask
			glstate.stencilOp.set(stencilOps[gstate.stencilop & 0x7],  // stencil fail
				stencilOps[(gstate.stencilop >> 8) & 0x7],  // depth fail
				stencilOps[(gstate.stencilop >> 16) & 0x7]); // depth pass
		} else {
			glstate.stencilTest.disable();
		}
	}

	float renderWidthFactor, renderHeightFactor;
	float renderWidth, renderHeight;
	float renderX, renderY;
	if (g_Config.bBufferedRendering) {
		renderX = 0;
		renderY = 0;
		renderWidth = framebufferManager_->GetRenderWidth();
		renderHeight = framebufferManager_->GetRenderHeight();
		renderWidthFactor = (float)renderWidth / framebufferManager_->GetTargetWidth();
		renderHeightFactor = (float)renderHeight / framebufferManager_->GetTargetHeight();
	} else {
		// TODO: Aspect-ratio aware and centered
		float pixelW = PSP_CoreParameter().pixelWidth;
		float pixelH = PSP_CoreParameter().pixelHeight;
		CenterRect(&renderX, &renderY, &renderWidth, &renderHeight, 480, 272, pixelW, pixelH);
		renderWidthFactor = renderWidth / 480.f;
		renderHeightFactor = renderHeight / 272.f;
	}

	bool throughmode = (gstate.vertType & GE_VTYPE_THROUGH_MASK) != 0;

	// Scissor
	int scissorX1 = gstate.scissor1 & 0x3FF;
	int scissorY1 = (gstate.scissor1 >> 10) & 0x3FF;
	int scissorX2 = gstate.scissor2 & 0x3FF;
	int scissorY2 = (gstate.scissor2 >> 10) & 0x3FF;

	// This is a bit of a hack as the render buffer isn't always that size
	if (scissorX1 == 0 && scissorY1 == 0 
		&& scissorX2 >= (int) (gstate_c.curRTWidth - 1) 
		&& scissorY2 >= (int) (gstate_c.curRTHeight - 1)) {
		glstate.scissorTest.disable();
	} else {
		glstate.scissorTest.enable();
		glstate.scissorRect.set(
			renderX + scissorX1 * renderWidthFactor,
			renderY + renderHeight - (scissorY2 * renderHeightFactor),
			(scissorX2 - scissorX1 + 1) * renderWidthFactor,
			(scissorY2 - scissorY1 + 1) * renderHeightFactor);
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
		glstate.viewport.set(
			renderX + (0 + regionX1) * renderWidthFactor, 
			renderY + (0 - regionY1) * renderHeightFactor,
			(regionX2 - regionX1) * renderWidthFactor,
			(regionY2 - regionY1) * renderHeightFactor);
		glstate.depthRange.set(0.0f, 1.0f);
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
		gstate_c.vpWidth = vpXa * 2;
		gstate_c.vpHeight = -vpYa * 2;

		float vpWidth = fabsf(gstate_c.vpWidth);
		float vpHeight = fabsf(gstate_c.vpHeight);

		vpX0 *= renderWidthFactor;
		vpY0 *= renderHeightFactor;
		vpWidth *= renderWidthFactor;
		vpHeight *= renderHeightFactor;

		// Flip vpY0 to match the OpenGL coordinate system.
		vpY0 = renderHeight - (vpY0 + vpHeight);
		glstate.viewport.set(vpX0 + renderX, vpY0 + renderY, vpWidth, vpHeight);
		// Sadly, as glViewport takes integers, we will not be able to support sub pixel offsets this way. But meh.
		// shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);

		float zScale = getFloat24(gstate.viewportz1) / 65535.0f;
		float zOff = getFloat24(gstate.viewportz2) / 65535.0f;
		float depthRangeMin = zOff - zScale;
		float depthRangeMax = zOff + zScale;
		glstate.depthRange.set(depthRangeMin, depthRangeMax);
	}
}
