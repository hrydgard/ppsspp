#ifdef _WIN32

#include "d3d9_shader.h"
#include "thin3d/d3dx9_loader.h"
#include "Common/CommonFuncs.h"

namespace DX9 {

bool CompilePixelShader(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DPIXELSHADER9 *pShader, LPD3DXCONSTANTTABLE *pShaderTable, std::string &errorMessage) {
	ID3DXBuffer *pShaderCode = nullptr;
	ID3DXBuffer *pErrorMsg = nullptr;

	// Compile pixel shader.
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

bool CompileVertexShader(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DVERTEXSHADER9 *pShader, LPD3DXCONSTANTTABLE *pShaderTable, std::string &errorMessage) {
	ID3DXBuffer *pShaderCode = nullptr;
	ID3DXBuffer *pErrorMsg = nullptr;

	// Compile pixel shader.
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
