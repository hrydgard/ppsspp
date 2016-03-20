#include <vector>
#include <stdio.h>
#include <inttypes.h>

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

static const D3DPRIMITIVETYPE primToD3D9[] = {
	D3DPT_POINTLIST,
	D3DPT_LINELIST,
	D3DPT_TRIANGLELIST,
};

static const int primCountDivisor[] = {
	1,
	2,
	3,
};

class Thin3DDX9DepthStencilState : public Thin3DDepthStencilState {
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

class Thin3DDX9BlendState : public Thin3DBlendState {
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

class Thin3DDX9SamplerState : public Thin3DSamplerState {
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

class Thin3DDX9Buffer : public Thin3DBuffer {
public:
	Thin3DDX9Buffer(LPDIRECT3DDEVICE9 device, size_t size, uint32_t flags) : vbuffer_(nullptr), ibuffer_(nullptr), maxSize_(size) {
		if (flags & T3DBufferUsage::INDEXDATA) {
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
	Thin3DDX9VertexFormat(LPDIRECT3DDEVICE9 device, const std::vector<Thin3DVertexComponent> &components, int stride);
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

class Thin3DDX9Shader : public Thin3DShader {
public:
	Thin3DDX9Shader(bool isPixelShader) : isPixelShader_(isPixelShader), vshader_(NULL), pshader_(NULL), constantTable_(NULL) {}
	~Thin3DDX9Shader() {
		if (vshader_)
			vshader_->Release();
		if (pshader_)
			pshader_->Release();
		if (constantTable_)
			constantTable_->Release();
	}
	bool Compile(LPDIRECT3DDEVICE9 device, const char *source, const char *profile);
	void Apply(LPDIRECT3DDEVICE9 device) {
		if (isPixelShader_) {
			device->SetPixelShader(pshader_);
		} else {
			device->SetVertexShader(vshader_);
		}
	}
	void SetVector(LPDIRECT3DDEVICE9 device, const char *name, float *value, int n);
	void SetMatrix4x4(LPDIRECT3DDEVICE9 device, const char *name, const float value[16]);

private:
	bool isPixelShader_;
	LPDIRECT3DVERTEXSHADER9 vshader_;
	LPDIRECT3DPIXELSHADER9 pshader_;
	LPD3DXCONSTANTTABLE constantTable_;
};

class Thin3DDX9ShaderSet : public Thin3DShaderSet {
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

class Thin3DDX9Texture : public Thin3DTexture {
public:
	Thin3DDX9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx) : device_(device), deviceEx_(deviceEx), type_(T3DTextureType::UNKNOWN), fmt_(D3DFMT_UNKNOWN), tex_(NULL), volTex_(NULL), cubeTex_(NULL) {
	}
	Thin3DDX9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx, T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels);
	~Thin3DDX9Texture();
	bool Create(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) override;
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, const uint8_t *data) override;
	void AutoGenMipmaps() override {}
	void SetToSampler(LPDIRECT3DDEVICE9 device, int sampler);
	void Finalize(int zim_flags) override {}

private:
	LPDIRECT3DDEVICE9 device_;
	LPDIRECT3DDEVICE9EX deviceEx_;
	T3DTextureType type_;
	D3DFORMAT fmt_;
	LPDIRECT3DTEXTURE9 tex_;
	LPDIRECT3DVOLUMETEXTURE9 volTex_;
	LPDIRECT3DCUBETEXTURE9 cubeTex_;
};

D3DFORMAT FormatToD3D(T3DImageFormat fmt) {
	switch (fmt) {
	case RGBA8888: return D3DFMT_A8R8G8B8;
	case RGBA4444: return D3DFMT_A4R4G4B4;
	case D24S8: return D3DFMT_D24S8;
	case D16: return D3DFMT_D16;
	default: return D3DFMT_UNKNOWN;
	}
}

Thin3DDX9Texture::Thin3DDX9Texture(LPDIRECT3DDEVICE9 device, LPDIRECT3DDEVICE9EX deviceEx, T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels)
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

bool Thin3DDX9Texture::Create(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) {
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

	Thin3DDepthStencilState *CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, T3DComparison depthCompare);
	Thin3DBlendState *CreateBlendState(const T3DBlendStateDesc &desc) override;
	Thin3DSamplerState *CreateSamplerState(const T3DSamplerStateDesc &desc) override;
	Thin3DBuffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Thin3DShaderSet *CreateShaderSet(Thin3DShader *vshader, Thin3DShader *fshader) override;
	Thin3DVertexFormat *CreateVertexFormat(const std::vector<Thin3DVertexComponent> &components, int stride, Thin3DShader *vshader) override;
	Thin3DTexture *CreateTexture() override;
	Thin3DTexture *CreateTexture(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) override;

	Thin3DShader *CreateVertexShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) override;
	Thin3DShader *CreateFragmentShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) override;

	// Bound state objects. Too cumbersome to add them all as parameters to Draw.
	void SetBlendState(Thin3DBlendState *state) {
		Thin3DDX9BlendState *bs = static_cast<Thin3DDX9BlendState *>(state);
		bs->Apply(device_);
	}
	void SetSamplerStates(int start, int count, Thin3DSamplerState **states) {
		for (int i = 0; i < count; ++i) {
			Thin3DDX9SamplerState *s = static_cast<Thin3DDX9SamplerState *>(states[start + i]);
			s->Apply(device_, start + i);
		}
	}
	void SetDepthStencilState(Thin3DDepthStencilState *state) {
		Thin3DDX9DepthStencilState *bs = static_cast<Thin3DDX9DepthStencilState *>(state);
		bs->Apply(device_);
	}

	void SetTextures(int start, int count, Thin3DTexture **textures);

	// Raster state
	void SetScissorEnabled(bool enable);
	void SetScissorRect(int left, int top, int width, int height);
	void SetViewports(int count, T3DViewport *viewports);
	void SetRenderState(T3DRenderState rs, uint32_t value) override;

	void Draw(T3DPrimitive prim, Thin3DShaderSet *pipeline, Thin3DVertexFormat *format, Thin3DBuffer *vdata, int vertexCount, int offset) override;
	void DrawIndexed(T3DPrimitive prim, Thin3DShaderSet *pipeline, Thin3DVertexFormat *format, Thin3DBuffer *vdata, Thin3DBuffer *idata, int vertexCount, int offset) override;
	void DrawUP(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, const void *vdata, int vertexCount) override;
	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal);

	std::string GetInfoString(T3DInfo info) const override {
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

Thin3DDX9Context::Thin3DDX9Context(IDirect3D9 *d3d, IDirect3D9Ex *d3dEx, int adapterId, IDirect3DDevice9 *device, IDirect3DDevice9Ex *deviceEx) : d3d_(d3d), d3dEx_(d3dEx), adapterId_(adapterId), device_(device), deviceEx_(deviceEx) {
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

Thin3DShader *Thin3DDX9Context::CreateVertexShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) {
	Thin3DDX9Shader *shader = new Thin3DDX9Shader(false);
	if (shader->Compile(device_, hlsl_source, "vs_2_0")) {
		return shader;
	} else {
		delete shader;
		return NULL;
	}
}

Thin3DShader *Thin3DDX9Context::CreateFragmentShader(const char *glsl_source, const char *hlsl_source, const char *vulkan_source) {
	Thin3DDX9Shader *shader = new Thin3DDX9Shader(true);
	if (shader->Compile(device_, hlsl_source, "ps_2_0")) {
		return shader;
	} else {
		delete shader;
		return NULL;
	}
}

Thin3DShaderSet *Thin3DDX9Context::CreateShaderSet(Thin3DShader *vshader, Thin3DShader *fshader) {
	if (!vshader || !fshader) {
		ELOG("ShaderSet requires both a valid vertex and a fragment shader: %p %p", vshader, fshader);
		return NULL;
	}
	Thin3DDX9ShaderSet *shaderSet = new Thin3DDX9ShaderSet(device_);
	shaderSet->vshader = static_cast<Thin3DDX9Shader *>(vshader);
	shaderSet->pshader = static_cast<Thin3DDX9Shader *>(fshader);
	return shaderSet;
}

Thin3DDepthStencilState *Thin3DDX9Context::CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, T3DComparison depthCompare) {
	Thin3DDX9DepthStencilState *ds = new Thin3DDX9DepthStencilState();
	ds->depthCompare = compareToD3D9[depthCompare];
	ds->depthTestEnabled = depthTestEnabled;
	ds->depthWriteEnabled = depthWriteEnabled;
	return ds;
}

Thin3DVertexFormat *Thin3DDX9Context::CreateVertexFormat(const std::vector<Thin3DVertexComponent> &components, int stride, Thin3DShader *vshader) {
	Thin3DDX9VertexFormat *fmt = new Thin3DDX9VertexFormat(device_, components, stride);
	return fmt;
}

Thin3DBlendState *Thin3DDX9Context::CreateBlendState(const T3DBlendStateDesc &desc) {
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

Thin3DSamplerState *Thin3DDX9Context::CreateSamplerState(const T3DSamplerStateDesc &desc) {
	Thin3DDX9SamplerState *samps = new Thin3DDX9SamplerState();
	samps->wrapS = texWrapToD3D9[desc.wrapS];
	samps->wrapT = texWrapToD3D9[desc.wrapT];
	samps->magFilt = texFilterToD3D9[desc.magFilt];
	samps->minFilt = texFilterToD3D9[desc.minFilt];
	samps->mipFilt = texFilterToD3D9[desc.mipFilt];
	return samps;
}

Thin3DTexture *Thin3DDX9Context::CreateTexture() {
	Thin3DDX9Texture *tex = new Thin3DDX9Texture(device_, deviceEx_);
	return tex;
}

Thin3DTexture *Thin3DDX9Context::CreateTexture(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) {
	Thin3DDX9Texture *tex = new Thin3DDX9Texture(device_, deviceEx_, type, format, width, height, depth, mipLevels);
	return tex;
}

void Thin3DDX9Context::SetTextures(int start, int count, Thin3DTexture **textures) {
	for (int i = start; i < start + count; i++) {
		Thin3DDX9Texture *tex = static_cast<Thin3DDX9Texture *>(textures[i - start]);
		tex->SetToSampler(device_, i);
	}
}

void SemanticToD3D9UsageAndIndex(int semantic, BYTE *usage, BYTE *index) {
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

static int VertexDataTypeToD3DType(T3DVertexDataType type) {
	switch (type) {
	case T3DVertexDataType::FLOATx2: return D3DDECLTYPE_FLOAT2;
	case T3DVertexDataType::FLOATx3: return D3DDECLTYPE_FLOAT3;
	case T3DVertexDataType::FLOATx4: return D3DDECLTYPE_FLOAT4;
	case T3DVertexDataType::UNORM8x4: return D3DDECLTYPE_UBYTE4N;  // D3DCOLOR?
	default: return D3DDECLTYPE_UNUSED;
	}
}

Thin3DDX9VertexFormat::Thin3DDX9VertexFormat(LPDIRECT3DDEVICE9 device, const std::vector<Thin3DVertexComponent> &components, int stride) : decl_(NULL) {
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

Thin3DBuffer *Thin3DDX9Context::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new Thin3DDX9Buffer(device_, size, usageFlags);
}

void Thin3DDX9ShaderSet::Apply(LPDIRECT3DDEVICE9 device) {
	vshader->Apply(device);
	pshader->Apply(device);
}

void Thin3DDX9Context::SetRenderState(T3DRenderState rs, uint32_t value) {
	switch (rs) {
	case T3DRenderState::CULL_MODE:
		switch (value) {
		case T3DCullMode::CCW: device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW); break;
		case T3DCullMode::CW: device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW); break;
		case T3DCullMode::NO_CULL: device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); break;
		}
		break;
	}
}

void Thin3DDX9Context::Draw(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, Thin3DBuffer *vdata, int vertexCount, int offset) {
	Thin3DDX9Buffer *vbuf = static_cast<Thin3DDX9Buffer *>(vdata);
	Thin3DDX9VertexFormat *fmt = static_cast<Thin3DDX9VertexFormat *>(format);
	Thin3DDX9ShaderSet *ss = static_cast<Thin3DDX9ShaderSet*>(shaderSet);

	vbuf->BindAsVertexBuf(device_, fmt->GetStride(), offset);
	ss->Apply(device_);
	fmt->Apply(device_);
	device_->DrawPrimitive(primToD3D9[prim], offset, vertexCount / 3);
}

void Thin3DDX9Context::DrawIndexed(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, Thin3DBuffer *vdata, Thin3DBuffer *idata, int vertexCount, int offset) {
	Thin3DDX9Buffer *vbuf = static_cast<Thin3DDX9Buffer *>(vdata);
	Thin3DDX9Buffer *ibuf = static_cast<Thin3DDX9Buffer *>(idata);
	Thin3DDX9VertexFormat *fmt = static_cast<Thin3DDX9VertexFormat *>(format);
	Thin3DDX9ShaderSet *ss = static_cast<Thin3DDX9ShaderSet*>(shaderSet);

	ss->Apply(device_);
	fmt->Apply(device_);
	vbuf->BindAsVertexBuf(device_, fmt->GetStride(), offset);
	ibuf->BindAsIndexBuf(device_);
	device_->DrawIndexedPrimitive(primToD3D9[prim], 0, 0, vertexCount, 0, vertexCount / primCountDivisor[prim]);
}

void Thin3DDX9Context::DrawUP(T3DPrimitive prim, Thin3DShaderSet *shaderSet, Thin3DVertexFormat *format, const void *vdata, int vertexCount) {
	Thin3DDX9VertexFormat *fmt = static_cast<Thin3DDX9VertexFormat *>(format);
	Thin3DDX9ShaderSet *ss = static_cast<Thin3DDX9ShaderSet*>(shaderSet);

	ss->Apply(device_);
	fmt->Apply(device_);
	device_->DrawPrimitiveUP(primToD3D9[prim], vertexCount / 3, vdata, fmt->GetStride());
}

static uint32_t SwapRB(uint32_t c) {
	return (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c << 16) & 0xFF0000);
}

void Thin3DDX9Context::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	UINT d3dMask = 0;
	if (mask & T3DClear::COLOR) d3dMask |= D3DCLEAR_TARGET;
	if (mask & T3DClear::DEPTH) d3dMask |= D3DCLEAR_ZBUFFER;
	if (mask & T3DClear::STENCIL) d3dMask |= D3DCLEAR_STENCIL;

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

void Thin3DDX9Context::SetViewports(int count, T3DViewport *viewports) {
	D3DVIEWPORT9 vp;
	vp.X = (DWORD)viewports[0].TopLeftX;
	vp.Y = (DWORD)viewports[0].TopLeftY;
	vp.Width = (DWORD)viewports[0].Width;
	vp.Height = (DWORD)viewports[0].Height;
	vp.MinZ = viewports[0].MinDepth;
	vp.MaxZ = viewports[0].MaxDepth;
	device_->SetViewport(&vp);
}

bool Thin3DDX9Shader::Compile(LPDIRECT3DDEVICE9 device, const char *source, const char *profile) {
	LPD3DXMACRO defines = NULL;
	LPD3DXINCLUDE includes = NULL;
	DWORD flags = 0;
	LPD3DXBUFFER codeBuffer;
	LPD3DXBUFFER errorBuffer;
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
	if (isPixelShader_) {
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
