#pragma once

#include "Common/CommonWindows.h"
#include <initguid.h>
#include <string>
#include <d3d9.h>

struct ID3DXConstantTable;

namespace DX9 {

bool CompilePixelShader(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DPIXELSHADER9 *pShader, ID3DXConstantTable **pShaderTable, std::string &errorMessage);
bool CompileVertexShader(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DVERTEXSHADER9 *pShader, ID3DXConstantTable **pShaderTable, std::string &errorMessage);

}  // namespace
