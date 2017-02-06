#include <stdlib.h>

#include "base/logging.h"

#include "gfx/GLStateCache.h"

OpenGLState glstate;

int OpenGLState::state_count = 0;

void OpenGLState::Restore() {
	int count = 0;

	blend.restore(); count++;
	blendEquationSeparate.restore(); count++;
	blendFuncSeparate.restore(); count++;
	blendColor.restore(); count++;

	scissorTest.restore(); count++;
	scissorRect.restore(); count++;

	cullFace.restore(); count++;
	cullFaceMode.restore(); count++;
	frontFace.restore(); count++;

	depthTest.restore(); count++;
	depthRange.restore(); count++;
	depthFunc.restore(); count++;
	depthWrite.restore(); count++;

	colorMask.restore(); count++;
	viewport.restore(); count++;

	stencilTest.restore(); count++;
	stencilOp.restore(); count++;
	stencilFunc.restore(); count++;
	stencilMask.restore(); count++;

	dither.restore(); count++;

#if !defined(USING_GLES2)
	colorLogicOp.restore(); count++;
	logicOp.restore(); count++;
#endif

	arrayBuffer.restore(); count++;
	elementArrayBuffer.restore(); count++;

	if (count != state_count) {
		FLOG("OpenGLState::Restore is missing some states");
	}
}
