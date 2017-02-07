#include "thin3d/thin3d.h"
#include "thin3d/d3d11_loader.h"
#include "math/dataconv.h"

#include <d3d11.h>
#include <d3dcompiler.h>

namespace Draw {

// A problem is that we can't get the D3Dcompiler.dll without using a later SDK than 7.1, which was the last that
// supported XP. A possible solution might be here:
// https://tedwvc.wordpress.com/2014/01/01/how-to-target-xp-with-vc2012-or-vc2013-and-continue-to-use-the-windows-8-x-sdk/

class D3D11Pipeline;
class D3D11BlendState;
class D3D11DepthStencilState;
class D3D11SamplerState;
class D3D11RasterState;


class D3D11DrawContext : public DrawContext {
public:
	D3D11DrawContext(ID3D11Device *device, ID3D11DeviceContext *deviceContext, HWND hWnd);
	~D3D11DrawContext();

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}
	uint32_t GetSupportedShaderLanguages() const override {
		return (uint32_t)ShaderLanguage::HLSL_D3D11 | (uint32_t)ShaderLanguage::HLSL_D3D11_BYTECODE;
	}
	uint32_t GetDataFormatSupport(DataFormat fmt) const override;

	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	Texture *CreateTexture(const TextureDesc &desc) override;
	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize) override;
	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override;

	void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth) override;
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter) override;

	// These functions should be self explanatory.
	void BindFramebufferAsRenderTarget(Framebuffer *fbo) override;
	// color must be 0, for now.
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) override;
	void BindFramebufferForRead(Framebuffer *fbo) override;

	void BindBackbufferAsRenderTarget() override;
	uintptr_t GetFramebufferAPITexture(Framebuffer *fbo, int channelBit, int attachment) override;

	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;

	void BindTextures(int start, int count, Texture **textures) override;
	void BindSamplerStates(int start, int count, SamplerState **states) override;
	void BindPipeline(Pipeline *pipeline) override;
	void BindVertexBuffers(int start, int count, Buffer **buffers, int *offsets) override;
	void BindIndexBuffer(Buffer *indexBuffer, int offset) override;

	// Raster state
	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewports(int count, Viewport *viewports) override;
	void SetBlendFactor(float color[4]) override {
		if (memcmp(blendFactor_, color, sizeof(float) * 4)) {
			memcpy(blendFactor_, color, sizeof(float) * 4);
			blendFactorDirty_ = true;
		}
	}

	void Draw(int vertexCount, int offset) override;
	void DrawIndexed(int vertexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;
	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal);

	std::string GetInfoString(InfoField info) const override {
		switch (info) {
		case APIVERSION: return "DirectX 11.0";
		case VENDORSTRING: return "N/A";
		case VENDOR: return "-";
		case RENDERER: return "N/A";
		case SHADELANGVERSION: return "N/A";
		case APINAME: return "Direct3D 11";
		default: return "?";
		}
	}

	uintptr_t GetNativeObject(NativeObject obj) const override {
		switch (obj) {
		case NativeObject::DEVICE:
			return (uintptr_t)device_;
		case NativeObject::CONTEXT:
			return (uintptr_t)context_;
		default:
			return 0;
		}
	}

	void HandleEvent(Event ev) override {
		switch (ev) {
		case Event::PRESENT_REQUESTED:
			swapChain_->Present(0, 0);
			break;
		}
	}

private:
	void ApplyCurrentState();

	HWND hWnd_;
	ID3D11Device *device_;
	ID3D11DeviceContext *context_;
	IDXGISwapChain *swapChain_ = nullptr;

	ID3D11RenderTargetView *bbRenderTargetView_ = nullptr;
	// Strictly speaking we don't need a depth buffer for the backbuffer.
	ID3D11Texture2D *bbDepthStencilTex_ = nullptr;
	ID3D11DepthStencilView *bbDepthStencilView_ = nullptr;

	D3D11Pipeline *curPipeline_;
	DeviceCaps caps_;

	D3D11BlendState *curBlend_ = nullptr;
	D3D11DepthStencilState *curDepth_ = nullptr;
	D3D11RasterState *curRaster_ = nullptr;
	ID3D11InputLayout *curInputLayout_ = nullptr;
	ID3D11VertexShader *curVS_ = nullptr;
	ID3D11PixelShader *curPS_ = nullptr;
	ID3D11GeometryShader *curGS_ = nullptr;
	D3D11_PRIMITIVE_TOPOLOGY curTopology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

	ID3D11Buffer *nextVertexBuffers_[4]{};
	int nextVertexBufferOffsets_[4]{};
	ID3D11Buffer *nextIndexBuffer_ = nullptr;
	int nextIndexBufferOffset_ = 0;

	// Dynamic state
	float blendFactor_[4];
	bool blendFactorDirty_ = false;
	uint8_t stencilRef_;
	bool stencilRefDirty_;
};

static void GetRes(HWND hWnd, int &xres, int &yres) {
	RECT rc;
	GetClientRect(hWnd, &rc);
	xres = rc.right - rc.left;
	yres = rc.bottom - rc.top;
}

D3D11DrawContext::D3D11DrawContext(ID3D11Device *device, ID3D11DeviceContext *context, HWND hWnd) : device_(device), context_(context), hWnd_(hWnd) {
	HRESULT hr;
	// Obtain DXGI factory from device (since we used nullptr for pAdapter above)
	IDXGIFactory1* dxgiFactory = nullptr;
	IDXGIDevice* dxgiDevice = nullptr;
	IDXGIAdapter* adapter = nullptr;
	hr = device_->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
	if (SUCCEEDED(hr)) {
		hr = dxgiDevice->GetAdapter(&adapter);
		if (SUCCEEDED(hr)) {
			hr = adapter->GetParent(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&dxgiFactory));
			adapter->Release();
		}
		dxgiDevice->Release();
	}

	int width;
	int height;
	GetRes(hWnd_, width, height);

	// DirectX 11.0 systems
	DXGI_SWAP_CHAIN_DESC sd;
	ZeroMemory(&sd, sizeof(sd));
	sd.BufferCount = 1;
	sd.BufferDesc.Width = width;
	sd.BufferDesc.Height = height;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hWnd_;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.Windowed = TRUE;

	hr = dxgiFactory->CreateSwapChain(device_, &sd, &swapChain_);
	dxgiFactory->MakeWindowAssociation(hWnd_, DXGI_MWA_NO_ALT_ENTER);
	dxgiFactory->Release();

	if (FAILED(hr))
		return;

	// Create a render target view
	ID3D11Texture2D* pBackBuffer = nullptr;
	hr = swapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
	if (FAILED(hr))
		return;

	hr = device_->CreateRenderTargetView(pBackBuffer, nullptr, &bbRenderTargetView_);
	pBackBuffer->Release();
	if (FAILED(hr))
		return;

	// Create depth stencil texture
	D3D11_TEXTURE2D_DESC descDepth{};
	descDepth.Width = width;
	descDepth.Height = height;
	descDepth.MipLevels = 1;
	descDepth.ArraySize = 1;
	descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	descDepth.SampleDesc.Count = 1;
	descDepth.SampleDesc.Quality = 0;
	descDepth.Usage = D3D11_USAGE_DEFAULT;
	descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	descDepth.CPUAccessFlags = 0;
	descDepth.MiscFlags = 0;
	hr = device_->CreateTexture2D(&descDepth, nullptr, &bbDepthStencilTex_);

	// Create the depth stencil view
	D3D11_DEPTH_STENCIL_VIEW_DESC descDSV{};
	descDSV.Format = descDepth.Format;
	descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	descDSV.Texture2D.MipSlice = 0;
	hr = device_->CreateDepthStencilView(bbDepthStencilTex_, &descDSV, &bbDepthStencilView_);

	context_->OMSetRenderTargets(1, &bbRenderTargetView_, bbDepthStencilView_);

	CreatePresets();
}

D3D11DrawContext::~D3D11DrawContext() {

}

void D3D11DrawContext::SetViewports(int count, Viewport *viewports) {
	// Intentionally binary compatible
	context_->RSSetViewports(count, (D3D11_VIEWPORT *)viewports);
}

void D3D11DrawContext::SetScissorRect(int left, int top, int width, int height) {
	D3D11_RECT rc{};
	rc.left = left;
	rc.top = top;
	rc.right = left + width;
	rc.bottom = top + height;
	context_->RSSetScissorRects(1, &rc);
}

class D3D11DepthStencilState : public DepthStencilState {
public:
	~D3D11DepthStencilState() {
		dss->Release();
	}
	ID3D11DepthStencilState *dss;
};

static const D3D11_COMPARISON_FUNC compareToD3D11[] = {
	D3D11_COMPARISON_NEVER,
	D3D11_COMPARISON_LESS,
	D3D11_COMPARISON_EQUAL,
	D3D11_COMPARISON_LESS_EQUAL,
	D3D11_COMPARISON_GREATER,
	D3D11_COMPARISON_NOT_EQUAL,
	D3D11_COMPARISON_GREATER_EQUAL,
	D3D11_COMPARISON_ALWAYS
};

static const D3D11_STENCIL_OP stencilOpToD3D11[] = {
	D3D11_STENCIL_OP_KEEP,
	D3D11_STENCIL_OP_ZERO,
	D3D11_STENCIL_OP_REPLACE,
	D3D11_STENCIL_OP_INCR_SAT,
	D3D11_STENCIL_OP_DECR_SAT,
	D3D11_STENCIL_OP_INVERT,
	D3D11_STENCIL_OP_INCR,
	D3D11_STENCIL_OP_DECR,
};

static DXGI_FORMAT dataFormatToD3D11(DataFormat format) {
	switch (format) {
	case DataFormat::R32_FLOAT: return DXGI_FORMAT_R32_FLOAT;
	case DataFormat::R32G32_FLOAT: return DXGI_FORMAT_R32G32_FLOAT;
	case DataFormat::R32G32B32_FLOAT: return DXGI_FORMAT_R32G32B32_FLOAT;
	case DataFormat::R32G32B32A32_FLOAT: return DXGI_FORMAT_R32G32B32A32_FLOAT;
	case DataFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DataFormat::R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case DataFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DataFormat::B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case DataFormat::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
	case DataFormat::R16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
	case DataFormat::R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case DataFormat::D24_S8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case DataFormat::D16: return DXGI_FORMAT_D16_UNORM;
	case DataFormat::D32F: return DXGI_FORMAT_D32_FLOAT;
	case DataFormat::D32F_S8: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	case DataFormat::ETC1:
	default:
		return DXGI_FORMAT_UNKNOWN;
	}
}

static D3D11_PRIMITIVE_TOPOLOGY primToD3D11[] = {
	D3D11_PRIMITIVE_TOPOLOGY_POINTLIST,
	D3D11_PRIMITIVE_TOPOLOGY_LINELIST,
	D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,
	D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED,
	// Tesselation shader only
	D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST,   // ???
	// These are for geometry shaders only.
	D3D11_PRIMITIVE_TOPOLOGY_LINELIST_ADJ,
	D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ,
	D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ,
};

inline void CopyStencilSide(D3D11_DEPTH_STENCILOP_DESC &side, const StencilSide &input) {
	side.StencilFunc = compareToD3D11[(int)input.compareOp];
	side.StencilDepthFailOp = stencilOpToD3D11[(int)input.depthFailOp];
	side.StencilFailOp = stencilOpToD3D11[(int)input.failOp];
	side.StencilPassOp = stencilOpToD3D11[(int)input.passOp];
}

DepthStencilState *D3D11DrawContext::CreateDepthStencilState(const DepthStencilStateDesc &desc) {
	D3D11DepthStencilState *ds = new D3D11DepthStencilState();
	D3D11_DEPTH_STENCIL_DESC d3ddesc{};
	d3ddesc.DepthEnable = desc.depthTestEnabled;
	d3ddesc.DepthWriteMask = desc.depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	d3ddesc.DepthFunc = compareToD3D11[(int)desc.depthCompare];
	d3ddesc.StencilEnable = desc.stencilEnabled;
	CopyStencilSide(d3ddesc.FrontFace, desc.front);
	CopyStencilSide(d3ddesc.BackFace, desc.back);
	if (SUCCEEDED(device_->CreateDepthStencilState(&d3ddesc, &ds->dss)))
		return ds;
	delete ds;
	return nullptr;
}

static const D3D11_BLEND_OP blendOpToD3D11[] = {
	D3D11_BLEND_OP_ADD,
	D3D11_BLEND_OP_SUBTRACT,
	D3D11_BLEND_OP_REV_SUBTRACT,
	D3D11_BLEND_OP_MIN,
	D3D11_BLEND_OP_MAX,
};

static const D3D11_BLEND blendToD3D11[] = {
	D3D11_BLEND_ZERO,
	D3D11_BLEND_ONE,
	D3D11_BLEND_SRC_COLOR,
	D3D11_BLEND_INV_SRC_COLOR,
	D3D11_BLEND_DEST_COLOR,
	D3D11_BLEND_INV_DEST_COLOR,
	D3D11_BLEND_SRC_ALPHA,
	D3D11_BLEND_INV_SRC_ALPHA,
	D3D11_BLEND_DEST_ALPHA,
	D3D11_BLEND_INV_DEST_ALPHA,
	D3D11_BLEND_BLEND_FACTOR,
	D3D11_BLEND_INV_BLEND_FACTOR,
	D3D11_BLEND_BLEND_FACTOR,
	D3D11_BLEND_INV_BLEND_FACTOR,
	D3D11_BLEND_SRC1_COLOR,
	D3D11_BLEND_INV_SRC1_COLOR,
	D3D11_BLEND_SRC1_ALPHA,
	D3D11_BLEND_INV_SRC1_ALPHA,
};

class D3D11BlendState : public BlendState {
public:
	~D3D11BlendState() {
		bs->Release();
	}
	ID3D11BlendState *bs;
	float blendFactor[4];
};

BlendState *D3D11DrawContext::CreateBlendState(const BlendStateDesc &desc) {
	D3D11BlendState *bs = new D3D11BlendState();
	D3D11_BLEND_DESC d3ddesc{};
	d3ddesc.AlphaToCoverageEnable = FALSE;
	d3ddesc.IndependentBlendEnable = FALSE;
	d3ddesc.RenderTarget[0].BlendEnable = desc.enabled;
	d3ddesc.RenderTarget[0].RenderTargetWriteMask = desc.colorMask;
	d3ddesc.RenderTarget[0].BlendOp = blendOpToD3D11[(int)desc.eqCol];
	d3ddesc.RenderTarget[0].BlendOpAlpha = blendOpToD3D11[(int)desc.eqAlpha];
	d3ddesc.RenderTarget[0].SrcBlend = blendToD3D11[(int)desc.srcCol];
	d3ddesc.RenderTarget[0].SrcBlendAlpha = blendToD3D11[(int)desc.srcAlpha];
	d3ddesc.RenderTarget[0].DestBlend = blendToD3D11[(int)desc.dstCol];
	d3ddesc.RenderTarget[0].DestBlendAlpha = blendToD3D11[(int)desc.dstAlpha];
	if (SUCCEEDED(device_->CreateBlendState(&d3ddesc, &bs->bs)))
		return bs;
	delete bs;
	return nullptr;
}

class D3D11RasterState : public RasterState {
public:
	~D3D11RasterState() {
		rs->Release();
	}
	ID3D11RasterizerState *rs;
};

RasterState *D3D11DrawContext::CreateRasterState(const RasterStateDesc &desc) {
	D3D11RasterState *rs = new D3D11RasterState();
	D3D11_RASTERIZER_DESC d3ddesc{}; 
	d3ddesc.FillMode = D3D11_FILL_SOLID;
	switch (desc.cull) {
	case CullMode::BACK: d3ddesc.CullMode = D3D11_CULL_BACK; break;
	case CullMode::FRONT: d3ddesc.CullMode = D3D11_CULL_FRONT; break;
	default:
	case CullMode::NONE: d3ddesc.CullMode = D3D11_CULL_NONE; break;
	}
	d3ddesc.FrontCounterClockwise = desc.frontFace == Facing::CCW;
	if (SUCCEEDED(device_->CreateRasterizerState(&d3ddesc, &rs->rs)))
		return rs;
	delete rs;
	return nullptr;
}

class D3D11SamplerState : public SamplerState {
public:
	~D3D11SamplerState() {
		ss->Release();
	}
	ID3D11SamplerState *ss;
};

static const D3D11_TEXTURE_ADDRESS_MODE taddrToD3D11[] = {
	D3D11_TEXTURE_ADDRESS_WRAP,
	D3D11_TEXTURE_ADDRESS_MIRROR,
	D3D11_TEXTURE_ADDRESS_CLAMP,
	D3D11_TEXTURE_ADDRESS_BORDER,
};

SamplerState *D3D11DrawContext::CreateSamplerState(const SamplerStateDesc &desc) {
	D3D11SamplerState *ss = new D3D11SamplerState();
	D3D11_SAMPLER_DESC d3ddesc{};
	d3ddesc.AddressU = taddrToD3D11[(int)desc.wrapU];
	d3ddesc.AddressV = taddrToD3D11[(int)desc.wrapV];
	d3ddesc.AddressW = taddrToD3D11[(int)desc.wrapW];
	// TODO: Needs improvement
	d3ddesc.Filter = desc.magFilter == TextureFilter::LINEAR ? D3D11_FILTER_MIN_MAG_MIP_LINEAR : D3D11_FILTER_MIN_MAG_MIP_POINT;
	d3ddesc.MaxAnisotropy = (UINT)desc.maxAniso;
	d3ddesc.ComparisonFunc = compareToD3D11[(int)desc.shadowCompareFunc];
	if (SUCCEEDED(device_->CreateSamplerState(&d3ddesc, &ss->ss)))
		return ss;
	delete ss;
	return nullptr;
}

// Input layout creation is delayed to pipeline creation, as we need the vertex shader bytecode.
class D3D11InputLayout : public InputLayout {
public:
	D3D11InputLayout() {}
	InputLayoutDesc desc;
	std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
	std::vector<int> strides;
};

const char *semanticToD3D11(int semantic, UINT *index) {
	*index = 0;
	switch (semantic) {
	case SEM_POSITION: return "POSITION";
	case SEM_COLOR0: *index = 0; return "COLOR";
	case SEM_TEXCOORD0: *index = 0; return "TEXCOORD";
	case SEM_TEXCOORD1: *index = 1; return "TEXCOORD";
	case SEM_NORMAL: return "NORMAL";
	case SEM_TANGENT: return "TANGENT";
	case SEM_BINORMAL: return "BINORMAL"; // really BITANGENT
	default: return "UNKNOWN";
	}
}

InputLayout *D3D11DrawContext::CreateInputLayout(const InputLayoutDesc &desc) {
	D3D11InputLayout *inputLayout = new D3D11InputLayout();
	inputLayout->desc = desc;

	// Translate to D3D11 elements;
	for (size_t i = 0; i < desc.attributes.size(); i++) {
		D3D11_INPUT_ELEMENT_DESC el;
		el.AlignedByteOffset = desc.attributes[i].offset;
		el.Format = dataFormatToD3D11(desc.attributes[i].format);
		el.InstanceDataStepRate = desc.bindings[desc.attributes[i].binding].instanceRate ? 1 : 0;
		el.InputSlot = desc.attributes[i].binding;
		el.SemanticName = semanticToD3D11(desc.attributes[i].location, &el.SemanticIndex);
		el.InputSlotClass = desc.bindings[desc.attributes[i].binding].instanceRate ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA;
		inputLayout->elements.push_back(el);
	}
	for (size_t i = 0; i < desc.bindings.size(); i++) {
		inputLayout->strides.push_back(desc.bindings[i].stride);
	}
	return inputLayout;
}

class D3D11Pipeline : public Pipeline {
public:
	~D3D11Pipeline() {
		input->Release();
		blend->Release();
		depth->Release();
		raster->Release();
		il->Release();
	}
	// TODO: Refactor away these.
	void SetVector(const char *name, float *value, int n) { }
	void SetMatrix4x4(const char *name, const float value[16]) { }  // pshaders don't usually have matrices
	bool RequiresBuffer() {
		return true;
	}

	D3D11InputLayout *input;
	ID3D11InputLayout *il = nullptr;
	D3D11BlendState *blend;
	D3D11DepthStencilState *depth;
	D3D11RasterState *raster;
	ID3D11VertexShader *vs;
	ID3D11PixelShader *ps;
	ID3D11GeometryShader *gs;
	D3D11_PRIMITIVE_TOPOLOGY topology;
};

class D3D11Texture : public Texture {
public:
	D3D11Texture(const TextureDesc &desc) {
		width_ = desc.width;
		height_ = desc.height;
		depth_ = desc.depth;
	}
	~D3D11Texture() {
		if (tex)
			tex->Release();
		if (view)
			view->Release();
	}

	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
		ELOG("SetImageData not supported, create a new texture instead");
	}

	ID3D11Texture2D *tex = nullptr;
	ID3D11ShaderResourceView *view = nullptr;
};

Texture *D3D11DrawContext::CreateTexture(const TextureDesc &desc) {
	D3D11Texture *tex = new D3D11Texture(desc);

	if (!(GetDataFormatSupport(desc.format) & FMT_TEXTURE)) {
		// D3D11 does not support this format as a texture format.
		return false;
	}

	D3D11_TEXTURE2D_DESC descColor{};
	descColor.Width = desc.width;
	descColor.Height = desc.height;
	descColor.MipLevels = desc.mipLevels;
	descColor.ArraySize = 1;
	descColor.Format = dataFormatToD3D11(desc.format);
	descColor.SampleDesc.Count = 1;
	descColor.SampleDesc.Quality = 0;
	descColor.Usage = D3D11_USAGE_DEFAULT;
	descColor.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	descColor.CPUAccessFlags = 0;
	descColor.MiscFlags = 0;

	D3D11_SUBRESOURCE_DATA initData[12]{};
	if (desc.initData.size()) {
		int w = desc.width;
		int h = desc.height;
		for (int i = 0; i < desc.initData.size(); i++) {
			initData[i].pSysMem = desc.initData[0];
			initData[i].SysMemPitch = (UINT)(w * DataFormatSizeInBytes(desc.format));
			initData[i].SysMemSlicePitch = (UINT)(w * h * DataFormatSizeInBytes(desc.format));
			w /= 2;
			h /= 2;
		}
	}

	HRESULT hr = device_->CreateTexture2D(&descColor, desc.initData.size() ? initData : nullptr, &tex->tex);
	if (!SUCCEEDED(hr)) {
		delete tex;
		return nullptr;
	}
	hr = device_->CreateShaderResourceView(tex->tex, nullptr, &tex->view);
	if (!SUCCEEDED(hr)) {
		delete tex;
		return nullptr;
	}
	return tex;
}

class D3D11ShaderModule : public ShaderModule {
public:
	~D3D11ShaderModule() {
		if (vs)
			vs->Release();
		if (ps)
			ps->Release();
		if (gs)
			gs->Release();
	}
	ShaderStage GetStage() const override { return stage; }

	std::vector<uint8_t> byteCode_;
	ShaderStage stage;

	ID3D11VertexShader *vs = nullptr;
	ID3D11PixelShader *ps = nullptr;
	ID3D11GeometryShader *gs = nullptr;
};

ShaderModule *D3D11DrawContext::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize) {
	switch (language) {
	case ShaderLanguage::HLSL_D3D11:
	case ShaderLanguage::HLSL_D3D11_BYTECODE:
		break;
	default:
		ELOG("Unsupported shader language");
		return nullptr;
	}

	std::string compiled;
	std::string errors;
	if (language == ShaderLanguage::HLSL_D3D11) {
		const char *target = nullptr;
		switch (stage) {
		case ShaderStage::FRAGMENT: target = "ps_5_0"; break;
		case ShaderStage::GEOMETRY: target = "gs_5_0"; break;
		case ShaderStage::VERTEX: target = "vs_5_0"; break;
			break;
		case ShaderStage::COMPUTE:
		case ShaderStage::CONTROL:
		case ShaderStage::EVALUATION:
		default:
			break;
		}
		if (!target) {
			return nullptr;
		}

		ID3DBlob *compiledCode = nullptr;
		ID3DBlob *errorMsgs = nullptr;
		HRESULT result = ptr_D3DCompile(data, dataSize, nullptr, nullptr, nullptr, "main", target, 0, 0, &compiledCode, &errorMsgs);
		if (compiledCode) {
			compiled = std::string((const char *)compiledCode->GetBufferPointer(), compiledCode->GetBufferSize());
			compiledCode->Release();
		}
		if (errorMsgs) {
			errors = std::string((const char *)errorMsgs->GetBufferPointer(), errorMsgs->GetBufferSize());
			ELOG("Failed compiling:\n%s\n%s", data, errors.c_str());
			errorMsgs->Release();
		}

		if (result != S_OK) {
			return nullptr;
		}

		// OK, we can now proceed
		language = ShaderLanguage::HLSL_D3D11_BYTECODE;
		data = (const uint8_t *)compiled.c_str();
		dataSize = compiled.size();
	}

	if (language == ShaderLanguage::HLSL_D3D11_BYTECODE) {
		// Easy!
		D3D11ShaderModule *module = new D3D11ShaderModule();
		module->stage = stage;
		module->byteCode_ = std::vector<uint8_t>(data, data + dataSize);
		HRESULT result = S_OK;
		switch (stage) {
		case ShaderStage::VERTEX:
			result = device_->CreateVertexShader(data, dataSize, nullptr, &module->vs);
			break;
		case ShaderStage::FRAGMENT:
			result = device_->CreatePixelShader(data, dataSize, nullptr, &module->ps);
			break;
		case ShaderStage::GEOMETRY:
			result = device_->CreateGeometryShader(data, dataSize, nullptr, &module->gs);
			break;
		default:
			ELOG("Unsupported shader stage");
			result = S_FALSE;
			break;
		}
		if (result == S_OK) {
			return module;
		} else {
			delete module;
			return nullptr;
		}
	}
	return nullptr;
}

Pipeline *D3D11DrawContext::CreateGraphicsPipeline(const PipelineDesc &desc) {
	D3D11Pipeline *dPipeline = new D3D11Pipeline();
	dPipeline->blend = (D3D11BlendState *)desc.blend;
	dPipeline->depth = (D3D11DepthStencilState *)desc.depthStencil;
	dPipeline->input = (D3D11InputLayout *)desc.inputLayout;
	dPipeline->raster = (D3D11RasterState *)desc.raster;
	dPipeline->blend->AddRef();
	dPipeline->depth->AddRef();
	dPipeline->input->AddRef();
	dPipeline->raster->AddRef();
	dPipeline->topology = primToD3D11[(int)desc.prim];

	std::vector<D3D11ShaderModule *> shaders;
	D3D11ShaderModule *vshader = nullptr;
	for (auto iter : desc.shaders) {
		D3D11ShaderModule *module = (D3D11ShaderModule *)iter;
		shaders.push_back(module);
		switch (module->GetStage()) {
		case ShaderStage::VERTEX:
			vshader = module;
			dPipeline->vs = module->vs;
			break;
		case ShaderStage::FRAGMENT:
			dPipeline->ps = module->ps;
			break;
		case ShaderStage::GEOMETRY:
			dPipeline->gs = module->gs;
			break;
		}
	}

	if (!vshader) {
		// No vertex shader - no graphics
		dPipeline->Release();
		return nullptr;
	}

	// Can finally create the input layout
	auto &inputDesc = dPipeline->input->desc;
	const std::vector<D3D11_INPUT_ELEMENT_DESC> &elements = dPipeline->input->elements;
	HRESULT hr = device_->CreateInputLayout(elements.data(), (UINT)elements.size(), vshader->byteCode_.data(), vshader->byteCode_.size(), &dPipeline->il);
	if (!SUCCEEDED(hr)) {
		Crash();
	}
	return dPipeline;
}

void D3D11DrawContext::BindPipeline(Pipeline *pipeline) {
	D3D11Pipeline *dPipeline = (D3D11Pipeline *)pipeline;
	if (curPipeline_ == dPipeline)
		return;
	curPipeline_ = dPipeline;
}

// Gonna need dirtyflags soon..
void D3D11DrawContext::ApplyCurrentState() {
	if (curBlend_ != curPipeline_->blend || blendFactorDirty_) {
		context_->OMSetBlendState(curPipeline_->blend->bs, blendFactor_, 0);
		curBlend_ = curPipeline_->blend;
		blendFactorDirty_ = false;
	}
	if (curDepth_ != curPipeline_->depth || stencilRefDirty_) {
		context_->OMSetDepthStencilState(curPipeline_->depth->dss, stencilRef_);
		curDepth_ = curPipeline_->depth;
		stencilRefDirty_ = false;
	}
	if (curRaster_ != curPipeline_->raster) {
		context_->RSSetState(curPipeline_->raster->rs);
		curRaster_ = curPipeline_->raster;
	}
	if (curInputLayout_ != curPipeline_->il) {
		context_->IASetInputLayout(curPipeline_->il);
		curInputLayout_ = curPipeline_->il;
	}
	if (curVS_ != curPipeline_->vs) {
		context_->VSSetShader(curPipeline_->vs, nullptr, 0);
		curVS_ = curPipeline_->vs;
	}
	if (curPS_ != curPipeline_->ps) {
		context_->PSSetShader(curPipeline_->ps, nullptr, 0);
		curPS_ = curPipeline_->ps;
	}
	if (curGS_ != curPipeline_->gs) {
		context_->GSSetShader(curPipeline_->gs, nullptr, 0);
		curGS_ = curPipeline_->gs;
	}
	if (curTopology_ != curPipeline_->topology) {
		context_->IASetPrimitiveTopology(curPipeline_->topology);
		curTopology_ = curPipeline_->topology;
	}

	int numVBs = (int)curPipeline_->input->strides.size();
	context_->IASetVertexBuffers(0, 1, nextVertexBuffers_, (UINT *)curPipeline_->input->strides.data(), (UINT *)nextVertexBufferOffsets_);
	if (nextIndexBuffer_) {
		context_->IASetIndexBuffer(nextIndexBuffer_, DXGI_FORMAT_R32_UINT, nextIndexBufferOffset_);
	}
}

class D3D11Buffer : public Buffer {
public:
	~D3D11Buffer() {
		if (buf)
			buf->Release();
		if (srView)
			srView->Release();
	}
	ID3D11Buffer *buf = nullptr;
	ID3D11ShaderResourceView *srView = nullptr;
	size_t size;
};

Buffer *D3D11DrawContext::CreateBuffer(size_t size, uint32_t usageFlags) {
	D3D11Buffer *b = new D3D11Buffer();
	D3D11_BUFFER_DESC desc{};
	desc.ByteWidth = (UINT)size;
	desc.BindFlags = 0;
	if (usageFlags & VERTEXDATA)
		desc.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
	if (usageFlags & INDEXDATA)
		desc.BindFlags |= D3D11_BIND_INDEX_BUFFER;
	if (usageFlags & UNIFORM)
		desc.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;

	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.Usage = D3D11_USAGE_DYNAMIC;

	b->size = size;
	HRESULT hr = device_->CreateBuffer(&desc, nullptr, &b->buf);
	if (FAILED(hr)) {
		delete b;
		return nullptr;
	}
	if (usageFlags & UNIFORM) {
		// D3D11_SHADER_RESOURCE_VIEW_DESC resDesc{};
		// device_->CreateShaderResourceView(b->buf, &resDesc, &b->srView);
	}
	return b;
}

void D3D11DrawContext::UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) {
	D3D11Buffer *buf = (D3D11Buffer *)buffer;
	if ((flags & UPDATE_DISCARD) || (offset == 0 && size == buf->size)) {
		// Can just discard the old contents. This is only allowed for DYNAMIC buffers.
		D3D11_MAPPED_SUBRESOURCE map;
		context_->Map(buf->buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
		memcpy(map.pData, data, size);
		context_->Unmap(buf->buf, 0);
		return;
	}

	D3D11_BOX box{};
	box.left = (UINT)offset;
	box.right = (UINT)(offset + size);
	context_->UpdateSubresource(buf->buf, 0, &box, data, 0, 0);
}

void D3D11DrawContext::BindVertexBuffers(int start, int count, Buffer **buffers, int *offsets) {
	// Lazy application
	for (int i = 0; i < count; i++) {
		D3D11Buffer *buf = (D3D11Buffer *)buffers[i];
		nextVertexBuffers_[start + i] = buf->buf;
		nextVertexBufferOffsets_[start + i] = offsets ? offsets[i] : 0;
	}
}

void D3D11DrawContext::BindIndexBuffer(Buffer *indexBuffer, int offset) {
	D3D11Buffer *buf = (D3D11Buffer *)indexBuffer;
	// Lazy application
	nextIndexBuffer_ = buf->buf;
	nextIndexBufferOffset_ = offset;
}

void D3D11DrawContext::Draw(int vertexCount, int offset) {
	ApplyCurrentState();
	context_->Draw(vertexCount, offset);
}

void D3D11DrawContext::DrawIndexed(int indexCount, int offset) {
	ApplyCurrentState();
	context_->DrawIndexed(indexCount, offset, 0);
}

void D3D11DrawContext::DrawUP(const void *vdata, int vertexCount) {
	ApplyCurrentState();
	// TODO: Upload the data then draw..
}

uint32_t D3D11DrawContext::GetDataFormatSupport(DataFormat fmt) const {
	// TODO: Actually do proper checks
	switch (fmt) {
	case DataFormat::B8G8R8A8_UNORM:
		return FMT_RENDERTARGET | FMT_TEXTURE;

	// D3D11 has no support for 4-bit component formats.
	case DataFormat::B4G4R4A4_UNORM_PACK16:
	case DataFormat::R4G4B4A4_UNORM_PACK16:
	case DataFormat::A4B4G4R4_UNORM_PACK16:
		return 0;

	case DataFormat::R8G8B8A8_UNORM:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_INPUTLAYOUT;

	case DataFormat::R32_FLOAT:
	case DataFormat::R32G32_FLOAT:
	case DataFormat::R32G32B32_FLOAT:
	case DataFormat::R32G32B32A32_FLOAT:
		return FMT_INPUTLAYOUT;

	case DataFormat::R8_UNORM:
		return 0;
	case DataFormat::BC1_RGBA_UNORM_BLOCK:
	case DataFormat::BC2_UNORM_BLOCK:
	case DataFormat::BC3_UNORM_BLOCK:
		return FMT_TEXTURE;
	default:
		return 0;
	}
}

// A D3D11Framebuffer is a D3D11Framebuffer plus all the textures it owns.
class D3D11Framebuffer : public Framebuffer {
public:
	D3D11Framebuffer() {}
	~D3D11Framebuffer() {
		if (colorTex)
			colorTex->Release();
		if (colorRTView)
			colorRTView->Release();
		if (colorSRView)
			colorSRView->Release();
		if (depthStencilTex)
			depthStencilTex->Release();
		if (depthStencilRTView)
			depthStencilRTView->Release();
	}
	int width;
	int height;

	ID3D11Texture2D *colorTex = nullptr;
	ID3D11RenderTargetView *colorRTView = nullptr;
	ID3D11ShaderResourceView *colorSRView = nullptr;

	ID3D11Texture2D *depthStencilTex = nullptr;
	ID3D11DepthStencilView *depthStencilRTView = nullptr;
};

Framebuffer *D3D11DrawContext::CreateFramebuffer(const FramebufferDesc &desc) {
	HRESULT hr;
	D3D11Framebuffer *fb = new D3D11Framebuffer();
	fb->width = desc.width;
	fb->height = desc.height;

	if (desc.numColorAttachments) {
		D3D11_TEXTURE2D_DESC descColor{};
		descColor.Width = desc.width;
		descColor.Height = desc.height;
		descColor.MipLevels = 1;
		descColor.ArraySize = 1;
		descColor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		descColor.SampleDesc.Count = 1;
		descColor.SampleDesc.Quality = 0;
		descColor.Usage = D3D11_USAGE_DEFAULT;
		descColor.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		descColor.CPUAccessFlags = 0;
		descColor.MiscFlags = 0;
		hr = device_->CreateTexture2D(&descColor, nullptr, &fb->colorTex);
		if (FAILED(hr)) {
			delete fb;
			return nullptr;
		}
		hr = device_->CreateRenderTargetView(fb->colorTex, nullptr, &fb->colorRTView);
		if (FAILED(hr)) {
			delete fb;
			return nullptr;
		}
		hr = device_->CreateShaderResourceView(fb->colorTex, nullptr, &fb->colorSRView);
		if (FAILED(hr)) {
			delete fb;
			return nullptr;
		}
	}

	if (desc.z_stencil) {
		D3D11_TEXTURE2D_DESC descDepth{};
		descDepth.Width = desc.width;
		descDepth.Height = desc.height;
		descDepth.MipLevels = 1;
		descDepth.ArraySize = 1;
		descDepth.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		descDepth.SampleDesc.Count = 1;
		descDepth.SampleDesc.Quality = 0;
		descDepth.Usage = D3D11_USAGE_DEFAULT;
		descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		descDepth.CPUAccessFlags = 0;
		descDepth.MiscFlags = 0;
		hr = device_->CreateTexture2D(&descDepth, nullptr, &fb->depthStencilTex);
		if (FAILED(hr)) {
			delete fb;
			return nullptr;
		}
		D3D11_DEPTH_STENCIL_VIEW_DESC descDSV{};
		descDSV.Format = descDepth.Format;
		descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		descDSV.Texture2D.MipSlice = 0;
		hr = device_->CreateDepthStencilView(fb->depthStencilTex, &descDSV, &fb->depthStencilRTView);
		if (FAILED(hr)) {
			delete fb;
			return nullptr;
		}
	}

	return fb;
}

void D3D11DrawContext::BindTextures(int start, int count, Texture **textures) {
	// Collect the resource views from the textures.
	ID3D11ShaderResourceView *views[8];
	for (int i = 0; i < count; i++) {
		D3D11Texture *tex = (D3D11Texture *)textures[i];
		views[i] = tex->view;
	}
	context_->PSSetShaderResources(start, count, views);
}

void D3D11DrawContext::BindSamplerStates(int start, int count, SamplerState **states) {
	ID3D11SamplerState *samplers[8];
	for (int i = 0; i < count; i++) {
		D3D11SamplerState *samp = (D3D11SamplerState *)states[i];
		samplers[i] = samp->ss;
	}
	context_->PSSetSamplers(start, count, samplers);
}

void D3D11DrawContext::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	if (mask & ClearFlag::COLOR) {
		float colorRGBA[4];
		Uint8x1ToFloat4(colorRGBA, colorval);
		context_->ClearRenderTargetView(bbRenderTargetView_, colorRGBA);
	}
	if (mask & (ClearFlag::DEPTH | ClearFlag::STENCIL)) {
		UINT clearFlag = 0;
		if (mask & ClearFlag::DEPTH)
			clearFlag |= D3D11_CLEAR_DEPTH;
		if (mask & ClearFlag::STENCIL)
			clearFlag |= D3D11_CLEAR_STENCIL;
		context_->ClearDepthStencilView(bbDepthStencilView_, clearFlag, depthVal, stencilVal);
	}
}

void D3D11DrawContext::CopyFramebufferImage(Framebuffer *srcfb, int level, int x, int y, int z, Framebuffer *dstfb, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth) {
	D3D11Framebuffer *src = (D3D11Framebuffer *)srcfb;
	D3D11Framebuffer *dst = (D3D11Framebuffer *)dstfb;

	// CopySubResource ?
}

bool D3D11DrawContext::BlitFramebuffer(Framebuffer *srcfb, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dstfb, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter) {
	D3D11Framebuffer *src = (D3D11Framebuffer *)srcfb;
	D3D11Framebuffer *dst = (D3D11Framebuffer *)dstfb;

	// Unfortunately D3D11 has no equivalent to this, gotta render a quad.
	return true;
}

// These functions should be self explanatory.
void D3D11DrawContext::BindFramebufferAsRenderTarget(Framebuffer *fbo) {
	D3D11Framebuffer *fb = (D3D11Framebuffer *)fbo;
	context_->OMSetRenderTargets(1, &fb->colorRTView, fb->depthStencilRTView);
}

// color must be 0, for now.
void D3D11DrawContext::BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) {
	D3D11Framebuffer *fb = (D3D11Framebuffer *)fbo;
	context_->PSSetShaderResources(binding, 1, &fb->colorSRView);
}

void D3D11DrawContext::BindFramebufferForRead(Framebuffer *fbo) {
	// This is meaningless in D3D11
}

void D3D11DrawContext::BindBackbufferAsRenderTarget() {
	context_->OMSetRenderTargets(1, &bbRenderTargetView_, bbDepthStencilView_);
}

uintptr_t D3D11DrawContext::GetFramebufferAPITexture(Framebuffer *fbo, int channelBit, int attachment) {
	// D3D11Framebuffer *fb = (D3D11Framebuffer *)fbo;
	return 0;
}

void D3D11DrawContext::GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) {
	D3D11Framebuffer *fb = (D3D11Framebuffer *)fbo;
	*w = fb->width;
	*h = fb->height;
}

DrawContext *T3DCreateD3D11Context(ID3D11Device *device, ID3D11DeviceContext *context, HWND hWnd) {
	return new D3D11DrawContext(device, context, hWnd);
}

}  // namespace Draw