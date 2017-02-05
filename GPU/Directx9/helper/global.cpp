

#include "GPU/Directx9/helper/global.h"
#include "GPU/Directx9/helper/dx_fbo.h"
#include "thin3d/d3dx9_loader.h"
#include "Common/CommonFuncs.h"

namespace DX9 {

LPDIRECT3DDEVICE9 pD3Ddevice = nullptr;
LPDIRECT3DDEVICE9EX pD3DdeviceEx = nullptr;
LPDIRECT3D9 pD3D = nullptr;

IDirect3DVertexDeclaration9* pFramebufferVertexDecl = nullptr;

static const D3DVERTEXELEMENT9 VertexElements[] = {
	{ 0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
	D3DDECL_END()
};

LPDIRECT3DVERTEXSHADER9      pFramebufferVertexShader = nullptr; // Vertex Shader
LPDIRECT3DPIXELSHADER9       pFramebufferPixelShader = nullptr;  // Pixel Shader

bool CompilePixelShader(const char *code, LPDIRECT3DPIXELSHADER9 *pShader, LPD3DXCONSTANTTABLE *pShaderTable, std::string &errorMessage) {
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
	pD3Ddevice->CreatePixelShader( (DWORD*)pShaderCode->GetBufferPointer(), 
		pShader );

	pShaderCode->Release();

	return true;
}

bool CompileVertexShader(const char *code, LPDIRECT3DVERTEXSHADER9 *pShader, LPD3DXCONSTANTTABLE *pShaderTable, std::string &errorMessage) {
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
	pD3Ddevice->CreateVertexShader( (DWORD*)pShaderCode->GetBufferPointer(), 
		pShader );

	pShaderCode->Release();

	return true;
}

bool CompileShaders(std::string &errorMsg) {
	pD3Ddevice->CreateVertexDeclaration(VertexElements, &pFramebufferVertexDecl);
	pD3Ddevice->SetVertexDeclaration(pFramebufferVertexDecl);
	return true;
}

void DestroyShaders() {
	if (pFramebufferVertexDecl) {
		pFramebufferVertexDecl->Release();
		pFramebufferVertexDecl = nullptr;
	}
}

};
