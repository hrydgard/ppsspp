#pragma once
#include "ppsspp_config.h"

#include <Windows.h>
#include <D3Dcompiler.h>

bool LoadD3DCompilerDynamic();

HRESULT dyn_D3DCompile(LPCSTR pSrcData, UINT SrcDataSize, LPCSTR pFileName, CONST D3D_SHADER_MACRO* pDefines,
	LPD3DINCLUDE pInclude, LPCSTR pEntrypoint, LPCSTR pTarget,
	UINT Flags1, UINT Flags2, LPD3DBLOB* ppShader, LPD3DBLOB* ppErrorMsgs);

bool UnloadD3DCompiler();

int GetD3DCompilerVersion();
