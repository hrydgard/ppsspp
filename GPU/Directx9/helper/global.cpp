#include "global.h"
#include "fbo.h"
#include <xgraphics.h>

namespace DX9 {

	
#define USE_PREDICATED_TILLING 1

#ifdef USE_PREDICATED_TILLING


D3DTexture* pFrontBufferTexture;
D3DTexture* pPostResolveTexture;
D3DSurface* pTilingRenderTarget;
D3DSurface* pTilingDepthStencil;

struct XboxTilingSetting
{
    const char* name;
    int screenWidth;
    int screenHeight;
    D3DMULTISAMPLE_TYPE MSAAType;
    int tileCount;
    D3DRECT tilingRects[15];
};

static const XboxTilingSetting tilingSettings[] =
{
    {
        "1280x720 No MSAA",
        1280,
        720,
        D3DMULTISAMPLE_NONE,
        1,
        {
            { 0,   0, 1280, 720 },
        }
    },
    {
        "1280x720 2xMSAA Horizontal Split",
        1280,
        720,
        D3DMULTISAMPLE_2_SAMPLES,
        2,
        {
            { 0,   0, 1280, 384 },
            { 0, 384, 1280, 720 }
        }
    },
    {
        "1280x720 2xMSAA Vertical Split",
        1280,
        720,
        D3DMULTISAMPLE_2_SAMPLES,
        2,
        {
            {   0, 0,  640, 720 },
            { 640, 0, 1280, 720 }
        }
    },
    {
        "1280x720 4xMSAA Horizontal Split",
        1280,
        720,
        D3DMULTISAMPLE_4_SAMPLES,
        3,
        {
            { 0,   0, 1280, 256 },
            { 0, 256, 1280, 512 },
            { 0, 512, 1280, 720 },
        }
    },
    {
        "1280x720 4xMSAA Vertical Split",
        1280,
        720,
        D3DMULTISAMPLE_4_SAMPLES,
        4,
        {
            {   0,   0,  320, 720 },
            { 320,   0,  640, 720 },
            { 640,   0,  960, 720 },
            { 960,   0, 1280, 720 },
        }
    },
    {
        "1920x1080 No MSAA Horizontal Split",
        1920,
        1080,
        D3DMULTISAMPLE_NONE,
        2,
        {
            {   0,   0, 1920,  544 },
            {   0, 544, 1920, 1080 },
        }
    },
    {
        "1920x1080 No MSAA Vertical Split",
        1920,
        1080,
        D3DMULTISAMPLE_NONE,
        2,
        {
            {   0,   0,  960, 1080 },
            { 960,   0, 1920, 1080 },
        }
    },
    {
       "1920x1080 2x MSAA Vertical Split",
        1920,
        1080,
        D3DMULTISAMPLE_2_SAMPLES,
        4,
        {
            {    0,   0,  480, 1080 },
            {  480,   0,  960, 1080 },
            {  960,   0, 1440, 1080 },
            { 1440,   0, 1920, 1080 },
        }
    },
    {
        "1920x1080 4x MSAA Horizontal Split",
        1920,
        1080,
        D3DMULTISAMPLE_4_SAMPLES,
        7,
        {
            {    0,    0, 1920,  160 },
            {    0,  160, 1920,  320 },
            {    0,  320, 1920,  480 },
            {    0,  480, 1920,  640 },
            {    0,  640, 1920,  800 },
            {    0,  800, 1920,  960 },
            {    0,  960, 1920, 1080 },
        }
    },
    { NULL }
};

#endif


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

#ifdef USE_PREDICATED_TILLING
VOID LargestTileRectSize( const XboxTilingSetting& Scenario, D3DPOINT* pMaxSize )
{
    pMaxSize->x = 0;
    pMaxSize->y = 0;
    for( DWORD i = 0; i < Scenario.tileCount; i++ )
    {
        DWORD dwWidth = Scenario.tilingRects[i].x2 - Scenario.tilingRects[i].x1;
        DWORD dwHeight = Scenario.tilingRects[i].y2 - Scenario.tilingRects[i].y1;
        if( dwWidth > ( DWORD )pMaxSize->x )
            pMaxSize->x = dwWidth;
        if( dwHeight > ( DWORD )pMaxSize->y )
            pMaxSize->y = dwHeight;
    }
}

const XboxTilingSetting & getCurrentTilingScenario() { return tilingSettings[0]; }

#endif

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
#ifdef USE_PREDICATED_TILLING
	// Setup the presentation parameters
	const XboxTilingSetting & CurrentScenario = getCurrentTilingScenario();

    d3dpp.BackBufferWidth = CurrentScenario.screenWidth;
    d3dpp.BackBufferHeight = CurrentScenario.screenHeight;
	
    d3dpp.DisableAutoBackBuffer = TRUE;
    d3dpp.DisableAutoFrontBuffer = TRUE;
#else
    d3dpp.BackBufferWidth = 1280;
    d3dpp.BackBufferHeight = 720;
#endif
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
#ifdef USE_PREDICATED_TILLING
	 pD3Ddevice->CreateTexture( d3dpp.BackBufferWidth,
		d3dpp.BackBufferHeight,
		1, 0,
		( D3DFORMAT )( D3DFMT_LE_A8R8G8B8 ),
		D3DPOOL_DEFAULT,
		&pFrontBufferTexture,
		NULL );

    pD3Ddevice->CreateTexture( d3dpp.BackBufferWidth,
		d3dpp.BackBufferHeight,
		1, 0,
		( D3DFORMAT )( D3DFMT_LE_A8R8G8B8 ),
		D3DPOOL_DEFAULT,
		&pPostResolveTexture,
		NULL 
	);


	// Find largest tiling rect size
    D3DPOINT LargestTileSize;
    LargestTileRectSize( CurrentScenario, &LargestTileSize );

    // Create color and depth/stencil rendertargets.
    // These rendertargets are where Predicated Tiling will render each tile.  Therefore,
    // these rendertargets should be set up with all rendering quality settings you desire,
    // such as multisample antialiasing.
    // Note how we use the dimension of the largest tile rectangle to define how big the
    // rendertargets are.
    DWORD dwTileWidth = 0;
    DWORD dwTileHeight = 0;
    switch( CurrentScenario.MSAAType )
    {
        case D3DMULTISAMPLE_NONE:
            dwTileWidth = XGNextMultiple( LargestTileSize.x, GPU_EDRAM_TILE_WIDTH_1X );
            dwTileHeight = XGNextMultiple( LargestTileSize.y, GPU_EDRAM_TILE_HEIGHT_1X );
            break;
        case D3DMULTISAMPLE_2_SAMPLES:
            dwTileWidth = XGNextMultiple( LargestTileSize.x, GPU_EDRAM_TILE_WIDTH_2X );
            dwTileHeight = XGNextMultiple( LargestTileSize.y, GPU_EDRAM_TILE_HEIGHT_2X );
            break;
        case D3DMULTISAMPLE_4_SAMPLES:
            dwTileWidth = XGNextMultiple( LargestTileSize.x, GPU_EDRAM_TILE_WIDTH_4X );
            dwTileHeight = XGNextMultiple( LargestTileSize.y, GPU_EDRAM_TILE_HEIGHT_4X );
            break;
    }

    if( CurrentScenario.tileCount > 1 )
    {
        // Expand tile surface dimensions to texture tile size, if it isn't already
        dwTileWidth = XGNextMultiple( dwTileWidth, GPU_TEXTURE_TILE_DIMENSION );
        dwTileHeight = XGNextMultiple( dwTileHeight, GPU_TEXTURE_TILE_DIMENSION );
    }

    // Use custom EDRAM allocation to create the rendertargets.
    // The color rendertarget is placed at address 0 in EDRAM.
    D3DSURFACE_PARAMETERS SurfaceParams;
    memset( &SurfaceParams, 0, sizeof( D3DSURFACE_PARAMETERS ) );
    SurfaceParams.Base = 0;
    hr = pD3Ddevice->CreateRenderTarget( dwTileWidth,
                                           dwTileHeight,
                                           ( D3DFORMAT )( D3DFMT_A8R8G8B8 ),
                                           CurrentScenario.MSAAType,
                                           0, FALSE,
                                           &pTilingRenderTarget,
                                           &SurfaceParams );
    if( FAILED( hr ) )
    {
		OutputDebugString( "Cannot create tiling rendertarget.\n" );
		DebugBreak();
    }

    // Record the size of the created rendertarget, and then set up allocation
    // for the next rendertarget right after the end of the first rendertarget.
    // Put the hierarchical Z buffer at the start of hierarchical Z memory.
    DWORD m_dwTileTargetSizeBytes = pTilingRenderTarget->Size;
    SurfaceParams.Base = m_dwTileTargetSizeBytes / GPU_EDRAM_TILE_SIZE;
    SurfaceParams.HierarchicalZBase = 0;

    hr = pD3Ddevice->CreateDepthStencilSurface( dwTileWidth,
                                                  dwTileHeight,
                                                  D3DFMT_D24S8,
                                                  CurrentScenario.MSAAType,
                                                  0, FALSE,
                                                  &pTilingDepthStencil,
                                                  &SurfaceParams );
    if( FAILED( hr ) )
    {
        OutputDebugString( "Cannot create tiling depth/stencil.\n" );
		DebugBreak();
    }


	
    pD3Ddevice->SetRenderTarget( 0, pTilingRenderTarget );
    pD3Ddevice->SetDepthStencilSurface( pTilingDepthStencil );
#endif
#endif

	CompileShaders();

	fbo_init();
}

void BeginFrame() {
#ifdef USE_PREDICATED_TILLING	
	D3DVECTOR4 ClearColor = { 0, 0, 0, 0};
	const XboxTilingSetting & CurrentScenario = getCurrentTilingScenario();

	// Set our tiled render target.
    pD3Ddevice->SetRenderTarget( 0, pTilingRenderTarget );
    pD3Ddevice->SetDepthStencilSurface( pTilingDepthStencil );

	pD3Ddevice->BeginTiling(
            D3DSEQM_PRECLIP,
            CurrentScenario.tileCount,
            CurrentScenario.tilingRects,
            &ClearColor, 0.0f, 0L );
#else
	pD3Ddevice->Clear(0, NULL, D3DCLEAR_STENCIL|D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 0.f, 0);	
#endif
}

void EndFrame() {
#ifdef USE_PREDICATED_TILLING
	
	D3DVECTOR4 ClearColor = { 0, 0, 0, 0 };

	// Resolve the rendered scene back to the front buffer.
    pD3Ddevice->EndTiling( D3DRESOLVE_RENDERTARGET0 |
                                 D3DRESOLVE_ALLFRAGMENTS |
                                 D3DRESOLVE_CLEARRENDERTARGET |
                                 D3DRESOLVE_CLEARDEPTHSTENCIL,
                                 NULL, pFrontBufferTexture,
                                 &ClearColor, 0.0f, 0L, NULL );
#endif
}

void SwapBuffers() {
#ifndef USE_PREDICATED_TILLING
	pD3Ddevice->Present(NULL, NULL, NULL, NULL);
#else
	pD3Ddevice->SynchronizeToPresentationInterval();
    pD3Ddevice->Swap( pFrontBufferTexture, NULL );
#endif
}

};