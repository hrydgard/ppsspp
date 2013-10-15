#include "global.h"
#include "fbo.h"

namespace DX9 {

LPDIRECT3DDEVICE9 pD3Ddevice = NULL;
LPDIRECT3D9 pD3D = NULL;


static const char * vscode =
    " float4x4 matWVP : register(c0);              "
    "                                              "
    " struct VS_IN                                 "
    "                                              "
    " {                                            "
    "		float4 ObjPos   : POSITION;              "                 
	"		float2 Uv    : TEXCOORD0;                 "  // Vertex color
    " };                                           "
    "                                              "
    " struct VS_OUT                                "
    " {                                            "
    "		float4 ProjPos  : POSITION;              " 
	"		float2 Uv    : TEXCOORD0;                 "  // Vertex color
    " };                                           "
    "                                              "
    " VS_OUT main( VS_IN In )                      "
    " {                                            "
    "		VS_OUT Out;                              "
	"     Out.ProjPos = mul( matWVP, In.ObjPos );  "  // Transform vertex into
	"		Out.Uv = In.Uv;			"
    "		return Out;                              "  // Transfer color
    " }                                            ";

//--------------------------------------------------------------------------------------
// Pixel shader
//--------------------------------------------------------------------------------------
static const char * pscode =
	" sampler s: register(s0);					   "
    " struct PS_IN                                 "
    " {                                            "
    "     float2 Uv : TEXCOORD0;                   "                     
    " };                                           " 
    "                                              "
    " float4 main( PS_IN In ) : COLOR              "
    " {                                            "
    "   float4 c =  tex2D(s, In.Uv)  ;           "
	"	c.a = 1.0f;"
	"   return c;								   "
    " }                                            ";

IDirect3DVertexDeclaration9* pFramebufferVertexDecl = NULL;

static const D3DVERTEXELEMENT9  VertexElements[] =
{
    { 0,  0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, 12, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
    D3DDECL_END()
};

IDirect3DVertexDeclaration9* pSoftVertexDecl = NULL;

static const D3DVERTEXELEMENT9  SoftTransVertexElements[] =
{
    { 0,  0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0 },
	{ 0, 16, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0 },
    { 0, 28, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,  0 },
	{ 0, 32, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_COLOR,  1 },
    D3DDECL_END()
};

LPDIRECT3DVERTEXSHADER9      pFramebufferVertexShader = NULL; // Vertex Shader
LPDIRECT3DPIXELSHADER9       pFramebufferPixelShader = NULL;  // Pixel Shader

bool CompilePixelShader(const char * code, LPDIRECT3DPIXELSHADER9 * pShader, LPD3DXCONSTANTTABLE * pShaderTable) {
	LPD3DXCONSTANTTABLE shaderTable = *pShaderTable;

	ID3DXBuffer* pShaderCode = NULL;
	ID3DXBuffer* pErrorMsg = NULL;

	HRESULT hr = -1;

#ifdef _XBOX
	// Compile pixel shader.
	hr = D3DXCompileShader( code, 
		(UINT)strlen( code ),
		NULL, 
		NULL, 
		"main", 
		"ps_3_0", 
		0, 
		&pShaderCode, 
		&pErrorMsg,
		pShaderTable );
#endif
	if( FAILED(hr) )
	{
		OutputDebugStringA((CHAR*)pErrorMsg->GetBufferPointer());
		DebugBreak();
		return false;
	}

	// Create pixel shader.
	pD3Ddevice->CreatePixelShader( (DWORD*)pShaderCode->GetBufferPointer(), 
		pShader );

	pShaderCode->Release();

	return true;
}

bool CompileVertexShader(const char * code, LPDIRECT3DVERTEXSHADER9 * pShader, LPD3DXCONSTANTTABLE * pShaderTable) {
	LPD3DXCONSTANTTABLE shaderTable = *pShaderTable;

	ID3DXBuffer* pShaderCode = NULL;
	ID3DXBuffer* pErrorMsg = NULL;

	HRESULT hr = -1;

	// Compile pixel shader.
#ifdef _XBOX
	hr = D3DXCompileShader( code, 
		(UINT)strlen( code ),
		NULL, 
		NULL, 
		"main", 
		"vs_3_0", 
		0, 
		&pShaderCode, 
		&pErrorMsg,
		pShaderTable );
#endif
	if( FAILED(hr) )
	{
		OutputDebugStringA((CHAR*)pErrorMsg->GetBufferPointer());
		DebugBreak();
		return false;
	}

	// Create pixel shader.
	pD3Ddevice->CreateVertexShader( (DWORD*)pShaderCode->GetBufferPointer(), 
		pShader );

	pShaderCode->Release();

	return true;
}

void CompileShaders() {
	ID3DXBuffer* pShaderCode = NULL;
	ID3DXBuffer* pErrorMsg = NULL;
	HRESULT hr = -1;

#ifdef _XBOX

	// Compile vertex shader.
	hr = D3DXCompileShader( vscode, 
		(UINT)strlen( vscode ),
		NULL, 
		NULL, 
		"main", 
		"vs_2_0", 
		0, 
		&pShaderCode, 
		&pErrorMsg,
		NULL );
#endif 

	if( FAILED(hr) )
	{
		OutputDebugStringA((CHAR*)pErrorMsg->GetBufferPointer());
		DebugBreak();
	}

	// Create pixel shader.
	pD3Ddevice->CreateVertexShader( (DWORD*)pShaderCode->GetBufferPointer(), 
		&pFramebufferVertexShader );

	pShaderCode->Release();

#ifdef _XBOX
	// Compile pixel shader.
	hr = D3DXCompileShader( pscode, 
		(UINT)strlen( pscode ),
		NULL, 
		NULL, 
		"main", 
		"ps_2_0", 
		0, 
		&pShaderCode, 
		&pErrorMsg,
		NULL );
#endif

	if( FAILED(hr) )
	{
		OutputDebugStringA((CHAR*)pErrorMsg->GetBufferPointer());
		DebugBreak();
	}

	// Create pixel shader.
	pD3Ddevice->CreatePixelShader( (DWORD*)pShaderCode->GetBufferPointer(), 
		&pFramebufferPixelShader );

	pShaderCode->Release();

	pD3Ddevice->CreateVertexDeclaration( VertexElements, &pFramebufferVertexDecl );
	pD3Ddevice->SetVertexDeclaration( pFramebufferVertexDecl );

	pD3Ddevice->CreateVertexDeclaration( SoftTransVertexElements, &pSoftVertexDecl );
}


bool useVsync = false;

void DirectxInit(HWND window) {

	pD3D = Direct3DCreate9( D3D_SDK_VERSION );
	
#ifdef _XBOX
	D3DRING_BUFFER_PARAMETERS d3dr = {0};
	d3dr.PrimarySize = 0;  // Direct3D will use the default size of 32KB
    d3dr.SecondarySize = 4 * 1024 * 1024;
    d3dr.SegmentCount = 0; // Direct3D will use the default segment count of 32

    // Setting the pPrimary and pSecondary members to NULL means that Direct3D will
    // allocate the ring buffers itself.  You can optionally provide a buffer that
    // you allocated yourself (it must be write-combined physical memory, aligned to
    // GPU_COMMAND_BUFFER_ALIGNMENT).
    d3dr.pPrimary = NULL;
    d3dr.pSecondary = NULL;
#endif

    // Set up the structure used to create the D3DDevice. Most parameters are
    // zeroed out. We set Windowed to TRUE, since we want to do D3D in a
    // window, and then set the SwapEffect to "discard", which is the most
    // efficient method of presenting the back buffer to the display.  And 
    // we request a back buffer format that matches the current desktop display 
    // format.
    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory( &d3dpp, sizeof( d3dpp ) );
#ifdef _XBOX
    d3dpp.BackBufferWidth = 1280;
    d3dpp.BackBufferHeight = 720;
    d3dpp.BackBufferFormat =  ( D3DFORMAT )( D3DFMT_A8R8G8B8 );

    d3dpp.FrontBufferFormat = ( D3DFORMAT )( D3DFMT_LE_A8R8G8B8 );
#else
	// TODO?
	d3dpp.Windowed = TRUE;
#endif
    d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    d3dpp.MultiSampleQuality = 0;
    d3dpp.BackBufferCount = 1;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
	//d3dpp.PresentationInterval = (useVsync == true)?D3DPRESENT_INTERVAL_ONE:D3DPRESENT_INTERVAL_IMMEDIATE;
	//d3dpp.RingBufferParameters = d3dr;

	HRESULT hr = pD3D->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                      D3DCREATE_HARDWARE_VERTEXPROCESSING,
                                      &d3dpp, &pD3Ddevice);
	if (hr != D3D_OK) {
		// TODO
	}

	
#ifdef _XBOX
	pD3Ddevice->SetRingBufferParameters( &d3dr );
#endif

	CompileShaders();

	fbo_init();
}

};