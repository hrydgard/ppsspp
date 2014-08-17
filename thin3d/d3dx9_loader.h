// Cut-down version of https://code.google.com/p/d3dx9-dynamic-load/
// by ksacvet777, licensed under the MIT

#pragma once

#include <Windows.h>
#include <d3dx9.h>

// Returns the D3DX9 version we got. 0 if none.
int LoadD3DX9Dynamic();
int GetD3DXVersion();
void UnloadD3DXDynamic();

// Call these instead of the real D3DX9 functions.
BOOL dyn_D3DXCheckVersion(UINT D3DSDKVersion, UINT D3DXSDKVersion);
HRESULT dyn_D3DXAssembleShader(LPCSTR pSrcData, UINT SrcDataLen, CONST D3DXMACRO* pDefines,
	LPD3DXINCLUDE pInclude, DWORD Flags, LPD3DXBUFFER* ppShader, LPD3DXBUFFER * ppErrorMsgs);
HRESULT dyn_D3DXCompileShader(LPCSTR pSrcData, UINT srcDataLen, CONST D3DXMACRO* pDefines,
	LPD3DXINCLUDE pInclude, LPCSTR pFunctionName, LPCSTR pProfile,
	DWORD Flags, LPD3DXBUFFER* ppShader, LPD3DXBUFFER* ppErrorMsgs,
	LPD3DXCONSTANTTABLE * ppConstantTable);
