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
	D3DBLEND_SRCALPHA,
	D3DBLEND_INVSRCCOLOR,
	D3DBLEND_INVSRCALPHA,
	D3DBLEND_DESTCOLOR,
	D3DBLEND_DESTALPHA,
	D3DBLEND_INVDESTCOLOR,
	D3DBLEND_INVDESTALPHA,
	D3DBLEND_BLENDFACTOR,
};

static const D3DTEXTUREADDRESS texWrapToD3D9[] = {
	D3DTADDRESS_WRAP,
	D3DTADDRESS_CLAMP,
};

static const D3DTEXTUREFILTERTYPE texFilterToD3D9[] = {
	D3DTEXF_POINT,
	D3DTEXF_LINEAR,
};

inline D3DPRIMITIVETYPE PrimToD3D9(Primitive prim) {
	switch (prim) {
	case Primitive::POINT_LIST: return D3DPT_POINTLIST;
	case Primitive::LINE_LIST: return D3DPT_LINELIST;
	case Primitive::LINE_STRIP: return D3DPT_LINESTRIP;
	case Primitive::TRIANGLE_LIST: return D3DPT_TRIANGLELIST;
	case Primitive::TRIANGLE_STRIP: return D3DPT_TRIANGLESTRIP;
	case Primitive::TRIANGLE_FAN: return D3DPT_TRIANGLEFAN;
	}
}

inline int PrimCountDivisor(Primitive prim) {
	switch (prim) {
	case Primitive::POINT_LIST: return 1;
	case Primitive::LINE_LIST: return 2;
	case Primitive::LINE_STRIP: return 2;
	case Primitive::TRIANGLE_LIST: return 3;
	case Primitive::TRIANGLE_STRIP: return 3;
	case Primitive::TRIANGLE_FAN: return 3;
	}
}

class Thin3DDX9DepthStencilState : public DepthStencilState {
public:
	BOOL depthTestEnabled;
	BOOL depthWriteEnabled;
	D3DCMPFUNC depthCompare;

	void Apply(LPDIRECT3DDEVICE9 device) {
		device->SetRenderState(D3DRS_ZENABLE, depthTestEnabled);
		device->SetRenderState(D3DRS_ZWRITEENABLE, depthWriteEnabled);
		device->SetRenderState(D3DRS_ZFUNC, depthCompare);
	}
};


class Thin3DDX9RasterState : public RasterState {
public:
	DWORD cullMode;

	void Apply(LPDIRECT3DDEVICE9 device) {
		device->SetRenderState(D3DRS_CULLMODE, cullMode);
	}
};


class Thin3DDX9BlendState : public BlendState {
public:
	bool enabled;
	D3DBLENDOP eqCol, eqAlpha;
	D3DBLEND srcCol, srcAlpha, dstCol, dstAlpha;
	uint32_t fixedColor;

	void Apply(LPDIRECT3DDEVICE9 device) {
		device->SetRenderState(D3DRS_ALPHABLENDENABLE, (DWORD)enabled);
		device->SetRenderState(D3DRS_BLENDOP, eqCol);
		device->SetRenderState(D3DRS_BLENDOPALPHA, eqAlpha);
		device->SetRenderState(D3DRS_SRCBLEND, srcCol);
		device->SetRenderState(D3DRS_DESTBLEND, dstCol);
		device->SetRenderState(D3DRS_SRCBLENDALPHA, srcAlpha);
		device->SetRenderState(D3DRS_DESTBLENDALPHA, dstAlpha);
		// device->SetRenderState(, fixedColor);
	}
};

class Thin3DDX9SamplerState : public SamplerState {
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


class Thin3DDX9VertexFormat : public Thin3DVertexFormat {
public:
	Thin3DDX9VertexFormat(LPDIRECT3DDEVICE9 device, const std::vector<VertexComponent> &components, int stride);
	~Thin3DDX9VertexFormat() {
		if (decl_) {
			decl_->Release();
		}
	}
	int GetStride() const { return stride_; }
	void Apply(LPDIRECT3DDEVICE9 device) {
		device->SetVertexDeclaration(decl_);
	}
	bool RequiresBuffer() override {
		return false;
	}
private:
	LPDIRECT3DVERTEXDECLARATION9 decl_;
	int stride_;
};

class Thin3DDX9Shader : public Shader {
public:
	Thin3DDX9Shader(ShaderStage stage) : stage_(stage), vshader_(NULL), pshader_(NULL), constantTable_(NULL) {}
	~Thin3DDX9Shader() {
		if (vshader_)
			vshader_->Release();
		if (pshader_)
			pshader_->Release();
		if (constantTable_)
			constantTable_->Release();
	}
	bool Compile(LPDIRECT3DDEVICE9 device, const char *source);
	void Apply(LPDIRECT3DDEVICE9 device) {
		if (stage_ == ShaderStage::FRAGMENT) {
			device->SetPixelShader(pshader_);
		} else {
			device->SetVertexShader(vshader_);
		}
	}
	void SetVector(LPDIRECT3DDEVICE9 device, const char *name, float *value, int n);
	void SetMatrix4x4(LPDIRECT3DDEVICE9 device, const char *name, const float value[16]);

private:
	ShaderStage stage_;
	LPDIRECT3DVERTEXSHADER9 vshader_;
	LPDIRECT3DPIXELSHADER9 pshader_;
	LPD3DXCONSTANTTABLE constantTable_;
};

class Thin3DDX9ShaderSet : public ShaderSet {
public:
	Thin3DDX9ShaderSet(LPDIRECT3DDEVICE9 device) : device_(device) {}
	Thin3DDX9Shader *vshader;
	Thin3DDX9Shader *pshader;
	void Apply(LPDIRECT3DDEVICE9 device);
	void SetVector(const char *name, float *value, int n) { vshader->SetVector(device_, name, value, n); pshader->SetVector(device_, name, value, n); }
	void SetMatrix4x4(const char *name, const float value[16]) { vshader->SetMatrix4x4(device_, name, value); }  // pshaders don't usually have matrices
private:
	LPDIRECT3DDEVICE9 device_;
};

class Thin3DDX9Texture : public Texture {
public:
	Thin3DDX9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx) : device_(device), deviceEx_(deviceEx), type_(TextureType::UNKNOWN), fmt_(D3DFMT_UNKNOWN), tex_(NULL), volTex_(NULL), cubeTex_(NULL) {
	}
	Thin3DDX9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx, TextureType type, DataFormat format, int width, int height, int depth, int mipLevels);
	~Thin3DDX9Texture();
	bool Create(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override;
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) override;
	void AutoGenMipmaps() override {}
	void SetToSampler(LPDIRECT3DDEVICE9 device, int sampler);
	void Finalize(int zim_flags) override {}

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
	case DataFormat::R8A8G8B8_UNORM: return D3DFMT_A8R8G8B8;
	case DataFormat::R4G4B4A4_UNORM: return D3DFMT_A4R4G4B4;
	case DataFormat::D24S8: return D3DFMT_D24S8;
	case DataFormat::D16: return D3DFMT_D16;
	default: return D3DFMT_UNKNOWN;
	}
}

Thin3DDX9Texture::Thin3DDX9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx, TextureType type, DataFormat format, int width, int height, int depth, int mipLevels)
	: device_(device), deviceEx_(deviceEx), type_(type), tex_(NULL), volTex_(NULL), cubeTex_(NULL) {
	Create(type, format, width, height, depth, mipLevels);
}

Thin3DDX9Texture::~Thin3DDX9Texture() {
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

bool Thin3DDX9Texture::Create(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) {
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


void Thin3DDX9Texture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) {
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

void Thin3DDX9Texture::SetToSampler(LPDIRECT3DDEVICE9 device, int sampler) {
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

class Thin3DDX9Context : public Thin3DContext {
public:
	Thin3DDX9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx);
	~Thin3DDX9Context();

	DepthStencilState *CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, Comparison depthCompare);
	BlendState *CreateBlendState(const BlendStateDesc &desc) override;
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override;
	RasterState *CreateRasterState(const T3DRasterStateDesc &desc) override;
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	ShaderSet *CreateShaderSet(Shader *vshader, Shader *fshader) override;
	Thin3DVertexFormat *CreateVertexFormat(const std::vector<VertexComponent> &components, int stride, Shader *vshader) override;
	Texture *CreateTexture() override;
	Texture *CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) override;
	Shader *CreateShader(ShaderStage stage, const char *glsl_source, const char *hlsl_source, const char *vulkan_source) override;

	// Bound state objects. Too cumbersome to add them all as parameters to Draw.
	void SetBlendState(BlendState *state) {
		Thin3DDX9BlendState *bs = static_cast<Thin3DDX9BlendState *>(state);
		bs->Apply(device_);
	}
	void SetSamplerStates(int start, int count, SamplerState **states) {
		for (int i = 0; i < count; ++i) {
			Thin3DDX9SamplerState *s = static_cast<Thin3DDX9SamplerState *>(states[start + i]);
			s->Apply(device_, start + i);
		}
	}
	void SetDepthStencilState(DepthStencilState *state) {
		Thin3DDX9DepthStencilState *bs = static_cast<Thin3DDX9DepthStencilState *>(state);
		bs->Apply(device_);
	}
	void SetRasterState(RasterState *state) {
		Thin3DDX9RasterState *bs = static_cast<Thin3DDX9RasterState *>(state);
		bs->Apply(device_);
	}

	void BindTextures(int start, int count, Texture **textures);

	// Raster state
	void SetScissorEnabled(bool enable);
	void SetScissorRect(int left, int top, int width, int height);
	void SetViewports(int count, Viewport *viewports);

	void Draw(Primitive prim, ShaderSet *pipeline, Thin3DVertexFormat *format, Buffer *vdata, int vertexCount, int offset) override;
	void DrawIndexed(Primitive prim, ShaderSet *pipeline, Thin3DVertexFormat *format, Buffer *vdata, Buffer *idata, int vertexCount, int offset) override;
	void DrawUP(Primitive prim, ShaderSet *shaderSet, Thin3DVertexFormat *format, const void *vdata, int vertexCount) override;
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
	D3DCAPS9 caps_;
	char shadeLangVersion_[64];
};

Thin3DDX9Context::Thin3DDX9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx)
	: d3d_(d3d), d3dEx_(d3dEx), adapterId_(adapterId), device_(device), deviceEx_(deviceEx) {
	CreatePresets();
	d3d->GetAdapterIdentifier(adapterId, 0, &identifier_);
	if (!FAILED(device->GetDeviceCaps(&caps_))) {
		sprintf(shadeLangVersion_, "PS: %04x VS: %04x", caps_.PixelShaderVersion & 0xFFFF, caps_.VertexShaderVersion & 0xFFFF);
	} else {
		strcpy(shadeLangVersion_, "N/A");
	}
}

Thin3DDX9Context::~Thin3DDX9Context() {
}

Shader *Thin3DDX9Context::CreateShader(ShaderStage stage, const char *glsl_source, const char *hlsl_source, const char *vulkan_source) {
	Thin3DDX9Shader *shader = new Thin3DDX9Shader(stage);
	if (shader->Compile(device_, hlsl_source)) {
		return shader;
	} else {
		delete shader;
		return NULL;
	}
}

ShaderSet *Thin3DDX9Context::CreateShaderSet(Shader *vshader, Shader *fshader) {
	if (!vshader || !fshader) {
		ELOG("ShaderSet requires both a valid vertex and a fragment shader: %p %p", vshader, fshader);
		return NULL;
	}
	Thin3DDX9ShaderSet *shaderSet = new Thin3DDX9ShaderSet(device_);
	shaderSet->vshader = static_cast<Thin3DDX9Shader *>(vshader);
	shaderSet->pshader = static_cast<Thin3DDX9Shader *>(fshader);
	return shaderSet;
}

DepthStencilState *Thin3DDX9Context::CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, Comparison depthCompare) {
	Thin3DDX9DepthStencilState *ds = new Thin3DDX9DepthStencilState();
	ds->depthCompare = compareToD3D9[depthCompare];
	ds->depthTestEnabled = depthTestEnabled;
	ds->depthWriteEnabled = depthWriteEnabled;
	return ds;
}

Thin3DVertexFormat *Thin3DDX9Context::CreateVertexFormat(const std::vector<VertexComponent> &components, int stride, Shader *vshader) {
	Thin3DDX9VertexFormat *fmt = new Thin3DDX9VertexFormat(device_, components, stride);
	return fmt;
}

BlendState *Thin3DDX9Context::CreateBlendState(const BlendStateDesc &desc) {
	Thin3DDX9BlendState *bs = new Thin3DDX9BlendState();
	bs->enabled = desc.enabled;
	bs->eqCol = blendEqToD3D9[desc.eqCol];
	bs->srcCol = blendFactorToD3D9[desc.srcCol];
	bs->dstCol = blendFactorToD3D9[desc.dstCol];
	bs->eqAlpha = blendEqToD3D9[desc.eqAlpha];
	bs->srcAlpha = blendFactorToD3D9[desc.srcAlpha];
	bs->dstAlpha = blendFactorToD3D9[desc.dstAlpha];
	// Ignore logic ops, we don't support them in D3D9
	return bs;
}

SamplerState *Thin3DDX9Context::CreateSamplerState(const SamplerStateDesc &desc) {
	Thin3DDX9SamplerState *samps = new Thin3DDX9SamplerState();
	samps->wrapS = texWrapToD3D9[(int)desc.wrapS];
	samps->wrapT = texWrapToD3D9[(int)desc.wrapT];
	samps->magFilt = texFilterToD3D9[(int)desc.magFilt];
	samps->minFilt = texFilterToD3D9[(int)desc.minFilt];
	samps->mipFilt = texFilterToD3D9[(int)desc.mipFilt];
	return samps;
}

RasterState *Thin3DDX9Context::CreateRasterState(const T3DRasterStateDesc &desc) {
	Thin3DDX9RasterState *rs = new Thin3DDX9RasterState();
	rs->cullMode = D3DCULL_NONE;
	if (desc.cull == CullMode::NONE) {
		return rs;
	}
	switch (desc.facing) {
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

Texture *Thin3DDX9Context::CreateTexture() {
	Thin3DDX9Texture *tex = new Thin3DDX9Texture(device_, deviceEx_);
	return tex;
}

Texture *Thin3DDX9Context::CreateTexture(TextureType type, DataFormat format, int width, int height, int depth, int mipLevels) {
	Thin3DDX9Texture *tex = new Thin3DDX9Texture(device_, deviceEx_, type, format, width, height, depth, mipLevels);
	return tex;
}

void Thin3DDX9Context::BindTextures(int start, int count, Texture **textures) {
	for (int i = start; i < start + count; i++) {
		Thin3DDX9Texture *tex = static_cast<Thin3DDX9Texture *>(textures[i - start]);
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
	case DataFormat::FLOATx2: return D3DDECLTYPE_FLOAT2;
	case DataFormat::FLOATx3: return D3DDECLTYPE_FLOAT3;
	case DataFormat::FLOATx4: return D3DDECLTYPE_FLOAT4;
	case DataFormat::UNORM8x4: return D3DDECLTYPE_UBYTE4N;  // D3DCOLOR?
	default: return D3DDECLTYPE_UNUSED;
	}
}

Thin3DDX9VertexFormat::Thin3DDX9VertexFormat(LPDIRECT3DDEVICE9 device, const std::vector<VertexComponent> &components, int stride) : decl_(NULL) {
	D3DVERTEXELEMENT9 *elements = new D3DVERTEXELEMENT9[components.size() + 1];
	size_t i;
	for (i = 0; i < components.size(); i++) {
		elements[i].Stream = 0;
		elements[i].Offset = components[i].offset;
		elements[i].Method = D3DDECLMETHOD_DEFAULT;
		SemanticToD3D9UsageAndIndex(components[i].semantic, &elements[i].Usage, &elements[i].UsageIndex);
		elements[i].Type = VertexDataTypeToD3DType(components[i].type);
	}
	D3DVERTEXELEMENT9 end = D3DDECL_END();
	// Zero the last one.
	memcpy(&elements[i], &end, sizeof(elements[i]));

	HRESULT hr = device->CreateVertexDeclaration(elements, &decl_);
	if (FAILED(hr)) {
		ELOG("Error creating vertex decl");
	}
	delete[] elements;
	stride_ = stride;
}

Buffer *Thin3DDX9Context::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new Thin3DDX9Buffer(device_, size, usageFlags);
}

void Thin3DDX9ShaderSet::Apply(LPDIRECT3DDEVICE9 device) {
	vshader->Apply(device);
	pshader->Apply(device);
}

void Thin3DDX9Context::Draw(Primitive prim, ShaderSet *shaderSet, Thin3DVertexFormat *format, Buffer *vdata, int vertexCount, int offset) {
	Thin3DDX9Buffer *vbuf = static_cast<Thin3DDX9Buffer *>(vdata);
	Thin3DDX9VertexFormat *fmt = static_cast<Thin3DDX9VertexFormat *>(format);
	Thin3DDX9ShaderSet *ss = static_cast<Thin3DDX9ShaderSet*>(shaderSet);

	vbuf->BindAsVertexBuf(device_, fmt->GetStride(), offset);
	ss->Apply(device_);
	fmt->Apply(device_);
	device_->DrawPrimitive(PrimToD3D9(prim), offset, vertexCount / 3);
}

void Thin3DDX9Context::DrawIndexed(Primitive prim, ShaderSet *shaderSet, Thin3DVertexFormat *format, Buffer *vdata, Buffer *idata, int vertexCount, int offset) {
	Thin3DDX9Buffer *vbuf = static_cast<Thin3DDX9Buffer *>(vdata);
	Thin3DDX9Buffer *ibuf = static_cast<Thin3DDX9Buffer *>(idata);
	Thin3DDX9VertexFormat *fmt = static_cast<Thin3DDX9VertexFormat *>(format);
	Thin3DDX9ShaderSet *ss = static_cast<Thin3DDX9ShaderSet*>(shaderSet);

	ss->Apply(device_);
	fmt->Apply(device_);
	vbuf->BindAsVertexBuf(device_, fmt->GetStride(), offset);
	ibuf->BindAsIndexBuf(device_);
	device_->DrawIndexedPrimitive(PrimToD3D9(prim), 0, 0, vertexCount, 0, vertexCount / PrimCountDivisor(prim));
}

void Thin3DDX9Context::DrawUP(Primitive prim, ShaderSet *shaderSet, Thin3DVertexFormat *format, const void *vdata, int vertexCount) {
	Thin3DDX9VertexFormat *fmt = static_cast<Thin3DDX9VertexFormat *>(format);
	Thin3DDX9ShaderSet *ss = static_cast<Thin3DDX9ShaderSet*>(shaderSet);

	ss->Apply(device_);
	fmt->Apply(device_);
	device_->DrawPrimitiveUP(PrimToD3D9(prim), vertexCount / 3, vdata, fmt->GetStride());
}

static uint32_t SwapRB(uint32_t c) {
	return (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000);
}

void Thin3DDX9Context::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	UINT d3dMask = 0;
	if (mask & ClearFlag::COLOR) d3dMask |= D3DCLEAR_TARGET;
	if (mask & ClearFlag::DEPTH) d3dMask |= D3DCLEAR_ZBUFFER;
	if (mask & ClearFlag::STENCIL) d3dMask |= D3DCLEAR_STENCIL;

	device_->Clear(0, NULL, d3dMask, (D3DCOLOR)SwapRB(colorval), depthVal, stencilVal);
}

void Thin3DDX9Context::SetScissorEnabled(bool enable) {
	device_->SetRenderState(D3DRS_SCISSORTESTENABLE, enable);
}

void Thin3DDX9Context::SetScissorRect(int left, int top, int width, int height) {
	RECT rc;
	rc.left = left;
	rc.top = top;
	rc.right = left + width;
	rc.bottom = top + height;
	device_->SetScissorRect(&rc);
}

void Thin3DDX9Context::SetViewports(int count, Viewport *viewports) {
	D3DVIEWPORT9 vp;
	vp.X = (DWORD)viewports[0].TopLeftX;
	vp.Y = (DWORD)viewports[0].TopLeftY;
	vp.Width = (DWORD)viewports[0].Width;
	vp.Height = (DWORD)viewports[0].Height;
	vp.MinZ = viewports[0].MinDepth;
	vp.MaxZ = viewports[0].MaxDepth;
	device_->SetViewport(&vp);
}

bool Thin3DDX9Shader::Compile(LPDIRECT3DDEVICE9 device, const char *source) {
	LPD3DXMACRO defines = NULL;
	LPD3DXINCLUDE includes = NULL;
	DWORD flags = 0;
	LPD3DXBUFFER codeBuffer;
	LPD3DXBUFFER errorBuffer;
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

void Thin3DDX9Shader::SetVector(LPDIRECT3DDEVICE9 device, const char *name, float *value, int n) {
	D3DXHANDLE handle = constantTable_->GetConstantByName(NULL, name);
	if (handle) {
		constantTable_->SetFloatArray(device, handle, value, n);
	}
}

void Thin3DDX9Shader::SetMatrix4x4(LPDIRECT3DDEVICE9 device, const char *name, const float value[16]) {
	D3DXHANDLE handle = constantTable_->GetConstantByName(NULL, name);
	if (handle) {
		constantTable_->SetFloatArray(device, handle, value, 16);
	}
}

Thin3DContext *T3DCreateDX9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx) {
	int d3dx_ver = LoadD3DX9Dynamic();
	if (!d3dx_ver) {
		ELOG("Failed to load D3DX9!");
		return NULL;
	}
	return new Thin3DDX9Context(d3d, d3dEx, adapterId, device, deviceEx);
}

}  // namespace Draw