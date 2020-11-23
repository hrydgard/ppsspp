#pragma once

#include "Common/CommonWindows.h"
#include "Common/GPU/D3D9/D3DCompilerLoader.h"

#include <initguid.h>
#include <string>
#include <d3d9.h>

struct ID3DXConstantTable;

LPD3DBLOB CompileShaderToByteCodeD3D9(const char *code, const char *target, std::string *errorMessage);

bool CompilePixelShaderD3D9(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DPIXELSHADER9 *pShader, std::string *errorMessage);
bool CompileVertexShaderD3D9(LPDIRECT3DDEVICE9 device, const char *code, LPDIRECT3DVERTEXSHADER9 *pShader, std::string *errorMessage);
