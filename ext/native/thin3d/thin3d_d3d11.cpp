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

	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	Texture *CreateTexture() override;
	Texture *CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override;
	ShaderModule *CreateShaderModule(ShaderStage stage, const char *glsl_source, const char *hlsl_source, const char *vulkan_source) override;

	void BindTextures(int start, int count, Texture **textures) override;
	void BindSamplerStates(int start, int count, SamplerState **states) override;
	void BindPipeline(Pipeline *pipeline) {
		curPipeline_ = (D3D11Pipeline *)pipeline;
	}

	// Raster state
	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewports(int count, Viewport *viewports) override;

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
	ID3D11Device *device_;
	ID3D11DeviceContext *context_;
	D3D11Pipeline *curPipeline_;
	DeviceCaps caps_;
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

#endif

DrawContext *T3DCreateD3D11Context(ID3D11Device *device, ID3D11DeviceContext *context) {
	return nullptr; // new D3D11DrawContext(device, context);
}

}  // namespace Draw