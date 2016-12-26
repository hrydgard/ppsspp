#include "thin3d/thin3d.h"

#include <d3d11.h>

namespace Draw {

#if 0

class D3D11Pipeline : public Pipeline {
	
};

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
#endif

DrawContext *T3DCreateD3D11Context(ID3D11Device *device, ID3D11DeviceContext *context) {
	return nullptr; // new D3D11DrawContext(device, context);
}

}  // namespace Draw