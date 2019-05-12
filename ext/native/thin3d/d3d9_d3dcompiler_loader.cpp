#include <assert.h>
#include <Windows.h>
#include <D3Dcompiler.h>

#include "thin3d/d3d9_d3dcompiler_loader.h"

static HMODULE g_D3DCompileModule;
extern pD3DCompile ptr_D3DCompile;

int d3dcompiler_version = 47;

#define GB_MAKE_STR(s)  # s
#define GB_MAKE_STR2(x) GB_MAKE_STR(x)
#define GB_D3D9_D3DCOMPILER_LOADER_CHECK_ENTRY_NULL_PTR(funcname)  assert(false && GB_MAKE_STR2(funcname) ); 

bool LoadD3DCompilerDynamic() {
	g_D3DCompileModule = LoadLibrary(D3DCOMPILER_DLL);
#if PPSSPP_ARCH(X86)
	// Workaround for distributing both 32-bit and 64-bit versions of the DLL.
	if (!g_D3DCompileModule)
		g_D3DCompileModule = LoadLibrary(L"D3dcompiler_47.x86.dll");
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

	if (g_D3DCompileModule) {
		FreeLibrary(g_D3DCompileModule);
		g_D3DCompileModule = nullptr;
	}

	return true;
}


HRESULT dyn_D3DCompile(LPCSTR pSrcData, UINT SrcDataSize, LPCSTR pFileName, CONST D3D_SHADER_MACRO* pDefines,
	LPD3DINCLUDE pInclude, LPCSTR pEntrypoint, LPCSTR pTarget,
	UINT Flags1, UINT Flags2, LPD3DBLOB* ppShader, LPD3DBLOB* ppErrorMsgs)
{
	if (!g_D3DCompileModule) {
		GB_D3D9_D3DCOMPILER_LOADER_CHECK_ENTRY_NULL_PTR(D3DCompile)
	}
	return ptr_D3DCompile(pSrcData, SrcDataSize, pFileName, pDefines, pInclude, pEntrypoint, pTarget, Flags1, Flags2, ppShader, ppErrorMsgs);
}
