#ifdef _WIN32

#include "ppsspp_config.h"
#include "d3d9_shader.h"
#include "Common/CommonFuncs.h"

#if PPSSPP_API(D3DX9)
#include "thin3d/d3dx9_loader.h"

// They are the same types, just different names.
#define LPD3D_SHADER_MACRO LPD3DXMACRO
#define LPD3DINCLUDE LPD3DXINCLUDE
#define LPD3DBLOB LPD3DXBUFFER
#elif PPSSPP_API(D3D9_D3DCOMPILER)
#include "thin3d/d3d9_d3dcompiler_loader.h"
#endif

struct ID3DXConstantTable;

namespace DX9 {

bool CompilePixelShader(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DPIXELSHADER9 *pShader, ID3DXConstantTable **pShaderTable, std::string &errorMessage) {
	LPD3DBLOB pShaderCode = nullptr;
	LPD3DBLOB pErrorMsg = nullptr;

	// Compile pixel shader.
#if PPSSPP_API(D3DX9)
	HRESULT hr = dyn_D3DXCompileShader(code,
		(UINT)strlen(code),
		nullptr,
		nullptr,
		"main",
		"ps_2_0",
		0,
		&pShaderCode,
		&pErrorMsg,
		pShaderTable);
#elif PPSSPP_API(D3D9_D3DCOMPILER)
	HRESULT hr = dyn_D3DCompile(code,
		(UINT)strlen(code),
		nullptr,
		nullptr,
		nullptr,
		"main",
		"ps_2_0",
		0,
		0,
		&pShaderCode,
		&pErrorMsg);
#endif

	if (pErrorMsg) {
		errorMessage = (CHAR *)pErrorMsg->GetBufferPointer();
		pErrorMsg->Release();
	} else if (FAILED(hr)) {
		errorMessage = GetStringErrorMsg(hr);
	} else {
		errorMessage = "";
	}

	if (FAILED(hr) || !pShaderCode) {
		if (pShaderCode)
			pShaderCode->Release();
		return false;
	}

	// Create pixel shader.
	device->CreatePixelShader( (DWORD*)pShaderCode->GetBufferPointer(), 
		pShader );

	pShaderCode->Release();

	return true;
}

bool CompileVertexShader(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DVERTEXSHADER9 *pShader, ID3DXConstantTable **pShaderTable, std::string &errorMessage) {
	LPD3DBLOB pShaderCode = nullptr;
	LPD3DBLOB pErrorMsg = nullptr;

	// Compile pixel shader.
#if PPSSPP_API(D3DX9)
	HRESULT hr = dyn_D3DXCompileShader(code,
		(UINT)strlen(code),
		nullptr,
		nullptr,
		"main",
		"vs_2_0",
		0,
		&pShaderCode,
		&pErrorMsg,
		pShaderTable);
#elif PPSSPP_API(D3D9_D3DCOMPILER)
	HRESULT hr = dyn_D3DCompile(code,
		(UINT)strlen(code),
		nullptr,
		nullptr,
		nullptr,
		"main",
		"vs_2_0",
		0,
		0,
		&pShaderCode,
		&pErrorMsg);
#endif

	if (pErrorMsg) {
		errorMessage = (CHAR *)pErrorMsg->GetBufferPointer();
		pErrorMsg->Release();
	} else if (FAILED(hr)) {
		errorMessage = GetStringErrorMsg(hr);
	} else {
		errorMessage = "";
	}

	if (FAILED(hr) || !pShaderCode) {
		if (pShaderCode)
			pShaderCode->Release();
		return false;
	}

	// Create pixel shader.
	device->CreateVertexShader( (DWORD*)pShaderCode->GetBufferPointer(), 
		pShader );

	pShaderCode->Release();

	return true;
}

}  // namespace

#endif
