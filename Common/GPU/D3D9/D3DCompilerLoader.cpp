#include "ppsspp_config.h"

#include "Common/CommonWindows.h"
#include <D3Dcompiler.h>

#include "Common/Log.h"
#include "Common/GPU/D3D9/D3DCompilerLoader.h"

static HMODULE g_D3DCompileModule;
extern pD3DCompile ptr_D3DCompile;

static int d3dcompiler_version = 47;

bool LoadD3DCompilerDynamic() {
	g_D3DCompileModule = LoadLibrary(D3DCOMPILER_DLL);
#if PPSSPP_ARCH(X86)
	// Workaround for distributing both 32-bit and 64-bit versions of the DLL.
	if (!g_D3DCompileModule) {
		g_D3DCompileModule = LoadLibrary(L"D3dcompiler_47.x86.dll");
	}
#endif
	if (!g_D3DCompileModule) {
		g_D3DCompileModule = LoadLibrary(L"D3dcompiler_42.dll");
		d3dcompiler_version = 42;
	}

	if (!g_D3DCompileModule) {
		return false;
	} else {
		ptr_D3DCompile = (pD3DCompile)GetProcAddress(g_D3DCompileModule, "D3DCompile");
		return true;
	}
		
}

int GetD3DCompilerVersion() {
	return d3dcompiler_version;
}

bool UnloadD3DCompiler() {
	if (!g_D3DCompileModule)
		return false;

	FreeLibrary(g_D3DCompileModule);
	g_D3DCompileModule = nullptr;

	return true;
}

HRESULT dyn_D3DCompile(LPCSTR pSrcData, UINT SrcDataSize, LPCSTR pFileName, CONST D3D_SHADER_MACRO* pDefines,
	LPD3DINCLUDE pInclude, LPCSTR pEntrypoint, LPCSTR pTarget,
	UINT Flags1, UINT Flags2, LPD3DBLOB* ppShader, LPD3DBLOB* ppErrorMsgs)
{
	_assert_(g_D3DCompileModule != nullptr);
	return ptr_D3DCompile(pSrcData, SrcDataSize, pFileName, pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2, ppShader, ppErrorMsgs);
}
