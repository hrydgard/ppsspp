#include <vector>
#include <inttypes.h>
#include <d3d9.h>
#include <d3dx9.h>

#include "base/logging.h"
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
			vbuffer_->Lock((UINT)offset, (UINT)size, &ptr, D3DLOCK_DISCARD);
			memcpy(ptr, data, size);
			vbuffer_->Unlock();
		} else if (ibuffer_) {
			void *ptr;
			ibuffer_->Lock((UINT)offset, (UINT)size, &ptr, D3DLOCK_DISCARD);
			memcpy(ptr, data, size);
			ibuffer_->Unlock();
		}
	}
	void BindVertex(LPDIRECT3DDEVICE9 device, int vertexSize, int offset = 0) {
		if (vbuffer_)
			device->SetStreamSource(0, vbuffer_, offset, vertexSize);
	}
	void BindIndex(LPDIRECT3DDEVICE9 device) {
		if (ibuffer_)
			device->SetIndices(ibuffer_);
	}

private:
	LPDIRECT3DVERTEXBUFFER9 vbuffer_;
	LPDIRECT3DINDEXBUFFER9 ibuffer_;
};


class Thin3DDX9VertexFormat : public Thin3DVertexFormat {
public:
	int GetVertexSize() { return 0; }

private:
	LPDIRECT3DVERTEXDECLARATION9 decl_;
};

class Thin3DDX9Shader : public Thin3DShader {
public:
	Thin3DDX9Shader(bool isPixelShader) : isPixelShader_(isPixelShader), vshader(NULL), pshader(NULL) {}
	~Thin3DDX9Shader() {
		if (vshader)
			vshader->Release();
		if (pshader)
			pshader->Release();
		if (constantTable)
			constantTable->Release();
	}
	bool Compile(LPDIRECT3DDEVICE9 device, const char *source, const char *profile);
	void Apply(LPDIRECT3DDEVICE9 device) {
		if (isPixelShader_) {
			device->SetPixelShader(pshader);
		} else {
			device->SetVertexShader(vshader);
		}
	}

private:
	bool isPixelShader_;
	LPDIRECT3DVERTEXSHADER9 vshader;
	LPDIRECT3DPIXELSHADER9 pshader;
	LPD3DXCONSTANTTABLE constantTable;
};

class Thin3DDX9Pipeline : public Thin3DShaderSet {
public:
	Thin3DDX9Shader *vshader;
	Thin3DDX9Shader *pshader;
	void Apply(LPDIRECT3DDEVICE9 device);

private:
};

class Thin3DDX9Context : public Thin3DContext {
public:
	Thin3DDX9Context(LPDIRECT3DDEVICE9 device);
	~Thin3DDX9Context();

	Thin3DDepthStencilState *CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, T3DComparison depthCompare);
	Thin3DBlendState *CreateBlendState(const T3DBlendStateDesc &desc) override;
	Thin3DBuffer *CreateBuffer(size_t size, uint32_t usageFlags) override;
	Thin3DShaderSet *CreateShaderSet(Thin3DShader *vshader, Thin3DShader *fshader) override;
	Thin3DVertexFormat *CreateVertexFormat(T3DVertexFormatPreset preset) override;

	Thin3DShader *CreateVertexShader(const char *glsl_source, const char *hlsl_source);
	Thin3DShader *CreateFragmentShader(const char *glsl_source, const char *hlsl_source);

	// Bound state objects. Too cumbersome to add them all as parameters to Draw.
	void SetBlendState(Thin3DBlendState *state);

	// Raster state
	void SetScissorEnabled(bool enable);
	void SetScissorRect(int left, int top, int width, int height);
	void SetViewports(int count, T3DViewport *viewports);

	void Draw(T3DPrimitive prim, Thin3DShaderSet *pipeline, Thin3DVertexFormat *format, Thin3DBuffer *vdata, int vertexCount, int offset) override;
	void DrawIndexed(T3DPrimitive prim, Thin3DShaderSet *pipeline, Thin3DVertexFormat *format, Thin3DBuffer *vdata, Thin3DBuffer *idata, int vertexCount, int offset) override;
	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal);

private:
	// void CompileShader(const char *hlsl_source);

	LPDIRECT3DDEVICE9 device_;
};

Thin3DDX9Context::Thin3DDX9Context(LPDIRECT3DDEVICE9 device) : device_(device) {
	CreatePresets();
	int d3dx_ver = LoadD3DX9Dynamic();
	if (!d3dx_ver) {
		ELOG("Failed to load D3DX9!");
	}
}

Thin3DDX9Context::~Thin3DDX9Context() {
	for (int i = 0; i < BS_MAX_PRESET; i++) {
		bsPresets_[i]->Release();
	}
	
}

Thin3DShader *Thin3DDX9Context::CreateVertexShader(const char *glsl_source, const char *hlsl_source) {
	Thin3DDX9Shader *shader = new Thin3DDX9Shader(false);
	if (shader->Compile(device_, hlsl_source, "ps_3_0")) {
		return shader;
	} else {
		delete shader;
		return NULL;
	}
}

Thin3DShader *Thin3DDX9Context::CreateFragmentShader(const char *glsl_source, const char *hlsl_source) {
	Thin3DDX9Shader *shader = new Thin3DDX9Shader(false);
	if (shader->Compile(device_, hlsl_source, "vs_3_0")) {
		return shader;
	} else {
		delete shader;
		return NULL;
	}
}

Thin3DDepthStencilState *CreateDepthStencilState(bool depthTestEnabled, bool depthWriteEnabled, T3DComparison depthCompare) {
	Thin3DDX9DepthStencilState *ds = new Thin3DDX9DepthStencilState();
	ds->depthCompare = compareToD3D9[depthCompare];
	ds->depthTestEnabled = depthTestEnabled;
	ds->depthWriteEnabled = depthWriteEnabled;
	return ds;
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

void Thin3DDX9Context::SetBlendState(Thin3DBlendState *state) {
	Thin3DDX9BlendState *bs = static_cast<Thin3DDX9BlendState *>(state);
}

Thin3DBuffer *Thin3DDX9Context::CreateBuffer(size_t size, uint32_t usageFlags) {
	return new Thin3DDX9Buffer(device_, size, usageFlags);
}

void Thin3DDX9Pipeline::Apply(LPDIRECT3DDEVICE9 device) {
	vshader->Apply(device);
	pshader->Apply(device);
}

void Thin3DDX9Context::Draw(T3DPrimitive prim, Thin3DShaderSet *pipeline, Thin3DVertexFormat *format, Thin3DBuffer *vdata, int vertexCount, int offset) {
	Thin3DDX9Buffer *vbuf = static_cast<Thin3DDX9Buffer *>(vdata);
	Thin3DDX9VertexFormat *fmt = static_cast<Thin3DDX9VertexFormat *>(format);
	vbuf->BindVertex(device_, fmt->GetVertexSize(), offset);
	device_->DrawPrimitive(primToD3D9[prim], offset, vertexCount / 3);
}

void Thin3DDX9Context::DrawIndexed(T3DPrimitive prim, Thin3DShaderSet *pipeline, Thin3DVertexFormat *format, Thin3DBuffer *vdata, Thin3DBuffer *idata, int vertexCount, int offset) {
	Thin3DDX9Buffer *vbuf = static_cast<Thin3DDX9Buffer *>(vdata);
	Thin3DDX9Buffer *ibuf = static_cast<Thin3DDX9Buffer *>(idata);
	Thin3DDX9VertexFormat *fmt = static_cast<Thin3DDX9VertexFormat *>(format);
	vbuf->BindVertex(device_, fmt->GetVertexSize(), offset);
	ibuf->BindIndex(device_);
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

bool Thin3DDX9Shader::Compile(LPDIRECT3DDEVICE9 device, const char *source, const char *profile) {
	LPD3DXMACRO defines = NULL;
	LPD3DXINCLUDE includes = NULL;
	DWORD flags = 0;
	LPD3DXBUFFER codeBuffer;
	LPD3DXBUFFER errorBuffer;
	HRESULT hr = dyn_D3DXCompileShader(source, strlen(source), defines, includes, "main", profile, flags, &codeBuffer, &errorBuffer, &constantTable);
	if (FAILED(hr)) {
		codeBuffer->Release();
		if (constantTable) 
			constantTable->Release();
		return false;
	}

	bool success = false;
	if (isPixelShader_) {
		HRESULT result = device->CreatePixelShader((DWORD *)codeBuffer->GetBufferPointer(), &pshader);
		success = SUCCEEDED(result);
	} else {
		HRESULT result = device->CreateVertexShader((DWORD *)codeBuffer->GetBufferPointer(), &vshader);
		success = SUCCEEDED(result);
	}
	codeBuffer->Release();

	// delete[] code;
	return true;
}
