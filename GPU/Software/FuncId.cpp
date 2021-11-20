// Copyright (c) 2021- PPSSPP Project.

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

#include "GPU/Software/FuncId.h"
#include "GPU/GPUState.h"

void ComputePixelFuncID(PixelFuncID *id) {
	id->fullKey = 0;

	// TODO: Could this be minz > 0x0000 || maxz < 0xFFFF?  Maybe unsafe, depending on verts...
	id->applyDepthRange = !gstate.isModeThrough();
	// Dither happens even in clear mode.
	id->dithering = gstate.isDitherEnabled();
	id->fbFormat = gstate.FrameBufFormat();
	id->useStandardStride = gstate.FrameBufStride() == 512 && gstate.DepthBufStride() == 512;
	id->applyColorWriteMask = gstate.getColorMask() != 0;

	id->clearMode = gstate.isModeClear();
	if (id->clearMode) {
		id->colorClear = gstate.isClearModeColorMask();
		id->stencilClear = gstate.isClearModeAlphaMask();
		id->depthClear = gstate.isClearModeDepthMask();
	} else {
		id->colorTest = gstate.isColorTestEnabled() && gstate.getColorTestFunction() != GE_COMP_ALWAYS;
		if (gstate.isStencilTestEnabled() && gstate.getStencilTestFunction() == GE_COMP_ALWAYS) {
			// If stencil always passes, force off when we won't write any stencil bits.
			bool stencilWrite = (gstate.pmska & 0xFF) != 0xFF && gstate.FrameBufFormat() != GE_FORMAT_565;
			if (gstate.isDepthTestEnabled() && gstate.getDepthTestFunction() != GE_COMP_ALWAYS)
				id->stencilTest = stencilWrite && (gstate.getStencilOpZPass() != GE_STENCILOP_KEEP || gstate.getStencilOpZFail() != GE_STENCILOP_KEEP);
			else
				id->stencilTest = stencilWrite && gstate.getStencilOpZPass() != GE_STENCILOP_KEEP;
		} else {
			id->stencilTest = gstate.isStencilTestEnabled();
		}
		id->depthWrite = gstate.isDepthTestEnabled() && gstate.isDepthWriteEnabled();

		if (id->stencilTest) {
			id->stencilTestFunc = gstate.getStencilTestFunction();
			id->stencilTestRef = gstate.getStencilTestRef() & gstate.getStencilTestMask();
			id->hasStencilTestMask = gstate.getStencilTestMask() != 0xFF;
			id->sFail = gstate.getStencilOpSFail();
			id->zFail = gstate.isDepthTestEnabled() ? gstate.getStencilOpZFail() : GE_STENCILOP_KEEP;
			id->zPass = gstate.getStencilOpZPass();
		}

		id->depthTestFunc = gstate.isDepthTestEnabled() ? gstate.getDepthTestFunction() : GE_COMP_ALWAYS;
		id->alphaTestFunc = gstate.isAlphaTestEnabled() ? gstate.getAlphaTestFunction() : GE_COMP_ALWAYS;
		if (id->alphaTestFunc != GE_COMP_ALWAYS) {
			id->alphaTestRef = gstate.getAlphaTestRef() & gstate.getAlphaTestMask();
			id->hasAlphaTestMask = gstate.getAlphaTestMask() != 0xFF;
		}

		id->alphaBlend = gstate.isAlphaBlendEnabled();
		if (id->alphaBlend) {
			id->alphaBlendEq = gstate.getBlendEq();
			id->alphaBlendSrc = gstate.getBlendFuncA();
			id->alphaBlendDst = gstate.getBlendFuncB();
		}

		id->applyLogicOp = gstate.isLogicOpEnabled() && gstate.getLogicOp() != GE_LOGIC_COPY;
		id->applyFog = gstate.isFogEnabled() && !gstate.isModeThrough();
	}
}
