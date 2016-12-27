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
	uint8_t stencilReference;
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
			device->SetRenderState(D3DRS_STENCILREF, stencilReference);
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

class Thin3DDX9Buffer : public Buffer {
public:
	Thin3DDX9Buffer(LPDIRECT3DDEVICE9 device, size_t size, uint32_t flags) : vbuffer_(nullptr), ibuffer_(nullptr), maxSize_(size) {
		if (flags & BufferUsageFlag::INDEXDATA) {
			DWORD usage = D3DUSAGE_DYNAMIC;
			device->CreateIndexBuffer((UINT)size, usage, D3DFMT_INDEX32, D3DPOOL_DEFAULT, &ibuffer_, NULL);
		} else {
			DWORD usage = D3DUSAGE_DYNAMIC;
			device->CreateVertexBuffer((UINT)size, usage, 0, D3DPOOL_DEFAULT, &vbuffer_, NULL);
		}
	}
	virtual ~Thin3DDX9Buffer() override {
		if (ibuffer_) {
			ibuffer_->Release();
		}
		if (vbuffer_) {
			vbuffer_->Release();
		}
	}

	void SetData(const uint8_t *data, size_t size) override {
		if (!size)
			return;
		if (size > maxSize_) {
			ELOG("Can't SetData with bigger size than buffer was created with on D3D");
			return;
		}
		if (vbuffer_) {
			void *ptr;
			vbuffer_->Lock(0, (UINT)size, &ptr, D3DLOCK_DISCARD);
			memcpy(ptr, data, size);
			vbuffer_->Unlock();
		} else if (ibuffer_) {
			void *ptr;
			ibuffer_->Lock(0, (UINT)size, &ptr, D3DLOCK_DISCARD);
			memcpy(ptr, data, size);
			ibuffer_->Unlock();
		}
	}
	void SubData(const uint8_t *data, size_t offset, size_t size) override {
		if (!size)
			return;
		if (offset + size > maxSize_) {
			ELOG("Can't SubData with bigger size than buffer was created with on D3D");
			return;
		}
		if (vbuffer_) {
			void *ptr;
			HRESULT res = vbuffer_->Lock((UINT)offset, (UINT)size, &ptr, D3DLOCK_DISCARD);
			if (!FAILED(res)) {
				memcpy(ptr, data, size);
				vbuffer_->Unlock();
			}
		} else if (ibuffer_) {
			void *ptr;
			HRESULT res = ibuffer_->Lock((UINT)offset, (UINT)size, &ptr, D3DLOCK_DISCARD);
			if (!FAILED(res)) {
				memcpy(ptr, data, size);
				ibuffer_->Unlock();
			}
		}
	}
	void BindAsVertexBuf(LPDIRECT3DDEVICE9 device, int vertexSize, int offset = 0) {
		if (vbuffer_)
			device->SetStreamSource(0, vbuffer_, offset, vertexSize);
	}
	void BindAsIndexBuf(LPDIRECT3DDEVICE9 device) {
		if (ibuffer_)
			device->SetIndices(ibuffer_);
	}

private:
	LPDIRECT3DVERTEXBUFFER9 vbuffer_;
	LPDIRECT3DINDEXBUFFER9 ibuffer_;
	size_t maxSize_;
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
	D3D9ShaderModule(ShaderStage stage) : stage_(stage), vshader_(NULL), pshader_(NULL), constantTable_(NULL) {}
	~D3D9ShaderModule() {
		if (vshader_)
			vshader_->Release();
		if (pshader_)
			pshader_->Release();
		if (constantTable_)
			constantTable_->Release();
	}
	bool Compile(LPDIRECT3DDEVICE9 device, const uint8_t *data, size_t size);
	void Apply(LPDIRECT3DDEVICE9 device) {
		if (stage_ == ShaderStage::FRAGMENT) {
			device->SetPixelShader(pshader_);
		} else {
			device->SetVertexShader(vshader_);
		}
	}
	void SetVector(LPDIRECT3DDEVICE9 device, const char *name, float *value, int n);
	void SetMatrix4x4(LPDIRECT3DDEVICE9 device, const char *name, const float value[16]);
	ShaderStage GetStage() const override { return stage_; }

private:
	ShaderStage stage_;
	LPDIRECT3DVERTEXSHADER9 vshader_;
	LPDIRECT3DPIXELSHADER9 pshader_;
	LPD3DXCONSTANTTABLE constantTable_;
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

	void Apply(LPDIRECT3DDEVICE9 device);
	void SetVector(const char *name, float *value, int n) { vshader->SetVector(device_, name, value, n); pshader->SetVector(device_, name, value, n); }
	void SetMatrix4x4(const char *name, const float value[16]) { vshader->SetMatrix4x4(device_, name, value); }  // pshaders don't usually have matrices
private:
	LPDIRECT3DDEVICE9 device_;
};

class D3D9Texture : public Texture {
public:
	D3D9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx) : device_(device), deviceEx_(deviceEx), type_(TextureType::UNKNOWN), fmt_(D3DFMT_UNKNOWN), tex_(NULL), volTex_(NULL), cubeTex_(NULL) {
	}
	D3D9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx, TextureType type, DataFormat format, int width, int height, int depth, int mipLevels);
	~D3D9Texture();
	bool Create(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override;
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) override;
	void AutoGenMipmaps() override {}
	void SetToSampler(LPDIRECT3DDEVICE9 device, int sampler);
	void Finalize() override {}

private:
	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9EX deviceEx_;
	TextureType type_;
	D3DFORMAT fmt_;
	LPDIRECT3DTEXTURE9 tex_;
	LPDIRECT3DVOLUMETEXTURE9 volTex_;
	LPDIRECT3DCUBETEXTURE9 cubeTex_;
};

D3DFORMAT FormatToD3D(DataFormat fmt) {
	switch (fmt) {
	case DataFormat::R8G8B8A8_UNORM: return D3DFMT_A8R8G8B8;
	case DataFormat::R4G4B4A4_UNORM: return D3DFMT_A4R4G4B4;
	case DataFormat::D24_S8: return D3DFMT_D24S8;
	case DataFormat::D16: return D3DFMT_D16;
	default: return D3DFMT_UNKNOWN;
	}
}

D3D9Texture::D3D9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx, TextureType type, DataFormat format, int width, int height, int depth, int mipLevels)
	: device_(device), deviceEx_(deviceEx), type_(type), tex_(NULL), volTex_(NULL), cubeTex_(NULL) {
	Create(type, format, width, height, depth, mipLevels);
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

bool D3D9Texture::Create(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) {
	width_ = width;
	height_ = height;
	depth_ = depth;
	type_ = type;
	tex_ = NULL;
	fmt_ = FormatToD3D(format);
	HRESULT hr = E_FAIL;

	D3DPOOL pool = D3DPOOL_MANAGED;
	int usage = 0;
	if (deviceEx_ != nullptr) {
		pool = D3DPOOL_DEFAULT;
		usage = D3DUSAGE_DYNAMIC;
	}
	switch (type) {
	case LINEAR1D:
	case LINEAR2D:
		hr = device_->CreateTexture(width, height, mipLevels, usage, fmt_, pool, &tex_, NULL);
		break;
	case LINEAR3D:
		hr = device_->CreateVolumeTexture(width, height, depth, mipLevels, usage, fmt_, pool, &volTex_, NULL);
		break;
	case CUBE:
		hr = device_->CreateCubeTexture(width, mipLevels, usage, fmt_, pool, &cubeTex_, NULL);
		break;
	}
	if (FAILED(hr)) {
		ELOG("Texture creation failed");
		return false;
	}
	return true;
}

inline uint16_t Shuffle4444(uint16_t x) {
	return (x << 12) | (x >> 4);
}

// Just switches R and G.
inline uint32_t Shuffle8888(uint32_t x) {
	return (x & 0xFF00FF00) | ((x >> 16) & 0xFF) | ((x << 16) & 0xFF0000);
}


void D3D9Texture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
	if (!tex_) {
		return;
	}
	if (level == 0) {
		width_ = width;
		height_ = height;
		depth_ = depth;
	}

	switch (type_) {
	case LINEAR2D:
	{
		D3DLOCKED_RECT rect;
		if (x == 0 && y == 0) {
			tex_->LockRect(level, &rect, NULL, 0);

			for (int i = 0; i < height; i++) {
				uint8_t *dest = (uint8_t *)rect.pBits + rect.Pitch * i;
				const uint8_t *source = data + stride * i;
				int j;
				switch (fmt_) {
				case D3DFMT_A4R4G4B4:
					// This is backwards from OpenGL so we need to shuffle around a bit.
					for (j = 0; j < width; j++) {
						((uint16_t *)dest)[j] = Shuffle4444(((uint16_t *)source)[j]);
					}
					break;

				case D3DFMT_A8R8G8B8:
					for (j = 0; j < width; j++) {
						((uint32_t *)dest)[j] = Shuffle8888(((uint32_t *)source)[j]);
					}
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
	case LINEAR1D:
	case LINEAR2D:
		device->SetTexture(sampler, tex_);
		break;

	case LINEAR3D:
		device->SetTexture(sampler, volTex_);
		break;

	case CUBE:
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
		return (uint32_t)ShaderLanguage::HLSL_D3D9 | (uint32_t)ShaderLanguage::HLSL_D3D9_BYTECODE;
	}

	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const uint8_t *data, size_t dataSize) override;
	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override;
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const RasterStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override;
	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override;
	Texture *CreateTexture() override;
	Texture *CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override;

	void BindTextures(int start, int count, Texture **textures) override;
	void BindSamplerStates(int start, int count, SamplerState **states) override {
		for (int i = 0; i < count; ++i) {
			D3D9SamplerState *s = static_cast<D3D9SamplerState *>(states[start + i]);
			s->Apply(device_, start + i);
		}
	}
	void BindPipeline(Pipeline *pipeline) {
		curPipeline_ = (D3D9Pipeline *)pipeline;
	}

	// Raster state
	void SetScissorRect(int left, int top, int width, int height) override;
	void SetViewports(int count, Viewport *viewports) override;
	void SetBlendFactor(float color[4]) override;

	void Draw(Buffer *vdata, int vertexCount, int offset) override;
	void DrawIndexed(Buffer *vdata, Buffer *idata, int vertexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;
	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal);

	std::string GetInfoString(InfoField info) const override {
		switch (info) {
		case APIVERSION: return "DirectX 9.0";
		case VENDORSTRING: return identifier_.Description;
		case VENDOR: return "-";
		case RENDERER: return identifier_.Driver;  // eh, sort of
		case SHADELANGVERSION: return shadeLangVersion_;
		case APINAME: return "Direct3D 9";
		default: return "?";
		}
	}

private:
	LPDIRECT3D9 d3d_;
	LPDIRECT3D9EX d3dEx_;
	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9EX deviceEx_;
	int adapterId_;
	D3DADAPTER_IDENTIFIER9 identifier_;
	D3DCAPS9 d3dCaps_;
	char shadeLangVersion_[64];
	D3D9Pipeline *curPipeline_;
	DeviceCaps caps_;
};

D3D9Context::D3D9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx)
	: d3d_(d3d), d3dEx_(d3dEx), adapterId_(adapterId), device_(device), deviceEx_(deviceEx), caps_{} {
	CreatePresets();
	d3d->GetAdapterIdentifier(adapterId, 0, &identifier_);
	if (!FAILED(device->GetDeviceCaps(&d3dCaps_))) {
		sprintf(shadeLangVersion_, "PS: %04x VS: %04x", d3dCaps_.PixelShaderVersion & 0xFFFF, d3dCaps_.VertexShaderVersion & 0xFFFF);
	} else {
		strcpy(shadeLangVersion_, "N/A");
	}
	caps_.multiViewport = false;
	caps_.anisoSupported = true;
	caps_.depthRangeMinusOneToOne = false;
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
	ds->stencilReference = desc.front.reference;
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

Texture *D3D9Context::CreateTexture() {
	D3D9Texture *tex = new D3D9Texture(device_, deviceEx_);
	return tex;
}

Texture *D3D9Context::CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) {
	D3D9Texture *tex = new D3D9Texture(device_, deviceEx_, type, format, width, height, depth, mipLevels);
	return tex;
}

void D3D9Context::BindTextures(int start, int count, Texture **textures) {
	for (int i = start; i < start + count; i++) {
		D3D9Texture *tex = static_cast<D3D9Texture *>(textures[i - start]);
		tex->SetToSampler(device_, i);
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

static int VertexDataTypeToD3DType(DataFormat type) {
	switch (type) {
	case DataFormat::R32G32_FLOAT: return D3DDECLTYPE_FLOAT2;
	case DataFormat::R32G32B32_FLOAT: return D3DDECLTYPE_FLOAT3;
	case DataFormat::R32G32B32A32_FLOAT: return D3DDECLTYPE_FLOAT4;
	case DataFormat::R8G8B8A8_UNORM: return D3DDECLTYPE_UBYTE4N;  // D3DCOLOR?
	default: return D3DDECLTYPE_UNUSED;
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
		elements[i].Type = VertexDataTypeToD3DType(desc.attributes[i].format);
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

Buffer *D3D9Context::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new Thin3DDX9Buffer(device_, size, usageFlags);
}

void D3D9Pipeline::Apply(LPDIRECT3DDEVICE9 device) {
	vshader->Apply(device);
	pshader->Apply(device);
	blend->Apply(device);
	depthStencil->Apply(device);
	raster->Apply(device);
}

void D3D9Context::Draw(Buffer *vdata, int vertexCount, int offset) {
	Thin3DDX9Buffer *vbuf = static_cast<Thin3DDX9Buffer *>(vdata);

	vbuf->BindAsVertexBuf(device_, curPipeline_->inputLayout->GetStride(9), offset);
	curPipeline_->Apply(device_);
	curPipeline_->inputLayout->Apply(device_);
	device_->DrawPrimitive(curPipeline_->prim, offset, vertexCount / 3);
}

void D3D9Context::DrawIndexed(Buffer *vdata, Buffer *idata, int vertexCount, int offset) {
	Thin3DDX9Buffer *vbuf = static_cast<Thin3DDX9Buffer *>(vdata);
	Thin3DDX9Buffer *ibuf = static_cast<Thin3DDX9Buffer *>(idata);

	curPipeline_->Apply(device_);
	curPipeline_->inputLayout->Apply(device_);
	vbuf->BindAsVertexBuf(device_, curPipeline_->inputLayout->GetStride(0), offset);
	ibuf->BindAsIndexBuf(device_);
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
	if (mask & ClearFlag::COLOR) d3dMask |= D3DCLEAR_TARGET;
	if (mask & ClearFlag::DEPTH) d3dMask |= D3DCLEAR_ZBUFFER;
	if (mask & ClearFlag::STENCIL) d3dMask |= D3DCLEAR_STENCIL;

	device_->Clear(0, NULL, d3dMask, (D3DCOLOR)SwapRB(colorval), depthVal, stencilVal);
}

void D3D9Context::SetScissorRect(int left, int top, int width, int height) {
	RECT rc;
	rc.left = left;
	rc.top = top;
	rc.right = left + width;
	rc.bottom = top + height;
	device_->SetScissorRect(&rc);
}

void D3D9Context::SetViewports(int count, Viewport *viewports) {
	D3DVIEWPORT9 vp;
	vp.X = (DWORD)viewports[0].TopLeftX;
	vp.Y = (DWORD)viewports[0].TopLeftY;
	vp.Width = (DWORD)viewports[0].Width;
	vp.Height = (DWORD)viewports[0].Height;
	vp.MinZ = viewports[0].MinDepth;
	vp.MaxZ = viewports[0].MaxDepth;
	device_->SetViewport(&vp);
}

void D3D9Context::SetBlendFactor(float color[4]) {
	uint32_t r = (uint32_t)(color[0] * 255.0f);
	uint32_t g = (uint32_t)(color[1] * 255.0f);
	uint32_t b = (uint32_t)(color[2] * 255.0f);
	uint32_t a = (uint32_t)(color[3] * 255.0f);
	device_->SetRenderState(D3DRS_BLENDFACTOR, r | (g << 8) | (b << 16) | (a << 24));
}

bool D3D9ShaderModule::Compile(LPDIRECT3DDEVICE9 device, const uint8_t *data, size_t size) {
	LPD3DXMACRO defines = NULL;
	LPD3DXINCLUDE includes = NULL;
	DWORD flags = 0;
	LPD3DXBUFFER codeBuffer;
	LPD3DXBUFFER errorBuffer;
	const char *source = (const char *)data;
	const char *profile = stage_ == ShaderStage::FRAGMENT ? "ps_2_0" : "vs_2_0";
	HRESULT hr = dyn_D3DXCompileShader(source, (UINT)strlen(source), defines, includes, "main", profile, flags, &codeBuffer, &errorBuffer, &constantTable_);
	if (FAILED(hr)) {
		const char *error = (const char *)errorBuffer->GetBufferPointer();
		OutputDebugStringA(source);
		OutputDebugStringA(error);
		errorBuffer->Release();

		if (codeBuffer) 
			codeBuffer->Release();
		if (constantTable_) 
			constantTable_->Release();
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

#if 0
	// Just for testing. Will later use to pre populate uniform tables.

	D3DXCONSTANTTABLE_DESC desc;
	constantTable_->GetDesc(&desc);

	for (UINT i = 0; i < desc.Constants; i++) {
		D3DXHANDLE c = constantTable_->GetConstant(NULL, i);
		D3DXCONSTANT_DESC cdesc;
		UINT count = 1;
		constantTable_->GetConstantDesc(c, &cdesc, &count);
		ILOG("%s", cdesc.Name);
	}
#endif

	codeBuffer->Release();
	return true;
}

void D3D9ShaderModule::SetVector(LPDIRECT3DDEVICE9 device, const char *name, float *value, int n) {
	D3DXHANDLE handle = constantTable_->GetConstantByName(NULL, name);
	if (handle) {
		constantTable_->SetFloatArray(device, handle, value, n);
	}
}

void D3D9ShaderModule::SetMatrix4x4(LPDIRECT3DDEVICE9 device, const char *name, const float value[16]) {
	D3DXHANDLE handle = constantTable_->GetConstantByName(NULL, name);
	if (handle) {
		constantTable_->SetFloatArray(device, handle, value, 16);
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

}  // namespace Draw