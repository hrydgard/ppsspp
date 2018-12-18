#include <vector>
#include <cstdio>
#include <cstdint>

#ifdef _DEBUG
#define D3D_DEBUG_INFO
#endif

#include <d3d9.h>
#ifdef USE_CRT_DBG
#undef new
#endif
#include <d3dx9.h>
#ifdef USE_CRT_DBG
#define new DBG_NEW
#endif

#include "base/logging.h"
#include "math/lin/matrix4x4.h"
#include "thin3d/thin3d.h"
#include "thin3d/d3dx9_loader.h"
#include "gfx/d3d9_state.h"

namespace Draw {

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

static const int primCountDivisor[] = {
	1,
	2,
	3,
	3,
	3,
	1,
	1,
	1,
	1,
	1,
};

D3DFORMAT FormatToD3DFMT(DataFormat fmt) {
	switch (fmt) {
	case DataFormat::R8G8B8A8_UNORM: return D3DFMT_A8R8G8B8;
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
	uint8_t stencilCompareMask;
	uint8_t stencilWriteMask;
	void Apply(LPDIRECT3DDEVICE9 device) {
		device->SetRenderState(D3DRS_ZENABLE, depthTestEnabled);
		if (depthTestEnabled) {
			device->SetRenderState(D3DRS_ZWRITEENABLE, depthWriteEnabled);
			device->SetRenderState(D3DRS_ZFUNC, depthCompare);
		}
		device->SetRenderState(D3DRS_STENCILENABLE, stencilEnabled);
		if (stencilEnabled) {
			device->SetRenderState(D3DRS_STENCILFAIL, stencilFail);
			device->SetRenderState(D3DRS_STENCILZFAIL, stencilZFail);
			device->SetRenderState(D3DRS_STENCILPASS, stencilPass);
			device->SetRenderState(D3DRS_STENCILFUNC, stencilCompareOp);
			device->SetRenderState(D3DRS_STENCILMASK, stencilCompareMask);
			device->SetRenderState(D3DRS_STENCILWRITEMASK, stencilWriteMask);
		}
	}
};

class D3D9RasterState : public RasterState {
public:
	DWORD cullMode;

	void Apply(LPDIRECT3DDEVICE9 device) {
		device->SetRenderState(D3DRS_CULLMODE, cullMode);
		device->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
	}
};

class D3D9BlendState : public BlendState {
public:
	bool enabled;
	D3DBLENDOP eqCol, eqAlpha;
	D3DBLEND srcCol, srcAlpha, dstCol, dstAlpha;
	uint32_t fixedColor;
	uint32_t colorMask;

	void Apply(LPDIRECT3DDEVICE9 device) {
		device->SetRenderState(D3DRS_ALPHABLENDENABLE, (DWORD)enabled);
		device->SetRenderState(D3DRS_BLENDOP, eqCol);
		device->SetRenderState(D3DRS_BLENDOPALPHA, eqAlpha);
		device->SetRenderState(D3DRS_SRCBLEND, srcCol);
		device->SetRenderState(D3DRS_DESTBLEND, dstCol);
		device->SetRenderState(D3DRS_SRCBLENDALPHA, srcAlpha);
		device->SetRenderState(D3DRS_DESTBLENDALPHA, dstAlpha);
		device->SetRenderState(D3DRS_COLORWRITEENABLE, colorMask);
		// device->SetRenderState(, fixedColor);
	}
};

class D3D9SamplerState : public SamplerState {
public:
	D3DTEXTUREADDRESS wrapS, wrapT;
	D3DTEXTUREFILTERTYPE magFilt, minFilt, mipFilt;

	void Apply(LPDIRECT3DDEVICE9 device, int index) {
		device->SetSamplerState(index, D3DSAMP_ADDRESSU, wrapS);
		device->SetSamplerState(index, D3DSAMP_ADDRESSV, wrapT);
		device->SetSamplerState(index, D3DSAMP_MAGFILTER, magFilt);
		device->SetSamplerState(index, D3DSAMP_MINFILTER, minFilt);
		device->SetSamplerState(index, D3DSAMP_MIPFILTER, mipFilt);
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
	D3D9ShaderModule(ShaderStage stage) : stage_(stage), vshader_(nullptr), pshader_(nullptr) {}
	~D3D9ShaderModule() {
		if (vshader_)
			vshader_->Release();
		if (pshader_)
			pshader_->Release();
	}
	bool Compile(LPDIRECT3DDEVICE9 device, const uint8_t *data, size_t size);
	void Apply(LPDIRECT3DDEVICE9 device) {
		if (stage_ == ShaderStage::FRAGMENT) {
			device->SetPixelShader(pshader_);
		} else {
			device->SetVertexShader(vshader_);
		}
	}
	ShaderStage GetStage() const override { return stage_; }

private:
	ShaderStage stage_;
	LPDIRECT3DVERTEXSHADER9 vshader_;
	LPDIRECT3DPIXELSHADER9 pshader_;
};

class D3D9Pipeline : public Pipeline {
public:
	D3D9Pipeline(LPDIRECT3DDEVICE9 device) : device_(device) {}
	~D3D9Pipeline() {
		if (depthStencil) depthStencil->Release();
		if (blend) blend->Release();
		if (raster) raster->Release();
		if (inputLayout) inputLayout->Release();
	}
	bool RequiresBuffer() override {
		return false;
	}

	D3D9ShaderModule *vshader;
	D3D9ShaderModule *pshader;

	D3DPRIMITIVETYPE prim;
	int primDivisor;
	D3D9InputLayout *inputLayout = nullptr;
	D3D9DepthStencilState *depthStencil = nullptr;
	D3D9BlendState *blend = nullptr;
	D3D9RasterState *raster = nullptr;
	UniformBufferDesc dynamicUniforms;

	void Apply(LPDIRECT3DDEVICE9 device);
private:
	LPDIRECT3DDEVICE9 device_;
};

class D3D9Texture : public Texture {
public:
	D3D9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx, const TextureDesc &desc);
	~D3D9Texture();
	void SetToSampler(LPDIRECT3DDEVICE9 device, int sampler);

private:
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data);
	bool Create(const TextureDesc &desc);
	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9EX deviceEx_;
	TextureType type_;
	DataFormat format_;
	D3DFORMAT d3dfmt_;
	LPDIRECT3DTEXTURE9 tex_;
	LPDIRECT3DVOLUMETEXTURE9 volTex_;
	LPDIRECT3DCUBETEXTURE9 cubeTex_;
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
		ELOG("Texture creation failed");
		return false;
	}

	if (desc.initData.size()) {
		// In D3D9, after setting D3DUSAGE_AUTOGENMIPS, we can only access the top layer. The rest will be
		// automatically generated.
		int maxLevel = desc.generateMips ? 1 : (int)desc.initData.size();
		for (int i = 0; i < maxLevel; i++) {
			SetImageData(0, 0, 0, width_, height_, depth_, i, 0, desc.initData[i]);
		}
	}
	return true;
}

// Just switches R and G.
inline uint32_t Shuffle8888(uint32_t x) {
	return (x & 0xFF00FF00) | ((x >> 16) & 0xFF) | ((x << 16) & 0xFF0000);
}

void D3D9Texture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
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
					// Native
					memcpy(dest, source, width * sizeof(uint16_t));
					break;

				case DataFormat::R8G8B8A8_UNORM:
					for (j = 0; j < width; j++) {
						((uint32_t *)dest)[j] = Shuffle8888(((uint32_t *)source)[j]);
					}
					break;

				case DataFormat::B8G8R8A8_UNORM:
					memcpy(dest, source, sizeof(uint32_t) * width);
					break;
				}
			}
			tex_->UnlockRect(level);
		}
		break;
	}

	default:
		ELOG("Non-LINEAR2D textures not yet supported");
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

	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize) override;
	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	Texture *CreateTexture(const TextureDesc &desc) override;

	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override;

	void UpdateBuffer(Buffer *buffer, const uint8_t *data, size_t offset, size_t size, UpdateBufferFlags flags) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits) override {}
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter) override;

	// These functions should be self explanatory.
	void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp) override;
	// color must be 0, for now.
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) override;
	
	uintptr_t GetFramebufferAPITexture(Framebuffer *fbo, int channelBits, int attachment) override;

	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;

	void BindTextures(int start, int count, Texture **textures) override;
	void BindSamplerStates(int start, int count, SamplerState **states) override {
		for (int i = 0; i < count; ++i) {
			D3D9SamplerState *s = static_cast<D3D9SamplerState *>(states[start + i]);
			s->Apply(device_, start + i);
		}
	}
	void BindVertexBuffers(int start, int count, Buffer **buffers, int *offsets) override {
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

	void UpdateDynamicUniformBuffer(const void *ub, size_t size) override;

	// Raster state
	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewports(int count, Viewport *viewports) override;
	void SetBlendFactor(float color[4]) override;
	void SetStencilRef(uint8_t ref) override;

	void Draw(int vertexCount, int offset) override;
	void DrawIndexed(int vertexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;
	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal);

	uintptr_t GetNativeObject(NativeObject obj) override {
		switch (obj) {
		case NativeObject::CONTEXT:
			return (uintptr_t)d3d_;
		case NativeObject::DEVICE:
			return (uintptr_t)device_;
		case NativeObject::DEVICE_EX:
			return (uintptr_t)deviceEx_;
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

private:
	LPDIRECT3D9 d3d_;
	LPDIRECT3D9EX d3dEx_;
	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9EX deviceEx_;
	int adapterId_ = -1;
	D3DADAPTER_IDENTIFIER9 identifier_{};
	D3DCAPS9 d3dCaps_;
	char shadeLangVersion_[64]{};
	DeviceCaps caps_{};

	// Bound state
	D3D9Pipeline *curPipeline_ = nullptr;
	D3D9Buffer *curVBuffers_[4]{};
	int curVBufferOffsets_[4]{};
	D3D9Buffer *curIBuffer_ = nullptr;
	int curIBufferOffset_ = 0;

	// Framebuffer state
	LPDIRECT3DSURFACE9 deviceRTsurf = 0;
	LPDIRECT3DSURFACE9 deviceDSsurf = 0;
	bool supportsINTZ = false;
};

#define FB_DIV 1
#define FOURCC_INTZ ((D3DFORMAT)(MAKEFOURCC('I', 'N', 'T', 'Z')))

D3D9Context::D3D9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx)
	: d3d_(d3d), d3dEx_(d3dEx), adapterId_(adapterId), device_(device), deviceEx_(deviceEx), caps_{} {
	if (FAILED(d3d->GetAdapterIdentifier(adapterId, 0, &identifier_))) {
		ELOG("Failed to get adapter identifier: %d", adapterId);
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

	caps_.multiViewport = false;
	caps_.anisoSupported = true;
	caps_.depthRangeMinusOneToOne = false;
	caps_.preferredDepthBufferFormat = DataFormat::D24_S8;
	caps_.dualSourceBlend = false;
	caps_.tesselationShaderSupported = false;
	caps_.framebufferBlitSupported = true;
	caps_.framebufferCopySupported = false;
	caps_.framebufferDepthBlitSupported = true;
	caps_.framebufferDepthCopySupported = false;
	if (d3d) {
		D3DDISPLAYMODE displayMode;
		d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &displayMode);

		// To be safe, make sure both the display format and the FBO format support INTZ.
		HRESULT displayINTZ = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, displayMode.Format, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, FOURCC_INTZ);
		HRESULT fboINTZ = d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, FOURCC_INTZ);
		supportsINTZ = SUCCEEDED(displayINTZ) && SUCCEEDED(fboINTZ);
	}
}

D3D9Context::~D3D9Context() {
}

ShaderModule *D3D9Context::CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t size) {
	D3D9ShaderModule *shader = new D3D9ShaderModule(stage);
	if (shader->Compile(device_, data, size)) {
		return shader;
	} else {
		delete shader;
		return NULL;
	}
}

Pipeline *D3D9Context::CreateGraphicsPipeline(const PipelineDesc &desc) {
	if (!desc.shaders.size()) {
		ELOG("Pipeline requires at least one shader");
		return NULL;
	}
	D3D9Pipeline *pipeline = new D3D9Pipeline(device_);
	for (auto iter : desc.shaders) {
		if (!iter) {
			ELOG("NULL shader passed to CreateGraphicsPipeline");
			delete pipeline;
			return NULL;
		}
		if (iter->GetStage() == ShaderStage::FRAGMENT) {
			pipeline->pshader = static_cast<D3D9ShaderModule *>(iter);
		}
		else if (iter->GetStage() == ShaderStage::VERTEX) {
			pipeline->vshader = static_cast<D3D9ShaderModule *>(iter);
		}
	}
	pipeline->prim = primToD3D9[(int)desc.prim];
	pipeline->primDivisor = primCountDivisor[(int)desc.prim];
	pipeline->depthStencil = (D3D9DepthStencilState *)desc.depthStencil;
	pipeline->blend = (D3D9BlendState *)desc.blend;
	pipeline->raster = (D3D9RasterState *)desc.raster;
	pipeline->inputLayout = (D3D9InputLayout *)desc.inputLayout;
	pipeline->depthStencil->AddRef();
	pipeline->blend->AddRef();
	pipeline->raster->AddRef();
	pipeline->inputLayout->AddRef();
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
	ds->stencilCompareOp = compareToD3D9[(int)desc.front.compareOp];
	ds->stencilPass = stencilOpToD3D9[(int)desc.front.passOp];
	ds->stencilFail = stencilOpToD3D9[(int)desc.front.failOp];
	ds->stencilZFail = stencilOpToD3D9[(int)desc.front.depthFailOp];
	ds->stencilWriteMask = desc.front.writeMask;
	ds->stencilCompareMask = desc.front.compareMask;
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
	for (int i = start; i < start + count; i++) {
		D3D9Texture *tex = static_cast<D3D9Texture *>(textures[i - start]);
		if (tex) {
			tex->SetToSampler(device_, i);
		} else {
			device_->SetTexture(i, nullptr);
		}
	}
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
		ELOG("Error creating vertex decl");
	}
	delete[] elements;
}

// Simulate a simple buffer type like the other backends have, use the usage flags to create the right internal type.
class D3D9Buffer : public Buffer {
public:
	D3D9Buffer(LPDIRECT3DDEVICE9 device, size_t size, uint32_t flags) : vbuffer_(nullptr), ibuffer_(nullptr), maxSize_(size) {
		if (flags & BufferUsageFlag::INDEXDATA) {
			DWORD usage = D3DUSAGE_DYNAMIC;
			device->CreateIndexBuffer((UINT)size, usage, D3DFMT_INDEX32, D3DPOOL_DEFAULT, &ibuffer_, NULL);
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
	if (size != curPipeline_->dynamicUniforms.uniformBufferSize)
		Crash();
	for (auto &uniform : curPipeline_->dynamicUniforms.uniforms) {
		int count = 0;
		switch (uniform.type) {
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
			Transpose4x4(transp, srcPtr);
			device_->SetVertexShaderConstantF(uniform.vertexReg, transp, count);
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
		ELOG("Can't SubData with bigger size than buffer was created with");
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

void D3D9Pipeline::Apply(LPDIRECT3DDEVICE9 device) {
	vshader->Apply(device);
	pshader->Apply(device);
	blend->Apply(device);
	depthStencil->Apply(device);
	raster->Apply(device);
}

void D3D9Context::Draw(int vertexCount, int offset) {
	device_->SetStreamSource(0, curVBuffers_[0]->vbuffer_, curVBufferOffsets_[0], curPipeline_->inputLayout->GetStride(0));
	curPipeline_->Apply(device_);
	curPipeline_->inputLayout->Apply(device_);
	device_->DrawPrimitive(curPipeline_->prim, offset, vertexCount / 3);
}

void D3D9Context::DrawIndexed(int vertexCount, int offset) {
	D3D9Buffer *vbuf = static_cast<D3D9Buffer *>(curVBuffers_[0]);
	D3D9Buffer *ibuf = static_cast<D3D9Buffer *>(curIBuffer_);

	curPipeline_->Apply(device_);
	curPipeline_->inputLayout->Apply(device_);
	device_->SetStreamSource(0, curVBuffers_[0]->vbuffer_, curVBufferOffsets_[0], curPipeline_->inputLayout->GetStride(0));
	device_->SetIndices(curIBuffer_->ibuffer_);
	device_->DrawIndexedPrimitive(curPipeline_->prim, 0, 0, vertexCount, 0, vertexCount / curPipeline_->primDivisor);
}

void D3D9Context::DrawUP(const void *vdata, int vertexCount) {
	curPipeline_->Apply(device_);
	curPipeline_->inputLayout->Apply(device_);
	device_->DrawPrimitiveUP(curPipeline_->prim, vertexCount / 3, vdata, curPipeline_->inputLayout->GetStride(0));
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
	using namespace DX9;

	dxstate.scissorRect.set(left, top, left + width, top + height);
}

void D3D9Context::SetViewports(int count, Viewport *viewports) {
	using namespace DX9;

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
	device_->SetRenderState(D3DRS_BLENDFACTOR, r | (g << 8) | (b << 16) | (a << 24));
}

void D3D9Context::SetStencilRef(uint8_t ref) {
	device_->SetRenderState(D3DRS_STENCILREF, (DWORD)ref);
}

bool D3D9ShaderModule::Compile(LPDIRECT3DDEVICE9 device, const uint8_t *data, size_t size) {
	LPD3DXMACRO defines = nullptr;
	LPD3DXINCLUDE includes = nullptr;
	DWORD flags = 0;
	LPD3DXBUFFER codeBuffer = nullptr;
	LPD3DXBUFFER errorBuffer = nullptr;
	const char *source = (const char *)data;
	const char *profile = stage_ == ShaderStage::FRAGMENT ? "ps_2_0" : "vs_2_0";
	HRESULT hr = dyn_D3DXCompileShader(source, (UINT)strlen(source), defines, includes, "main", profile, flags, &codeBuffer, &errorBuffer, nullptr);
	if (FAILED(hr)) {
		const char *error = errorBuffer ? (const char *)errorBuffer->GetBufferPointer() : "(no errorbuffer returned)";
		if (hr == ERROR_MOD_NOT_FOUND) {
			// No D3D9-compatible shader compiler installed.
			error = "D3D9 shader compiler not installed";
		}
		OutputDebugStringA(source);
		OutputDebugStringA(error);
		if (errorBuffer)
			errorBuffer->Release();
		if (codeBuffer) 
			codeBuffer->Release();
		return false;
	}

	bool success = false;
	if (stage_ == ShaderStage::FRAGMENT) {
		HRESULT result = device->CreatePixelShader((DWORD *)codeBuffer->GetBufferPointer(), &pshader_);
		success = SUCCEEDED(result);
	} else {
		HRESULT result = device->CreateVertexShader((DWORD *)codeBuffer->GetBufferPointer(), &vshader_);
		success = SUCCEEDED(result);
	}

	codeBuffer->Release();
	return true;
}

class D3D9Framebuffer : public Framebuffer {
public:
	~D3D9Framebuffer();
	uint32_t id;
	LPDIRECT3DSURFACE9 surf;
	LPDIRECT3DSURFACE9 depthstencil;
	LPDIRECT3DTEXTURE9 tex;
	LPDIRECT3DTEXTURE9 depthstenciltex;

	int width;
	int height;
	FBColorDepth colorDepth;
};

Framebuffer *D3D9Context::CreateFramebuffer(const FramebufferDesc &desc) {
	static uint32_t id = 0;

	D3D9Framebuffer *fbo = new D3D9Framebuffer{};
	fbo->width = desc.width;
	fbo->height = desc.height;
	fbo->colorDepth = desc.colorDepth;
	fbo->depthstenciltex = nullptr;

	HRESULT rtResult = device_->CreateTexture(fbo->width, fbo->height, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &fbo->tex, NULL);
	if (FAILED(rtResult)) {
		ELOG("Failed to create render target");
		delete fbo;
		return NULL;
	}
	fbo->tex->GetSurfaceLevel(0, &fbo->surf);

	HRESULT dsResult;
	if (supportsINTZ) {
		dsResult = device_->CreateTexture(fbo->width, fbo->height, 1, D3DUSAGE_DEPTHSTENCIL, FOURCC_INTZ, D3DPOOL_DEFAULT, &fbo->depthstenciltex, NULL);
		if (SUCCEEDED(dsResult)) {
			dsResult = fbo->depthstenciltex->GetSurfaceLevel(0, &fbo->depthstencil);
		}
	} else {
		dsResult = device_->CreateDepthStencilSurface(fbo->width, fbo->height, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &fbo->depthstencil, NULL);
	}
	if (FAILED(dsResult)) {
		ELOG("Failed to create depth buffer");
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

void D3D9Context::BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp) {
	using namespace DX9;
	if (fbo) {
		D3D9Framebuffer *fb = (D3D9Framebuffer *)fbo;
		device_->SetRenderTarget(0, fb->surf);
		device_->SetDepthStencilSurface(fb->depthstencil);
	} else {
		device_->SetRenderTarget(0, deviceRTsurf);
		device_->SetDepthStencilSurface(deviceDSsurf);
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

LPDIRECT3DSURFACE9 fbo_get_color_for_read(D3D9Framebuffer *fbo) {
	return fbo->surf;
}

void D3D9Context::BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int color) {
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
		*w = fb->width;
		*h = fb->height;
	} else {
		*w = targetWidth_;
		*h = targetHeight_;
	}
}

bool D3D9Context::BlitFramebuffer(Framebuffer *srcfb, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dstfb, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter) {
	D3D9Framebuffer *src = (D3D9Framebuffer *)srcfb;
	D3D9Framebuffer *dst = (D3D9Framebuffer *)dstfb;
	if (channelBits != FB_COLOR_BIT)
		return false;
	RECT srcRect{ (LONG)srcX1, (LONG)srcY1, (LONG)srcX2, (LONG)srcY2 };
	RECT dstRect{ (LONG)dstX1, (LONG)dstY1, (LONG)dstX2, (LONG)dstY2 };
	LPDIRECT3DSURFACE9 srcSurf = src ? src->surf : deviceRTsurf;
	LPDIRECT3DSURFACE9 dstSurf = dst ? dst->surf : deviceRTsurf;
	return SUCCEEDED(device_->StretchRect(srcSurf, &srcRect, dstSurf, &dstRect, filter == FB_BLIT_LINEAR ? D3DTEXF_LINEAR : D3DTEXF_POINT));
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
	}
}

DrawContext *T3DCreateDX9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx) {
	int d3dx_ver = LoadD3DX9Dynamic();
	if (!d3dx_ver) {
		ELOG("Failed to load D3DX9!");
		return NULL;
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
