#ifdef _WIN32

#include "ppsspp_config.h"

#include "Common/CommonWindows.h"
#include "Common/GPU/D3D9/D3DCompilerLoader.h"
#include "Common/GPU/D3D9/D3D9ShaderCompiler.h"
#include "Common/CommonFuncs.h"
#include "Common/SysError.h"

struct ID3DXConstantTable;

namespace DX9 {

bool CompilePixelShader(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DPIXELSHADER9 *pShader, ID3DXConstantTable **pShaderTable, std::string &errorMessage) {
	LPD3DBLOB pShaderCode = nullptr;
	LPD3DBLOB pErrorMsg = nullptr;

	// Compile pixel shader.
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
