#include "thin3d/thin3d.h"

#include <d3d11.h>

namespace Draw {

#if 0

class D3D11Pipeline;

class D3D11DrawContext : public DrawContext {
public:
	D3D11DrawContext(ID3D11Device *device, ID3D11DeviceContext *deviceContext);
	~D3D11DrawContext();

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}
	uint32_t GetSupportedShaderLanguages() const override {
		return (uint32_t)ShaderLanguage::HLSL_D3D11 | (uint32_t)ShaderLanguage::HLSL_D3D11_BYTECODE;
	}

	ShaderModule *CreateShaderModule(ShaderStage stage, const char *glsl_source, const char *hlsl_source, const char *vulkan_source) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	Texture *CreateTexture() override;
	Texture *CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override;

	void BindTextures(int start, int count, Texture **textures) override;
	void BindSamplerStates(int start, int count, SamplerState **states) override;
	void BindPipeline(Pipeline *pipeline) {
		curPipeline_ = (D3D11Pipeline *)pipeline;
	}

	// Raster state
	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewports(int count, Viewport *viewports) override;
	void SetBlendFactor(float color[4]) override {
		memcpy(blendFactor_, color, sizeof(float) * 4);
	}

	void Draw(Buffer *vdata, int vertexCount, int offset) override;
	void DrawIndexed(Buffer *vdata, Buffer *idata, int vertexCount, int offset) override;
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

private:
	void ApplyCurrentState();

	ID3D11Device *device_;
	ID3D11DeviceContext *context_;
	D3D11Pipeline *curPipeline_;
	DeviceCaps caps_;
	float blendFactor_[4];
};


D3D11DrawContext::D3D11DrawContext(ID3D11Device *device, ID3D11DeviceContext *context) : device_(device), context_(context) {

}

D3D11DrawContext::~D3D11DrawContext() {

}

void D3D11DrawContext::SetViewports(int count, Viewport *viewports) {
	// Intentionally binary compatible
	context_->RSSetViewports(count, (D3D11_VIEWPORT *)viewports);
}

void D3D11DrawContext::SetScissorRect(int left, int top, int width, int height) {
	D3D11_RECT rc;
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

DepthStencilState *D3D11DrawContext::CreateDepthStencilState(const DepthStencilStateDesc &desc) {
	D3D11DepthStencilState *ds = new D3D11DepthStencilState();
	D3D11_DEPTH_STENCIL_DESC d3ddesc{};
	d3ddesc.DepthEnable = desc.depthTestEnabled;
	d3ddesc.DepthWriteMask = desc.depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	d3ddesc.DepthFunc = compareToD3D11[(int)desc.depthCompare];
	// ...
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
	d3ddesc.MaxAnisotropy = desc.maxAniso;
	d3ddesc.ComparisonFunc = compareToD3D11[(int)desc.shadowCompareFunc];
	if (SUCCEEDED(device_->CreateSamplerState(&d3ddesc, &ss->ss)))
		return ss;
	delete ss;
	return nullptr;
}

// Input layout creation is delayed to pipeline creation, as we need the vertex shader bytecode.
class D3D11InputLayout : public InputLayout {
public:
	InputLayoutDesc desc;
};

InputLayout *D3D11DrawContext::CreateInputLayout(const InputLayoutDesc &desc) {
	D3D11InputLayout *inputLayout = new D3D11InputLayout();
	inputLayout->desc = desc;
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
};

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
	// Can finally create the input layout

	auto &inputDesc = dPipeline->input->desc;
	std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
	device_->CreateInputLayout(elements.data(), elements.size(), shaderBytecode, shaderBytecodeSize, &dPipeline->il);
	return dPipeline;
}

void D3D11DrawContext::BindPipeline(Pipeline *pipeline) {
	D3D11Pipeline *dPipeline = (D3D11Pipeline *)pipeline;
	curPipeline_ = dPipeline;
}

void D3D11DrawContext::ApplyCurrentState() {
	
}

void D3D11DrawContext::Draw(Buffer *vdata, int vertexCount, int offset) {

}

void D3D11DrawContext::DrawIndexed(Buffer *vdata, Buffer *idata, int vertexCount, int offset) {

}

void D3D11DrawContext::DrawUP(const void *vdata, int vertexCount) {

}

#endif

DrawContext *T3DCreateD3D11Context(ID3D11Device *device, ID3D11DeviceContext *context) {
	return nullptr; // new D3D11DrawContext(device, context);
}

}  // namespace Draw