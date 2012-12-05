#include "gl_state.h"

OpenGLState glstate;

void OpenGLState::Initialize() {
	if(initialized) return;

	Restore();

	initialized = true;
}

void OpenGLState::Restore() {
	blend.restore();
	blendEquation.restore();
	blendFunc.restore();
	blendColor.restore();

	scissorTest.restore();

	cullFace.restore();
	cullFaceMode.restore();
	frontFace.restore();

	depthTest.restore();
	depthRange.restore();
	depthFunc.restore();
}
