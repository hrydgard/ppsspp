#include "ppsspp_config.h"

#ifdef _WIN32

#include "Common/CommonWindows.h"
#include "Common/GPU/D3D9/D3DCompilerLoader.h"
#include "Common/GPU/D3D9/D3D9ShaderCompiler.h"
#include "Common/CommonFuncs.h"
#include "Common/SysError.h"
#include "Common/Log.h"
#include "Common/StringUtils.h"

struct ID3DXConstantTable;

LPD3DBLOB CompileShaderToByteCodeD3D9(const char *code, const char *target, std::string *errorMessage) {
	LPD3DBLOB pShaderCode = nullptr;
	LPD3DBLOB pErrorMsg = nullptr;

	// Compile pixel shader.
	HRESULT hr = dyn_D3DCompile(code,
		(UINT)strlen(code),
		nullptr,
		nullptr,
		nullptr,
		"main",
		target,
		0,
		0,
		&pShaderCode,
		&pErrorMsg);

	if (pErrorMsg) {
		*errorMessage = std::string((CHAR *)pErrorMsg->GetBufferPointer());

		OutputDebugStringUTF8(LineNumberString(std::string(code)).c_str());
		OutputDebugStringUTF8(errorMessage->c_str());

		pErrorMsg->Release();
		if (pShaderCode) {
			pShaderCode->Release();
			pShaderCode = nullptr;
		}
	} else if (FAILED(hr)) {
		*errorMessage = GetStringErrorMsg(hr);
		if (pShaderCode) {
			pShaderCode->Release();
			pShaderCode = nullptr;
		}
	} else {
		errorMessage->clear();
	}

	return pShaderCode;
}

bool CompilePixelShaderD3D9(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DPIXELSHADER9 *pShader, std::string *errorMessage) {
	LPD3DBLOB pShaderCode = CompileShaderToByteCodeD3D9(code, "ps_3_0", errorMessage);
	if (pShaderCode) {
		// Create pixel shader.
		device->CreatePixelShader((DWORD*)pShaderCode->GetBufferPointer(), pShader);
		pShaderCode->Release();
		return true;
	} else {
		return false;
	}
}

bool CompileVertexShaderD3D9(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DVERTEXSHADER9 *pShader, std::string *errorMessage) {
	LPD3DBLOB pShaderCode = CompileShaderToByteCodeD3D9(code, "vs_3_0", errorMessage);
	if (pShaderCode) {
		// Create vertex shader.
		device->CreateVertexShader((DWORD*)pShaderCode->GetBufferPointer(), pShader);
		pShaderCode->Release();
		return true;
	} else {
		return false;
	}
}

#endif
