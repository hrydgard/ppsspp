#include <vector>
#include <inttypes.h>
#include <d3d9.h>
#include <d3dx9.h>

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

static const D3DPRIMITIVETYPE primToD3D9[] = {
	D3DPT_POINTLIST,
	D3DPT_LINELIST,
	D3DPT_TRIANGLELIST,
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

class Thin3DDX9Buffer : public Thin3DBuffer {
public:
	Thin3DDX9Buffer(LPDIRECT3DDEVICE9 device, size_t size, uint32_t flags) : vbuffer_(nullptr), ibuffer_(nullptr) {
		if (flags & T3DBufferUsage::INDEXDATA) {
			DWORD usage = 0;
			device->CreateIndexBuffer((UINT)size, usage, D3DFMT_INDEX32, D3DPOOL_MANAGED, &ibuffer_, NULL);
		} else {
			DWORD usage = 0;
			device->CreateVertexBuffer((UINT)size, usage, 0, D3DPOOL_MANAGED, &vbuffer_, NULL);
		}
	}
	void SetData(const uint8_t *data, size_t size) override {
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
};


class Thin3DDX9VertexFormat : public Thin3DVertexFormat {
public:
	Thin3DDX9VertexFormat(LPDIRECT3DDEVICE9 device, const std::vector<Thin3DVertexComponent> &components, int stride);
	int GetStride() const { return stride_; }
	void Apply(LPDIRECT3DDEVICE9 device) {
		device->SetVertexDeclaration(decl_);
	}
private:
	LPDIRECT3DVERTEXDECLARATION9 decl_;
	int stride_;
};

class Thin3DDX9Shader : public Thin3DShader {
public:
	Thin3DDX9Shader(bool isPixelShader) : isPixelShader_(isPixelShader), vshader_(NULL), pshader_(NULL) {}
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
	void SetMatrix4x4(LPDIRECT3DDEVICE9 device, const char *name, const Matrix4x4 &value);

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
	void SetMatrix4x4(const char *name, const Matrix4x4 &value) { vshader->SetMatrix4x4(device_, name, value); }  // pshaders don't usually have matrices
private:
	LPDIRECT3DDEVICE9 device_;
};

class Thin3DDX9Texture : public Thin3DTexture {
public:
	Thin3DDX9Texture(LPDIRECT3DDEVICE9 device, T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels);
	void SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, uint8_t *data) override;
	void AutoGenMipmaps() override {}
	void SetToSampler(LPDIRECT3DDEVICE9 device, int sampler);
	void Finalize(int zim_flags) override {}

private:
	T3DTextureType type_;
	D3DFORMAT fmt_;
	LPDIRECT3DTEXTURE9 tex_;
	LPDIRECT3DVOLUMETEXTURE9 volTex_;
	LPDIRECT3DCUBETEXTURE9 cubeTex_;
};

D3DFORMAT FormatToD3D(T3DImageFormat fmt) {
	switch (fmt) {
	case RGBA8888: return D3DFMT_A8R8G8B8;
	case D24S8: return D3DFMT_D24S8;
	case D16: return D3DFMT_D16;
	default: return D3DFMT_UNKNOWN;
	}
}

Thin3DDX9Texture::Thin3DDX9Texture(LPDIRECT3DDEVICE9 device, T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels)
  : type_(type) {
	width_ = width;
	height_ = height;
	depth_ = depth;
	tex_ = NULL;
	fmt_ = FormatToD3D(format);
	HRESULT hr;
	switch (type_) {
	case LINEAR1D:
	case LINEAR2D:
		hr = device->CreateTexture(width, height, mipLevels, 0, fmt_, D3DPOOL_MANAGED, &tex_, NULL);
		break;
	case LINEAR3D:
		hr = device->CreateVolumeTexture(width, height, depth, mipLevels, 0, fmt_, D3DPOOL_MANAGED, &volTex_, NULL);
		break;
	case CUBE:
		hr = device->CreateCubeTexture(width, mipLevels, 0, fmt_, D3DPOOL_MANAGED, &cubeTex_, NULL);
		break;
	}
}

void Thin3DDX9Texture::SetImageData(int x, int y, int z, int width, int height, int depth, int level, int stride, uint8_t *data) {
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
			tex_->LockRect(level, &rect, NULL, D3DLOCK_DISCARD);
			for (int i = 0; i < height; i++) {
				memcpy((uint8_t *)rect.pBits + rect.Pitch * i, data + stride * i, width * 4);
			}
			tex_->UnlockRect(level);
		}
		// tex_->LockRect()
		break;
	}

	default:
		// TODO
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
	Thin3DDX9Context(LPDIRECT3DDEVICE9 device);
	~Thin3DDX9Context();

	Thin3DDepthStencilState *CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, T3DComparison depthCompare);
	Thin3DBlendState *CreateBlendState(const T3DBlendStateDesc &desc) override;
	Thin3DBuffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Thin3DShaderSet *CreateShaderSet(Thin3DShader *vshader, Thin3DShader *fshader) override;
	Thin3DVertexFormat *CreateVertexFormat(const std::vector<Thin3DVertexComponent> &components, int stride) override;
	Thin3DTexture *CreateTexture(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) override;

	Thin3DShader *CreateVertexShader(const char *glsl_source, const char *hlsl_source) override;
	Thin3DShader *CreateFragmentShader(const char *glsl_source, const char *hlsl_source) override;

	// Bound state objects. Too cumbersome to add them all as parameters to Draw.
	void SetBlendState(Thin3DBlendState *state) {
		Thin3DDX9BlendState *bs = static_cast<Thin3DDX9BlendState *>(state);
		bs->Apply(device_);
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
	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal);

private:
	// void CompileShader(const char *hlsl_source);

	LPDIRECT3DDEVICE9 device_;
};

Thin3DDX9Context::Thin3DDX9Context(LPDIRECT3DDEVICE9 device) : device_(device) {
	int d3dx_ver = LoadD3DX9Dynamic();
	if (!d3dx_ver) {
		ELOG("Failed to load D3DX9!");
	}
	CreatePresets();
}

Thin3DDX9Context::~Thin3DDX9Context() {
}

Thin3DShader *Thin3DDX9Context::CreateVertexShader(const char *glsl_source, const char *hlsl_source) {
	Thin3DDX9Shader *shader = new Thin3DDX9Shader(false);
	if (shader->Compile(device_, hlsl_source, "vs_3_0")) {
		return shader;
	} else {
		delete shader;
		return NULL;
	}
}

Thin3DShader *Thin3DDX9Context::CreateFragmentShader(const char *glsl_source, const char *hlsl_source) {
	Thin3DDX9Shader *shader = new Thin3DDX9Shader(false);
	if (shader->Compile(device_, hlsl_source, "ps_3_0")) {
		return shader;
	} else {
		delete shader;
		return NULL;
	}
}

Thin3DShaderSet *Thin3DDX9Context::CreateShaderSet(Thin3DShader *vshader, Thin3DShader *fshader) {
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

Thin3DVertexFormat *Thin3DDX9Context::CreateVertexFormat(const std::vector<Thin3DVertexComponent> &components, int stride) {
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
	return bs;
}

Thin3DTexture *Thin3DDX9Context::CreateTexture(T3DTextureType type, T3DImageFormat format, int width, int height, int depth, int mipLevels) {
	Thin3DDX9Texture *tex = new Thin3DDX9Texture(device_, type, format, width, height, depth, mipLevels);
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
	case T3DVertexDataType::UNORM8x4: return D3DDECLTYPE_UBYTE4;  // D3DCOLOR?
	default: return D3DDECLTYPE_UNUSED;
	}
}

Thin3DDX9VertexFormat::Thin3DDX9VertexFormat(LPDIRECT3DDEVICE9 device, const std::vector<Thin3DVertexComponent> &components, int stride) {
	D3DVERTEXELEMENT9 *elements = new D3DVERTEXELEMENT9[components.size() + 1];
	int i;
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

	device_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	device_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	device_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
	device_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
	device_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);

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
	device_->DrawIndexedPrimitive(primToD3D9[prim], 0, 0, vertexCount, 0, vertexCount / 3);
}

void Thin3DDX9Context::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	UINT d3dMask = 0;
	if (mask & T3DClear::COLOR) d3dMask |= D3DCLEAR_TARGET;
	if (mask & T3DClear::DEPTH) d3dMask |= D3DCLEAR_ZBUFFER;
	if (mask & T3DClear::STENCIL) d3dMask |= D3DCLEAR_STENCIL;
	device_->Clear(0, NULL, d3dMask, (D3DCOLOR)colorval, depthVal, stencilVal);
}

Thin3DContext *T3DCreateDX9Context(LPDIRECT3DDEVICE9 device) {
	return new Thin3DDX9Context(device);
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
	vp.X = viewports[0].TopLeftX;
	vp.Y = viewports[0].TopLeftY;
	vp.Width = viewports[0].Width;
	vp.Height = viewports[0].Height;
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
	HRESULT hr = dyn_D3DXCompileShader(source, strlen(source), defines, includes, "main", profile, flags, &codeBuffer, &errorBuffer, &constantTable_);
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

	D3DXCONSTANTTABLE_DESC desc;
	constantTable_->GetDesc(&desc);

	//D3DXCONSTANT_DESC *descs = new D3DXCONSTANT_DESC[desc.Constants];
	//constantTable_->GetConstantDesc()
	for (int i = 0; i < desc.Constants; i++) {
		D3DXHANDLE c = constantTable_->GetConstant(NULL, i);
		D3DXCONSTANT_DESC cdesc;
		UINT count = 1;
		constantTable_->GetConstantDesc(c, &cdesc, &count);
		ILOG("%s", cdesc.Name);
	}

	codeBuffer->Release();
	return true;
}

void Thin3DDX9Shader::SetVector(LPDIRECT3DDEVICE9 device, const char *name, float *value, int n) {
	D3DXHANDLE handle = constantTable_->GetConstantByName(NULL, name);
	if (handle) {
		constantTable_->SetFloatArray(device, handle, value, n);
	}
}

void Thin3DDX9Shader::SetMatrix4x4(LPDIRECT3DDEVICE9 device, const char *name, const Matrix4x4 &value) {
	D3DXHANDLE handle = constantTable_->GetConstantByName(NULL, name);
	if (handle) {
		constantTable_->SetFloatArray(device, handle, value.getReadPtr(), 16);
	}
}
