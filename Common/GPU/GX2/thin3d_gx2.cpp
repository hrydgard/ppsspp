#include "ppsspp_config.h"

#include "Common/Profiler/Profiler.h"
#include "Common/GPU/thin3d.h"
#include "Common/System/Display.h"
#include "Common/Data/Convert/SmallDataConvert.h"
#include "Common/Data/Encoding/Utf8.h"
#include "Common/ColorConv.h"
#include <cassert>
#include <cfloat>

#include <wiiu/gx2.h>
#include <wiiu/os/memory.h>
#include <wiiu/os/debug.h>

extern "C" GX2VertexShader GX2_vsTexCol, GX2_vsCol;
extern "C" GX2PixelShader GX2_fsTexCol, GX2_fsTexCol_sw, GX2_fsCol;

namespace Draw {

static const GX2CompareFunction compareToGX2[] = { GX2_COMPARE_FUNC_NEVER, GX2_COMPARE_FUNC_LESS, GX2_COMPARE_FUNC_EQUAL, GX2_COMPARE_FUNC_LEQUAL, GX2_COMPARE_FUNC_GREATER, GX2_COMPARE_FUNC_NOT_EQUAL, GX2_COMPARE_FUNC_GEQUAL, GX2_COMPARE_FUNC_ALWAYS };

static const GX2StencilFunction stencilOpToGX2[] = {
	GX2_STENCIL_FUNCTION_KEEP, GX2_STENCIL_FUNCTION_ZERO, GX2_STENCIL_FUNCTION_REPLACE, GX2_STENCIL_FUNCTION_INCR_CLAMP, GX2_STENCIL_FUNCTION_DECR_CLAMP, GX2_STENCIL_FUNCTION_INV, GX2_STENCIL_FUNCTION_INCR_WRAP, GX2_STENCIL_FUNCTION_DECR_WRAP,
};
static GX2PrimitiveMode primToGX2[] = {
	GX2_PRIMITIVE_MODE_POINTS, GX2_PRIMITIVE_MODE_LINES, GX2_PRIMITIVE_MODE_LINE_STRIP, GX2_PRIMITIVE_MODE_TRIANGLES, GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, GX2_PRIMITIVE_MODE_INVALID,
	// Tesselation shader only
	GX2_PRIMITIVE_MODE_INVALID, // GX2_PRIMITIVE_MODE_CONTROL_POINT_PATCHLIST,   // ???
	// These are for geometry shaders only.
	GX2_PRIMITIVE_MODE_INVALID, // GX2_PRIMITIVE_MODE_LINELIST_ADJ,
	GX2_PRIMITIVE_MODE_INVALID, // GX2_PRIMITIVE_MODE_LINESTRIP_ADJ,
	GX2_PRIMITIVE_MODE_INVALID, // GX2_PRIMITIVE_MODE_TRIANGLELIST_ADJ,
	GX2_PRIMITIVE_MODE_INVALID, // GX2_PRIMITIVE_MODE_TRIANGLESTRIP_ADJ,
};

static const GX2BlendCombineMode blendOpToGX2[] = {
	GX2_BLEND_COMBINE_MODE_ADD, GX2_BLEND_COMBINE_MODE_SUB, GX2_BLEND_COMBINE_MODE_REV_SUB, GX2_BLEND_COMBINE_MODE_MIN, GX2_BLEND_COMBINE_MODE_MAX,
};

static const GX2BlendMode blendToGX2[] = {
	GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_SRC_COLOR, GX2_BLEND_MODE_INV_SRC_COLOR, GX2_BLEND_MODE_DST_COLOR, GX2_BLEND_MODE_INV_DST_COLOR, GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_MODE_DST_ALPHA, GX2_BLEND_MODE_INV_DST_ALPHA, GX2_BLEND_MODE_BLEND_FACTOR, GX2_BLEND_MODE_INV_BLEND_FACTOR, GX2_BLEND_MODE_BLEND_FACTOR, GX2_BLEND_MODE_INV_BLEND_FACTOR, GX2_BLEND_MODE_SRC1_COLOR, GX2_BLEND_MODE_INV_SRC1_COLOR, GX2_BLEND_MODE_SRC1_ALPHA, GX2_BLEND_MODE_INV_SRC1_ALPHA,
};

static const GX2LogicOp logicOpToGX2[] = {
	GX2_LOGIC_OP_CLEAR, GX2_LOGIC_OP_SET, GX2_LOGIC_OP_COPY, GX2_LOGIC_OP_INV_COPY, GX2_LOGIC_OP_NOP, GX2_LOGIC_OP_INV, GX2_LOGIC_OP_AND, GX2_LOGIC_OP_NOT_AND, GX2_LOGIC_OP_OR, GX2_LOGIC_OP_NOR, GX2_LOGIC_OP_XOR, GX2_LOGIC_OP_EQUIV, GX2_LOGIC_OP_REV_AND, GX2_LOGIC_OP_INV_AND, GX2_LOGIC_OP_REV_OR, GX2_LOGIC_OP_INV_OR,
};

static const GX2TexClampMode taddrToGX2[] = {
	GX2_TEX_CLAMP_MODE_WRAP,
	GX2_TEX_CLAMP_MODE_MIRROR,
	GX2_TEX_CLAMP_MODE_CLAMP,
	GX2_TEX_CLAMP_MODE_CLAMP_BORDER,
};
static GX2SurfaceFormat dataFormatToGX2SurfaceFormat(DataFormat format) {
	switch (format) {
	case DataFormat::R32_FLOAT: return GX2_SURFACE_FORMAT_FLOAT_R32;
	case DataFormat::R32G32_FLOAT:
		return GX2_SURFACE_FORMAT_FLOAT_R32_G32;
		//	case DataFormat::R32G32B32_FLOAT:
		//		return GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32;
	case DataFormat::R32G32B32A32_FLOAT: return GX2_SURFACE_FORMAT_FLOAT_R32_G32_B32_A32;
	case DataFormat::A4R4G4B4_UNORM_PACK16: return GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4;
	case DataFormat::A1R5G5B5_UNORM_PACK16: return GX2_SURFACE_FORMAT_UNORM_A1_B5_G5_R5;
	case DataFormat::R5G5B5A1_UNORM_PACK16: return GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1;
	case DataFormat::R5G6B5_UNORM_PACK16: return GX2_SURFACE_FORMAT_UNORM_R5_G6_B5;
	case DataFormat::R8G8B8_UNORM:
	case DataFormat::R8G8B8A8_UNORM: return GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
	case DataFormat::R8G8B8A8_UNORM_SRGB: return GX2_SURFACE_FORMAT_SRGB_R8_G8_B8_A8;
	case DataFormat::R16_FLOAT: return GX2_SURFACE_FORMAT_FLOAT_R16;
	case DataFormat::R16G16_FLOAT: return GX2_SURFACE_FORMAT_FLOAT_R16_G16;
	case DataFormat::R16G16B16A16_FLOAT: return GX2_SURFACE_FORMAT_FLOAT_R16_G16_B16_A16;

	case DataFormat::D16: return GX2_SURFACE_FORMAT_UNORM_D16;
	case DataFormat::D24_S8: return GX2_SURFACE_FORMAT_UNORM_D24_S8;
	case DataFormat::S8: return GX2_SURFACE_FORMAT_INVALID;
	case DataFormat::D32F: return GX2_SURFACE_FORMAT_FLOAT_D32;
	case DataFormat::D32F_S8: return GX2_SURFACE_FORMAT_FLOAT_D32_UINT_S8_X24;

	case DataFormat::ETC1:
	default: return GX2_SURFACE_FORMAT_INVALID;
	}
}

static u32 dataFormatToGX2SurfaceCompSelect(DataFormat format) {
	switch (format) {
	case DataFormat::R16_FLOAT:
	case DataFormat::R32_FLOAT: return GX2_COMP_SEL(_r, _0, _0, _1);
	case DataFormat::R16G16_FLOAT:
	case DataFormat::R32G32_FLOAT: return GX2_COMP_SEL(_r, _g, _0, _1);
	case DataFormat::R8G8B8_UNORM:
	case DataFormat::R5G6B5_UNORM_PACK16: return GX2_COMP_SEL(_r, _g, _b, _1);
	case DataFormat::B8G8R8A8_UNORM:
	case DataFormat::B8G8R8A8_UNORM_SRGB: return GX2_COMP_SEL(_b, _g, _r, _a);
	default: return GX2_COMP_SEL(_r, _g, _b, _a);
	}
}

static int dataFormatToSwapSize(DataFormat format) {
	switch (format) {
	case DataFormat::A4R4G4B4_UNORM_PACK16:
	case DataFormat::B4G4R4A4_UNORM_PACK16:
	case DataFormat::R4G4B4A4_UNORM_PACK16:
	case DataFormat::R5G6B5_UNORM_PACK16:
	case DataFormat::B5G6R5_UNORM_PACK16:
	case DataFormat::R5G5B5A1_UNORM_PACK16:
	case DataFormat::B5G5R5A1_UNORM_PACK16:
	case DataFormat::A1R5G5B5_UNORM_PACK16:
	case DataFormat::R16_FLOAT:
	case DataFormat::D16:
	case DataFormat::R16G16_FLOAT:
	case DataFormat::R16G16B16A16_FLOAT: return 2;
	default: return 4;
	}
}

static GX2AttribFormat dataFormatToGX2AttribFormat(DataFormat format) {
	switch (format) {
	case DataFormat::R8_UNORM: return GX2_ATTRIB_FORMAT_UNORM_8;
	case DataFormat::R8G8_UNORM: return GX2_ATTRIB_FORMAT_UNORM_8_8;
	case DataFormat::B8G8R8A8_UNORM:
	case DataFormat::R8G8B8A8_UNORM: return GX2_ATTRIB_FORMAT_UNORM_8_8_8_8;
	case DataFormat::R8G8B8A8_UINT: return GX2_ATTRIB_FORMAT_UINT_8_8_8_8;
	case DataFormat::R8G8B8A8_SNORM: return GX2_ATTRIB_FORMAT_SNORM_8_8_8_8;
	case DataFormat::R8G8B8A8_SINT: return GX2_ATTRIB_FORMAT_SINT_8_8_8_8;
	case DataFormat::R32_FLOAT: return GX2_ATTRIB_FORMAT_FLOAT_32;
	case DataFormat::R32G32_FLOAT: return GX2_ATTRIB_FORMAT_FLOAT_32_32;
	case DataFormat::R32G32B32_FLOAT: return GX2_ATTRIB_FORMAT_FLOAT_32_32_32;
	case DataFormat::R32G32B32A32_FLOAT: return GX2_ATTRIB_FORMAT_FLOAT_32_32_32_32;

	default: return (GX2AttribFormat)-1;
	}
}
static u32 dataFormatToGX2AttribCompSelect(DataFormat format) {
	switch (format) {
	case DataFormat::R8_UNORM:
	case DataFormat::R32_FLOAT: return GX2_COMP_SEL(_x, _0, _0, _1);
	case DataFormat::R8G8_UNORM:
	case DataFormat::R32G32_FLOAT: return GX2_COMP_SEL(_x, _y, _0, _1);
	case DataFormat::R32G32B32_FLOAT: return GX2_COMP_SEL(_x, _y, _z, _1);
	case DataFormat::R8G8B8A8_UNORM_SRGB:
	case DataFormat::B8G8R8A8_UNORM:
	case DataFormat::B8G8R8A8_UNORM_SRGB: return GX2_COMP_SEL(_b, _g, _r, _a);
	case DataFormat::R8G8B8A8_UNORM:
	case DataFormat::R8G8B8A8_SNORM:
	case DataFormat::R8G8B8A8_UINT:
	case DataFormat::R8G8B8A8_SINT: return GX2_COMP_SEL(_a, _b, _g, _r);
	default: return GX2_COMP_SEL(_x, _y, _z, _w);
	}
}

class GX2VertexShaderModule : public ShaderModule {
public:
	GX2VertexShaderModule(GX2VertexShader *shader) : shader_(shader) {}
	~GX2VertexShaderModule() {
		if (shader_->gx2rBuffer.flags & GX2R_RESOURCE_LOCKED_READ_ONLY)
			return;
		free(shader_);
	}
	ShaderStage GetStage() const { return ShaderStage::VERTEX; }

	GX2VertexShader *shader_;
};

class GX2PixelShaderModule : public ShaderModule {
public:
	GX2PixelShaderModule(GX2PixelShader *shader) : shader_(shader) {}
	~GX2PixelShaderModule() {
		if (shader_->gx2rBuffer.flags & GX2R_RESOURCE_LOCKED_READ_ONLY)
			return;
		free(shader_);
	}
	ShaderStage GetStage() const { return ShaderStage::FRAGMENT; }

	GX2PixelShader *shader_;
};

class GX2GeometryShaderModule : public ShaderModule {
public:
	GX2GeometryShaderModule(GX2GeometryShader *shader) : shader_(shader) {}
	~GX2GeometryShaderModule() {
		if (shader_->gx2rBuffer.flags & GX2R_RESOURCE_LOCKED_READ_ONLY)
			return;
		free(shader_);
	}
	ShaderStage GetStage() const { return ShaderStage::GEOMETRY; }

	GX2GeometryShader *shader_;
};

class GX2Buffer : public Buffer {
public:
	GX2Buffer(size_t size, uint32_t usageFlags) : size_(size) {
		int align;
		switch (usageFlags & 0xF) {
		case VERTEXDATA:
			align = GX2_VERTEX_BUFFER_ALIGNMENT;
			invMode_ = GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER;
			break;
		case INDEXDATA:
			align = GX2_INDEX_BUFFER_ALIGNMENT;
			invMode_ = GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER;
			break;
		case UNIFORM:
			needswap = true;
			size_ = (size_ + 0x3F) & ~0x3F;
			/* fallthrough */
		default:
		case GENERIC: align = GX2_UNIFORM_BLOCK_ALIGNMENT; invMode_ = GX2_INVALIDATE_MODE_CPU_UNIFORM_BLOCK;
		}
		data_ = (u8 *)MEM2_alloc(size_, align);
	}
	~GX2Buffer() { MEM2_free(data_); }

	size_t size_;
	u8 *data_;
	GX2InvalidateMode invMode_;
	bool needswap = false;
};

class GX2DepthStencilState : public DepthStencilState {
public:
	GX2DepthStencilState(const DepthStencilStateDesc &desc) { GX2InitDepthStencilControlReg(&reg_, desc.depthTestEnabled, desc.depthWriteEnabled, compareToGX2[(int)desc.depthCompare], desc.stencilEnabled, desc.stencilEnabled, compareToGX2[(int)desc.front.compareOp], stencilOpToGX2[(int)desc.front.passOp], stencilOpToGX2[(int)desc.front.depthFailOp], stencilOpToGX2[(int)desc.front.failOp], compareToGX2[(int)desc.back.compareOp], stencilOpToGX2[(int)desc.back.passOp], stencilOpToGX2[(int)desc.back.depthFailOp], stencilOpToGX2[(int)desc.back.failOp]); }
	~GX2DepthStencilState() {}
	GX2DepthStencilControlReg reg_;
};

class GX2BlendState : public BlendState {
public:
	GX2BlendState(const BlendStateDesc &desc) {
		GX2InitBlendControlReg(&reg, GX2_RENDER_TARGET_0, blendToGX2[(int)desc.srcCol], blendToGX2[(int)desc.dstCol], blendOpToGX2[(int)desc.eqCol], (int)desc.srcAlpha && (int)desc.dstAlpha, blendToGX2[(int)desc.srcAlpha], blendToGX2[(int)desc.dstAlpha], blendOpToGX2[(int)desc.eqAlpha]);
		GX2InitColorControlReg(&color_reg, desc.logicEnabled ? logicOpToGX2[(int)desc.logicOp] : GX2_LOGIC_OP_COPY, desc.enabled ? 0xFF : 0x00, false, desc.colorMask != 0);
		GX2InitTargetChannelMasksReg(&mask_reg, (GX2ChannelMask)desc.colorMask, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0, (GX2ChannelMask)0);
		logicEnabled = desc.logicEnabled;
	}
	~GX2BlendState() {}
	GX2BlendControlReg reg;
	GX2ColorControlReg color_reg;
	GX2TargetChannelMaskReg mask_reg;
	bool logicEnabled;
};

class GX2RasterState : public RasterState {
public:
	GX2RasterState(const RasterStateDesc &desc) {
		frontFace_ = desc.frontFace == Facing::CW ? GX2_FRONT_FACE_CW : GX2_FRONT_FACE_CCW;
		cullFront_ = desc.cull == CullMode::FRONT || desc.cull == CullMode::FRONT_AND_BACK;
		cullBack_ = desc.cull == CullMode::BACK || desc.cull == CullMode::FRONT_AND_BACK;
	}
	~GX2RasterState() {}
	GX2FrontFace frontFace_;
	BOOL cullFront_;
	BOOL cullBack_;
};

class GX2SamplerState : public SamplerState {
public:
	GX2SamplerState(const SamplerStateDesc &desc) {
		static const GX2TexBorderType borderColorToGX2[] = {
			GX2_TEX_BORDER_TYPE_TRANSPARENT_BLACK,
			GX2_TEX_BORDER_TYPE_TRANSPARENT_BLACK,
			GX2_TEX_BORDER_TYPE_BLACK,
			GX2_TEX_BORDER_TYPE_WHITE,
		};

		GX2InitSampler(&sampler_, taddrToGX2[(int)desc.wrapU], (GX2TexXYFilterMode)desc.magFilter);
		GX2InitSamplerBorderType(&sampler_, borderColorToGX2[(int)desc.borderColor]);
		GX2InitSamplerClamping(&sampler_, taddrToGX2[(int)desc.wrapU], taddrToGX2[(int)desc.wrapV], taddrToGX2[(int)desc.wrapW]);
		if (desc.shadowCompareEnabled)
			GX2InitSamplerDepthCompare(&sampler_, compareToGX2[(int)desc.shadowCompareFunc]);
		GX2InitSamplerLOD(&sampler_, 0.0f, desc.maxLod, 0.0f);
		GX2InitSamplerXYFilter(&sampler_, (GX2TexXYFilterMode)desc.magFilter, (GX2TexXYFilterMode)desc.minFilter, GX2_TEX_ANISO_RATIO_NONE);
		GX2InitSamplerZMFilter(&sampler_, (GX2TexZFilterMode)((int)desc.mipFilter + 1), (GX2TexMipFilterMode)((int)desc.mipFilter + 1));
	}
	~GX2SamplerState() {}
	GX2Sampler sampler_ = {};
};

class GX2InputLayout : public InputLayout {
public:
	GX2InputLayout(const InputLayoutDesc &desc) {
		for (size_t i = 0; i < desc.attributes.size(); i++) {
			GX2AttribStream el;
			el.location = desc.attributes[i].location;
			el.buffer = desc.attributes[i].binding;
			el.offset = desc.attributes[i].offset;
			el.format = dataFormatToGX2AttribFormat(desc.attributes[i].format);
			el.type = desc.bindings[desc.attributes[i].binding].instanceRate ? GX2_ATTRIB_INDEX_PER_INSTANCE : GX2_ATTRIB_INDEX_PER_VERTEX;
			el.aluDivisor = 0;
			el.mask = dataFormatToGX2AttribCompSelect(desc.attributes[i].format);
			el.endianSwap = GX2_ENDIAN_SWAP_DEFAULT;
			attribute_stream.push_back(el);
		}
		for (size_t i = 0; i < desc.bindings.size(); i++) {
			strides.push_back(desc.bindings[i].stride);
		}
		fs.size = GX2CalcFetchShaderSizeEx(desc.attributes.size(), GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
		fs.program = (u8 *)MEM2_alloc(fs.size, GX2_SHADER_ALIGNMENT);
		GX2InitFetchShaderEx(&fs, fs.program, desc.attributes.size(), attribute_stream.data(), GX2_FETCH_SHADER_TESSELLATION_NONE, GX2_TESSELLATION_MODE_DISCRETE);
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_SHADER, fs.program, fs.size);
	}
	~GX2InputLayout() { MEM2_free(fs.program); }
	std::vector<GX2AttribStream> attribute_stream;
	std::vector<int> strides;
	GX2FetchShader fs = {};
};

class GX2Pipeline : public Pipeline {
public:
	GX2Pipeline(const PipelineDesc &desc) {
		prim_ = primToGX2[(int)desc.prim];
		inputLayout_ = (GX2InputLayout *)desc.inputLayout;
		inputLayout_->AddRef();
		depthStencil_ = (GX2DepthStencilState *)desc.depthStencil;
		depthStencil_->AddRef();
		blend_ = (GX2BlendState *)desc.blend;
		blend_->AddRef();
		raster_ = (GX2RasterState *)desc.raster;
		raster_->AddRef();

		for (ShaderModule *shader : desc.shaders) {
			switch (shader->GetStage()) {
			case ShaderStage::VERTEX:
				vs_ = (GX2VertexShaderModule *)shader;
				vs_->AddRef();
				break;
			case ShaderStage::FRAGMENT:
				ps_ = (GX2PixelShaderModule *)shader;
				ps_->AddRef();
				break;
			case ShaderStage::GEOMETRY:
				gs_ = (GX2GeometryShaderModule *)shader;
				gs_->AddRef();
				break;
			}
		}
		if (desc.uniformDesc->uniformBufferSize)
			ubo = new GX2Buffer(desc.uniformDesc->uniformBufferSize, BufferUsageFlag::DYNAMIC | BufferUsageFlag::UNIFORM);
	}
	~GX2Pipeline() {
		inputLayout_->Release();
		depthStencil_->Release();
		blend_->Release();
		raster_->Release();
		if (vs_)
			vs_->Release();
		if (ps_)
			ps_->Release();
		if (gs_)
			gs_->Release();
		if (ubo)
			ubo->Release();
	}
	bool RequiresBuffer() { return true; }
	GX2PrimitiveMode prim_;
	GX2VertexShaderModule *vs_ = nullptr;
	GX2PixelShaderModule *ps_ = nullptr;
	GX2GeometryShaderModule *gs_ = nullptr;
	GX2InputLayout *inputLayout_;
	GX2DepthStencilState *depthStencil_;
	GX2BlendState *blend_;
	GX2RasterState *raster_;
	GX2Buffer *ubo = nullptr;
};

class GX2TextureObject : public Texture {
public:
	GX2TextureObject(const TextureDesc &desc) {
		_assert_(desc.initData.size());
		_assert_(desc.initData[0]);

		tex.surface.width = desc.width;
		tex.surface.height = desc.height;
		tex.surface.depth = 1;
		tex.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
		tex.surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
		tex.surface.use = GX2_SURFACE_USE_TEXTURE;
		tex.viewNumSlices = 1;

		tex.surface.format = dataFormatToGX2SurfaceFormat(desc.format);
		tex.compMap = dataFormatToGX2SurfaceCompSelect(desc.format);

		GX2CalcSurfaceSizeAndAlignment(&tex.surface);
		GX2InitTextureRegs(&tex);
		width_ = tex.surface.width;
		height_ = tex.surface.height;
		depth_ = tex.surface.depth;

		tex.surface.image = MEM2_alloc(tex.surface.imageSize, tex.surface.alignment);
		_assert_(tex.surface.image);
		memset(tex.surface.image, 0xFF, tex.surface.imageSize);
		if (desc.initDataCallback) {
			desc.initDataCallback((u8 *)tex.surface.image, desc.initData[0], width_, height_, depth_, tex.surface.pitch * DataFormatSizeInBytes(desc.format), 1);
		} else {
			const u8 *src = desc.initData[0];
			u8 *dst = (u8 *)tex.surface.image;
			for (int i = 0; i < desc.height; i++) {
				memcpy(dst, src, desc.width * DataFormatSizeInBytes(desc.format));
				dst += tex.surface.pitch * DataFormatSizeInBytes(desc.format);
				src += desc.width * DataFormatSizeInBytes(desc.format);
			}
		}
#if 0
		DEBUG_STR(desc.tag);
		DEBUG_VAR(desc.format);
		DEBUG_VAR(desc.width);
		DEBUG_VAR(desc.height);
		DEBUG_VAR(desc.type);
		DEBUG_VAR(tex.compMap);
		DEBUG_VAR(tex.surface.dim);
		DEBUG_VAR(tex.surface.format);
#endif
		GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, tex.surface.image, tex.surface.imageSize);
	}
	~GX2TextureObject() { MEM2_free(tex.surface.image); }
	GX2Texture tex = {};
};

class GX2Framebuffer : public Framebuffer {
public:
	GX2Framebuffer(const FramebufferDesc &desc) {
		_assert_(desc.numColorAttachments == 1);
		_assert_(desc.depth == 1);
		tag = desc.tag;
		colorBuffer.surface.width = desc.width;
		colorBuffer.surface.height = desc.height;
		colorBuffer.surface.depth = 1;
		colorBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
		colorBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
		colorBuffer.surface.use = (GX2SurfaceUse)(GX2_SURFACE_USE_COLOR_BUFFER | GX2_SURFACE_USE_TEXTURE);
		colorBuffer.viewNumSlices = 1;
		switch (desc.colorDepth) {
		case FBO_565: colorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R5_G6_B5; break;
		case FBO_4444: colorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R4_G4_B4_A4; break;
		case FBO_5551: colorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R5_G5_B5_A1; break;
		default:
		case FBO_8888: colorBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8; break;
		}
		GX2CalcSurfaceSizeAndAlignment(&colorBuffer.surface);
		GX2InitColorBufferRegs(&colorBuffer);

		colorBuffer.surface.image = MEM1_alloc(colorBuffer.surface.imageSize, colorBuffer.surface.alignment);
		if(colorBuffer.surface.image)
			colorBuffer.surface.use = (GX2SurfaceUse)(colorBuffer.surface.use | GX2R_RESOURCE_USAGE_FORCE_MEM1);
		else
		{
			colorBuffer.surface.image = MEM2_alloc(colorBuffer.surface.imageSize, colorBuffer.surface.alignment);
			_assert_(colorBuffer.surface.image);
			colorBuffer.surface.use = (GX2SurfaceUse)(colorBuffer.surface.use | GX2R_RESOURCE_USAGE_FORCE_MEM2);
		}
		GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, colorBuffer.surface.image, colorBuffer.surface.imageSize);

		colorTexture.surface = colorBuffer.surface;
		colorTexture.compMap = desc.colorDepth == FBO_565 ? GX2_COMP_SEL(_r, _g, _b, _1) : GX2_COMP_SEL(_r, _g, _b, _a);
		colorTexture.viewNumSlices = 1;
		GX2InitTextureRegs(&colorTexture);

		if (desc.z_stencil) {
			depthBuffer.surface.width = desc.width;
			depthBuffer.surface.height = desc.height;
			depthBuffer.surface.depth = 1;
			depthBuffer.surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
			depthBuffer.surface.tileMode = GX2_TILE_MODE_DEFAULT;
			depthBuffer.surface.use = (GX2SurfaceUse)(GX2_SURFACE_USE_DEPTH_BUFFER | GX2_SURFACE_USE_TEXTURE);
			depthBuffer.viewNumSlices = 1;
			depthBuffer.surface.format = GX2_SURFACE_FORMAT_UNORM_D24_S8;
			GX2CalcSurfaceSizeAndAlignment(&depthBuffer.surface);
			GX2InitDepthBufferRegs(&depthBuffer);

			depthBuffer.surface.image = MEM1_alloc(depthBuffer.surface.imageSize, depthBuffer.surface.alignment);
			if(depthBuffer.surface.image)
				depthBuffer.surface.use = (GX2SurfaceUse)(depthBuffer.surface.use | GX2R_RESOURCE_USAGE_FORCE_MEM1);
			else
			{
				depthBuffer.surface.image = MEM2_alloc(depthBuffer.surface.imageSize, depthBuffer.surface.alignment);
				_assert_(depthBuffer.surface.image);
				depthBuffer.surface.use = (GX2SurfaceUse)(depthBuffer.surface.use | GX2R_RESOURCE_USAGE_FORCE_MEM2);
			}
			GX2Invalidate(GX2_INVALIDATE_MODE_DEPTH_BUFFER, depthBuffer.surface.image, depthBuffer.surface.imageSize);

			depthTexture.surface = depthBuffer.surface;
			depthTexture.compMap = GX2_COMP_SEL(_x, _y, _0, _0);
			depthTexture.viewNumSlices = 1;
			GX2InitTextureRegs(&depthTexture);
		}
#if 0
		DEBUG_STR(desc.tag);
		DEBUG_VAR2(desc.width);
		DEBUG_VAR2(desc.height);
		DEBUG_VAR2(desc.colorDepth);
		DEBUG_VAR2(colorBuffer.surface.imageSize);
		DEBUG_VAR2(depthBuffer.surface.imageSize);
		DEBUG_VAR2(MEM1_avail());
		DEBUG_VAR2(MEM2_avail());
#endif

	}
	~GX2Framebuffer() {
		if (colorBuffer.surface.use & GX2R_RESOURCE_USAGE_FORCE_MEM1)
			MEM1_free(colorBuffer.surface.image);
		else
			MEM2_free(colorBuffer.surface.image);

		if (depthBuffer.surface.use & GX2R_RESOURCE_USAGE_FORCE_MEM1)
			MEM1_free(depthBuffer.surface.image);
		else
			MEM2_free(depthBuffer.surface.image);
#if 0
		DEBUG_STR(tag.c_str());
		DEBUG_VAR2(MEM1_avail());
		DEBUG_VAR2(MEM2_avail());
#endif
	}
	GX2ColorBuffer colorBuffer = {};
	GX2DepthBuffer depthBuffer = {};
	GX2Texture colorTexture = {};
	GX2Texture depthTexture = {};
	std::string tag;
};

static GX2VertexShaderModule vsCol(&GX2_vsCol);
static GX2VertexShaderModule vsTexCol(&GX2_vsTexCol);
static GX2PixelShaderModule fsCol(&GX2_fsCol);
static GX2PixelShaderModule fsTexCol(&GX2_fsTexCol);
static GX2PixelShaderModule fsTexCol_sw(&GX2_fsTexCol_sw);

class GX2DrawContext : public DrawContext {
public:
	GX2DrawContext(GX2ContextState *context_state, GX2ColorBuffer *color_buffer, GX2DepthBuffer *depth_buffer);
	~GX2DrawContext();
	bool CreatePresets() override {
		vsPresets_[VS_TEXTURE_COLOR_2D] = &vsTexCol;
		vsPresets_[VS_TEXTURE_COLOR_2D]->AddRef();
		vsPresets_[VS_COLOR_2D] = &vsCol;
		vsPresets_[VS_COLOR_2D]->AddRef();

		fsPresets_[FS_TEXTURE_COLOR_2D] = &fsTexCol;
		fsPresets_[FS_TEXTURE_COLOR_2D]->AddRef();
		fsPresets_[FS_TEXTURE_COLOR_2D_RB_SWIZZLE] = &fsTexCol_sw;
		fsPresets_[FS_TEXTURE_COLOR_2D_RB_SWIZZLE]->AddRef();
		fsPresets_[FS_COLOR_2D] = &fsCol;
		fsPresets_[FS_COLOR_2D]->AddRef();

		static_assert(VS_MAX_PRESET == 2 && FS_MAX_PRESET == 3, "");
		return true;
	}

	const DeviceCaps &GetDeviceCaps() const override { return caps_; }
	uint32_t GetSupportedShaderLanguages() const override { return 0; }
	uint32_t GetDataFormatSupport(DataFormat fmt) const override;

	InputLayout *CreateInputLayout(const InputLayoutDesc &desc) override { return new GX2InputLayout(desc); }
	DepthStencilState *CreateDepthStencilState(const DepthStencilStateDesc &desc) override { return new GX2DepthStencilState(desc); }
	BlendState *CreateBlendState(const BlendStateDesc &desc) override { return new GX2BlendState(desc); }
	SamplerState *CreateSamplerState(const SamplerStateDesc &desc) override { return new GX2SamplerState(desc); }
	RasterState *CreateRasterState(const RasterStateDesc &desc) override { return new GX2RasterState(desc); }
	Buffer *CreateBuffer(size_t size, uint32_t usageFlags) override { return new GX2Buffer(size, usageFlags); }
	Pipeline *CreateGraphicsPipeline(const PipelineDesc &desc) override { return new GX2Pipeline(desc); }
	Texture *CreateTexture(const TextureDesc &desc) override { return new GX2TextureObject(desc); }
	ShaderModule *CreateShaderModule(ShaderStage stage, ShaderLanguage language, const u8 *data, size_t dataSize, const std::string &tag) override 	{
		ERROR_LOG(G3D, "missing shader for %s: ", tag.c_str());
		Crash();
		return nullptr;
	}
	Framebuffer *CreateFramebuffer(const FramebufferDesc &desc) override { return new GX2Framebuffer(desc); }

	void UpdateBuffer(Buffer *buffer, const u8 *data, size_t offset, size_t size, UpdateBufferFlags flags) override;

	void CopyFramebufferImage(Framebuffer *src, int level, int x, int y, int z, Framebuffer *dst, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBits, const char *tag) override;
	bool BlitFramebuffer(Framebuffer *src, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dst, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter, const char *tag) override;
	bool CopyFramebufferToMemorySync(Framebuffer *src, int channelBits, int x, int y, int w, int h, Draw::DataFormat format, void *pixels, int pixelStride, const char *tag) override;

	// These functions should be self explanatory.
	void BindFramebufferAsRenderTarget(Framebuffer *fbo, const RenderPassInfo &rp, const char *tag) override;
	// color must be 0, for now.
	void BindFramebufferAsTexture(Framebuffer *fbo, int binding, FBChannel channelBit, int attachment) override;

	uintptr_t GetFramebufferAPITexture(Framebuffer *fbo, int channelBit, int attachment) override;

	void GetFramebufferDimensions(Framebuffer *fbo, int *w, int *h) override;

	void BindTextures(int start, int count, Texture **textures) override;
	void BindSamplerStates(int start, int count, SamplerState **states) override;
	void BindVertexBuffers(int start, int count, Buffer **buffers, int *offsets) override;
	void BindIndexBuffer(Buffer *indexBuffer, int offset) override;
	void BindPipeline(Pipeline *pipeline) override;

	void UpdateDynamicUniformBuffer(const void *ub, size_t size) override;

	// Raster state
	void SetScissorRect(int left, int top, int width, int height) override { GX2SetScissor(left, top, width, height); }
	void SetViewports(int count, Viewport *viewports) override {
		assert(count == 1);
		GX2SetViewport(viewports->TopLeftX, viewports->TopLeftY, viewports->Width, viewports->Height, viewports->MinDepth, viewports->MaxDepth);
		// needed to prevent overwriting memory outside the rendertarget;
		// TODO: check and set this during draw calls instead.
		GX2SetScissor(0, 0, current_color_buffer_->surface.width, current_color_buffer_->surface.height);
	}
	void SetBlendFactor(float color[4]) override {
		DEBUG_LINE();
		GX2SetBlendConstantColorReg((GX2BlendConstantColorReg *)color);
	}
	void SetStencilRef(uint8_t ref) override { /*TODO*/ }

	void InvalidateCachedState() override {
		if (pipeline_)
			pipeline_->Release();
		pipeline_ = nullptr;
		indexBuffer_ = nullptr;
		GX2SetContextState(context_state_);
		GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
	}

	void Draw(int vertexCount, int offset) override;
	void DrawIndexed(int vertexCount, int offset) override;
	void DrawUP(const void *vdata, int vertexCount) override;
	void Clear(int mask, uint32_t colorval, float depthVal, int stencilVal);

	void BeginFrame() override;
	void EndFrame() override { /*TODO*/ }

	std::string GetInfoString(InfoField info) const override {
		switch (info) {
		case APIVERSION: return "1";
		case VENDORSTRING: return "AMD";
		case VENDOR: return "";
		case DRIVER: return "-";
		case SHADELANGVERSION: return "AMD R700 microcode";
		case APINAME: return "GX2";
		default: return "?";
		}
	}

	uint64_t GetNativeObject(NativeObject obj) override {
		switch (obj) {
		case NativeObject::CONTEXT: return (uintptr_t)context_state_;
		case NativeObject::BACKBUFFER_COLOR_VIEW: return current_color_buffer_? (uintptr_t)current_color_buffer_ : (uintptr_t)color_buffer_;
		case NativeObject::CONTEXT_EX:
		case NativeObject::DEVICE:
		case NativeObject::DEVICE_EX:
		case NativeObject::BACKBUFFER_DEPTH_VIEW:
		case NativeObject::BACKBUFFER_COLOR_TEX:
		case NativeObject::BACKBUFFER_DEPTH_TEX:
		case NativeObject::FEATURE_LEVEL:
		case NativeObject::COMPATIBLE_RENDERPASS:
		case NativeObject::BACKBUFFER_RENDERPASS:
		case NativeObject::FRAMEBUFFER_RENDERPASS:
		case NativeObject::INIT_COMMANDBUFFER:
		case NativeObject::BOUND_TEXTURE0_IMAGEVIEW:
		case NativeObject::BOUND_TEXTURE1_IMAGEVIEW:
		case NativeObject::RENDER_MANAGER:
		default:
			DEBUG_VAR(obj);
			Crash();
			return 0;
		}
	}

	void HandleEvent(Event ev, int width, int height, void *param1, void *param2) override;

	virtual int GetCurrentStepId() const override { return 0; /*TODO*/ }

private:
	void ApplyCurrentState();

	DeviceCaps caps_ = {};
	GX2ContextState *context_state_;
	GX2ColorBuffer *color_buffer_;
	GX2DepthBuffer *depth_buffer_;
	GX2ColorBuffer *current_color_buffer_;
	GX2DepthBuffer *current_depth_buffer_;
	GX2Pipeline *pipeline_ = nullptr;
	void *indexBuffer_ = nullptr;
};

GX2DrawContext::GX2DrawContext(GX2ContextState *context_state, GX2ColorBuffer *color_buffer, GX2DepthBuffer *depth_buffer) : context_state_(context_state), color_buffer_(color_buffer), depth_buffer_(depth_buffer), current_color_buffer_(color_buffer), current_depth_buffer_(depth_buffer) {
	caps_.vendor = GPUVendor::VENDOR_AMD;
	//	caps_.anisoSupported = true;
	caps_.depthRangeMinusOneToOne = false;
	caps_.geometryShaderSupported = false; // for now
	caps_.tesselationShaderSupported = false;
	caps_.multiViewport = true;
	caps_.dualSourceBlend = true;
	caps_.logicOpSupported = true;
	caps_.framebufferCopySupported = true;
	caps_.framebufferBlitSupported = true;
	caps_.framebufferDepthCopySupported = true;
	caps_.framebufferDepthBlitSupported = true;
}

GX2DrawContext::~GX2DrawContext() { BindPipeline(nullptr); }

void GX2DrawContext::HandleEvent(Event ev, int width, int height, void *param1, void *param2) {
	DEBUG_LINE();
	switch (ev) {
	case Event::LOST_BACKBUFFER: {
		break;
	}
	case Event::GOT_BACKBUFFER: {
		break;
	}
	case Event::PRESENTED: break;
	}
}

void GX2DrawContext::UpdateDynamicUniformBuffer(const void *ub, size_t size) {
	if (!pipeline_) {
		ERROR_LOG(G3D,  "GX2DrawContext::UpdateDynamicUniformBuffer called without an active pipeline.");
	}
	if (!pipeline_->ubo || pipeline_->ubo->size_ < size) {
		Crash();
	}
	u32 *src = (u32 *)ub;
	u32 *dst = (u32 *)pipeline_->ubo->data_;
	int count = size >> 2;
	while (count--) {
		*dst++ = __builtin_bswap32(*src++);
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_CPU_UNIFORM_BLOCK, pipeline_->ubo->data_, size);
}

void GX2DrawContext::BindPipeline(Pipeline *pipeline) {
	if (pipeline_)
		pipeline_->Release();
	pipeline_ = (GX2Pipeline *)pipeline;
	if (pipeline_ && pipeline_->vs_ && pipeline_->inputLayout_) {
		pipeline_->AddRef();
		GX2SetFetchShader(&pipeline_->inputLayout_->fs);
		GX2SetVertexShader(pipeline_->vs_->shader_);
		if (pipeline_->ps_)
			GX2SetPixelShader(pipeline_->ps_->shader_);
		if (pipeline_->gs_) {
			GX2SetShaderMode(GX2_SHADER_MODE_GEOMETRY_SHADER);
			GX2SetGeometryShader(pipeline_->gs_->shader_);
		} else {
			GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
		}
		GX2SetBlendControlReg(&pipeline_->blend_->reg);
		GX2SetColorControlReg(&pipeline_->blend_->color_reg);
		GX2SetTargetChannelMasksReg(&pipeline_->blend_->mask_reg);
		GX2SetDepthStencilControlReg(&pipeline_->depthStencil_->reg_);
		GX2SetCullOnlyControl(pipeline_->raster_->frontFace_, pipeline_->raster_->cullFront_, pipeline_->raster_->cullBack_);
		if (pipeline_->ubo) {
			GX2SetVertexUniformBlock(1, pipeline_->ubo->size_, pipeline_->ubo->data_);
			GX2SetVertexUniformBlock(0, pipeline_->ubo->size_, pipeline_->ubo->data_);
			//			GX2SetPixelUniformBlock(0, pipeline_->ubo->size_, pipeline_->ubo->data_);
			//			GX2SetGeometryUniformBlock(0, pipeline_->ubo->size_, pipeline_->ubo->data_);
		}
	}
}

void GX2DrawContext::ApplyCurrentState() { DEBUG_LINE(); }

void GX2DrawContext::UpdateBuffer(Buffer *buffer_, const u8 *data, size_t offset, size_t size, UpdateBufferFlags flags) {
	GX2Buffer *buffer = (GX2Buffer *)buffer_;
	if (buffer->needswap && !(offset & 0x3) && !(size & 0x3)) {
		u32 *src = (u32 *)data;
		u32 *dst = (u32 *)(buffer->data_ + offset);
		int count = size >> 2;
		while (count--)
			*dst++ = __builtin_bswap32(*src++);
	} else {
		memcpy(buffer->data_ + offset, data, size);
	}
	GX2Invalidate(buffer->invMode_, buffer->data_ + offset, size);
}

void GX2DrawContext::BindVertexBuffers(int start, int count, Buffer **buffers, int *offsets) {
	if (!pipeline_) {
		ERROR_LOG(G3D,  "GX2DrawContext::BindVertexBuffers called without an active pipeline.");
	}
	if (pipeline_->inputLayout_->strides.size() > start + count) {
		ERROR_LOG(G3D,  "GX2DrawContext::BindVertexBuffers called invalid start + count.");
		return;
	}

	for (int i = start; i < start + count; i++) {
		GX2Buffer *vbo = (GX2Buffer *)buffers[i];
		u8 *data = vbo->data_;
		size_t size = vbo->size_;
		int stride = pipeline_->inputLayout_->strides[i];

		if (offsets && offsets[i] < size) {
			data += offsets[i];
			size -= offsets[i];
		}
		GX2SetAttribBuffer(i, size, stride, data);
	}
}

void GX2DrawContext::BindIndexBuffer(Buffer *indexBuffer, int offset) {
	if (!indexBuffer) {
		return;
	}
	indexBuffer_ = ((GX2Buffer *)indexBuffer)->data_ + offset;
}

void GX2DrawContext::Draw(int vertexCount, int offset) {
	if (!pipeline_) {
		ERROR_LOG(G3D,  "GX2DrawContext::Draw called without an active pipeline.");
		return;
	}
#if 0
	struct Vertex {
		float x, y, z;
		float u, v;
		uint32_t rgba;
	};
#define col 0xFFFFFFFF
	__attribute__((aligned(GX2_VERTEX_BUFFER_ALIGNMENT))) static Vertex v[4] = {
		{ 0, 0, 0, 0, 0, col },
		{ 840, 0, 0, 1, 0, col },
		{ 840, 480, 0, 1, 1, col },
		{ 0, 480, 0, 0, 1, col }
	};
	GX2SetAttribBuffer(0, sizeof(v), sizeof(*v), v);
	GX2DrawEx(GX2_PRIMITIVE_MODE_QUADS, 4, 0, 1);
#else

	GX2DrawEx(pipeline_->prim_, vertexCount, offset, 1);
	// TODO: get rid of this call, which is currently needed to prevent overwriting arribute memory during draw
	PROFILE_THIS_SCOPE("GX2DrawDone");
	GX2DrawDone();
#endif
}

void GX2DrawContext::DrawIndexed(int indexCount, int offset) {
	if (!pipeline_) {
		ERROR_LOG(G3D,  "GX2DrawContext::DrawIndexed called without an active pipeline.");
		return;
	}
	if (!indexBuffer_) {
		ERROR_LOG(G3D,  "GX2DrawContext::DrawIndexed called without an active index buffer.");
		return;
	}
	if (!indexCount)
		return;

	GX2DrawIndexedImmediateEx(pipeline_->prim_, indexCount, GX2_INDEX_TYPE_U16, indexBuffer_, offset, 1);
}

void GX2DrawContext::DrawUP(const void *vdata, int vertexCount) { DEBUG_LINE(); }

uint32_t GX2DrawContext::GetDataFormatSupport(DataFormat fmt) const {
	GX2AttribFormat afmt = dataFormatToGX2AttribFormat(fmt);
	GX2SurfaceFormat sfmt = dataFormatToGX2SurfaceFormat(fmt);
	uint32_t support = 0;

	if (afmt != (GX2AttribFormat)-1)
		support |= FMT_INPUTLAYOUT;

	if (sfmt != GX2_SURFACE_FORMAT_INVALID) {
		if (DataFormatIsDepthStencil(fmt)) {
			support |= FMT_DEPTHSTENCIL;
			if (sfmt != GX2_SURFACE_FORMAT_FLOAT_D24_S8) {
				support |= FMT_TEXTURE;
			}
		} else {
			support |= FMT_TEXTURE | FMT_RENDERTARGET;
		}
		//	support |= FMT_AUTOGEN_MIPS;
	}

	return support;
}

void GX2DrawContext::BindTextures(int start, int count, Texture **textures) {
	//	GX2DrawDone();
	while (count--) {
		GX2TextureObject *texture = (GX2TextureObject *)*textures++;
		if (texture && texture->tex.surface.image) {
			GX2SetPixelTexture(&texture->tex, start);
		}
		start++;
	}
}

void GX2DrawContext::BindSamplerStates(int start, int count, SamplerState **states) {
	while (count--) {
		GX2SamplerState *samplerState = (GX2SamplerState *)*states++;
		if (samplerState)
			GX2SetPixelSampler(&samplerState->sampler_, start++);
	}
}

void GX2DrawContext::Clear(int mask, uint32_t colorval, float depthVal, int stencilVal) {
	float f[4];
	Uint8x4ToFloat4(f, colorval);

	//	GX2DrawDone();
	int flags = (mask >> 1) & 0x3;

	if (flags && (mask & FBChannel::FB_COLOR_BIT)) {
		GX2ClearBuffersEx(current_color_buffer_, current_depth_buffer_, f[0], f[1], f[2], f[3], depthVal, stencilVal, (GX2ClearFlags)flags);
	} else if (mask & FBChannel::FB_COLOR_BIT) {
		GX2ClearColor(current_color_buffer_, f[0], f[1], f[2], f[3]);
	} else if (flags) {
		GX2ClearDepthStencilEx(current_depth_buffer_, depthVal, stencilVal, (GX2ClearFlags)flags);
	}

	GX2SetContextState(context_state_);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
}

void GX2DrawContext::BeginFrame() {}

void GX2DrawContext::CopyFramebufferImage(Framebuffer *srcfb, int level, int x, int y, int z, Framebuffer *dstfb, int dstLevel, int dstX, int dstY, int dstZ, int width, int height, int depth, int channelBit, const char *tag) {
	_assert_(level == 0 && dstLevel == 0 && z == 0 && dstZ == 0 && depth == 1);
	GX2Rect srcRegion = { x, y, x + width, y + height };
	GX2Point dstCoords = { dstX, dstY };
	GX2Surface *srcSurface, *dstSurface;
	if (channelBit == Draw::FB_COLOR_BIT) {
		srcSurface = &((GX2Framebuffer *)srcfb)->colorBuffer.surface;
		dstSurface = &((GX2Framebuffer *)dstfb)->colorBuffer.surface;
	} else {
		srcSurface = &((GX2Framebuffer *)srcfb)->depthBuffer.surface;
		dstSurface = &((GX2Framebuffer *)dstfb)->depthBuffer.surface;
	}
	GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, srcSurface->image, srcSurface->imageSize);
	GX2CopySurfaceEx(srcSurface, level, z, dstSurface, dstLevel, dstZ, 1, &srcRegion, &dstCoords);
	GX2SetContextState(context_state_);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
}

bool GX2DrawContext::BlitFramebuffer(Framebuffer *srcfb, int srcX1, int srcY1, int srcX2, int srcY2, Framebuffer *dstfb, int dstX1, int dstY1, int dstX2, int dstY2, int channelBits, FBBlitFilter filter, const char *tag) {
	// TODO
	DEBUG_LINE();
//		Crash();
	return false;
}

bool GX2DrawContext::CopyFramebufferToMemorySync(Framebuffer *src, int channelBits, int bx, int by, int bw, int bh, Draw::DataFormat format, void *pixels, int pixelStride, const char *tag) {
	_assert_(channelBits == FB_COLOR_BIT);
	PROFILE_THIS_SCOPE("fbcpy_sync");
	GX2Framebuffer *fb = (GX2Framebuffer *)src;
	GX2DrawDone();

	GX2Surface *surface;
	if (channelBits == FB_COLOR_BIT) {
		surface = fb ? &fb->colorBuffer.surface : &color_buffer_->surface;
		GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, surface->image, surface->imageSize);
		_assert_(surface->format == GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8);
	} else {
		surface = fb ? &fb->depthBuffer.surface : &depth_buffer_->surface;
		GX2Invalidate(GX2_INVALIDATE_MODE_DEPTH_BUFFER, surface->image, surface->imageSize);
	}

	if (bx >= surface->width || by >= surface->height)
		return true;

	// TODO: Figure out where the badness really comes from.
	if (bx + bw > surface->width) {
		bw = surface->width - bx;
	}

	if (by + bh > surface->height) {
		bh = surface->height - by;
	}

	switch (channelBits) {
	case FB_COLOR_BIT: {
		// Pixel size always 4 here because we always request RGBA8888.
		const u32 *src = nullptr;
		u32 handle;
		GX2AllocateTilingApertureEx(surface, 0, 0, GX2_ENDIAN_SWAP_NONE, &handle, (void **)&src);
		src += by * surface->pitch + bx;
		ConvertFromRGBA8888((u8 *)pixels, (u8 *)src, pixelStride, surface->pitch, bw, bh, format);
		GX2FreeTilingAperture(handle);
		break;
	}
	case FB_DEPTH_BIT:
		Crash(); // TODO
		for (int y = by; y < by + bh; y++) {
			float *dest = (float *)((u8 *)pixels + y * pixelStride * sizeof(float));
			const u32 *src = (u32 *)surface->image + by * surface->pitch + bx;
			for (int x = 0; x < bw; x++) {
				dest[x] = (src[x] & 0xFFFFFF) / (256.f * 256.f * 256.f);
			}
		}
		break;
	case FB_STENCIL_BIT:
		Crash(); // TODO
		for (int y = by; y < by + bh; y++) {
			u8 *destStencil = (u8 *)pixels + y * pixelStride;
			const u32 *src = (u32 *)surface->image + by * surface->pitch + bx;
			for (int x = 0; x < bw; x++) {
				destStencil[x] = src[x] >> 24;
			}
		}
		break;
	}

	return true;
}

void GX2DrawContext::BindFramebufferAsRenderTarget(Framebuffer *fbo_, const RenderPassInfo &rp, const char *tag) {
	GX2Framebuffer *fbo = (GX2Framebuffer *)fbo_;

	//	GX2DrawDone();
	if (fbo) {
		current_color_buffer_ = &fbo->colorBuffer;
		current_depth_buffer_ = &fbo->depthBuffer;
	} else {
		current_color_buffer_ = color_buffer_;
		current_depth_buffer_ = depth_buffer_;
	}

	GX2SetColorBuffer(current_color_buffer_, GX2_RENDER_TARGET_0);
	GX2SetDepthBuffer(current_depth_buffer_);
	GX2SetScissor(0, 0, current_color_buffer_->surface.width, current_color_buffer_->surface.height);
	float f[4];
	Uint8x4ToFloat4(f, rp.clearColor);
	int flags = 0;
	if (rp.depth == RPAction::CLEAR)
		flags |= (int)GX2_CLEAR_FLAGS_DEPTH;
	if (rp.stencil == RPAction::CLEAR)
		flags |= (int)GX2_CLEAR_FLAGS_STENCIL;

	if ((rp.color == RPAction::CLEAR) && flags) {
		GX2ClearBuffersEx(current_color_buffer_, current_depth_buffer_, f[0], f[1], f[2], f[3], rp.clearDepth, rp.clearStencil, (GX2ClearFlags)flags);
	} else if (rp.color == RPAction::CLEAR) {
		GX2ClearColor(current_color_buffer_, f[0], f[1], f[2], f[3]);
	} else if (flags) {
		GX2ClearDepthStencilEx(current_depth_buffer_, rp.clearDepth, rp.clearStencil, (GX2ClearFlags)flags);
	}
	GX2SetContextState(context_state_);
	GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_BLOCK);
}

void GX2DrawContext::BindFramebufferAsTexture(Framebuffer *fbo_, int binding, FBChannel channelBit, int attachment) {
	GX2Framebuffer *fbo = (GX2Framebuffer *)fbo_;
	_assert_(channelBit == FB_COLOR_BIT);
	_assert_(!attachment);

	//	GX2DrawDone();
	if (channelBit == FB_COLOR_BIT) {
		GX2Invalidate(GX2_INVALIDATE_MODE_COLOR_BUFFER, fbo->colorBuffer.surface.image, fbo->colorBuffer.surface.imageSize);
		GX2Invalidate(GX2_INVALIDATE_MODE_TEXTURE, fbo->colorTexture.surface.image, fbo->colorTexture.surface.imageSize);
		GX2SetPixelTexture(&fbo->colorTexture, binding);
	}
}

uintptr_t GX2DrawContext::GetFramebufferAPITexture(Framebuffer *fbo_, int channelBit, int attachment) {
	GX2Framebuffer *fbo = (GX2Framebuffer *)fbo_;
	_assert_(channelBit == FB_COLOR_BIT);

	//	GX2DrawDone();
	if (channelBit == FB_COLOR_BIT) {
		return (uintptr_t)&fbo->colorTexture;
	}
	return 0;
}

void GX2DrawContext::GetFramebufferDimensions(Framebuffer *fbo_, int *w, int *h) {
	GX2Framebuffer *fbo = (GX2Framebuffer *)fbo_;
	if (fbo) {
		*w = fbo->colorBuffer.surface.width;
		*h = fbo->colorBuffer.surface.height;
	} else {
		*w = color_buffer_->surface.width;
		*h = color_buffer_->surface.height;
	}
}


DrawContext *T3DCreateGX2Context(GX2ContextState *context_state, GX2ColorBuffer *color_buffer, GX2DepthBuffer *depth_buffer) { return new GX2DrawContext(context_state, color_buffer, depth_buffer); }

} // namespace Draw
