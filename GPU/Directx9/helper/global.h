#pragma once

#include "Common/CommonWindows.h"
#ifdef _XBOX
// Used on XBox to create a linear format
// TODO: Might actually want to use nonlinear on xbox?
#define D3DFMT(x)	(D3DFORMAT)MAKELINFMT(x)
#else
#define D3DFMT(x) x
#endif

#include <d3d9.h>

struct ID3DXConstantTable;

namespace DX9 {

extern LPDIRECT3DDEVICE9 pD3Ddevice;

extern LPDIRECT3DVERTEXSHADER9      pFramebufferVertexShader; // Vertex Shader
extern LPDIRECT3DPIXELSHADER9       pFramebufferPixelShader;  // Pixel Shader

extern IDirect3DVertexDeclaration9* pFramebufferVertexDecl;
extern IDirect3DVertexDeclaration9* pSoftVertexDecl;

void CompileShaders();
bool CompilePixelShader(const char * code, LPDIRECT3DPIXELSHADER9 * pShader, ID3DXConstantTable **pShaderTable);
bool CompileVertexShader(const char * code, LPDIRECT3DVERTEXSHADER9 * pShader, ID3DXConstantTable **pShaderTable);
void DirectxInit(HWND window);

#define D3DBLEND_UNK	D3DSTENCILOP_FORCE_DWORD

};
