#include <vector>
#include <cstdio>
#include <cstdint>

#include "ppsspp_config.h"

#ifdef _DEBUG
#define D3D_DEBUG_INFO
#endif

#include <d3d9.h>
#ifdef USE_CRT_DBG
#undef new
#endif

#include <D3Dcompiler.h>
#include "Common/GPU/D3D9/D3DCompilerLoader.h"

#ifndef D3DXERR_INVALIDDATA
#define D3DXERR_INVALIDDATA 0x88760b59
#endif

#include "Common/Math/lin/matrix4x4.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/D3D9/D3D9StateCache.h"
#include "Common/StringUtils.h"

#include "Common/Log.h"

namespace Draw {

static constexpr int MAX_BOUND_TEXTURES = 8;

// Could be declared as u8
static const D3DCMPFUNC compareToD3D9[] = {
	D3DCMP_NEVER,
	D3DCMP_LESS,
	D3DCMP_EQUAL,
	D3DCMP_LESSEQUAL,
	D3DCMP_GREATER,
	D3DCMP_NOTEQUAL,
	D3DCMP_GREATEREQUAL,
	D3DCMP_ALWAYS
};

// Could be declared as u8
static const D3DBLENDOP blendEqToD3D9[] = {
	D3DBLENDOP_ADD,
	D3DBLENDOP_SUBTRACT,
	D3DBLENDOP_REVSUBTRACT,
	D3DBLENDOP_MIN,
	D3DBLENDOP_MAX,
};

// Could be declared as u8
static const D3DBLEND blendFactorToD3D9[] = {
	D3DBLEND_ZERO,
	D3DBLEND_ONE,
	D3DBLEND_SRCCOLOR,
	D3DBLEND_INVSRCCOLOR,
	D3DBLEND_DESTCOLOR,
	D3DBLEND_INVDESTCOLOR,
	D3DBLEND_SRCALPHA,
	D3DBLEND_INVSRCALPHA,
	D3DBLEND_DESTALPHA,
	D3DBLEND_INVDESTALPHA,
	D3DBLEND_BLENDFACTOR,
	D3DBLEND_INVBLENDFACTOR,
	D3DBLEND_BLENDFACTOR,
	D3DBLEND_INVBLENDFACTOR,
	D3DBLEND_ZERO,
	D3DBLEND_ZERO,
	D3DBLEND_ZERO,
	D3DBLEND_ZERO,
};

static const D3DTEXTUREADDRESS texWrapToD3D9[] = {
	D3DTADDRESS_WRAP,
	D3DTADDRESS_MIRROR,
	D3DTADDRESS_CLAMP,
	D3DTADDRESS_BORDER,
};

static const D3DTEXTUREFILTERTYPE texFilterToD3D9[] = {
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
};

static const D3DPRIMITIVETYPE primToD3D9[] = {
	D3DPT_POINTLIST,
	D3DPT_LINELIST,
	D3DPT_LINESTRIP,
	D3DPT_TRIANGLELIST,
	D3DPT_TRIANGLESTRIP,
	D3DPT_TRIANGLEFAN,
	// These aren't available.
	D3DPT_POINTLIST,  // tess
	D3DPT_POINTLIST,  // geom ... 
	D3DPT_POINTLIST,
	D3DPT_POINTLIST,
	D3DPT_POINTLIST,
};

static const D3DSTENCILOP stencilOpToD3D9[] = {
	D3DSTENCILOP_KEEP,
	D3DSTENCILOP_ZERO,
	D3DSTENCILOP_REPLACE,
	D3DSTENCILOP_INCRSAT,
	D3DSTENCILOP_DECRSAT,
	D3DSTENCILOP_INVERT,
	D3DSTENCILOP_INCR,
	D3DSTENCILOP_DECR,
};

D3DFORMAT FormatToD3DFMT(DataFormat fmt) {
	switch (fmt) {
	case DataFormat::R16_UNORM: return D3DFMT_L16;  // closest match, should be a fine substitution if we ignore channels except R.
	case DataFormat::R8G8B8A8_UNORM: return D3DFMT_A8R8G8B8;
	case DataFormat::B8G8R8A8_UNORM: return D3DFMT_A8R8G8B8;
	case DataFormat::R4G4B4A4_UNORM_PACK16: return D3DFMT_A4R4G4B4;  // emulated
	case DataFormat::B4G4R4A4_UNORM_PACK16: return D3DFMT_A4R4G4B4;  // native
	case DataFormat::A4R4G4B4_UNORM_PACK16: return D3DFMT_A4R4G4B4;  // emulated
	case DataFormat::R5G6B5_UNORM_PACK16: return D3DFMT_R5G6B5;
	case DataFormat::A1R5G5B5_UNORM_PACK16: return D3DFMT_A1R5G5B5;
	case DataFormat::D24_S8: return D3DFMT_D24S8;
	case DataFormat::D16: return D3DFMT_D16;
	default: return D3DFMT_UNKNOWN;
	}
}

static int FormatToD3DDeclType(DataFormat type) {
	switch (type) {
	case DataFormat::R32_FLOAT: return D3DDECLTYPE_FLOAT1;
	case DataFormat::R32G32_FLOAT: return D3DDECLTYPE_FLOAT2;
	case DataFormat::R32G32B32_FLOAT: return D3DDECLTYPE_FLOAT3;
	case DataFormat::R32G32B32A32_FLOAT: return D3DDECLTYPE_FLOAT4;
	case DataFormat::R8G8B8A8_UNORM: return D3DDECLTYPE_UBYTE4N;  // D3DCOLOR has a different byte ordering.
	default: return D3DDECLTYPE_UNUSED;
	}
}

class D3D9Buffer;

class D3D9DepthStencilState : public DepthStencilState {
public:
	BOOL depthTestEnabled;
	BOOL depthWriteEnabled;
	D3DCMPFUNC depthCompare;
	BOOL stencilEnabled;
	D3DSTENCILOP stencilFail;
	D3DSTENCILOP stencilZFail;
	D3DSTENCILOP stencilPass;
	D3DCMPFUNC stencilCompareOp;

	void Apply(LPDIRECT3DDEVICE9 device, uint8_t stencilRef, uint8_t stencilWriteMask, uint8_t stencilCompareMask) {
		dxstate.depthTest.set(depthTestEnabled);
		if (depthTestEnabled) {
			dxstate.depthWrite.set(depthWriteEnabled);
			dxstate.depthFunc.set(depthCompare);
		}
		dxstate.stencilTest.set(stencilEnabled);
		if (stencilEnabled) {
			dxstate.stencilOp.set(stencilFail, stencilZFail, stencilPass);
			dxstate.stencilFunc.set(stencilCompareOp);
			dxstate.stencilRef.set(stencilRef);
			dxstate.stencilCompareMask.set(stencilCompareMask);
			dxstate.stencilWriteMask.set(stencilWriteMask);
		}
	}
};

class D3D9RasterState : public RasterState {
public:
	DWORD cullMode;   // D3DCULL_*

	void Apply(LPDIRECT3DDEVICE9 device) {
		dxstate.cullMode.set(cullMode);
		dxstate.scissorTest.enable();
	}
};

class D3D9BlendState : public BlendState {
public:
	bool enabled;
	D3DBLENDOP eqCol, eqAlpha;
	D3DBLEND srcCol, srcAlpha, dstCol, dstAlpha;
	uint32_t colorMask;

	void Apply(LPDIRECT3DDEVICE9 device) {
		dxstate.blend.set(enabled);
		dxstate.blendFunc.set(srcCol, dstCol, srcAlpha, dstAlpha);
		dxstate.blendEquation.set(eqCol, eqAlpha);
		dxstate.colorMask.set(colorMask);
	}
};

class D3D9SamplerState : public SamplerState {
public:
	D3DTEXTUREADDRESS wrapS, wrapT;
	D3DTEXTUREFILTERTYPE magFilt, minFilt, mipFilt;

	void Apply(LPDIRECT3DDEVICE9 device, int index) {
		dxstate.texAddressU.set(wrapS);
		dxstate.texAddressV.set(wrapT);
		dxstate.texMagFilter.set(magFilt);
		dxstate.texMinFilter.set(minFilt);
		dxstate.texMipFilter.set(mipFilt);
	}
};

class D3D9InputLayout : public InputLayout {
public:
	D3D9InputLayout(LPDIRECT3DDEVICE9 device, const InputLayoutDesc &desc);
	~D3D9InputLayout() {
		if (decl_) {
			decl_->Release();
		}
	}
	int GetStride(int binding) const { return stride_[binding]; }
	void Apply(LPDIRECT3DDEVICE9 device) {
		device->SetVertexDeclaration(decl_);
	}

private:
	LPDIRECT3DVERTEXDECLARATION9 decl_;
	int stride_[4];
};

class D3D9ShaderModule : public ShaderModule {
public:
	D3D9ShaderModule(ShaderStage stage, const std::string &tag) : stage_(stage), tag_(tag) {}
	~D3D9ShaderModule() {
		if (vshader_)
			vshader_->Release();
		if (pshader_)
			pshader_->Release();
	}
	bool Compile(LPDIRECT3DDEVICE9 device, const uint8_t *data, size_t size);
	void Apply(LPDIRECT3DDEVICE9 device) {
		if (stage_ == ShaderStage::Fragment) {
			device->SetPixelShader(pshader_);
		} else {
			device->SetVertexShader(vshader_);
		}
	}
	ShaderStage GetStage() const override { return stage_; }

private:
	ShaderStage stage_;
	LPDIRECT3DVERTEXSHADER9 vshader_ = nullptr;
	LPDIRECT3DPIXELSHADER9 pshader_ = nullptr;
	std::string tag_;
};

class D3D9Pipeline : public Pipeline {
public:
	D3D9Pipeline() {}
	~D3D9Pipeline() {
		if (vshader) {
			vshader->Release();
		}
		if (pshader) {
			pshader->Release();
		}
	}

	D3D9ShaderModule *vshader = nullptr;
	D3D9ShaderModule *pshader = nullptr;

	D3DPRIMITIVETYPE prim{};
	AutoRef<D3D9InputLayout> inputLayout;
	AutoRef<D3D9DepthStencilState> depthStencil;
	AutoRef<D3D9BlendState> blend;
	AutoRef<D3D9RasterState> raster;
	UniformBufferDesc dynamicUniforms{};

	void Apply(LPDIRECT3DDEVICE9 device, uint8_t stencilRef, uint8_t stencilWriteMask, uint8_t stencilCompareMask);
};

class D3D9Texture : public Texture {
public:
	D3D9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx, const TextureDesc &desc);
	~D3D9Texture();
	void SetToSampler(LPDIRECT3DDEVICE9 device, int sampler);
	LPDIRECT3DBASETEXTURE9 Texture() const {
		// TODO: Cleanup
		if (tex_) {
			return tex_;
		} else if (volTex_) {
			return volTex_;
		} else if (cubeTex_) {
			return cubeTex_;
		} else {
			return nullptr;
		}
	}

private:
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data, TextureCallback callback);
	bool Create(const TextureDesc &desc);
	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9EX deviceEx_;
	TextureType type_;
	DataFormat format_;
	D3DFORMAT d3dfmt_;
	LPDIRECT3DTEXTURE9 tex_ = nullptr;
	LPDIRECT3DVOLUMETEXTURE9 volTex_ = nullptr;
	LPDIRECT3DCUBETEXTURE9 cubeTex_ = nullptr;
};

D3D9Texture::D3D9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx, const TextureDesc &desc)
	: device_(device), deviceEx_(deviceEx), tex_(nullptr), volTex_(nullptr), cubeTex_(nullptr) {
	Create(desc);
}

D3D9Texture::~D3D9Texture() {
	if (tex_) {
		tex_->Release();
	}
	if (volTex_) {
		volTex_->Release();
	}
	if (cubeTex_) {
		cubeTex_->Release();
	}
}

bool D3D9Texture::Create(const TextureDesc &desc) {
	width_ = desc.width;
	height_ = desc.height;
	depth_ = desc.depth;
	type_ = desc.type;
	format_ = desc.format;
	tex_ = NULL;
	d3dfmt_ = FormatToD3DFMT(desc.format);

	if (d3dfmt_ == D3DFMT_UNKNOWN) {
		return false;
	}
	HRESULT hr = E_FAIL;

	D3DPOOL pool = D3DPOOL_MANAGED;
	int usage = 0;
	if (deviceEx_ != nullptr) {
		pool = D3DPOOL_DEFAULT;
		usage = D3DUSAGE_DYNAMIC;
	}
	if (desc.generateMips)
		usage |= D3DUSAGE_AUTOGENMIPMAP;
	switch (type_) {
	case TextureType::LINEAR1D:
	case TextureType::LINEAR2D:
		hr = device_->CreateTexture(desc.width, desc.height, desc.generateMips ? 0 : desc.mipLevels, usage, d3dfmt_, pool, &tex_, NULL);
		break;
	case TextureType::LINEAR3D:
		hr = device_->CreateVolumeTexture(desc.width, desc.height, desc.depth, desc.mipLevels, usage, d3dfmt_, pool, &volTex_, NULL);
		break;
	case TextureType::CUBE:
		hr = device_->CreateCubeTexture(desc.width, desc.mipLevels, usage, d3dfmt_, pool, &cubeTex_, NULL);
		break;
	}
	if (FAILED(hr)) {
		ERROR_LOG(G3D,  "Texture creation failed");
		return false;
	}

	if (desc.initData.size()) {
		// In D3D9, after setting D3DUSAGE_AUTOGENMIPS, we can only access the top layer. The rest will be
		// automatically generated.
		int maxLevel = desc.generateMips ? 1 : (int)desc.initData.size();
		int w = desc.width;
		int h = desc.height;
		int d = desc.depth;
		for (int i = 0; i < maxLevel; i++) {
			SetImageData(0, 0, 0, w, h, d, i, 0, desc.initData[i], desc.initDataCallback);
			w = (w + 1) / 2;
			h = (h + 1) / 2;
			d = (d + 1) / 2;
		}
	}
	return true;
}

// Just switches R and G.
inline uint32_t Shuffle8888(uint32_t x) {
	return (x & 0xFF00FF00) | ((x >> 16) & 0xFF) | ((x << 16) & 0xFF0000);
}

void D3D9Texture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data, TextureCallback callback) {
	if (!tex_)
		return;

	if (level == 0) {
		width_ = width;
		height_ = height;
		depth_ = depth;
	}

	if (!stride) {
		stride = width * (int)DataFormatSizeInBytes(format_);
	}

	switch (type_) {
	case TextureType::LINEAR2D:
	{
		D3DLOCKED_RECT rect;
		if (x == 0 && y == 0) {
			tex_->LockRect(level, &rect, NULL, D3DLOCK_DISCARD);

			if (callback) {
				if (callback((uint8_t *)rect.pBits, data, width, height, depth, rect.Pitch, height * rect.Pitch)) {
					// Now this is the source.  All conversions below support in-place.
					data = (const uint8_t *)rect.pBits;
					stride = rect.Pitch;
				}
			}

			for (int i = 0; i < height; i++) {
				uint8_t *dest = (uint8_t *)rect.pBits + rect.Pitch * i;
				const uint8_t *source = data + stride * i;
				int j;
				switch (format_) {
				case DataFormat::B4G4R4A4_UNORM_PACK16:  // We emulate support for this format.
					for (j = 0; j < width; j++) {
						uint16_t color = ((const uint16_t *)source)[j];
						((uint16_t *)dest)[j] = (color << 12) | (color >> 4);
					}
					break;
				case DataFormat::A4R4G4B4_UNORM_PACK16:
				case DataFormat::A1R5G5B5_UNORM_PACK16:
					// Native
					if (data != rect.pBits)
						memcpy(dest, source, width * sizeof(uint16_t));
					break;

				case DataFormat::R8G8B8A8_UNORM:
					for (j = 0; j < width; j++) {
						((uint32_t *)dest)[j] = Shuffle8888(((uint32_t *)source)[j]);
					}
					break;

				case DataFormat::B8G8R8A8_UNORM:
					if (data != rect.pBits)
						memcpy(dest, source, sizeof(uint32_t) * width);
					break;
				default:
					// Unhandled data format copy.
					DebugBreak();
					break;
				}
			}
			tex_->UnlockRect(level);
		}
		break;
	}

	default:
		ERROR_LOG(G3D,  "Non-LINEAR2D textures not yet supported");
		break;
	}
}

void D3D9Texture::SetToSampler(LPDIRECT3DDEVICE9 device, int sampler) {
	switch (type_) {
	case TextureType::LINEAR1D:
	case TextureType::LINEAR2D:
		device->SetTexture(sampler, tex_);
		break;

	case TextureType::LINEAR3D:
		device->SetTexture(sampler, volTex_);
		break;

	case TextureType::CUBE:
		device->SetTexture(sampler, cubeTex_);
		break;
	}
}

class D3D9Context : public DrawContext {
public:
	D3D9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx);
	~D3D9Context();

	const DeviceCaps &GetDeviceCaps() const override {
		return caps_;
	}
	uint32_t GetSupportedShaderLanguages() const override {
		return (uint32_t)ShaderLanguage::HLSL_D3D9;
	}
	uint32_t GetDataFormatSupport(DataFormat fmt) const override;

	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize, const char *tag) override;
	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	Texture *CreateTexture(const TextureDesc &desc) override;

	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override;

	void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits, const char *tag) override {
		// Not implemented
	}
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter, const char *tag) override;

	// These functions should be self explanatory.
	void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) override;
	Framebuffer *GetCurrentRenderTarget() override {
		return curRenderTarget_;
	}
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) override;
	
	uintptr_t GetFramebufferAPITexture(Framebuffer *fbo, int channelBits, int attachment) override;

	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;

	void BindTextures(int start, int count, Texture **textures) override;
	void BindSamplerStates(int start, int count, SamplerState **states) override {
		_assert_(start + count <= MAX_BOUND_TEXTURES);
		for (int i = 0; i < count; ++i) {
			D3D9SamplerState *s = static_cast<D3D9SamplerState *>(states[i]);
			if (s)
				s->Apply(device_, start + i);
		}
	}
	void BindVertexBuffers(int start, int count, Buffer **buffers, const int *offsets) override {
		_assert_(start + count <= ARRAY_SIZE(curVBuffers_));
		for (int i = 0; i < count; i++) {
			curVBuffers_[i + start] = (D3D9Buffer *)buffers[i];
			curVBufferOffsets_[i + start] = offsets ? offsets[i] : 0;
		}
	}
	void BindIndexBuffer(Buffer *indexBuffer, int offset) override {
		curIBuffer_ = (D3D9Buffer *)indexBuffer;
		curIBufferOffset_ = offset;
	}

	void BindPipeline(Pipeline *pipeline) override {
		curPipeline_ = (D3D9Pipeline *)pipeline;
	}

	void EndFrame() override;

	void UpdateDynamicUniformBuffer(const void *ub, size_t size) override;

	// Raster state
	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewports(int count, Viewport *viewports) override;
	void SetBlendFactor(float color[4]) override;
	void SetStencilParams(uint8_t refValue, uint8_t writeMask, uint8_t compareMask) override;

	void ApplyDynamicState();
	void Draw(int vertexCount, int offset) override;
	void DrawIndexed(int vertexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;
	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) override;

	uint64_t GetNativeObject(NativeObject obj, void *srcObject) override {
		switch (obj) {
		case NativeObject::CONTEXT:
			return (uint64_t)(uintptr_t)d3d_;
		case NativeObject::DEVICE:
			return (uint64_t)(uintptr_t)device_;
		case NativeObject::DEVICE_EX:
			return (uint64_t)(uintptr_t)deviceEx_;
		case NativeObject::TEXTURE_VIEW:
			return (uint64_t)(((D3D9Texture *)srcObject)->Texture());
		default:
			return 0;
		}
	}

	std::string GetInfoString(InfoField info) const override {
		switch (info) {
		case APIVERSION: return "DirectX 9.0";
		case VENDORSTRING: return identifier_.Description;
		case VENDOR: return "";
		case DRIVER: return identifier_.Driver;  // eh, sort of
		case SHADELANGVERSION: return shadeLangVersion_;
		case APINAME: return "Direct3D 9";
		default: return "?";
		}
	}

	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override;

	int GetCurrentStepId() const override {
		return stepId_;
	}

	void InvalidateCachedState() override;

private:
	LPDIRECT3D9 d3d_;
	LPDIRECT3D9EX d3dEx_;
	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9EX deviceEx_;
	int stepId_ = -1;
	int adapterId_ = -1;
	D3DADAPTER_IDENTIFIER9 identifier_{};
	D3DCAPS9 d3dCaps_;
	char shadeLangVersion_[64]{};
	DeviceCaps caps_{};

	// Bound state
	AutoRef<D3D9Pipeline> curPipeline_;
	AutoRef<D3D9Buffer> curVBuffers_[4];
	int curVBufferOffsets_[4]{};
	AutoRef<D3D9Buffer> curIBuffer_;
	int curIBufferOffset_ = 0;
	AutoRef<Framebuffer> curRenderTarget_;

	u8 stencilRefValue_ = 0;
	u8 stencilCompareMask_ = 0xFF;
	u8 stencilWriteMask_ = 0xFF;

	// Framebuffer state
	LPDIRECT3DSURFACE9 deviceRTsurf = 0;
	LPDIRECT3DSURFACE9 deviceDSsurf = 0;
	bool supportsINTZ = false;

	// Dynamic state
	uint8_t stencilRef_ = 0;
};

void D3D9Context::InvalidateCachedState() {
	curPipeline_ = nullptr;
}

#define FB_DIV 1
#define FOURCC_INTZ ((D3DFORMAT)(MAKEFOURCC('I', 'N', 'T', 'Z')))

D3D9Context::D3D9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx)
	: d3d_(d3d), d3dEx_(d3dEx), device_(device), deviceEx_(deviceEx), adapterId_(adapterId), caps_{} {
	if (FAILED(d3d->GetAdapterIdentifier(adapterId, 0, &identifier_))) {
		ERROR_LOG(G3D,  "Failed to get adapter identifier: %d", adapterId);
	}
	switch (identifier_.VendorId) {
	case 0x10DE: caps_.vendor = GPUVendor::VENDOR_NVIDIA; break;
	case 0x1002:
	case 0x1022: caps_.vendor = GPUVendor::VENDOR_AMD; break;
	case 0x163C:
	case 0x8086:
	case 0x8087: caps_.vendor = GPUVendor::VENDOR_INTEL; break;
	default:
		caps_.vendor = GPUVendor::VENDOR_UNKNOWN;
	}

	if (!FAILED(device->GetDeviceCaps(&d3dCaps_))) {
		sprintf(shadeLangVersion_, "PS: %04x VS: %04x", d3dCaps_.PixelShaderVersion & 0xFFFF, d3dCaps_.VertexShaderVersion & 0xFFFF);
	} else {
		strcpy(shadeLangVersion_, "N/A");
	}
	caps_.deviceID = identifier_.DeviceId;
	caps_.multiViewport = false;
	caps_.anisoSupported = true;
	caps_.depthRangeMinusOneToOne = false;
	caps_.preferredDepthBufferFormat = DataFormat::D24_S8;
	caps_.dualSourceBlend = false;
	caps_.tesselationShaderSupported = false;
	caps_.framebufferBlitSupported = true;
	caps_.framebufferCopySupported = false;
	caps_.framebufferDepthBlitSupported = false;
	caps_.framebufferStencilBlitSupported = false;
	caps_.framebufferDepthCopySupported = false;
	caps_.framebufferSeparateDepthCopySupported = false;
	caps_.texture3DSupported = true;
	caps_.textureNPOTFullySupported = true;
	caps_.fragmentShaderDepthWriteSupported = true;

	if (d3d) {
		D3DDISPLAYMODE displayMode;
		d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode);

		// To be safe, make sure both the display format and the FBO format support INTZ.
		HRESULT displayINTZ = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, FOURCC_INTZ);
		HRESULT fboINTZ = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, FOURCC_INTZ);
		supportsINTZ = SUCCEEDED(displayINTZ) && SUCCEEDED(fboINTZ);
	}
	caps_.textureDepthSupported = supportsINTZ;

	shaderLanguageDesc_.Init(HLSL_D3D9);

	dxstate.Restore();
}

D3D9Context::~D3D9Context() {
}

ShaderModule *D3D9Context::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t size, const char *tag) {
	D3D9ShaderModule *shader = new D3D9ShaderModule(stage, tag);
	if (shader->Compile(device_, data, size)) {
		return shader;
	} else {
		delete shader;
		return NULL;
	}
}

Pipeline *D3D9Context::CreateGraphicsPipeline(const PipelineDesc &desc, const char *tag) {
	if (!desc.shaders.size()) {
		ERROR_LOG(G3D,  "Pipeline %s requires at least one shader", tag);
		return NULL;
	}
	D3D9Pipeline *pipeline = new D3D9Pipeline();
	for (auto iter : desc.shaders) {
		if (!iter) {
			ERROR_LOG(G3D,  "NULL shader passed to CreateGraphicsPipeline(%s)", tag);
			delete pipeline;
			return NULL;
		}
		if (iter->GetStage() == ShaderStage::Fragment) {
			pipeline->pshader = static_cast<D3D9ShaderModule *>(iter);
			pipeline->pshader->AddRef();
		}
		else if (iter->GetStage() == ShaderStage::Vertex) {
			pipeline->vshader = static_cast<D3D9ShaderModule *>(iter);
			pipeline->vshader->AddRef();
		}
	}
	pipeline->prim = primToD3D9[(int)desc.prim];
	pipeline->depthStencil = (D3D9DepthStencilState *)desc.depthStencil;
	pipeline->blend = (D3D9BlendState *)desc.blend;
	pipeline->raster = (D3D9RasterState *)desc.raster;
	pipeline->inputLayout = (D3D9InputLayout *)desc.inputLayout;
	if (desc.uniformDesc)
		pipeline->dynamicUniforms = *desc.uniformDesc;
	return pipeline;
}

DepthStencilState *D3D9Context::CreateDepthStencilState(const DepthStencilStateDesc &desc) {
	D3D9DepthStencilState *ds = new D3D9DepthStencilState();
	ds->depthTestEnabled = desc.depthTestEnabled;
	ds->depthWriteEnabled = desc.depthWriteEnabled;
	ds->depthCompare = compareToD3D9[(int)desc.depthCompare];
	ds->stencilEnabled = desc.stencilEnabled;
	ds->stencilCompareOp = compareToD3D9[(int)desc.stencil.compareOp];
	ds->stencilPass = stencilOpToD3D9[(int)desc.stencil.passOp];
	ds->stencilFail = stencilOpToD3D9[(int)desc.stencil.failOp];
	ds->stencilZFail = stencilOpToD3D9[(int)desc.stencil.depthFailOp];
	return ds;
}

InputLayout *D3D9Context::CreateInputLayout(const InputLayoutDesc &desc) {
	D3D9InputLayout *fmt = new D3D9InputLayout(device_, desc);
	return fmt;
}

BlendState *D3D9Context::CreateBlendState(const BlendStateDesc &desc) {
	D3D9BlendState *bs = new D3D9BlendState();
	bs->enabled = desc.enabled;
	bs->eqCol = blendEqToD3D9[(int)desc.eqCol];
	bs->srcCol = blendFactorToD3D9[(int)desc.srcCol];
	bs->dstCol = blendFactorToD3D9[(int)desc.dstCol];
	bs->eqAlpha = blendEqToD3D9[(int)desc.eqAlpha];
	bs->srcAlpha = blendFactorToD3D9[(int)desc.srcAlpha];
	bs->dstAlpha = blendFactorToD3D9[(int)desc.dstAlpha];
	bs->colorMask = desc.colorMask;
	// Ignore logic ops, we don't support them in D3D9
	return bs;
}

SamplerState *D3D9Context::CreateSamplerState(const SamplerStateDesc &desc) {
	D3D9SamplerState *samps = new D3D9SamplerState();
	samps->wrapS = texWrapToD3D9[(int)desc.wrapU];
	samps->wrapT = texWrapToD3D9[(int)desc.wrapV];
	samps->magFilt = texFilterToD3D9[(int)desc.magFilter];
	samps->minFilt = texFilterToD3D9[(int)desc.minFilter];
	samps->mipFilt = texFilterToD3D9[(int)desc.mipFilter];
	return samps;
}

RasterState *D3D9Context::CreateRasterState(const RasterStateDesc &desc) {
	D3D9RasterState *rs = new D3D9RasterState();
	rs->cullMode = D3DCULL_NONE;
	if (desc.cull == CullMode::NONE) {
		return rs;
	}
	switch (desc.frontFace) {
	case Facing::CW:
		switch (desc.cull) {
		case CullMode::FRONT: rs->cullMode = D3DCULL_CCW; break;
		case CullMode::BACK: rs->cullMode = D3DCULL_CW; break;
		}
	case Facing::CCW:
		switch (desc.cull) {
		case CullMode::FRONT: rs->cullMode = D3DCULL_CW; break;
		case CullMode::BACK: rs->cullMode = D3DCULL_CCW; break;
		}
	}
	return rs;
}

Texture *D3D9Context::CreateTexture(const TextureDesc &desc) {
	D3D9Texture *tex = new D3D9Texture(device_, deviceEx_, desc);
	return tex;
}

void D3D9Context::BindTextures(int start, int count, Texture **textures) {
	_assert_(start + count <= MAX_BOUND_TEXTURES);
	for (int i = start; i < start + count; i++) {
		D3D9Texture *tex = static_cast<D3D9Texture *>(textures[i - start]);
		if (tex) {
			tex->SetToSampler(device_, i);
		} else {
			device_->SetTexture(i, nullptr);
		}
	}
}

void D3D9Context::EndFrame() {
	curPipeline_ = nullptr;
}

static void SemanticToD3D9UsageAndIndex(int semantic, BYTE *usage, BYTE *index) {
	*index = 0;
	switch (semantic) {
	case SEM_POSITION:
		*usage = D3DDECLUSAGE_POSITION;
		break;
	case SEM_NORMAL:
		*usage = D3DDECLUSAGE_NORMAL;
		break;
	case SEM_TANGENT:
		*usage = D3DDECLUSAGE_TANGENT;
		break;
	case SEM_BINORMAL:
		*usage = D3DDECLUSAGE_BINORMAL;
		break;
	case SEM_COLOR0:
		*usage = D3DDECLUSAGE_COLOR;
		break;
	case SEM_TEXCOORD0:
		*usage = D3DDECLUSAGE_TEXCOORD;
		break;
	case SEM_TEXCOORD1:
		*usage = D3DDECLUSAGE_TEXCOORD;
		*index = 1;
		break;
	}
}

D3D9InputLayout::D3D9InputLayout(LPDIRECT3DDEVICE9 device, const InputLayoutDesc &desc) : decl_(NULL) {
	D3DVERTEXELEMENT9 *elements = new D3DVERTEXELEMENT9[desc.attributes.size() + 1];
	size_t i;
	for (i = 0; i < desc.attributes.size(); i++) {
		elements[i].Stream = desc.attributes[i].binding;
		elements[i].Offset = desc.attributes[i].offset;
		elements[i].Method = D3DDECLMETHOD_DEFAULT;
		SemanticToD3D9UsageAndIndex(desc.attributes[i].location, &elements[i].Usage, &elements[i].UsageIndex);
		elements[i].Type = FormatToD3DDeclType(desc.attributes[i].format);
	}
	D3DVERTEXELEMENT9 end = D3DDECL_END();
	// Zero the last one.
	memcpy(&elements[i], &end, sizeof(elements[i]));

	for (i = 0; i < desc.bindings.size(); i++) {
		stride_[i] = desc.bindings[i].stride;
	}

	HRESULT hr = device->CreateVertexDeclaration(elements, &decl_);
	if (FAILED(hr)) {
		ERROR_LOG(G3D,  "Error creating vertex decl");
	}
	delete[] elements;
}

// Simulate a simple buffer type like the other backends have, use the usage flags to create the right internal type.
class D3D9Buffer : public Buffer {
public:
	D3D9Buffer(LPDIRECT3DDEVICE9 device, size_t size, uint32_t flags) : vbuffer_(nullptr), ibuffer_(nullptr), maxSize_(size) {
		if (flags & BufferUsageFlag::INDEXDATA) {
			DWORD usage = D3DUSAGE_DYNAMIC;
			device->CreateIndexBuffer((UINT)size, usage, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &ibuffer_, NULL);
		} else {
			DWORD usage = D3DUSAGE_DYNAMIC;
			device->CreateVertexBuffer((UINT)size, usage, 0, D3DPOOL_DEFAULT, &vbuffer_, NULL);
		}
	}
	virtual ~D3D9Buffer() override {
		if (ibuffer_) {
			ibuffer_->Release();
		}
		if (vbuffer_) {
			vbuffer_->Release();
		}
	}

	LPDIRECT3DVERTEXBUFFER9 vbuffer_;
	LPDIRECT3DINDEXBUFFER9 ibuffer_;
	size_t maxSize_;
};

Buffer *D3D9Context::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new D3D9Buffer(device_, size, usageFlags);
}

inline void Transpose4x4(float out[16], const float in[16]) {
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			out[i * 4 + j] = in[j * 4 + i];
		}
	}
}

void D3D9Context::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	_assert_(size == curPipeline_->dynamicUniforms.uniformBufferSize);
	for (auto &uniform : curPipeline_->dynamicUniforms.uniforms) {
		int count = 0;
		switch (uniform.type) {
		case UniformType::FLOAT1:
		case UniformType::FLOAT2:
		case UniformType::FLOAT3:
		case UniformType::FLOAT4:
			count = 1;
			break;
		case UniformType::MATRIX4X4:
			count = 4;
			break;
		}
		const float *srcPtr = (const float *)((const uint8_t *)ub + uniform.offset);
		if (uniform.vertexReg != -1) {
			float transp[16];
			if (count == 4) {
				Transpose4x4(transp, srcPtr);
				srcPtr = transp;
			}
			device_->SetVertexShaderConstantF(uniform.vertexReg, srcPtr, count);
		}
		if (uniform.fragmentReg != -1) {
			device_->SetPixelShaderConstantF(uniform.fragmentReg, srcPtr, count);
		}
	}
}

void D3D9Context::UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) {
	D3D9Buffer *buf = (D3D9Buffer *)buffer;
	if (!size)
		return;
	if (offset + size > buf->maxSize_) {
		ERROR_LOG(G3D,  "Can't SubData with bigger size than buffer was created with");
		return;
	}
	if (buf->vbuffer_) {
		void *ptr;
		HRESULT res = buf->vbuffer_->Lock((UINT)offset, (UINT)size, &ptr, (flags & UPDATE_DISCARD) ? D3DLOCK_DISCARD : 0);
		if (!FAILED(res)) {
			memcpy(ptr, data, size);
			buf->vbuffer_->Unlock();
		}
	} else if (buf->ibuffer_) {
		void *ptr;
		HRESULT res = buf->ibuffer_->Lock((UINT)offset, (UINT)size, &ptr, (flags & UPDATE_DISCARD) ? D3DLOCK_DISCARD : 0);
		if (!FAILED(res)) {
			memcpy(ptr, data, size);
			buf->ibuffer_->Unlock();
		}
	}
}

void D3D9Pipeline::Apply(LPDIRECT3DDEVICE9 device, uint8_t stencilRef, uint8_t stencilWriteMask, uint8_t stencilCompareMask) {
	vshader->Apply(device);
	pshader->Apply(device);
	blend->Apply(device);
	depthStencil->Apply(device, stencilRef, stencilWriteMask, stencilCompareMask);
	raster->Apply(device);
}

void D3D9Context::ApplyDynamicState() {
	// Apply dynamic state.
	if (curPipeline_->depthStencil->stencilEnabled) {
		device_->SetRenderState(D3DRS_STENCILREF, (DWORD)stencilRefValue_);
		device_->SetRenderState(D3DRS_STENCILWRITEMASK, (DWORD)stencilWriteMask_);
		device_->SetRenderState(D3DRS_STENCILMASK, (DWORD)stencilCompareMask_);
	}
}

static const int D3DPRIMITIVEVERTEXCOUNT[8][2] = {
	{0, 0}, // invalid
	{1, 0}, // 1 = D3DPT_POINTLIST,
	{2, 0}, // 2 = D3DPT_LINELIST,
	{2, 1}, // 3 = D3DPT_LINESTRIP,
	{3, 0}, // 4 = D3DPT_TRIANGLELIST,
	{1, 2}, // 5 = D3DPT_TRIANGLESTRIP,
	{1, 2}, // 6 = D3DPT_TRIANGLEFAN,
};

inline int D3DPrimCount(D3DPRIMITIVETYPE prim, int size) {
	return (size / D3DPRIMITIVEVERTEXCOUNT[prim][0]) - D3DPRIMITIVEVERTEXCOUNT[prim][1];
}

void D3D9Context::Draw(int vertexCount, int offset) {
	device_->SetStreamSource(0, curVBuffers_[0]->vbuffer_, curVBufferOffsets_[0], curPipeline_->inputLayout->GetStride(0));
	curPipeline_->inputLayout->Apply(device_);
	curPipeline_->Apply(device_, stencilRef_, stencilWriteMask_, stencilCompareMask_);
	ApplyDynamicState();
	device_->DrawPrimitive(curPipeline_->prim, offset, D3DPrimCount(curPipeline_->prim, vertexCount));
}

void D3D9Context::DrawIndexed(int vertexCount, int offset) {
	curPipeline_->inputLayout->Apply(device_);
	curPipeline_->Apply(device_, stencilRef_, stencilWriteMask_, stencilCompareMask_);
	ApplyDynamicState();
	device_->SetStreamSource(0, curVBuffers_[0]->vbuffer_, curVBufferOffsets_[0], curPipeline_->inputLayout->GetStride(0));
	device_->SetIndices(curIBuffer_->ibuffer_);
	device_->DrawIndexedPrimitive(curPipeline_->prim, 0, 0, vertexCount, offset, D3DPrimCount(curPipeline_->prim, vertexCount));
}

void D3D9Context::DrawUP(const void *vdata, int vertexCount) {
	curPipeline_->inputLayout->Apply(device_);
	curPipeline_->Apply(device_, stencilRef_, stencilWriteMask_, stencilCompareMask_);
	ApplyDynamicState();

	device_->DrawPrimitiveUP(curPipeline_->prim, D3DPrimCount(curPipeline_->prim, vertexCount), vdata, curPipeline_->inputLayout->GetStride(0));
}

static uint32_t SwapRB(uint32_t c) {
	return (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000);
}

void D3D9Context::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	UINT d3dMask = 0;
	if (mask & FBChannel::FB_COLOR_BIT) d3dMask |= D3DCLEAR_TARGET;
	if (mask & FBChannel::FB_DEPTH_BIT) d3dMask |= D3DCLEAR_ZBUFFER;
	if (mask & FBChannel::FB_STENCIL_BIT) d3dMask |= D3DCLEAR_STENCIL;
	if (d3dMask) {
		device_->Clear(0, NULL, d3dMask, (D3DCOLOR)SwapRB(colorval), depthVal, stencilVal);
	}
}

void D3D9Context::SetScissorRect(int left, int top, int width, int height) {
	dxstate.scissorRect.set(left, top, left + width, top + height);
	dxstate.scissorTest.set(true);
}

void D3D9Context::SetViewports(int count, Viewport *viewports) {
	int x = (int)viewports[0].TopLeftX;
	int y = (int)viewports[0].TopLeftY;
	int w = (int)viewports[0].Width;
	int h = (int)viewports[0].Height;
	dxstate.viewport.set(x, y, w, h, viewports[0].MinDepth, viewports[0].MaxDepth);
}

void D3D9Context::SetBlendFactor(float color[4]) {
	uint32_t r = (uint32_t)(color[0] * 255.0f);
	uint32_t g = (uint32_t)(color[1] * 255.0f);
	uint32_t b = (uint32_t)(color[2] * 255.0f);
	uint32_t a = (uint32_t)(color[3] * 255.0f);
	dxstate.blendColor.set(color);
}

void D3D9Context::SetStencilParams(uint8_t refValue, uint8_t writeMask, uint8_t compareMask) {
	stencilRefValue_ = refValue;
	stencilWriteMask_ = writeMask;
	stencilCompareMask_ = compareMask;
}

bool D3D9ShaderModule::Compile(LPDIRECT3DDEVICE9 device, const uint8_t *data, size_t size) {
	LPD3D_SHADER_MACRO defines = nullptr;
	LPD3DINCLUDE includes = nullptr;
	LPD3DBLOB codeBuffer = nullptr;
	LPD3DBLOB errorBuffer = nullptr;
	const char *source = (const char *)data;
	auto compile = [&](const char *profile) -> HRESULT {
		return dyn_D3DCompile(source, (UINT)strlen(source), nullptr, defines, includes, "main", profile, 0, 0, &codeBuffer, &errorBuffer);
	};
	HRESULT hr = compile(stage_ == ShaderStage::Fragment ? "ps_3_0" : "vs_3_0");
	if (FAILED(hr)) {
		const char *error = errorBuffer ? (const char *)errorBuffer->GetBufferPointer() : "(no errorbuffer returned)";
		if (hr == ERROR_MOD_NOT_FOUND) {
			// No D3D9-compatible shader compiler installed.
			error = "D3D9 shader compiler not installed";
		}

		ERROR_LOG(G3D, "Compile error: %s", error);
		ERROR_LOG(G3D, "%s", LineNumberString(std::string((const char *)data)).c_str());

		OutputDebugStringA(source);
		OutputDebugStringA(error);
		if (errorBuffer)
			errorBuffer->Release();
		if (codeBuffer) 
			codeBuffer->Release();
		return false;
	}

	bool success = false;
	if (stage_ == ShaderStage::Fragment) {
		HRESULT result = device->CreatePixelShader((DWORD *)codeBuffer->GetBufferPointer(), &pshader_);
		success = SUCCEEDED(result);
	} else {
		HRESULT result = device->CreateVertexShader((DWORD *)codeBuffer->GetBufferPointer(), &vshader_);
		success = SUCCEEDED(result);
	}

	// There could have been warnings.
	if (errorBuffer)
		errorBuffer->Release();
	codeBuffer->Release();
	return true;
}

class D3D9Framebuffer : public Framebuffer {
public:
	D3D9Framebuffer(int width, int height) {
		width_ = width;
		height_ = height;
	}
	~D3D9Framebuffer();

	uint32_t id = 0;
	LPDIRECT3DSURFACE9 surf = nullptr;
	LPDIRECT3DSURFACE9 depthstencil = nullptr;
	LPDIRECT3DTEXTURE9 tex = nullptr;
	LPDIRECT3DTEXTURE9 depthstenciltex = nullptr;
};

Framebuffer *D3D9Context::CreateFramebuffer(const FramebufferDesc &desc) {
	static uint32_t id = 0;

	D3D9Framebuffer *fbo = new D3D9Framebuffer(desc.width, desc.height);
	fbo->depthstenciltex = nullptr;

	HRESULT rtResult = device_->CreateTexture(desc.width, desc.height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &fbo->tex, NULL);
	if (FAILED(rtResult)) {
		ERROR_LOG(G3D,  "Failed to create render target");
		delete fbo;
		return NULL;
	}
	fbo->tex->GetSurfaceLevel(0, &fbo->surf);

	HRESULT dsResult;
	if (supportsINTZ) {
		dsResult = device_->CreateTexture(desc.width, desc.height, 1, D3DUSAGE_DEPTHSTENCIL, FOURCC_INTZ, D3DPOOL_DEFAULT, &fbo->depthstenciltex, NULL);
		if (SUCCEEDED(dsResult)) {
			dsResult = fbo->depthstenciltex->GetSurfaceLevel(0, &fbo->depthstencil);
		}
	} else {
		dsResult = device_->CreateDepthStencilSurface(desc.width, desc.height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->depthstencil, NULL);
	}
	if (FAILED(dsResult)) {
		ERROR_LOG(G3D,  "Failed to create depth buffer");
		fbo->surf->Release();
		fbo->tex->Release();
		if (fbo->depthstenciltex) {
			fbo->depthstenciltex->Release();
		}
		delete fbo;
		return NULL;
	}
	fbo->id = id++;
	return fbo;
}

D3D9Framebuffer::~D3D9Framebuffer() {
	tex->Release();
	surf->Release();
	depthstencil->Release();
	if (depthstenciltex) {
		depthstenciltex->Release();
	}
}

void D3D9Context::BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) {
	if (fbo) {
		D3D9Framebuffer *fb = (D3D9Framebuffer *)fbo;
		device_->SetRenderTarget(0, fb->surf);
		device_->SetDepthStencilSurface(fb->depthstencil);
		curRenderTarget_ = fb;
	} else {
		device_->SetRenderTarget(0, deviceRTsurf);
		device_->SetDepthStencilSurface(deviceDSsurf);
		curRenderTarget_ = nullptr;
	}

	int clearFlags = 0;
	if (rp.color == RPAction::CLEAR) {
		clearFlags |= D3DCLEAR_TARGET;
	}
	if (rp.depth == RPAction::CLEAR) {
		clearFlags |= D3DCLEAR_ZBUFFER;
	}
	if (rp.stencil == RPAction::CLEAR) {
		clearFlags |= D3DCLEAR_STENCIL;
	}
	if (clearFlags) {
		dxstate.scissorTest.force(false);
		device_->Clear(0, nullptr, clearFlags, (D3DCOLOR)SwapRB(rp.clearColor), rp.clearDepth, rp.clearStencil);
		dxstate.scissorRect.restore();
	}

	dxstate.scissorRect.restore();
	dxstate.viewport.restore();
	stepId_++;
}

uintptr_t D3D9Context::GetFramebufferAPITexture(Framebuffer *fbo, int channelBits, int attachment) {
	D3D9Framebuffer *fb = (D3D9Framebuffer *)fbo;
	if (channelBits & FB_SURFACE_BIT) {
		switch (channelBits & 7) {
		case FB_DEPTH_BIT:
			return (uintptr_t)fb->depthstencil;
		case FB_STENCIL_BIT:
			return (uintptr_t)fb->depthstencil;
		case FB_COLOR_BIT:
		default:
			return (uintptr_t)fb->surf;
		}
	} else {
		switch (channelBits & 7) {
		case FB_DEPTH_BIT:
			return (uintptr_t)fb->depthstenciltex;
		case FB_STENCIL_BIT:
			return 0;  // Can't texture from stencil
		case FB_COLOR_BIT:
		default:
			return (uintptr_t)fb->tex;
		}
	}
}

void D3D9Context::BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int color) {
	_assert_(binding < MAX_BOUND_TEXTURES);
	D3D9Framebuffer *fb = (D3D9Framebuffer *)fbo;
	switch (channelBit) {
	case FB_DEPTH_BIT:
		if (fb->depthstenciltex) {
			device_->SetTexture(binding, fb->depthstenciltex);
		}
		break;
	case FB_COLOR_BIT:
	default:
		if (fb->tex) {
			device_->SetTexture(binding, fb->tex);
		}
		break;
	}
}

void D3D9Context::GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) {
	D3D9Framebuffer *fb = (D3D9Framebuffer *)fbo;
	if (fb) {
		*w = fb->Width();
		*h = fb->Height();
	} else {
		*w = targetWidth_;
		*h = targetHeight_;
	}
}

bool D3D9Context::BlitFramebuffer(Framebuffer *srcfb, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dstfb, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter, const char *tag) {
	D3D9Framebuffer *src = (D3D9Framebuffer *)srcfb;
	D3D9Framebuffer *dst = (D3D9Framebuffer *)dstfb;

	LPDIRECT3DSURFACE9 srcSurf;
	LPDIRECT3DSURFACE9 dstSurf;
	RECT srcRect{ (LONG)srcX1, (LONG)srcY1, (LONG)srcX2, (LONG)srcY2 };
	RECT dstRect{ (LONG)dstX1, (LONG)dstY1, (LONG)dstX2, (LONG)dstY2 };
	if (channelBits == FB_COLOR_BIT) {
		srcSurf = src ? src->surf : deviceRTsurf;
		dstSurf = dst ? dst->surf : deviceRTsurf;
	} else if (channelBits & FB_DEPTH_BIT) {
		if (!src || !dst) {
			// Might have implications for non-buffered rendering.
			return false;
		}
		srcSurf = src->depthstencil;
		dstSurf = dst->depthstencil;
	} else {
		return false;
	}
	stepId_++;
	return SUCCEEDED(device_->StretchRect(srcSurf, &srcRect, dstSurf, &dstRect, (filter == FB_BLIT_LINEAR && channelBits == FB_COLOR_BIT) ? D3DTEXF_LINEAR : D3DTEXF_POINT));
}

void D3D9Context::HandleEvent(Event ev, int width, int height, void *param1, void *param2) {
	switch (ev) {
	case Event::LOST_BACKBUFFER:
		if (deviceRTsurf)
			deviceRTsurf->Release();
		if (deviceDSsurf)
			deviceDSsurf->Release();
		deviceRTsurf = nullptr;
		deviceDSsurf = nullptr;
		break;
	case Event::GOT_BACKBUFFER:
		device_->GetRenderTarget(0, &deviceRTsurf);
		device_->GetDepthStencilSurface(&deviceDSsurf);
		break;
	case Event::PRESENTED:
		stepId_ = 0;
		break;
	}
}

DrawContext *T3DCreateDX9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx) {
	bool result = LoadD3DCompilerDynamic();
	if (!result) {
		ERROR_LOG(G3D,  "Failed to load D3DCompiler!");
		return nullptr;
	}
	return new D3D9Context(d3d, d3dEx, adapterId, device, deviceEx);
}

// Only partial implementation!
uint32_t D3D9Context::GetDataFormatSupport(DataFormat fmt) const {
	switch (fmt) {
	case DataFormat::B8G8R8A8_UNORM:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_AUTOGEN_MIPS;

	case DataFormat::R4G4B4A4_UNORM_PACK16:
		return 0;
	case DataFormat::B4G4R4A4_UNORM_PACK16:
		return FMT_TEXTURE;  // emulated support
	case DataFormat::R5G6B5_UNORM_PACK16:
	case DataFormat::A1R5G5B5_UNORM_PACK16:
	case DataFormat::A4R4G4B4_UNORM_PACK16:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_AUTOGEN_MIPS;  // native support

	case DataFormat::R8G8B8A8_UNORM:
		return FMT_RENDERTARGET | FMT_TEXTURE | FMT_INPUTLAYOUT | FMT_AUTOGEN_MIPS;

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


}  // namespace Draw
