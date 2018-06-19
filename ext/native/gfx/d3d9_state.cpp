#ifdef _WIN32

#include "d3d9_state.h"
#include <assert.h>

namespace DX9 {

DirectXState dxstate;
GLExtensions gl_extensions;

LPDIRECT3DDEVICE9 pD3Ddevice = nullptr;
LPDIRECT3DDEVICE9EX pD3DdeviceEx = nullptr;

int DirectXState::state_count = 0;

void DirectXState::Initialize() {
	if (initialized)
		return;

	Restore();

	initialized = true;
}

void DirectXState::Restore() {
	int count = 0;

	blend.restore(); count++;
	blendSeparate.restore(); count++;
	blendEquation.restore(); count++;
	blendFunc.restore(); count++;
	blendColor.restore(); count++;

	scissorTest.restore(); count++;
	scissorRect.restore(); count++;

	cullMode.restore(); count++;
	shadeMode.restore(); count++;

	depthTest.restore(); count++;
	depthFunc.restore(); count++;
	depthWrite.restore(); count++;

	colorMask.restore(); count++;

	viewport.restore(); count++;

	alphaTest.restore(); count++;
	alphaTestFunc.restore(); count++;
	alphaTestRef.restore(); count++;

	stencilTest.restore(); count++;
	stencilOp.restore(); count++;
	stencilFunc.restore(); count++;
	stencilMask.restore(); count++;

	dither.restore(); count++;

	texMinFilter.restore(); count++;
	texMagFilter.restore(); count++;
	texMipFilter.restore(); count++;
	texMipLodBias.restore(); count++;
	texMaxMipLevel.restore(); count++;
	texAddressU.restore(); count++;
	texAddressV.restore(); count++;
}

void CheckGLExtensions() {
	static bool done = false;
	if (done)
		return;
	done = true;
	memset(&gl_extensions, 0, sizeof(gl_extensions));
}

void DirectXState::SetVSyncInterval(int interval) {
}

}  // namespace DX9

#endif  // _MSC_VER
