#include "StateMapping.h"
#include "native/gfx_es2/gl_state.h"

#include "GPU/Math3D.h"
#include "GPU/GPUState.h"
#include "GPU/ge_constants.h"
#include "Core/System.h"
#include "Core/Config.h"
#include "DisplayListInterpreter.h"
#include "ShaderManager.h"
#include "TextureCache.h"

const GLint aLookup[11] = {
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

const GLint bLookup[11] = {
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

const GLint eqLookup[] = {
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

const GLint cullingMode[] = {
	GL_BACK,
	GL_FRONT,
};

const GLuint ztests[] = 
{
	GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, 
	GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL,
};

const GLuint stencilOps[] = {
	GL_KEEP,
	GL_ZERO,
	GL_REPLACE,
	GL_INVERT,
	GL_INCR_WRAP,
	GL_DECR_WRAP,  // don't know if these should be wrap or not
	GL_KEEP, // reserved
	GL_KEEP, // reserved
};

void TransformDrawEngine::ApplyDrawState(int prim) {
	// TODO: All this setup is soon so expensive that we'll need dirty flags, or simply do it in the command writes where we detect dirty by xoring. Silly to do all this work on every drawcall.

	if (gstate_c.textureChanged) {
		if ((gstate.textureMapEnable & 1) && !gstate.isModeClear()) {
			textureCache_->SetTexture();
		}
		gstate_c.textureChanged = false;
	}

	// TODO: The top bit of the alpha channel should be written to the stencil bit somehow. This appears to require very expensive multipass rendering :( Alternatively, one could do a
	// single fullscreen pass that converts alpha to stencil (or 2 passes, to set both the 0 and 1 values) very easily.

	// Set blend
	bool wantBlend = !gstate.isModeClear() && (gstate.alphaBlendEnable & 1);
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

		glstate.blendEquation.set(eqLookup[blendFuncEq]);

		if (blendFuncA != GE_SRCBLEND_FIXA && blendFuncB != GE_DSTBLEND_FIXB) {
			// All is valid, no blendcolor needed
			glstate.blendFunc.set(aLookup[blendFuncA], bLookup[blendFuncB]);
		} else {
			GLuint glBlendFuncA = blendFuncA == GE_SRCBLEND_FIXA ? GL_INVALID_ENUM : aLookup[blendFuncA];
			GLuint glBlendFuncB = blendFuncB == GE_DSTBLEND_FIXB ? GL_INVALID_ENUM : bLookup[blendFuncB];
			u32 fixA = gstate.getFixA();
			u32 fixB = gstate.getFixB();
			// Shortcut by using GL_ONE where possible, no need to set blendcolor
			if (glBlendFuncA == GL_INVALID_ENUM && blendFuncA == GE_SRCBLEND_FIXA) {
				if (fixA == 0xFFFFFF)
					glBlendFuncA = GL_ONE;
				else if (fixA == 0)
					glBlendFuncA = GL_ZERO;
			} 
			if (glBlendFuncB == GL_INVALID_ENUM && blendFuncB == GE_DSTBLEND_FIXB) {
				if (fixB == 0xFFFFFF)
					glBlendFuncB = GL_ONE;
				else if (fixB == 0)
					glBlendFuncB = GL_ZERO;
			}
			if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB != GL_INVALID_ENUM) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {(fixA & 0xFF)/255.0f, ((fixA >> 8) & 0xFF)/255.0f, ((fixA >> 16) & 0xFF)/255.0f, 1.0f};
				glstate.blendColor.set(blendColor);
				glBlendFuncA = GL_CONSTANT_COLOR;
			} else if (glBlendFuncA != GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {
				// Can use blendcolor trivially.
				const float blendColor[4] = {(fixB & 0xFF)/255.0f, ((fixB >> 8) & 0xFF)/255.0f, ((fixB >> 16) & 0xFF)/255.0f, 1.0f};
				glstate.blendColor.set(blendColor);
				glBlendFuncB = GL_CONSTANT_COLOR;
			} else if (glBlendFuncA == GL_INVALID_ENUM && glBlendFuncB == GL_INVALID_ENUM) {  // Should also check for approximate equality
				if (fixA == (fixB ^ 0xFFFFFF)) {
					glBlendFuncA = GL_CONSTANT_COLOR;
					glBlendFuncB = GL_ONE_MINUS_CONSTANT_COLOR;
					const float blendColor[4] = {(fixA & 0xFF)/255.0f, ((fixA >> 8) & 0xFF)/255.0f, ((fixA >> 16) & 0xFF)/255.0f, 1.0f};
					glstate.blendColor.set(blendColor);
				} else if (fixA == fixB) {
					glBlendFuncA = GL_CONSTANT_COLOR;
					glBlendFuncB = GL_CONSTANT_COLOR;
					const float blendColor[4] = {(fixA & 0xFF)/255.0f, ((fixA >> 8) & 0xFF)/255.0f, ((fixA >> 16) & 0xFF)/255.0f, 1.0f};
					glstate.blendColor.set(blendColor);
				} else {
					DEBUG_LOG(HLE, "ERROR INVALID blendcolorstate: FixA=%06x FixB=%06x FuncA=%i FuncB=%i", gstate.getFixA(), gstate.getFixB(), gstate.getBlendFuncA(), gstate.getBlendFuncB());
					glBlendFuncA = GL_ONE;
					glBlendFuncB = GL_ONE;
				}
			}
			// At this point, through all paths above, glBlendFuncA and glBlendFuncB will be set somehow.

			glstate.blendFunc.set(glBlendFuncA, glBlendFuncB);
		}
	}

	// Dither
	glstate.dither.set(gstate.ditherEnable & 1);
	// Set cull

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
		u8 cullMode = gstate.getCullMode();
		bool wantCull = !gstate.isModeThrough() && prim != GE_PRIM_RECTANGLES && gstate.isCullEnabled();
		glstate.cullFace.set(wantCull);
		glstate.cullFaceMode.set(cullingMode[cullMode]);

		if (gstate.isDepthTestEnabled()) {
			glstate.depthTest.enable();
			glstate.depthFunc.set(ztests[gstate.getDepthTestFunc()]);
			glstate.depthWrite.set(gstate.isDepthWriteEnabled() ? GL_TRUE : GL_FALSE);
		} else {
			glstate.depthTest.disable();
		}

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
}

void TransformDrawEngine::UpdateViewportAndProjection() {
	int renderWidth, renderHeight;
	if (g_Config.bBufferedRendering) {
	  renderWidth = framebufferManager_->GetRenderWidth();
	  renderHeight = framebufferManager_->GetRenderHeight();
	} else {
		// TODO: Aspect-ratio aware and centered
		renderWidth = PSP_CoreParameter().pixelWidth;
		renderHeight = PSP_CoreParameter().pixelHeight;
	}
	float renderWidthFactor = (float)renderWidth / framebufferManager_->GetTargetWidth();
	float renderHeightFactor = (float)renderHeight / framebufferManager_->GetTargetHeight();
	bool throughmode = (gstate.vertType & GE_VTYPE_THROUGH_MASK) != 0;

	// We can probably use these to simply set scissors? Maybe we need to offset by regionX1/Y1
	int regionX1 = gstate.region1 & 0x3FF;
	int regionY1 = (gstate.region1 >> 10) & 0x3FF;
	int regionX2 = (gstate.region2 & 0x3FF) + 1;
	int regionY2 = ((gstate.region2 >> 10) & 0x3FF) + 1;

	float offsetX = (float)(gstate.offsetx & 0xFFFF) / 16.0f;
	float offsetY = (float)(gstate.offsety & 0xFFFF) / 16.0f;

	if (throughmode) {
		// No viewport transform here. Let's experiment with using region.
		glstate.viewport.set((0 + regionX1) * renderWidthFactor, (0 - regionY1) * renderHeightFactor, (regionX2 - regionX1) * renderWidthFactor, (regionY2 - regionY1) * renderHeightFactor);
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
		glstate.viewport.set(vpX0, vpY0, vpWidth, vpHeight);
		// Sadly, as glViewport takes integers, we will not be able to support sub pixel offsets this way. But meh.
		// shaderManager_->DirtyUniform(DIRTY_PROJMATRIX);

		float zScale = getFloat24(gstate.viewportz1) / 65535.0f;
		float zOff = getFloat24(gstate.viewportz2) / 65535.0f;
		float depthRangeMin = zOff - zScale;
		float depthRangeMax = zOff + zScale;
		glstate.depthRange.set(depthRangeMin, depthRangeMax);
	}
}
