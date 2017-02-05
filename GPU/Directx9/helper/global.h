#pragma once

#include "Common/CommonWindows.h"

#include <string>
#include <d3d9.h>

struct ID3DXConstantTable;

namespace DX9 {

extern LPDIRECT3DDEVICE9 pD3Ddevice;
extern LPDIRECT3DDEVICE9EX pD3DdeviceEx;

bool CompilePixelShader(const char *code, LPDIRECT3DPIXELSHADER9 *pShader, ID3DXConstantTable **pShaderTable, std::string &errorMessage);
bool CompileVertexShader(const char *code, LPDIRECT3DVERTEXSHADER9 *pShader, ID3DXConstantTable **pShaderTable, std::string &errorMessage);

}  // namespace
