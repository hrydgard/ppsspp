#include "ppsspp_config.h"

#include "Common/GPU/thin3d.h"
#if PPSSPP_PLATFORM(UWP)
#define ptr_D3DCompile D3DCompile
#else
#include "Common/GPU/D3D11/D3D11Loader.h"
#endif
#include "Common/System/Display.h"

#include "Common/Data/Convert/ColorConv.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/TimeUtil.h"
#include "Common/Log.h"

#include <map>

#include <cfloat>
#include <D3Dcommon.h>
#ifndef __LIBRETRO__  // their build server uses an old SDK
#include <dxgi1_5.h>
#endif
#include <d3d11.h>
#include <d3d11_1.h>
#include <D3Dcompiler.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

namespace Draw {

static constexpr int MAX_BOUND_TEXTURES = 8;

// A problem is that we can't get the D3Dcompiler.dll without using a later SDK than 7.1, which was the last that
// supported XP. A possible solution might be here:
// https://tedwvc.wordpress.com/2014/01/01/how-to-target-xp-with-vc2012-or-vc2013-and-continue-to-use-the-windows-8-x-sdk/

class D3D11Pipeline;
class D3D11BlendState;
class D3D11DepthStencilState;
class D3D11SamplerState;
class D3D11Buffer;
class D3D11RasterState;

// This must stay POD for the memcmp to work reliably.
struct D3D11DepthStencilKey {
	DepthStencilStateDesc desc;
	u8 writeMask;
	u8 compareMask;

	bool operator < (const D3D11DepthStencilKey &other) const {
		return memcmp(this, &other, sizeof(D3D11DepthStencilKey)) < 0;
	}
};

class D3D11DepthStencilState : public DepthStencilState {
public:
	~D3D11DepthStencilState() = default;
	DepthStencilStateDesc desc;
};

// A D3D11Framebuffer is a D3D11Framebuffer plus all the textures it owns.
class D3D11Framebuffer : public Framebuffer {
public:
	D3D11Framebuffer(int width, int height) {
		width_ = width;
		height_ = height;
	}
	~D3D11Framebuffer() {
	}

	ComPtr<ID3D11Texture2D> colorTex;
	ComPtr<ID3D11RenderTargetView> colorRTView;
	ComPtr<ID3D11ShaderResourceView> colorSRView;
	ComPtr<ID3D11ShaderResourceView> depthSRView;
	ComPtr<ID3D11ShaderResourceView> stencilSRView;
	DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;

	ComPtr<ID3D11Texture2D> depthStencilTex;
	ComPtr<ID3D11DepthStencilView> depthStencilRTView;
	DXGI_FORMAT depthStencilFormat = DXGI_FORMAT_UNKNOWN;
};

class D3D11DrawContext : public DrawContext {
public:
	D3D11DrawContext(ID3D11Device *device, ID3D11DeviceContext *deviceContext, ID3D11Device1 *device1, ID3D11DeviceContext1 *deviceContext1, IDXGISwapChain *swapChain, D3D_FEATURE_LEVEL featureLevel, HWND hWnd, std::vector<std::string> deviceList, int maxInflightFrames);
	~D3D11DrawContext();

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}
	std::vector<std::string> GetDeviceList() const override {
		return deviceList_;
	}
	uint32_t GetSupportedShaderLanguages() const override {
		return (uint32_t)ShaderLanguage::HLSL_D3D11;
	}
	uint32_t GetDataFormatSupport(DataFormat fmt) const override;

	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) override;
	Texture *CreateTexture(const TextureDesc &desc) override;
	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const char *tag) override;
	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override;

	void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) override;
	void UpdateTextureLevels(Texture *texture, const uint8_t **data, TextureCallback initDataCallback, int numLevels) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, Aspect aspects, const char *tag) override;
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, Aspect aspects, FBBlitFilter filter, const char *tag) override;
	bool CopyFramebufferToMemory(Framebuffer *src, Aspect channelBits, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride, ReadbackMode mode, const char *tag) override;

	// These functions should be self explanatory.
	void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) override;
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, Aspect channelBit, int layer) override;

	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;

	void Invalidate(InvalidationFlags flags) override;

	void BindTextures(int start, int count, Texture **textures, TextureBindFlags flags) override;
	void BindNativeTexture(int index, void *nativeTexture) override;
	void BindSamplerStates(int start, int count, SamplerState **states) override;
	void BindVertexBuffer(Buffer *buffers, int offset) override;
	void BindIndexBuffer(Buffer *indexBuffer, int offset) override;
	void BindPipeline(Pipeline *pipeline) override;

	void UpdateDynamicUniformBuffer(const void *ub, size_t size) override;

	// Raster state
	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewport(const Viewport &viewport) override;
	void SetBlendFactor(float color[4]) override {
		if (0 != memcmp(blendFactor_, color, sizeof(float) * 4)) {
			memcpy(blendFactor_, color, sizeof(float) * 4);
			blendFactorDirty_ = true;
		}
	}
	void SetStencilParams(uint8_t refValue, uint8_t writeMask, uint8_t compareMask) override {
		stencilRef_ = refValue;
		stencilWriteMask_ = writeMask;
		stencilCompareMask_ = compareMask;
		stencilDirty_ = true;
	}


	void Draw(int vertexCount, int offset) override;
	void DrawIndexed(int indexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;
	void DrawIndexedUP(const void *vdata, int vertexCount, const void *idata, int indexCount) override;
	void DrawIndexedClippedBatchUP(const void *vdata, int vertexCount, const void *idata, int indexCount, Slice<ClippedDraw> draws, const void *ub, size_t ubSize) override;

	void Clear(Aspect mask, uint32_t colorval, float depthVal, int stencilVal) override;

	void BeginFrame(DebugFlags debugFlags) override;
	void EndFrame() override;
	void Present(PresentMode presentMode, int vblanks) override;

	int GetFrameCount() override { return frameCount_; }

	std::string GetInfoString(InfoField info) const override {
		switch (info) {
		case InfoField::APIVERSION: return "Direct3D 11";
		case InfoField::VENDORSTRING: return adapterDesc_;
		case InfoField::VENDOR: return "";
		case InfoField::DRIVER: return "-";
		case InfoField::SHADELANGVERSION:
			switch (featureLevel_) {
			case D3D_FEATURE_LEVEL_9_1: return "Feature Level 9.1";
			case D3D_FEATURE_LEVEL_9_2: return "Feature Level 9.2";
			case D3D_FEATURE_LEVEL_9_3: return "Feature Level 9.3";
			case D3D_FEATURE_LEVEL_10_0: return "Feature Level 10.0";
			case D3D_FEATURE_LEVEL_10_1: return "Feature Level 10.1";
			case D3D_FEATURE_LEVEL_11_0: return "Feature Level 11.0";
			case D3D_FEATURE_LEVEL_11_1: return "Feature Level 11.1";
			case D3D_FEATURE_LEVEL_12_0: return "Feature Level 12.0";
			case D3D_FEATURE_LEVEL_12_1: return "Feature Level 12.1";
#ifndef __LIBRETRO__
			case D3D_FEATURE_LEVEL_1_0_CORE: return "Feature Level 1.0 Core";  // This is for compute-only devices. Useless for us.
			case D3D_FEATURE_LEVEL_12_2: return "Feature Level 12.2";
#endif
			default: return "Feature Level X.X";
			}
			return "Unknown feature level";
		case InfoField::APINAME: return "Direct3D 11";
		default: return "?";
		}
	}

	uint64_t GetNativeObject(NativeObject obj, void *srcObject) override;

	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override;

	void SetInvalidationCallback(InvalidationCallback callback) override {
		invalidationCallback_ = callback;
	}

private:
	void ApplyCurrentState();

	HRESULT GetCachedDepthStencilState(const D3D11DepthStencilState *state, uint8_t stencilWriteMask, uint8_t stencilCompareMask, ID3D11DepthStencilState **);

	HWND hWnd_;
	ComPtr<ID3D11Device> device_;
	ComPtr<ID3D11Device1> device1_;
	ComPtr<ID3D11DeviceContext> context_;
	ComPtr<ID3D11DeviceContext1> context1_;
	ComPtr<IDXGISwapChain> swapChain_;
	bool swapChainTearingSupported_ = false;

	ID3D11Texture2D *bbRenderTargetTex_ = nullptr; // NOT OWNED
	ID3D11RenderTargetView *bbRenderTargetView_ = nullptr ; // NOT OWNED
	// Strictly speaking we don't need a depth buffer for the backbuffer.
	ComPtr<ID3D11Texture2D> bbDepthStencilTex_;
	ComPtr<ID3D11DepthStencilView> bbDepthStencilView_;

	AutoRef<Framebuffer> curRenderTarget_;
	ComPtr<ID3D11RenderTargetView> curRenderTargetView_;
	ComPtr<ID3D11DepthStencilView> curDepthStencilView_;
	// Needed to rotate stencil/viewport rectangles properly
	int bbWidth_ = 0;
	int bbHeight_ = 0;
	int curRTWidth_ = 0;
	int curRTHeight_ = 0;

	AutoRef<D3D11Pipeline> curPipeline_;
	DeviceCaps caps_{};

	AutoRef<D3D11BlendState> curBlend_;
	AutoRef<D3D11DepthStencilState> curDepthStencil_;
	AutoRef<D3D11RasterState> curRaster_;

	std::map<D3D11DepthStencilKey, ComPtr<ID3D11DepthStencilState>> depthStencilCache_;

	ComPtr<ID3D11InputLayout> curInputLayout_;
	ComPtr<ID3D11VertexShader> curVS_;
	ComPtr<ID3D11PixelShader> curPS_;
	ComPtr<ID3D11GeometryShader> curGS_;
	D3D11_PRIMITIVE_TOPOLOGY curTopology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

	ComPtr<ID3D11Buffer> nextVertexBuffer_;
	UINT nextVertexBufferOffset_ = 0;

	bool dirtyIndexBuffer_ = false;
	ComPtr<ID3D11Buffer> nextIndexBuffer_;
	UINT nextIndexBufferOffset_ = 0;

	InvalidationCallback invalidationCallback_;
	int frameCount_ = FRAME_TIME_HISTORY_LENGTH;

	// Dynamic state
	float blendFactor_[4]{};
	bool blendFactorDirty_ = false;
	uint8_t stencilRef_ = 0;
	uint8_t stencilWriteMask_ = 0xFF;
	uint8_t stencilCompareMask_ = 0xFF;

	bool stencilDirty_ = true;

	// Temporaries
	ComPtr<ID3D11Texture2D> packTexture_;
	Buffer *upBuffer_ = nullptr;
	Buffer *upIBuffer_ = nullptr;

	// System info
	D3D_FEATURE_LEVEL featureLevel_;
	std::string adapterDesc_;
	std::vector<std::string> deviceList_;
};

D3D11DrawContext::D3D11DrawContext(ID3D11Device *device, ID3D11DeviceContext *deviceContext, ID3D11Device1 *device1, ID3D11DeviceContext1 *deviceContext1, IDXGISwapChain *swapChain, D3D_FEATURE_LEVEL featureLevel, HWND hWnd, std::vector<std::string> deviceList, int maxInflightFrames)
	: hWnd_(hWnd),
		device_(device),
		context_(deviceContext1),
		device1_(device1),
		context1_(deviceContext1),
		featureLevel_(featureLevel),
		swapChain_(swapChain),
		deviceList_(std::move(deviceList)) {

	// We no longer support Windows Phone.
	_assert_(featureLevel_ >= D3D_FEATURE_LEVEL_9_3);

	caps_.coordConvention = CoordConvention::Direct3D11;

	// Seems like a fair approximation...
	caps_.dualSourceBlend = featureLevel_ >= D3D_FEATURE_LEVEL_10_0;
	caps_.depthClampSupported = featureLevel_ >= D3D_FEATURE_LEVEL_10_0;
	// SV_ClipDistance# seems to be 10+.
	caps_.clipDistanceSupported = featureLevel_ >= D3D_FEATURE_LEVEL_10_0;
	caps_.cullDistanceSupported = featureLevel_ >= D3D_FEATURE_LEVEL_10_0;

	caps_.depthRangeMinusOneToOne = false;
	caps_.framebufferBlitSupported = false;
	caps_.framebufferCopySupported = true;
	caps_.framebufferDepthBlitSupported = false;
	caps_.framebufferStencilBlitSupported = false;
	caps_.framebufferDepthCopySupported = true;
	caps_.framebufferSeparateDepthCopySupported = false;  // Though could be emulated with a draw.
	caps_.preferredDepthBufferFormat = DataFormat::D24_S8;
	caps_.textureDepthSupported = true;
	caps_.texture3DSupported = true;
	caps_.fragmentShaderInt32Supported = true;
	caps_.anisoSupported = true;
	caps_.textureNPOTFullySupported = true;
	caps_.fragmentShaderDepthWriteSupported = true;
	caps_.fragmentShaderStencilWriteSupported = false;
	caps_.blendMinMaxSupported = true;
	caps_.multiSampleLevelsMask = 1;   // More could be supported with some work.

	caps_.provokingVertexLast = false;  // D3D has it first, unfortunately. (and no way to change it).

	caps_.presentInstantModeChange = true;
	caps_.presentMaxInterval = 4;
	caps_.presentModesSupported = PresentMode::FIFO | PresentMode::IMMEDIATE;

	D3D11_FEATURE_DATA_D3D11_OPTIONS options{};
	HRESULT result = device_->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &options, sizeof(options));
	if (SUCCEEDED(result)) {
		if (options.OutputMergerLogicOp) {
			// Actually, need to check that the format supports logic ops as well.
			// Which normal UNORM formats don't seem to do in D3D11. So meh. We can't enable logicOp support.
			// caps_.logicOpSupported = true;
		}
	}

	ComPtr<IDXGIDevice> dxgiDevice;
	ComPtr<IDXGIAdapter> adapter;
	HRESULT hr = device_.As(&dxgiDevice);
	if (SUCCEEDED(hr)) {
		hr = dxgiDevice->GetAdapter(&adapter);
		if (SUCCEEDED(hr)) {
			DXGI_ADAPTER_DESC desc;
			adapter->GetDesc(&desc);
			adapterDesc_ = ConvertWStringToUTF8(desc.Description);
			switch (desc.VendorId) {
			case 0x10DE: caps_.vendor = GPUVendor::VENDOR_NVIDIA; break;
			case 0x1002:
			case 0x1022: caps_.vendor = GPUVendor::VENDOR_AMD; break;
			case 0x163C:
			case 0x8086:
			case 0x8087: caps_.vendor = GPUVendor::VENDOR_INTEL; break;
			// TODO: There are Windows ARM devices that could have Qualcomm here too.
			// Not sure where I'll find the vendor codes for those though...
			default:
				caps_.vendor = GPUVendor::VENDOR_UNKNOWN;
			}
			caps_.deviceID = desc.DeviceId;
		}
	}

	caps_.isTilingGPU = false;

#ifndef __LIBRETRO__  // their build server uses an old SDK
	if (swapChain_) {
		DXGI_SWAP_CHAIN_DESC swapChainDesc;
		swapChain_->GetDesc(&swapChainDesc);

		if (swapChainDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) {
			swapChainTearingSupported_ = true;
		}
	}
#endif

	// Temp texture for read-back of small images. Custom textures are created on demand for larger ones.
	// TODO: Should really benchmark if this extra complexity has any benefit.
	D3D11_TEXTURE2D_DESC packDesc{};
	packDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	packDesc.BindFlags = 0;
	packDesc.Width = 512;
	packDesc.Height = 512;
	packDesc.ArraySize = 1;
	packDesc.MipLevels = 1;
	packDesc.Usage = D3D11_USAGE_STAGING;
	packDesc.SampleDesc.Count = 1;
	packDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	hr = device_->CreateTexture2D(&packDesc, nullptr, &packTexture_);
	_assert_(SUCCEEDED(hr));

	shaderLanguageDesc_.Init(HLSL_D3D11);

	const size_t UP_MAX_BYTES = 65536 * 24;

	upBuffer_ = D3D11DrawContext::CreateBuffer(UP_MAX_BYTES, BufferUsageFlag::DYNAMIC | BufferUsageFlag::VERTEXDATA);
	upIBuffer_ = D3D11DrawContext::CreateBuffer(UP_MAX_BYTES, BufferUsageFlag::DYNAMIC | BufferUsageFlag::INDEXDATA);

	ComPtr<IDXGIDevice1> dxgiDevice1;
	hr = device_.As(&dxgiDevice1);
	if (SUCCEEDED(hr)) {
		caps_.setMaxFrameLatencySupported = true;
		dxgiDevice1->SetMaximumFrameLatency(maxInflightFrames);
	}
}

D3D11DrawContext::~D3D11DrawContext() {
	DestroyPresets();

	upBuffer_->Release();
	upIBuffer_->Release();

	// Release references.
	ID3D11RenderTargetView *view = nullptr;
	context_->OMSetRenderTargets(1, &view, nullptr);
	ID3D11ShaderResourceView *srv[2]{};
	context_->PSSetShaderResources(0, 2, srv);
}

void D3D11DrawContext::HandleEvent(Event ev, int width, int height, void *param1, void *param2) {
	switch (ev) {
	case Event::LOST_BACKBUFFER: {
		if (curRenderTargetView_.Get() == bbRenderTargetView_ || curDepthStencilView_ == bbDepthStencilView_) {
			ID3D11RenderTargetView *view = nullptr;
			context_->OMSetRenderTargets(1, &view, nullptr);
			curRenderTargetView_.Reset();
			curDepthStencilView_.Reset();
		}
		bbDepthStencilView_.Reset();
		bbDepthStencilTex_.Reset();
		curRTWidth_ = 0;
		curRTHeight_ = 0;
		break;
	}
	case Event::GOT_BACKBUFFER: {
		bbRenderTargetView_ = (ID3D11RenderTargetView *)param1;
		bbRenderTargetTex_ = (ID3D11Texture2D *)param2;
		bbWidth_ = width;
		bbHeight_ = height;

		// Create matching depth stencil texture. This is not really needed for PPSSPP though,
		// and probably not for most other renderers either as you're usually rendering to other render targets and
		// then blitting them with a shader to the screen.
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
		HRESULT hr = device_->CreateTexture2D(&descDepth, nullptr, &bbDepthStencilTex_);

		// Create the depth stencil view
		D3D11_DEPTH_STENCIL_VIEW_DESC descDSV{};
		descDSV.Format = descDepth.Format;
		descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		descDSV.Texture2D.MipSlice = 0;
		hr = device_->CreateDepthStencilView(bbDepthStencilTex_.Get(), &descDSV, &bbDepthStencilView_);

		context_->OMSetRenderTargets(1, &bbRenderTargetView_, bbDepthStencilView_.Get());

		curRenderTargetView_ = bbRenderTargetView_;
		curDepthStencilView_ = bbDepthStencilView_;
		curRTWidth_ = width;
		curRTHeight_ = height;
		break;
	}
	case Event::LOST_DEVICE:
	case Event::GOT_DEVICE:
	case Event::RESIZED:
	case Event::PRESENTED:
		break;
	}
}

void D3D11DrawContext::EndFrame() {
	// Fake a submit time.
	frameTimeHistory_[frameCount_].firstSubmit = time_now_d();
	curPipeline_ = nullptr;
}

void D3D11DrawContext::Present(PresentMode presentMode, int vblanks) {
	frameTimeHistory_[frameCount_].queuePresent = time_now_d();

	// Safety for libretro
	if (swapChain_) {
		uint32_t interval = vblanks;
		uint32_t flags = 0;
		if (presentMode != PresentMode::FIFO) {
			interval = 0;
#ifndef __LIBRETRO__  // their build server uses an old SDK
			flags |= swapChainTearingSupported_ ? DXGI_PRESENT_ALLOW_TEARING : 0; // Assume "vsync off" also means "allow tearing"
#endif
		}
		swapChain_->Present(interval, flags);
	}

	curRenderTargetView_.Reset();
	curDepthStencilView_.Reset();
	frameCount_++;
}

void D3D11DrawContext::SetViewport(const Viewport &viewport) {
	DisplayRect<float> rc{ viewport.TopLeftX , viewport.TopLeftY, viewport.Width, viewport.Height };
	if (curRenderTargetView_.Get() == bbRenderTargetView_)  // Only the backbuffer is actually rotated wrong!
		RotateRectToDisplay(rc, curRTWidth_, curRTHeight_);
	D3D11_VIEWPORT vp;
	vp.TopLeftX = rc.x;
	vp.TopLeftY = rc.y;
	vp.Width = rc.w;
	vp.Height = rc.h;
	vp.MinDepth = viewport.MinDepth;
	vp.MaxDepth = viewport.MaxDepth;
	context_->RSSetViewports(1, &vp);
}

void D3D11DrawContext::SetScissorRect(int left, int top, int width, int height) {
	_assert_(width >= 0);
	_assert_(height >= 0);
	DisplayRect<float> frc{ (float)left, (float)top, (float)width, (float)height };
	if (curRenderTargetView_.Get() == bbRenderTargetView_)  // Only the backbuffer is actually rotated wrong!
		RotateRectToDisplay(frc, curRTWidth_, curRTHeight_);
	D3D11_RECT rc{};
	rc.left = (INT)frc.x;
	rc.top = (INT)frc.y;
	rc.right = (INT)(frc.x + frc.w);
	rc.bottom = (INT)(frc.y + frc.h);
	context_->RSSetScissorRects(1, &rc);
}

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
	case DataFormat::A4R4G4B4_UNORM_PACK16: return DXGI_FORMAT_B4G4R4A4_UNORM;
	case DataFormat::A1R5G5B5_UNORM_PACK16: return DXGI_FORMAT_B5G5R5A1_UNORM;
	case DataFormat::R5G6B5_UNORM_PACK16: return DXGI_FORMAT_B5G6R5_UNORM;
	case DataFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
	case DataFormat::R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case DataFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
	case DataFormat::B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
	case DataFormat::R16_UNORM: return DXGI_FORMAT_R16_UNORM;
	case DataFormat::R16_FLOAT: return DXGI_FORMAT_R16_FLOAT;
	case DataFormat::R16G16_FLOAT: return DXGI_FORMAT_R16G16_FLOAT;
	case DataFormat::R16G16B16A16_FLOAT: return DXGI_FORMAT_R16G16B16A16_FLOAT;
	case DataFormat::D24_S8: return DXGI_FORMAT_D24_UNORM_S8_UINT;
	case DataFormat::D16: return DXGI_FORMAT_D16_UNORM;
	case DataFormat::D32F: return DXGI_FORMAT_D32_FLOAT;
	case DataFormat::D32F_S8: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
	case DataFormat::BC1_RGBA_UNORM_BLOCK: return DXGI_FORMAT_BC1_UNORM;
	case DataFormat::BC2_UNORM_BLOCK: return DXGI_FORMAT_BC2_UNORM;
	case DataFormat::BC3_UNORM_BLOCK: return DXGI_FORMAT_BC3_UNORM;
	case DataFormat::BC4_UNORM_BLOCK: return DXGI_FORMAT_BC4_UNORM;
	case DataFormat::BC5_UNORM_BLOCK: return DXGI_FORMAT_BC5_UNORM;
	case DataFormat::BC7_UNORM_BLOCK: return DXGI_FORMAT_BC7_UNORM;
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

inline void CopyStencilSide(D3D11_DEPTH_STENCILOP_DESC &side, const StencilSetup &input) {
	side.StencilFunc = compareToD3D11[(int)input.compareOp];
	side.StencilDepthFailOp = stencilOpToD3D11[(int)input.depthFailOp];
	side.StencilFailOp = stencilOpToD3D11[(int)input.failOp];
	side.StencilPassOp = stencilOpToD3D11[(int)input.passOp];
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
	}
	ComPtr<ID3D11BlendState> bs;
	float blendFactor[4];
};

HRESULT D3D11DrawContext::GetCachedDepthStencilState(const D3D11DepthStencilState *state, uint8_t stencilWriteMask, uint8_t stencilCompareMask,
ID3D11DepthStencilState **ppDepthStencilState) {
	D3D11DepthStencilKey key;
	key.desc = state->desc;
	key.writeMask = stencilWriteMask;
	key.compareMask = stencilCompareMask;

	auto findResult = depthStencilCache_.find(key);

	if (findResult != depthStencilCache_.end()) {
		findResult->second->AddRef();
		*ppDepthStencilState = findResult->second.Get();
		return S_OK;
	}

	// OK, create and insert.
	D3D11_DEPTH_STENCIL_DESC d3ddesc{};
	d3ddesc.DepthEnable = state->desc.depthTestEnabled;
	d3ddesc.DepthWriteMask = state->desc.depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
	d3ddesc.DepthFunc = compareToD3D11[(int)state->desc.depthCompare];
	d3ddesc.StencilEnable = state->desc.stencilEnabled;
	d3ddesc.StencilReadMask = stencilCompareMask;
	d3ddesc.StencilWriteMask = stencilWriteMask;
	if (d3ddesc.StencilEnable) {
		CopyStencilSide(d3ddesc.FrontFace, state->desc.stencil);
		CopyStencilSide(d3ddesc.BackFace, state->desc.stencil);
	}

	HRESULT hr = device_->CreateDepthStencilState(&d3ddesc, ppDepthStencilState);
	if (SUCCEEDED(hr)) {
		depthStencilCache_[key] = *ppDepthStencilState;
	}
	return hr;
}

DepthStencilState *D3D11DrawContext::CreateDepthStencilState(const DepthStencilStateDesc &desc) {
	D3D11DepthStencilState *dss = new D3D11DepthStencilState();
	dss->desc = desc;
	return static_cast<DepthStencilState *>(dss);
}

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
	}
	ComPtr<ID3D11RasterizerState> rs;
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
	d3ddesc.ScissorEnable = true;  // We always run with scissor enabled
	d3ddesc.DepthClipEnable = true;
	if (SUCCEEDED(device_->CreateRasterizerState(&d3ddesc, &rs->rs)))
		return rs;
	delete rs;
	return nullptr;
}

class D3D11SamplerState : public SamplerState {
public:
	~D3D11SamplerState() {
	}
	ComPtr<ID3D11SamplerState> ss;
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
	d3ddesc.MaxAnisotropy = 1.0f; // (UINT)desc.maxAniso;
	d3ddesc.MinLOD = -FLT_MAX;
	d3ddesc.MaxLOD = FLT_MAX;
	d3ddesc.ComparisonFunc = compareToD3D11[(int)desc.shadowCompareFunc];
	for (int i = 0; i < 4; i++) {
		d3ddesc.BorderColor[i] = 1.0f;
	}
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
	UINT stride;  // type to match function parameter
};

const char *semanticToD3D11(int semantic, UINT *index) {
	*index = 0;
	switch (semantic) {
	case SEM_POSITION: return "POSITION";
	case SEM_COLOR0: *index = 0; return "COLOR";
	case SEM_COLOR1: *index = 1; return "COLOR";
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
		el.InstanceDataStepRate = 0;
		el.InputSlot = 0;
		el.SemanticName = semanticToD3D11(desc.attributes[i].location, &el.SemanticIndex);
		el.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		inputLayout->elements.push_back(el);
	}
	inputLayout->stride = desc.stride;
	return inputLayout;
}

class D3D11ShaderModule : public ShaderModule {
public:
	D3D11ShaderModule(const std::string &tag) : tag_(tag) { }
	~D3D11ShaderModule() {
	}
	ShaderStage GetStage() const override { return stage; }

	std::vector<uint8_t> byteCode_;
	ShaderStage stage;
	std::string tag_;

	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3D11GeometryShader> gs;
};

class D3D11Pipeline : public Pipeline {
public:
	~D3D11Pipeline() {
		for (D3D11ShaderModule *shaderModule : shaderModules) {
			shaderModule->Release();
		}
	}

	AutoRef<D3D11InputLayout> input;
	ComPtr<ID3D11InputLayout> il;
	AutoRef<D3D11BlendState> blend;
	AutoRef<D3D11RasterState> raster;

	// Combined with dynamic state to key into cached D3D11DepthStencilState, to emulate dynamic parameters.
	AutoRef<D3D11DepthStencilState> depthStencil;

	ComPtr<ID3D11VertexShader> vs;
	ComPtr<ID3D11PixelShader> ps;
	ComPtr<ID3D11GeometryShader> gs;
	D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

	std::vector<D3D11ShaderModule *> shaderModules;

	size_t dynamicUniformsSize = 0;
	ComPtr<ID3D11Buffer> dynamicUniforms;
};

class D3D11Texture : public Texture {
public:
	D3D11Texture(const TextureDesc &desc) {
		width_ = desc.width;
		height_ = desc.height;
		depth_ = desc.depth;
		format_ = desc.format;
		mipLevels_ = desc.mipLevels;
	}
	~D3D11Texture() {
	}

	bool Create(ID3D11DeviceContext *context, ID3D11Device *device, const TextureDesc &desc, bool generateMips);

	bool CreateStagingTexture(ID3D11Device *device);
	void UpdateTextureLevels(ID3D11DeviceContext *context, ID3D11Device *device, Texture *texture, const uint8_t *const *data, TextureCallback initDataCallback, int numLevels);

	ID3D11ShaderResourceView *View() { return view_.Get(); }

private:
	bool FillLevel(ID3D11DeviceContext *context, int level, int w, int h, int d, const uint8_t *const *data, TextureCallback initDataCallback);

	ComPtr<ID3D11Texture2D> tex_;
	ComPtr<ID3D11Texture2D> stagingTex_;
	ComPtr<ID3D11ShaderResourceView> view_;
	int mipLevels_ = 0;
};

bool D3D11Texture::FillLevel(ID3D11DeviceContext *context, int level, int w, int h, int d, const uint8_t *const *data, TextureCallback initDataCallback) {
	D3D11_MAPPED_SUBRESOURCE mapped;
	HRESULT hr = context->Map(stagingTex_.Get(), level, D3D11_MAP_WRITE, 0, &mapped);
	if (!SUCCEEDED(hr)) {
		tex_.Reset();
		return false;
	}

	if (!initDataCallback((uint8_t *)mapped.pData, data[level], w, h, d, mapped.RowPitch, mapped.DepthPitch)) {
		for (int s = 0; s < d; ++s) {
			for (int y = 0; y < h; ++y) {
				void *dest = (uint8_t *)mapped.pData + mapped.DepthPitch * s + mapped.RowPitch * y;
				uint32_t byteStride = w * (uint32_t)DataFormatSizeInBytes(format_);
				const void *src = data[level] + byteStride * (y + h * s);
				memcpy(dest, src, byteStride);
			}
		}
	}
	context->Unmap(stagingTex_.Get(), level);
	return true;
}

bool D3D11Texture::CreateStagingTexture(ID3D11Device *device) {
	if (stagingTex_)
		return true;
	D3D11_TEXTURE2D_DESC descColor{};
	descColor.Width = width_;
	descColor.Height = height_;
	descColor.MipLevels = mipLevels_;
	descColor.ArraySize = 1;
	descColor.Format = dataFormatToD3D11(format_);
	descColor.SampleDesc.Count = 1;
	descColor.SampleDesc.Quality = 0;
	descColor.Usage = D3D11_USAGE_STAGING;
	descColor.BindFlags = 0;
	descColor.MiscFlags = 0;
	descColor.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	return SUCCEEDED(device->CreateTexture2D(&descColor, nullptr, &stagingTex_));
}

bool D3D11Texture::Create(ID3D11DeviceContext *context, ID3D11Device *device, const TextureDesc &desc, bool generateMips) {
	D3D11_TEXTURE2D_DESC descColor{};
	descColor.Width = desc.width;
	descColor.Height = desc.height;
	descColor.MipLevels = desc.mipLevels;
	descColor.ArraySize = 1;
	descColor.Format = dataFormatToD3D11(desc.format);
	descColor.SampleDesc.Count = 1;
	descColor.SampleDesc.Quality = 0;
	descColor.Usage = D3D11_USAGE_DEFAULT;
	descColor.BindFlags = generateMips ? (D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET) : D3D11_BIND_SHADER_RESOURCE;
	descColor.MiscFlags = generateMips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
	descColor.CPUAccessFlags = 0;

	// Make sure we have a staging texture if we'll need it.
	if (desc.initDataCallback && !CreateStagingTexture(device)) {
		return false;
	}

	D3D11_SUBRESOURCE_DATA *initDataParam = nullptr;
	D3D11_SUBRESOURCE_DATA initData[12]{};
	std::vector<uint8_t> initDataBuffer[12];
	if (desc.initData.size() && !generateMips && !desc.initDataCallback) {
		int w = desc.width;
		int h = desc.height;
		int d = desc.depth;
		for (int i = 0; i < (int)desc.initData.size(); i++) {
			uint32_t byteStride = w * (uint32_t)DataFormatSizeInBytes(desc.format);
			initData[i].pSysMem = desc.initData[i];
			initData[i].SysMemPitch = (UINT)byteStride;
			initData[i].SysMemSlicePitch = (UINT)(h * byteStride);
			w = (w + 1) / 2;
			h = (h + 1) / 2;
			d = (d + 1) / 2;
		}
		initDataParam = initData;
	}

	HRESULT hr = device->CreateTexture2D(&descColor, initDataParam, &tex_);
	if (!SUCCEEDED(hr)) {
		tex_ = nullptr;
		return false;
	}
	hr = device->CreateShaderResourceView(tex_.Get(), nullptr, &view_);
	if (!SUCCEEDED(hr)) {
		return false;
	}

	if (generateMips && desc.initData.size() >= 1) {
		if (desc.initDataCallback) {
			if (!FillLevel(context, 0, desc.width, desc.height, desc.depth, desc.initData.data(), desc.initDataCallback)) {
				tex_.Reset();
				return false;
			}

			context->CopyResource(tex_.Get(), stagingTex_.Get());
			stagingTex_.Reset();
		} else {
			uint32_t byteStride = desc.width * (uint32_t)DataFormatSizeInBytes(desc.format);
			context->UpdateSubresource(tex_.Get(), 0, nullptr, desc.initData[0], byteStride, 0);
		}
		context->GenerateMips(view_.Get());
	} else if (desc.initDataCallback) {
		int w = desc.width;
		int h = desc.height;
		int d = desc.depth;
		for (int i = 0; i < (int)desc.initData.size(); i++) {
			if (!FillLevel(context, i, w, h, d, desc.initData.data(), desc.initDataCallback)) {
				if (i == 0) {
					return false;
				} else {
					break;
				}
			}

			w = (w + 1) / 2;
			h = (h + 1) / 2;
			d = (d + 1) / 2;
		}

		context->CopyResource(tex_.Get(), stagingTex_.Get());
		stagingTex_.Reset();
	}
	return true;
}

void D3D11Texture::UpdateTextureLevels(ID3D11DeviceContext *context, ID3D11Device *device, Texture *texture, const uint8_t * const*data, TextureCallback initDataCallback, int numLevels) {
	if (!CreateStagingTexture(device)) {
		return;
	}

	int w = width_;
	int h = height_;
	int d = depth_;
	for (int i = 0; i < numLevels; i++) {
		if (!FillLevel(context, i, w, h, d, data, initDataCallback)) {
			break;
		}

		w = (w + 1) / 2;
		h = (h + 1) / 2;
		d = (d + 1) / 2;
	}

	context->CopyResource(tex_.Get(), stagingTex_.Get());
	stagingTex_.Reset();
}

Texture *D3D11DrawContext::CreateTexture(const TextureDesc &desc) {
	if (!(GetDataFormatSupport(desc.format) & FMT_TEXTURE)) {
		// D3D11 does not support this format as a texture format.
		return nullptr;
	}

	D3D11Texture *tex = new D3D11Texture(desc);
	bool generateMips = desc.generateMips;
	if (desc.generateMips && !(GetDataFormatSupport(desc.format) & FMT_AUTOGEN_MIPS)) {
		// D3D11 does not support autogenerating mipmaps for this format.
		generateMips = false;
	}
	if (!tex->Create(context_.Get(), device_.Get(), desc, generateMips)) {
		tex->Release();
		return nullptr;
	}

	return tex;
}


void D3D11DrawContext::UpdateTextureLevels(Texture *texture, const uint8_t **data, TextureCallback initDataCallback, int numLevels) {
	D3D11Texture *tex = (D3D11Texture *)texture;
	tex->UpdateTextureLevels(context_.Get(), device_.Get(), texture, data, initDataCallback, numLevels);
}

ShaderModule *D3D11DrawContext::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const char *tag) {
	if (language != ShaderLanguage::HLSL_D3D11) {
		ERROR_LOG(Log::G3D, "Unsupported shader language");
		return nullptr;
	}

	const char *vertexModel = "vs_4_0";
	const char *fragmentModel = "ps_4_0";
	const char *geometryModel = "gs_4_0";
	if (featureLevel_ <= D3D_FEATURE_LEVEL_9_3) {
		vertexModel = "vs_4_0_level_9_1";
		fragmentModel = "ps_4_0_level_9_1";
		geometryModel = nullptr;
	}

	std::string compiled;
	std::string errors;
	const char *target = nullptr;
	switch (stage) {
	case ShaderStage::Fragment: target = fragmentModel; break;
	case ShaderStage::Vertex: target = vertexModel; break;
	case ShaderStage::Geometry:
		if (!geometryModel)
			return nullptr;
		target = geometryModel;
		break;
	case ShaderStage::Compute:
	default:
		Crash();
		break;
	}
	if (!target) {
		return nullptr;
	}

	ComPtr<ID3DBlob> compiledCode;
	ComPtr<ID3DBlob> errorMsgs;
	int flags = D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;
	HRESULT result = ptr_D3DCompile(data, dataSize, nullptr, nullptr, nullptr, "main", target, flags, 0, &compiledCode, &errorMsgs);
	if (compiledCode) {
		compiled = std::string((const char *)compiledCode->GetBufferPointer(), compiledCode->GetBufferSize());
	}
	if (errorMsgs) {
		errors = std::string((const char *)errorMsgs->GetBufferPointer(), errorMsgs->GetBufferSize());
		ERROR_LOG(Log::G3D, "Failed compiling %s:\n%s\n%s", tag, data, errors.c_str());
	}

	if (result != S_OK) {
		return nullptr;
	}

	// OK, we can now proceed
	data = (const uint8_t *)compiled.c_str();
	dataSize = compiled.size();
	D3D11ShaderModule *module = new D3D11ShaderModule(tag);
	module->stage = stage;
	module->byteCode_ = std::vector<uint8_t>(data, data + dataSize);
	switch (stage) {
	case ShaderStage::Vertex:
		result = device_->CreateVertexShader(data, dataSize, nullptr, &module->vs);
		break;
	case ShaderStage::Fragment:
		result = device_->CreatePixelShader(data, dataSize, nullptr, &module->ps);
		break;
	case ShaderStage::Geometry:
		result = device_->CreateGeometryShader(data, dataSize, nullptr, &module->gs);
		break;
	default:
		ERROR_LOG(Log::G3D, "Unsupported shader stage");
		result = S_FALSE;
		break;
	}
	if (result == S_OK) {
		return module;
	} else {
		delete module;
		return nullptr;
	}
	return nullptr;
}

Pipeline *D3D11DrawContext::CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) {
	D3D11Pipeline *dPipeline = new D3D11Pipeline();
	dPipeline->blend = (D3D11BlendState *)desc.blend;
	dPipeline->depthStencil = (D3D11DepthStencilState *)desc.depthStencil;
	dPipeline->input = (D3D11InputLayout *)desc.inputLayout;
	dPipeline->raster = (D3D11RasterState *)desc.raster;
	dPipeline->topology = primToD3D11[(int)desc.prim];
	if (desc.uniformDesc) {
		dPipeline->dynamicUniformsSize = desc.uniformDesc->uniformBufferSize;
		D3D11_BUFFER_DESC bufdesc{};
		bufdesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		// We just round up to 16 here. If we get some garbage, that's fine.
		bufdesc.ByteWidth = ((UINT)dPipeline->dynamicUniformsSize + 15) & ~15;
		bufdesc.StructureByteStride = bufdesc.ByteWidth;
		bufdesc.Usage = D3D11_USAGE_DYNAMIC;
		bufdesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		HRESULT hr = device_->CreateBuffer(&bufdesc, nullptr, &dPipeline->dynamicUniforms);
		if (FAILED(hr)) {
			dPipeline->Release();
			return nullptr;
		}
	}

	std::vector<D3D11ShaderModule *> shaders;
	D3D11ShaderModule *vshader = nullptr;
	for (auto iter : desc.shaders) {
		iter->AddRef();

		D3D11ShaderModule *module = (D3D11ShaderModule *)iter;
		shaders.push_back(module);
		switch (module->GetStage()) {
		case ShaderStage::Vertex:
			vshader = module;
			dPipeline->vs = module->vs;
			break;
		case ShaderStage::Fragment:
			dPipeline->ps = module->ps;
			break;
		case ShaderStage::Geometry:
			dPipeline->gs = module->gs;
			break;
		case ShaderStage::Compute:
			break;
		}
	}
	dPipeline->shaderModules = shaders;

	if (!vshader) {
		// No vertex shader - no graphics
		dPipeline->Release();
		return nullptr;
	}

	// Can finally create the input layout
	if (dPipeline->input != nullptr) {
		const std::vector<D3D11_INPUT_ELEMENT_DESC> &elements = dPipeline->input->elements;
		HRESULT hr = device_->CreateInputLayout(elements.data(), (UINT)elements.size(), vshader->byteCode_.data(), vshader->byteCode_.size(), &dPipeline->il);
		if (!SUCCEEDED(hr)) {
			Crash();
		}
	} else {
		dPipeline->il.Reset();
	}
	return dPipeline;
}

void D3D11DrawContext::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	if (curPipeline_->dynamicUniformsSize != size) {
		Crash();
	}
	D3D11_MAPPED_SUBRESOURCE map{};
	context_->Map(curPipeline_->dynamicUniforms.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	memcpy(map.pData, ub, size);
	context_->Unmap(curPipeline_->dynamicUniforms.Get(), 0);
}

void D3D11DrawContext::Invalidate(InvalidationFlags flags) {
	if (flags & InvalidationFlags::CACHED_RENDER_STATE) {
		// This is a signal to forget all our state caching.
		curBlend_ = nullptr;
		curDepthStencil_ = nullptr;
		curRaster_ = nullptr;
		curPS_.Reset();
		curVS_.Reset();
		curGS_.Reset();
		curInputLayout_.Reset();
		curTopology_ = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
		curPipeline_= nullptr;
	}
}

void D3D11DrawContext::BindPipeline(Pipeline *pipeline) {
	D3D11Pipeline *dPipeline = (D3D11Pipeline *)pipeline;
	if (curPipeline_ == dPipeline)
		return;
	curPipeline_ = dPipeline;
}

void D3D11DrawContext::ApplyCurrentState() {
	if (curBlend_ != curPipeline_->blend || blendFactorDirty_) {
		context_->OMSetBlendState(curPipeline_->blend->bs.Get(), blendFactor_, 0xFFFFFFFF);
		curBlend_ = curPipeline_->blend;
		blendFactorDirty_ = false;
	}
	if (curDepthStencil_ != curPipeline_->depthStencil || stencilDirty_) {
		ComPtr<ID3D11DepthStencilState> dss;
		GetCachedDepthStencilState(curPipeline_->depthStencil, stencilWriteMask_, stencilCompareMask_, &dss);
		context_->OMSetDepthStencilState(dss.Get(), stencilRef_);
		curDepthStencil_ = curPipeline_->depthStencil;
		stencilDirty_ = false;
	}
	if (curRaster_ != curPipeline_->raster) {
		context_->RSSetState(curPipeline_->raster->rs.Get());
		curRaster_ = curPipeline_->raster;
	}
	if (curInputLayout_ != curPipeline_->il) {
		context_->IASetInputLayout(curPipeline_->il.Get());
		curInputLayout_ = curPipeline_->il;
	}
	if (curVS_ != curPipeline_->vs) {
		context_->VSSetShader(curPipeline_->vs.Get(), nullptr, 0);
		curVS_ = curPipeline_->vs;
	}
	if (curPS_ != curPipeline_->ps) {
		context_->PSSetShader(curPipeline_->ps.Get(), nullptr, 0);
		curPS_ = curPipeline_->ps;
	}
	if (curGS_ != curPipeline_->gs) {
		context_->GSSetShader(curPipeline_->gs.Get(), nullptr, 0);
		curGS_ = curPipeline_->gs;
	}
	if (curTopology_ != curPipeline_->topology) {
		context_->IASetPrimitiveTopology(curPipeline_->topology);
		curTopology_ = curPipeline_->topology;
	}

	if (curPipeline_->input != nullptr) {
		context_->IASetVertexBuffers(0, 1, nextVertexBuffer_.GetAddressOf(), &curPipeline_->input->stride, &nextVertexBufferOffset_);
	}
	if (dirtyIndexBuffer_) {
		context_->IASetIndexBuffer(nextIndexBuffer_.Get(), DXGI_FORMAT_R16_UINT, nextIndexBufferOffset_);
		dirtyIndexBuffer_ = false;
	}
	if (curPipeline_->dynamicUniforms) {
		context_->VSSetConstantBuffers(0, 1, curPipeline_->dynamicUniforms.GetAddressOf());
		context_->PSSetConstantBuffers(0, 1, curPipeline_->dynamicUniforms.GetAddressOf());
	}
}

class D3D11Buffer : public Buffer {
public:
	~D3D11Buffer() {
	}
	ComPtr<ID3D11Buffer> buf;
	ComPtr<ID3D11ShaderResourceView> srView;
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
	return b;
}

void D3D11DrawContext::UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) {
	D3D11Buffer *buf = (D3D11Buffer *)buffer;
	if ((flags & UPDATE_DISCARD) || (offset == 0 && size == buf->size)) {
		// Can just discard the old contents. This is only allowed for DYNAMIC buffers.
		D3D11_MAPPED_SUBRESOURCE map;
		context_->Map(buf->buf.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
		memcpy(map.pData, data, size);
		context_->Unmap(buf->buf.Get(), 0);
		return;
	}

	// Should probably avoid this case.
	D3D11_BOX box{};
	box.left = (UINT)offset;
	box.right = (UINT)(offset + size);
	box.bottom = 1;
	box.back = 1;
	context_->UpdateSubresource(buf->buf.Get(), 0, &box, data, 0, 0);
}

void D3D11DrawContext::BindVertexBuffer(Buffer *buffer, int offset) {
	// Lazy application
	D3D11Buffer *buf = (D3D11Buffer *)buffer;
	nextVertexBuffer_ = buf->buf;
	nextVertexBufferOffset_ = offset;
}

void D3D11DrawContext::BindIndexBuffer(Buffer *indexBuffer, int offset) {
	D3D11Buffer *buf = (D3D11Buffer *)indexBuffer;
	// Lazy application
	dirtyIndexBuffer_ = true;
	nextIndexBuffer_ = buf ? buf->buf : nullptr;
	nextIndexBufferOffset_ = buf ? offset : 0;
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
	int byteSize = vertexCount * curPipeline_->input->stride;

	UpdateBuffer(upBuffer_, (const uint8_t *)vdata, 0, byteSize, Draw::UPDATE_DISCARD);
	BindVertexBuffer(upBuffer_, 0);
	int offset = 0;
	Draw(vertexCount, offset);
}

void D3D11DrawContext::DrawIndexedUP(const void *vdata, int vertexCount, const void *idata, int indexCount) {
	int vbyteSize = vertexCount * curPipeline_->input->stride;
	int ibyteSize = indexCount * sizeof(u16);

	UpdateBuffer(upBuffer_, (const uint8_t *)vdata, 0, vbyteSize, Draw::UPDATE_DISCARD);
	BindVertexBuffer(upBuffer_, 0);

	UpdateBuffer(upIBuffer_, (const uint8_t *)idata, 0, ibyteSize, Draw::UPDATE_DISCARD);
	BindIndexBuffer(upIBuffer_, 0);
	DrawIndexed(indexCount, 0);
}

void D3D11DrawContext::DrawIndexedClippedBatchUP(const void *vdata, int vertexCount, const void *idata, int indexCount, Slice<ClippedDraw> draws, const void *ub, size_t ubSize) {
	if (draws.is_empty() || !vertexCount || !indexCount) {
		return;
	}

	curPipeline_ = (D3D11Pipeline *)draws[0].pipeline;

	int vbyteSize = vertexCount * curPipeline_->input->stride;
	int ibyteSize = indexCount * sizeof(u16);

	UpdateBuffer(upBuffer_, (const uint8_t *)vdata, 0, vbyteSize, Draw::UPDATE_DISCARD);
	BindVertexBuffer(upBuffer_, 0);

	UpdateBuffer(upIBuffer_, (const uint8_t *)idata, 0, ibyteSize, Draw::UPDATE_DISCARD);
	BindIndexBuffer(upIBuffer_, 0);

	UpdateDynamicUniformBuffer(ub, ubSize);
	ApplyCurrentState();

	for (int i = 0; i < draws.size(); i++) {
		if (draws[i].pipeline != curPipeline_) {
			curPipeline_ = (D3D11Pipeline *)draws[i].pipeline;
			ApplyCurrentState();
			UpdateDynamicUniformBuffer(ub, ubSize);
		}

		if (draws[i].bindTexture) {
			ComPtr<ID3D11ShaderResourceView> view = ((D3D11Texture *)draws[i].bindTexture)->View();
			context_->PSSetShaderResources(0, 1, view.GetAddressOf());
		} else {
			ComPtr<ID3D11ShaderResourceView> view = ((D3D11Framebuffer *)draws[i].bindFramebufferAsTex)->colorSRView;
			switch (draws[i].aspect) {
			case Aspect::DEPTH_BIT:
				view = ((D3D11Framebuffer *)draws[i].bindFramebufferAsTex)->depthSRView;
				break;
			case Aspect::STENCIL_BIT:
				view = ((D3D11Framebuffer *)draws[i].bindFramebufferAsTex)->stencilSRView;
				break;
			default:
				break;
			}
			context_->PSSetShaderResources(0, 1, view.GetAddressOf());
		}
		ComPtr<ID3D11SamplerState> sstate = ((D3D11SamplerState *)draws[i].samplerState)->ss;
		context_->PSSetSamplers(0, 1, sstate.GetAddressOf());
		D3D11_RECT rc;
		rc.left = draws[i].clipx;
		rc.top = draws[i].clipy;
		rc.right = draws[i].clipx + draws[i].clipw;
		rc.bottom = draws[i].clipy + draws[i].cliph;
		context_->RSSetScissorRects(1, &rc);
		context_->DrawIndexed(draws[i].indexCount, draws[i].indexOffset, 0);
	}
}

uint32_t D3D11DrawContext::GetDataFormatSupport(DataFormat fmt) const {
	DXGI_FORMAT giFmt = dataFormatToD3D11(fmt);
	if (giFmt == DXGI_FORMAT_UNKNOWN)
		return 0;
	UINT giSupport = 0;
	HRESULT result = device_->CheckFormatSupport(giFmt, &giSupport);
	if (FAILED(result))
		return 0;
	uint32_t support = 0;
	if (giSupport & D3D11_FORMAT_SUPPORT_TEXTURE2D)
		support |= FMT_TEXTURE;
	if (giSupport & D3D11_FORMAT_SUPPORT_RENDER_TARGET)
		support |= FMT_RENDERTARGET;
	if (giSupport & D3D11_FORMAT_SUPPORT_IA_VERTEX_BUFFER)
		support |= FMT_INPUTLAYOUT;
	if (giSupport & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL)
		support |= FMT_DEPTHSTENCIL;
	if (giSupport & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN)
		support |= FMT_AUTOGEN_MIPS;
	return support;
}

Framebuffer *D3D11DrawContext::CreateFramebuffer(const FramebufferDesc &desc) {
	HRESULT hr;
	D3D11Framebuffer *fb = new D3D11Framebuffer(desc.width, desc.height);

	// We don't (yet?) support multiview for D3D11. Not sure if there's a way to do it.
	// Texture arrays are supported but we don't have any other use cases yet.
	_dbg_assert_(desc.numLayers == 1);

	fb->colorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	D3D11_TEXTURE2D_DESC descColor{};
	descColor.Width = desc.width;
	descColor.Height = desc.height;
	descColor.MipLevels = 1;
	descColor.ArraySize = 1;
	descColor.Format = fb->colorFormat;
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
	hr = device_->CreateRenderTargetView(fb->colorTex.Get(), nullptr, &fb->colorRTView);
	if (FAILED(hr)) {
		delete fb;
		return nullptr;
	}
	hr = device_->CreateShaderResourceView(fb->colorTex.Get(), nullptr, &fb->colorSRView);
	if (FAILED(hr)) {
		delete fb;
		return nullptr;
	}

	if (desc.z_stencil) {
		fb->depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
		D3D11_TEXTURE2D_DESC descDepth{};
		descDepth.Width = desc.width;
		descDepth.Height = desc.height;
		descDepth.MipLevels = 1;
		descDepth.ArraySize = 1;
		descDepth.Format = DXGI_FORMAT_R24G8_TYPELESS;  // so we can create an R24X8 view of it.
		descDepth.SampleDesc.Count = 1;
		descDepth.SampleDesc.Quality = 0;
		descDepth.Usage = D3D11_USAGE_DEFAULT;
		descDepth.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
		descDepth.CPUAccessFlags = 0;
		descDepth.MiscFlags = 0;
		hr = device_->CreateTexture2D(&descDepth, nullptr, &fb->depthStencilTex);
		if (FAILED(hr)) {
			delete fb;
			return nullptr;
		}
		D3D11_DEPTH_STENCIL_VIEW_DESC descDSV{};
		descDSV.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		descDSV.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		descDSV.Texture2D.MipSlice = 0;
		hr = device_->CreateDepthStencilView(fb->depthStencilTex.Get(), &descDSV, &fb->depthStencilRTView);
		if (FAILED(hr)) {
			delete fb;
			return nullptr;
		}

		D3D11_SHADER_RESOURCE_VIEW_DESC depthViewDesc{};
		depthViewDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		depthViewDesc.Texture2D.MostDetailedMip = 0;
		depthViewDesc.Texture2D.MipLevels = 1;
		depthViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		hr = device_->CreateShaderResourceView(fb->depthStencilTex.Get(), &depthViewDesc, &fb->depthSRView);
		if (FAILED(hr)) {
			WARN_LOG(Log::G3D, "Failed to create SRV for depth buffer.");
			fb->depthSRView = nullptr;
		}


		D3D11_SHADER_RESOURCE_VIEW_DESC depthStencilViewDesc{};
		depthStencilViewDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		depthStencilViewDesc.Texture2D.MostDetailedMip = 0;
		depthStencilViewDesc.Texture2D.MipLevels = 1;
		depthStencilViewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		hr = device_->CreateShaderResourceView(fb->depthStencilTex.Get(), &depthViewDesc, &fb->stencilSRView);
		if (FAILED(hr)) {
			WARN_LOG(Log::G3D, "Failed to create SRV for depth+stencil buffer.");
			fb->depthSRView = nullptr;
		}
	}

	return fb;
}

void D3D11DrawContext::BindTextures(int start, int count, Texture **textures, TextureBindFlags flags) {
	// Collect the resource views from the textures.
	ID3D11ShaderResourceView *views[MAX_BOUND_TEXTURES];
	_assert_(start + count <= ARRAY_SIZE(views));
	for (int i = 0; i < count; i++) {
		D3D11Texture *tex = (D3D11Texture *)textures[i];
		views[i] = tex ? tex->View() : nullptr;
	}
	context_->PSSetShaderResources(start, count, views);
}

void D3D11DrawContext::BindNativeTexture(int index, void *nativeTexture) {
	// Collect the resource views from the textures.
	ID3D11ShaderResourceView *view = (ID3D11ShaderResourceView *)nativeTexture;
	context_->PSSetShaderResources(index, 1, &view);
}

void D3D11DrawContext::BindSamplerStates(int start, int count, SamplerState **states) {
	ID3D11SamplerState *samplers[MAX_BOUND_TEXTURES];
	_assert_(start + count <= ARRAY_SIZE(samplers));
	for (int i = 0; i < count; i++) {
		D3D11SamplerState *samp = (D3D11SamplerState *)states[i];
		samplers[i] = samp ? samp->ss.Get() : nullptr;
	}
	context_->PSSetSamplers(start, count, samplers);
}

void D3D11DrawContext::Clear(Aspect mask, uint32_t colorval, float depthVal, int stencilVal) {
	if ((mask & Aspect::COLOR_BIT) && curRenderTargetView_) {
		float colorRGBA[4];
		Uint8x4ToFloat4(colorRGBA, colorval);
		context_->ClearRenderTargetView(curRenderTargetView_.Get(), colorRGBA);
	}
	if ((mask & (Aspect::DEPTH_BIT | Aspect::STENCIL_BIT)) && curDepthStencilView_) {
		UINT clearFlag = 0;
		if (mask & Aspect::DEPTH_BIT)
			clearFlag |= D3D11_CLEAR_DEPTH;
		if (mask & Aspect::STENCIL_BIT)
			clearFlag |= D3D11_CLEAR_STENCIL;
		context_->ClearDepthStencilView(curDepthStencilView_.Get(), clearFlag, depthVal, stencilVal);
	}
}

void D3D11DrawContext::BeginFrame(DebugFlags debugFlags) {
	FrameTimeData &frameTimeData = frameTimeHistory_.Add(frameCount_);
	frameTimeData.afterFenceWait = time_now_d();
	frameTimeData.frameBegin = frameTimeData.afterFenceWait;

	context_->OMSetRenderTargets(1, curRenderTargetView_.GetAddressOf(), curDepthStencilView_.Get());

	if (curBlend_ != nullptr) {
		context_->OMSetBlendState(curBlend_->bs.Get(), blendFactor_, 0xFFFFFFFF);
	}
	if (curDepthStencil_ != nullptr) {
		ComPtr<ID3D11DepthStencilState> dss;
		GetCachedDepthStencilState(curDepthStencil_, stencilWriteMask_, stencilCompareMask_, &dss);
		context_->OMSetDepthStencilState(dss.Get(), stencilRef_);
	}
	if (curRaster_ != nullptr) {
		context_->RSSetState(curRaster_->rs.Get());
	}
	context_->IASetInputLayout(curInputLayout_.Get());
	context_->VSSetShader(curVS_.Get(), nullptr, 0);
	context_->PSSetShader(curPS_.Get(), nullptr, 0);
	context_->GSSetShader(curGS_.Get(), nullptr, 0);
	if (curTopology_ != D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED) {
		context_->IASetPrimitiveTopology(curTopology_);
	}
	if (curPipeline_ != nullptr) {
		context_->IASetVertexBuffers(0, 1, nextVertexBuffer_.GetAddressOf(), &curPipeline_->input->stride, &nextVertexBufferOffset_);
		context_->IASetIndexBuffer(nextIndexBuffer_.Get(), DXGI_FORMAT_R16_UINT, nextIndexBufferOffset_);
		if (curPipeline_->dynamicUniforms) {
			context_->VSSetConstantBuffers(0, 1, curPipeline_->dynamicUniforms.GetAddressOf());
			context_->PSSetConstantBuffers(0, 1, curPipeline_->dynamicUniforms.GetAddressOf());
		}
	}
}

void D3D11DrawContext::CopyFramebufferImage(Framebuffer *srcfb, int level, int x, int y, int z, Framebuffer *dstfb, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, Aspect aspect, const char *tag) {
	D3D11Framebuffer *src = (D3D11Framebuffer *)srcfb;
	D3D11Framebuffer *dst = (D3D11Framebuffer *)dstfb;

	ComPtr<ID3D11Texture2D> srcTex;
	ComPtr<ID3D11Texture2D> dstTex;
	switch (aspect) {
	case Aspect::COLOR_BIT:
		srcTex = src->colorTex;
		dstTex = dst->colorTex;
		break;
	case Aspect::DEPTH_BIT:
		srcTex = src->depthStencilTex;
		dstTex = dst->depthStencilTex;
		break;
	case Aspect::NO_BIT:
	case Aspect::STENCIL_BIT:
	case Aspect::SURFACE_BIT:
	case Aspect::VIEW_BIT:
	case Aspect::FORMAT_BIT:
		break;
	}
	_assert_(srcTex && dstTex);

	// TODO: Check for level too!
	if (width == src->Width() && width == dst->Width() && height == src->Height() && height == dst->Height() && x == 0 && y == 0 && z == 0 && dstX == 0 && dstY == 0 && dstZ == 0) {
		// Don't need to specify region. This might be faster, too.
		context_->CopyResource(dstTex.Get(), srcTex.Get());
		return;
	}

	if (aspect != Aspect::DEPTH_BIT) {
		// Non-full copies are not supported for the depth channel.
		// Note that we need to clip the source box.
		if (x < 0) {
			width += x;  // note that x is negative
			dstX -= x;
			x = 0;
		}
		if (y < 0) {
			height += y;  // note that y is negative
			dstY -= y;
			y = 0;
		}
		if (x + width > src->Width()) {
			width = src->Width() - x;
		}
		if (y + height > src->Height()) {
			height = src->Height() - y;
		}
		D3D11_BOX srcBox{ (UINT)x, (UINT)y, (UINT)z, (UINT)(x + width), (UINT)(y + height), (UINT)(z + depth) };
		context_->CopySubresourceRegion(dstTex.Get(), dstLevel, dstX, dstY, dstZ, srcTex.Get(), level, &srcBox);
	}
}

bool D3D11DrawContext::BlitFramebuffer(Framebuffer *srcfb, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dstfb, int dstX1, int dstY1, int dstX2, int dstY2, Aspect aspects, FBBlitFilter filter, const char *tag) {
	// Unfortunately D3D11 has no equivalent to this, gotta render a quad. Well, in some cases we can issue a copy instead.
	Crash();
	return false;
}

bool D3D11DrawContext::CopyFramebufferToMemory(Framebuffer *src, Aspect channelBits, int bx, int by, int bw, int bh, Draw::DataFormat destFormat, void *pixels, int pixelStride, ReadbackMode mode, const char *tag) {
	D3D11Framebuffer *fb = (D3D11Framebuffer *)src;

	if (fb) {
		_assert_(fb->colorFormat == DXGI_FORMAT_R8G8B8A8_UNORM);

		// TODO: Figure out where the badness really comes from.
		if (bx + bw > fb->Width()) {
			bw -= (bx + bw) - fb->Width();
		}
		if (by + bh > fb->Height()) {
			bh -= (by + bh) - fb->Height();
		}
	}

	if (bh <= 0 || bw <= 0)
		return true;

	bool useGlobalPacktex = (bx + bw <= 512 && by + bh <= 512) && channelBits == Aspect::COLOR_BIT;

	ComPtr<ID3D11Texture2D> packTex;
	if (!useGlobalPacktex) {
		D3D11_TEXTURE2D_DESC packDesc{};
		packDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		packDesc.BindFlags = 0;
		packDesc.Width = bw;
		packDesc.Height = bh;
		packDesc.ArraySize = 1;
		packDesc.MipLevels = 1;
		packDesc.Usage = D3D11_USAGE_STAGING;
		packDesc.SampleDesc.Count = 1;
		switch (channelBits) {
		case Aspect::COLOR_BIT:
			packDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // TODO: fb->colorFormat;
			break;
		case Aspect::DEPTH_BIT:
		case Aspect::STENCIL_BIT:
			if (!fb) {
				// Not supported.
				return false;
			}
			packDesc.Format = fb->depthStencilFormat;
			break;
		default:
			_assert_(false);
		}
		device_->CreateTexture2D(&packDesc, nullptr, &packTex);
	} else {
		switch (channelBits) {
		case Aspect::DEPTH_BIT:
		case Aspect::STENCIL_BIT:
			if (!fb)
				return false;
		default:
			break;
		}
		packTex = packTexture_;
	}

	if (!packTex)
		return false;

	D3D11_BOX srcBox{ (UINT)bx, (UINT)by, 0, (UINT)(bx + bw), (UINT)(by + bh), 1 };
	DataFormat srcFormat = DataFormat::UNDEFINED;
	switch (channelBits) {
	case Aspect::COLOR_BIT:
		context_->CopySubresourceRegion(packTex.Get(), 0, bx, by, 0, fb ? fb->colorTex.Get() : bbRenderTargetTex_, 0, &srcBox);
		srcFormat = DataFormat::R8G8B8A8_UNORM;
		break;
	case Aspect::DEPTH_BIT:
	case Aspect::STENCIL_BIT:
		// For depth/stencil buffers, we can't reliably copy subrectangles, so just copy the whole resource.
		_assert_(fb);  // Can't copy depth/stencil from backbuffer. Shouldn't happen thanks to checks above.
		context_->CopyResource(packTex.Get(), fb->depthStencilTex.Get());
		srcFormat = Draw::DataFormat::D24_S8;
		break;
	default:
		_assert_(false);
		break;
	}

	// Ideally, we'd round robin between two packTexture_, and simply use the other one. Though if the game
	// does a once-off copy, that won't work at all.

	// BIG GPU STALL
	D3D11_MAPPED_SUBRESOURCE map;
	HRESULT result = context_->Map(packTex.Get(), 0, D3D11_MAP_READ, 0, &map);
	if (FAILED(result)) {
		return false;
	}

	const size_t srcByteOffset = by * map.RowPitch + bx * DataFormatSizeInBytes(srcFormat);
	const uint8_t *srcWithOffset = (const uint8_t *)map.pData + srcByteOffset;
	switch ((Aspect)channelBits) {
	case Aspect::COLOR_BIT:
		// Pixel size always 4 here because we always request BGRA8888.
		ConvertFromRGBA8888((uint8_t *)pixels, srcWithOffset, pixelStride, map.RowPitch / sizeof(uint32_t), bw, bh, destFormat);
		break;
	case Aspect::DEPTH_BIT:
		if (srcFormat == destFormat) {
			// Can just memcpy when it matches no matter the format!
			uint8_t *dst = (uint8_t *)pixels;
			const uint8_t *src = (const uint8_t *)srcWithOffset;
			for (int y = 0; y < bh; ++y) {
				memcpy(dst, src, bw * DataFormatSizeInBytes(srcFormat));
				dst += pixelStride * DataFormatSizeInBytes(srcFormat);
				src += map.RowPitch;
			}
		} else if (destFormat == DataFormat::D32F) {
			ConvertToD32F((uint8_t *)pixels, srcWithOffset, pixelStride, map.RowPitch / sizeof(uint32_t), bw, bh, srcFormat);
		} else if (destFormat == DataFormat::D16) {
			ConvertToD16((uint8_t *)pixels, srcWithOffset, pixelStride, map.RowPitch / sizeof(uint32_t), bw, bh, srcFormat);
		} else {
			_assert_(false);
		}
		break;
	case Aspect::STENCIL_BIT:
		if (srcFormat == destFormat) {
			// Can just memcpy when it matches no matter the format!
			uint8_t *dst = (uint8_t *)pixels;
			const uint8_t *src = (const uint8_t *)srcWithOffset;
			for (int y = 0; y < bh; ++y) {
				memcpy(dst, src, bw * DataFormatSizeInBytes(srcFormat));
				dst += pixelStride * DataFormatSizeInBytes(srcFormat);
				src += map.RowPitch;
			}
		} else if (destFormat == DataFormat::S8) {
			for (int y = 0; y < bh; y++) {
				uint8_t *destStencil = (uint8_t *)pixels + y * pixelStride;
				const uint32_t *src = (const uint32_t *)(srcWithOffset + map.RowPitch * y);
				for (int x = 0; x < bw; x++) {
					destStencil[x] = src[x] >> 24;
				}
			}
		} else {
			_assert_(false);
		}
		break;
	case Aspect::NO_BIT:
	case Aspect::SURFACE_BIT:
	case Aspect::VIEW_BIT:
	case Aspect::FORMAT_BIT:
		break;
	}

	context_->Unmap(packTex.Get(), 0);

	return true;
}

void D3D11DrawContext::BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) {
	// TODO: deviceContext1 can actually discard. Useful on Windows Mobile.
	if (fbo) {
		D3D11Framebuffer *fb = (D3D11Framebuffer *)fbo;
		if (curRenderTargetView_ == fb->colorRTView && curDepthStencilView_ == fb->depthStencilRTView) {
			// No need to switch, but let's fallthrough to clear!
		} else {
			// It's not uncommon that the first slot happens to have the new render target bound as a texture,
			// so unbind to make the validation layers happy.
			ID3D11ShaderResourceView *empty[1] = {};
			context_->PSSetShaderResources(0, ARRAY_SIZE(empty), empty);
			context_->OMSetRenderTargets(1, fb->colorRTView.GetAddressOf(), fb->depthStencilRTView.Get());
			curRenderTargetView_ = fb->colorRTView;
			curDepthStencilView_ = fb->depthStencilRTView;
			curRTWidth_ = fb->Width();
			curRTHeight_ = fb->Height();
		}
		curRenderTarget_ = fb;
	} else {
		if (curRenderTargetView_.Get() == bbRenderTargetView_ && curDepthStencilView_ == bbDepthStencilView_) {
			// No need to switch, but let's fallthrough to clear!
		} else {
			context_->OMSetRenderTargets(1, &bbRenderTargetView_, bbDepthStencilView_.Get());
			curRenderTargetView_ = bbRenderTargetView_;
			curDepthStencilView_ = bbDepthStencilView_;
			curRTWidth_ = bbWidth_;
			curRTHeight_ = bbHeight_;
		}
		curRenderTarget_ = nullptr;
	}
	if (rp.color == RPAction::CLEAR && curRenderTargetView_) {
		float cv[4]{};
		if (rp.clearColor)
			Uint8x4ToFloat4(cv, rp.clearColor);
		context_->ClearRenderTargetView(curRenderTargetView_.Get(), cv);
	}
	int mask = 0;
	if (rp.depth == RPAction::CLEAR) {
		mask |= D3D11_CLEAR_DEPTH;
	}
	if (rp.stencil == RPAction::CLEAR) {
		mask |= D3D11_CLEAR_STENCIL;
	}
	if (mask && curDepthStencilView_) {
		context_->ClearDepthStencilView(curDepthStencilView_.Get(), mask, rp.clearDepth, rp.clearStencil);
	}

	if (invalidationCallback_) {
		invalidationCallback_(InvalidationCallbackFlags::RENDER_PASS_STATE);
	}
}

void D3D11DrawContext::BindFramebufferAsTexture(Framebuffer *fbo, int binding, Aspect channelBit, int layer) {
	_dbg_assert_(binding < MAX_BOUND_TEXTURES);
	_dbg_assert_(layer == ALL_LAYERS || layer == 0);  // No multiple layer support on D3D
	D3D11Framebuffer *fb = (D3D11Framebuffer *)fbo;
	switch (channelBit) {
	case Aspect::COLOR_BIT:
		context_->PSSetShaderResources(binding, 1, fb->colorSRView.GetAddressOf());
		break;
	case Aspect::DEPTH_BIT:
		if (fb->depthSRView) {
			context_->PSSetShaderResources(binding, 1, fb->depthSRView.GetAddressOf());
		}
		break;
	default:
		break;
	}
}

uint64_t D3D11DrawContext::GetNativeObject(NativeObject obj, void *srcObject) {
	switch (obj) {
	case NativeObject::DEVICE:
		return (uint64_t)(uintptr_t)device_.Get();
	case NativeObject::CONTEXT:
		return (uint64_t)(uintptr_t)context_.Get();
	case NativeObject::DEVICE_EX:
		return (uint64_t)(uintptr_t)device1_.Get();
	case NativeObject::CONTEXT_EX:
		return (uint64_t)(uintptr_t)context1_.Get();
	case NativeObject::BACKBUFFER_COLOR_TEX:
		return (uint64_t)(uintptr_t)bbRenderTargetTex_;
	case NativeObject::BACKBUFFER_DEPTH_TEX:
		return (uint64_t)(uintptr_t)bbDepthStencilTex_.Get();
	case NativeObject::BACKBUFFER_COLOR_VIEW:
		return (uint64_t)(uintptr_t)bbRenderTargetView_;
	case NativeObject::BACKBUFFER_DEPTH_VIEW:
		return (uint64_t)(uintptr_t)bbDepthStencilView_.Get();
	case NativeObject::FEATURE_LEVEL:
		return (uint64_t)(uintptr_t)featureLevel_;
	case NativeObject::TEXTURE_VIEW:
		return (uint64_t)(((D3D11Texture *)srcObject)->View());
	default:
		return 0;
	}
}

void D3D11DrawContext::GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) {
	D3D11Framebuffer *fb = (D3D11Framebuffer *)fbo;
	if (fb) {
		*w = fb->Width();
		*h = fb->Height();
	} else {
		*w = bbWidth_;
		*h = bbHeight_;
	}
}

DrawContext *T3DCreateD3D11Context(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Device1 *device1, ID3D11DeviceContext1 *context1, IDXGISwapChain *swapChain, D3D_FEATURE_LEVEL featureLevel, HWND hWnd, const std::vector<std::string> &adapterNames, int maxInflightFrames) {
	return new D3D11DrawContext(device, context, device1, context1, swapChain, featureLevel, hWnd, adapterNames, maxInflightFrames);
}

}  // namespace Draw
