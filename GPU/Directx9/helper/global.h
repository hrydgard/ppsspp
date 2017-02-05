#pragma once

#include "Common/CommonWindows.h"

#include <string>
#include <d3d9.h>

struct ID3DXConstantTable;

namespace DX9 {

extern LPDIRECT3DDEVICE9 pD3Ddevice;
extern LPDIRECT3DDEVICE9EX pD3DdeviceEx;

extern IDirect3DVertexDeclaration9* pFramebufferVertexDecl;

bool CompileShaders(std::string &errorMessage);
bool CompilePixelShader(const char *code, LPDIRECT3DPIXELSHADER9 *pShader, ID3DXConstantTable **pShaderTable, std::string &errorMessage);
bool CompileVertexShader(const char *code, LPDIRECT3DVERTEXSHADER9 *pShader, ID3DXConstantTable **pShaderTable, std::string &errorMessage);
void DestroyShaders();
void DirectxInit(HWND window);

void DXSetViewport(float x, float y, float w, float h, float minZ = 0.0f, float maxZ = 1.0f);

#define D3DBLEND_UNK	D3DBLEND_FORCE_DWORD

};
