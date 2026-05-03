#include <cassert>
#include <cstring>
#include <cstdint>

#include "Common/Data/Convert/ColorConv.h"
#include "Common/GPU/thin3d.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/Log.h"
#include "Common/System/Display.h"

namespace Draw {

size_t DataFormatSizeInBytes(DataFormat fmt) {
	switch (fmt) {
	case DataFormat::R8_UNORM: return 1;
	case DataFormat::R8G8_UNORM: return 2;
	case DataFormat::R8G8B8_UNORM: return 3;

	case DataFormat::R4G4_UNORM_PACK8: return 1;
	case DataFormat::R4G4B4A4_UNORM_PACK16: return 2;
	case DataFormat::B4G4R4A4_UNORM_PACK16: return 2;
	case DataFormat::A4R4G4B4_UNORM_PACK16: return 2;
	case DataFormat::R5G5B5A1_UNORM_PACK16: return 2;
	case DataFormat::B5G5R5A1_UNORM_PACK16: return 2;
	case DataFormat::R5G6B5_UNORM_PACK16: return 2;
	case DataFormat::B5G6R5_UNORM_PACK16: return 2;
	case DataFormat::A1R5G5B5_UNORM_PACK16: return 2;

	case DataFormat::R8G8B8A8_UNORM:
	case DataFormat::R8G8B8A8_UNORM_SRGB: return 4;
	case DataFormat::B8G8R8A8_UNORM:
	case DataFormat::B8G8R8A8_UNORM_SRGB: return 4;

	case DataFormat::R8G8B8A8_SNORM: return 4;
	case DataFormat::R8G8B8A8_UINT: return 4;
	case DataFormat::R8G8B8A8_SINT: return 4;

	case DataFormat::R16_UNORM: return 2;

	case DataFormat::R16_FLOAT: return 2;
	case DataFormat::R16G16_FLOAT: return 4;
	case DataFormat::R16G16B16A16_FLOAT: return 8;
	case DataFormat::R32_FLOAT: return 4;
	case DataFormat::R32G32_FLOAT: return 8;
	case DataFormat::R32G32B32_FLOAT: return 12;
	case DataFormat::R32G32B32A32_FLOAT: return 16;

	case DataFormat::S8: return 1;
	case DataFormat::D16: return 2;
	case DataFormat::D16_S8: return 3;
	case DataFormat::D24_S8: return 4;
	case DataFormat::D32F: return 4;
	// Or maybe 8...
	case DataFormat::D32F_S8: return 5;

	default:
		return 0;
	}
}

const char *DataFormatToString(DataFormat fmt) {
	switch (fmt) {
	case DataFormat::R8_UNORM: return "R8_UNORM";
	case DataFormat::R8G8_UNORM: return "R8G8_UNORM";
	case DataFormat::R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
	case DataFormat::B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
	case DataFormat::R16_UNORM: return "R16_UNORM";
	case DataFormat::R16_FLOAT: return "R16_FLOAT";
	case DataFormat::R32_FLOAT: return "R32_FLOAT";

	case DataFormat::S8: return "S8";
	case DataFormat::D16: return "D16";
	case DataFormat::D16_S8: return "D16_S8";
	case DataFormat::D24_S8: return "D24_S8";
	case DataFormat::D32F: return "D32F";
	case DataFormat::D32F_S8: return "D32F_S8";

	default:
		return "(N/A)";
	}
}

bool DataFormatIsDepthStencil(DataFormat fmt) {
	switch (fmt) {
	case DataFormat::D16:
	case DataFormat::D16_S8:
	case DataFormat::D24_S8:
	case DataFormat::S8:
	case DataFormat::D32F:
	case DataFormat::D32F_S8:
		return true;
	default:
		return false;
	}
}

// We don't bother listing the formats that are irrelevant for PPSSPP, like BC6 (HDR format)
// or weird-shaped ASTC formats. We only support 4x4 block size formats for now.
// If you pass in a blockSize parameter, it receives byte count that a 4x4 block takes in this format.
bool DataFormatIsBlockCompressed(DataFormat fmt, int *blockSize) {
	switch (fmt) {
	case DataFormat::BC1_RGBA_UNORM_BLOCK:
	case DataFormat::BC4_UNORM_BLOCK:
	case DataFormat::ETC2_R8G8B8_UNORM_BLOCK:
		if (blockSize) *blockSize = 8;  // 64 bits
		return true;
	case DataFormat::BC2_UNORM_BLOCK:
	case DataFormat::BC3_UNORM_BLOCK:
	case DataFormat::BC5_UNORM_BLOCK:
	case DataFormat::BC7_UNORM_BLOCK:
	case DataFormat::ETC2_R8G8B8A1_UNORM_BLOCK:
	case DataFormat::ETC2_R8G8B8A8_UNORM_BLOCK:
	case DataFormat::ASTC_4x4_UNORM_BLOCK:
		if (blockSize) *blockSize = 16;  // 128 bits
		return true;
	default:
		if (blockSize) *blockSize = 0;
		return false;
	}
}

int DataFormatNumChannels(DataFormat fmt) {
	switch (fmt) {
	case DataFormat::D16:
	case DataFormat::D32F:
	case DataFormat::R8_UNORM:
	case DataFormat::R16_UNORM:
	case DataFormat::R16_FLOAT:
	case DataFormat::R32_FLOAT:
		return 1;
	case DataFormat::R8G8B8A8_UNORM:
	case DataFormat::R8G8B8A8_UNORM_SRGB:
	case DataFormat::B8G8R8A8_UNORM:
	case DataFormat::B8G8R8A8_UNORM_SRGB:
		return 4;
	default:
		return 0;
	}
}

RefCountedObject::~RefCountedObject() {
	const int rc = refcount_.load();
	_dbg_assert_msg_(rc == 0xDEDEDE, "Unexpected refcount %d in object of type '%s'", rc, name_);
}

bool RefCountedObject::Release() {
	if (refcount_ > 0 && refcount_ < 10000) {
		if (--refcount_ == 0) {
			// Make it very obvious if we try to free this again.
			refcount_ = 0xDEDEDE;
			delete this;
			return true;
		}
	} else {
		// No point in printing the name here if the object has already been free-d, it'll be corrupt and dangerous to print.
		_dbg_assert_msg_(false, "Refcount (%d) invalid for object %p - corrupt?", refcount_.load(), this);
	}
	return false;
}

bool RefCountedObject::ReleaseAssertLast() {
	bool released = Release();
	_dbg_assert_msg_(released, "RefCountedObject: Expected to be the last reference, but isn't! (%s)", name_);
	return released;
}

const UniformBufferDesc vsColBufDesc { sizeof(VsColUB), {
	{ "WorldViewProj", 0, -1, UniformType::MATRIX4X4, 0 },
	{ "TintSaturation", 4, -1, UniformType::FLOAT2, 64 },
} };

static_assert(SEM_TEXCOORD0 == 3, "Semantic shader hardcoded in glsl above.");

const UniformBufferDesc vsTexColBufDesc{ sizeof(VsTexColUB),{
	{ "WorldViewProj", 0, -1, UniformType::MATRIX4X4, 0 },
	{ "TintSaturation", 4, -1, UniformType::FLOAT2, 64 },
} };

ShaderModule *CreateShader(DrawContext *draw, ShaderStage stage, const std::vector<ShaderSource> &sources) {
	uint32_t supported = draw->GetSupportedShaderLanguages();
	for (const auto &iter : sources) {
		if ((uint32_t)iter.lang & supported) {
			return draw->CreateShaderModule(stage, iter.lang, (const uint8_t *)iter.src, strlen(iter.src));
		}
	}
	return nullptr;
}

static const InputDef g_inputs[] = {
	{ "vec3", "Position", Draw::SEM_POSITION },
	{ "vec4", "Color0", Draw::SEM_COLOR0 },
	{ "vec2", "TexCoord0", Draw::SEM_TEXCOORD0 },
};

static const VaryingDef g_varyings[] = {
	{ "vec4", "oColor0", Draw::SEM_COLOR0, 0, "lowp" },
};

static const VaryingDef g_varyingsTex[] = {
	{ "vec4", "oColor0", Draw::SEM_COLOR0, 0, "lowp" },
	{ "vec2", "oTexCoord0", Draw::SEM_TEXCOORD0, 1, "highp" },
};

static const SamplerDef g_samplers[] = {
	{ 0, "tex" },
};

const UniformDef g_uniforms[] = {
	{ "mat4", "WorldViewProj", 0 },
	{ "vec2", "TintSaturation", 1 },
};

static ShaderModule *GenerateVShader(DrawContext *draw, bool texCoords, bool tint) {
	const ShaderLanguageDesc &shaderLanguageDesc = draw->GetShaderLanguageDesc();
	char code[2048];
	ShaderWriter vsWriter(code, shaderLanguageDesc, ShaderStage::Vertex);

	if (tint) {
		vsWriter.C(R"(
vec3 rgb2hsv(vec3 c) {
	vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
	vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
	float d = q.x - min(q.w, q.y);
	float e = 1.0e-10;
	return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv2rgb(vec3 c) {
	vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
)");
	}

	vsWriter.BeginVSMain(g_inputs, g_uniforms, texCoords ? Slice(g_varyingsTex) : Slice(g_varyings));
	vsWriter.C("gl_Position = mul(WorldViewProj, vec4(Position, 1.0));\n");
	if (tint) {
		vsWriter.C(R"(
		vec3 hsv = rgb2hsv(Color0.xyz);
		hsv.x += TintSaturation.x;
		hsv.y *= TintSaturation.y;
		oColor0 = vec4(hsv2rgb(hsv), Color0.w);
)");
	} else {
		vsWriter.C("oColor0 = Color0;\n");
	}
	if (texCoords) {
		vsWriter.C("oTexCoord0 = TexCoord0;\n");
	}
	vsWriter.EndVSMain(texCoords ? Slice(g_varyingsTex) : Slice(g_varyings));
	return draw->CreateShaderModule(ShaderStage::Vertex, shaderLanguageDesc.shaderLanguage, (const uint8_t *)code, strlen(code));
}

static ShaderModule *GenerateFShader(DrawContext *draw, bool texturing, bool rbSwizzle) {
	const ShaderLanguageDesc &shaderLanguageDesc = draw->GetShaderLanguageDesc();
	char code[2048];
	ShaderWriter fsWriter(code, shaderLanguageDesc, ShaderStage::Fragment);
	fsWriter.DeclareSamplers(g_samplers);
	fsWriter.BeginFSMain(g_uniforms, texturing ? Slice(g_varyingsTex) : Slice(g_varyings));
	if (texturing) {
		fsWriter.C("vec4 col = ");
		fsWriter.SampleTexture2D("tex", "oTexCoord0");
		if (rbSwizzle) {
			fsWriter.C(".zyxw * oColor0;\n");
		} else {
			fsWriter.C(" * oColor0;\n");
		}
	} else {
		fsWriter.C("vec4 col = oColor0;\n");
	}
	fsWriter.C("col.rgb *= oColor0.a;\n");  // premultiply alpha
	fsWriter.EndFSMain("col");
	return draw->CreateShaderModule(ShaderStage::Fragment, shaderLanguageDesc.shaderLanguage, (const uint8_t *)code, strlen(code));
}

bool DrawContext::CreatePresets() {
	bool tintSupported = true;
	if (bugs_.Has(Bugs::RASPBERRY_SHADER_COMP_HANG)) {
		tintSupported = false;
	}

	vsPresets_[VS_TEXTURE_COLOR_2D] = GenerateVShader(this, true, tintSupported);
	vsPresets_[VS_COLOR_2D] = GenerateVShader(this, false, tintSupported);

	fsPresets_[FS_TEXTURE_COLOR_2D] = GenerateFShader(this, true, false);
	fsPresets_[FS_COLOR_2D] = GenerateFShader(this, false, false);
	fsPresets_[FS_TEXTURE_COLOR_2D_RB_SWIZZLE] = GenerateFShader(this, true, true);

	return vsPresets_[VS_TEXTURE_COLOR_2D] && vsPresets_[VS_COLOR_2D] && fsPresets_[FS_TEXTURE_COLOR_2D] && fsPresets_[FS_COLOR_2D] && fsPresets_[FS_TEXTURE_COLOR_2D_RB_SWIZZLE];
}

void DrawContext::DestroyPresets() {
	for (int i = 0; i < VS_MAX_PRESET; i++) {
		if (vsPresets_[i]) {
			vsPresets_[i]->Release();
			vsPresets_[i] = nullptr;
		}
	}
	for (int i = 0; i < FS_MAX_PRESET; i++) {
		if (fsPresets_[i]) {
			fsPresets_[i]->Release();
			fsPresets_[i] = nullptr;
		}
	}
}

void ConvertFromRGBA8888(uint8_t *dst, const uint8_t *src, uint32_t dstStride, uint32_t srcStride, uint32_t width, uint32_t height, DataFormat format) {
	// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
	const uint32_t *src32 = (const uint32_t *)src;

	if (format == Draw::DataFormat::R8G8B8A8_UNORM) {
		uint32_t *dst32 = (uint32_t *)dst;
		if (src == dst) {
			return;
		} else {
			for (uint32_t y = 0; y < height; ++y) {
				memcpy(dst32, src32, width * 4);
				src32 += srcStride;
				dst32 += dstStride;
			}
		}
	} else if (format == Draw::DataFormat::R8G8B8_UNORM) {
		for (uint32_t y = 0; y < height; ++y) {
			ConvertRGBA8888ToRGB888(dst, src32, width);
			src32 += srcStride;
			dst += dstStride * 3;
		}
	} else {
		// But here it shouldn't matter if they do intersect
		uint16_t *dst16 = (uint16_t *)dst;
		switch (format) {
		case Draw::DataFormat::R5G6B5_UNORM_PACK16: // BGR 565
			for (uint32_t y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGB565(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case Draw::DataFormat::A1R5G5B5_UNORM_PACK16: // ABGR 1555
			for (uint32_t y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGBA5551(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case Draw::DataFormat::A4R4G4B4_UNORM_PACK16: // ABGR 4444
			for (uint32_t y = 0; y < height; ++y) {
				ConvertRGBA8888ToRGBA4444(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case Draw::DataFormat::R8G8B8A8_UNORM:
		case Draw::DataFormat::UNDEFINED:
		default:
			WARN_LOG(Log::G3D, "Unable to convert from format: %d", (int)format);
			break;
		}
	}
}

void ConvertFromBGRA8888(uint8_t *dst, const uint8_t *src, uint32_t dstStride, uint32_t srcStride, uint32_t width, uint32_t height, DataFormat format) {
	// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
	const uint32_t *src32 = (const uint32_t *)src;

	if (format == Draw::DataFormat::B8G8R8A8_UNORM) {
		uint32_t *dst32 = (uint32_t *)dst;
		if (src == dst) {
			return;
		} else {
			for (uint32_t y = 0; y < height; ++y) {
				memcpy(dst32, src32, width * 4);
				src32 += srcStride;
				dst32 += dstStride;
			}
		}
	} else if (format == Draw::DataFormat::R8G8B8A8_UNORM) {
		uint32_t *dst32 = (uint32_t *)dst;
		for (uint32_t y = 0; y < height; ++y) {
			ConvertBGRA8888ToRGBA8888(dst32, src32, width);
			src32 += srcStride;
			dst32 += dstStride;
		}
	} else if (format == Draw::DataFormat::R8G8B8_UNORM) {
		for (uint32_t y = 0; y < height; ++y) {
			ConvertBGRA8888ToRGB888(dst, src32, width);
			src32 += srcStride;
			dst += dstStride * 3;
		}
	} else {
		// But here it shouldn't matter if they do intersect
		uint16_t *dst16 = (uint16_t *)dst;
		switch (format) {
		case Draw::DataFormat::R5G6B5_UNORM_PACK16: // BGR 565
			for (uint32_t y = 0; y < height; ++y) {
				ConvertBGRA8888ToRGB565(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case Draw::DataFormat::A1R5G5B5_UNORM_PACK16: // ABGR 1555
			for (uint32_t y = 0; y < height; ++y) {
				ConvertBGRA8888ToRGBA5551(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case Draw::DataFormat::A4R4G4B4_UNORM_PACK16: // ABGR 4444
			for (uint32_t y = 0; y < height; ++y) {
				ConvertBGRA8888ToRGBA4444(dst16, src32, width);
				src32 += srcStride;
				dst16 += dstStride;
			}
			break;
		case Draw::DataFormat::R8G8B8A8_UNORM:
		case Draw::DataFormat::UNDEFINED:
		default:
			WARN_LOG(Log::G3D, "Unable to convert from format to BGRA: %d", (int)format);
			break;
		}
	}
}

void ConvertToD32F(uint8_t *dst, const uint8_t *src, uint32_t dstStride, uint32_t srcStride, uint32_t width, uint32_t height, DataFormat format) {
	if (format == Draw::DataFormat::D32F) {
		const float *src32 = (const float *)src;
		float *dst32 = (float *)dst;
		if (src == dst) {
			return;
		} else {
			for (uint32_t y = 0; y < height; ++y) {
				memcpy(dst32, src32, width * 4);
				src32 += srcStride;
				dst32 += dstStride;
			}
		}
	} else if (format == Draw::DataFormat::D16) {
		const uint16_t *src16 = (const uint16_t *)src;
		float *dst32 = (float *)dst;
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				dst32[x] = (float)(int)src16[x] / 65535.0f;
			}
			src16 += srcStride;
			dst32 += dstStride;
		}
	} else if (format == Draw::DataFormat::D24_S8) {
		const uint32_t *src32 = (const uint32_t *)src;
		float *dst32 = (float *)dst;
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				dst32[x] = (src32[x] & 0x00FFFFFF) / 16777215.0f;
			}
			src32 += srcStride;
			dst32 += dstStride;
		}
	} else {
		assert(false);
	}
}

// TODO: This is missing the conversion to the quarter-range we use if depth clamp is not available.
// That conversion doesn't necessarily belong here in thin3d, though.
void ConvertToD16(uint8_t *dst, const uint8_t *src, uint32_t dstStride, uint32_t srcStride, uint32_t width, uint32_t height, DataFormat format) {
	if (format == Draw::DataFormat::D32F) {
		const float *src32 = (const float *)src;
		uint16_t *dst16 = (uint16_t *)dst;
		if (src == dst) {
			return;
		} else {
			for (uint32_t y = 0; y < height; ++y) {
				for (uint32_t x = 0; x < width; ++x) {
					dst16[x] = (uint16_t)(src32[x] * 65535.0f);
				}
				src32 += srcStride;
				dst16 += dstStride;
			}
		}
	} else if (format == Draw::DataFormat::D16) {
		_assert_(src != dst);
		const uint16_t *src16 = (const uint16_t *)src;
		uint16_t *dst16 = (uint16_t *)dst;
		for (uint32_t y = 0; y < height; ++y) {
			memcpy(dst16, src16, width * 2);
			src16 += srcStride;
			dst16 += dstStride;
		}
	} else if (format == Draw::DataFormat::D24_S8) {
		_assert_(src != dst);
		const uint32_t *src32 = (const uint32_t *)src;
		uint16_t *dst16 = (uint16_t *)dst;
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				dst16[x] = (src32[x] & 0x00FFFFFF) >> 8;
			}
			src32 += srcStride;
			dst16 += dstStride;
		}
	} else {
		assert(false);
	}
}

const char *Bugs::GetBugName(uint32_t bug) {
	switch (bug) {
	case NO_DEPTH_CANNOT_DISCARD_STENCIL_MALI: return "NO_DEPTH_CANNOT_DISCARD_STENCIL_MALI";
	case NO_DEPTH_CANNOT_DISCARD_STENCIL_ADRENO: return "NO_DEPTH_CANNOT_DISCARD_STENCIL_ADRENO";
	case DUAL_SOURCE_BLENDING_BROKEN: return "DUAL_SOURCE_BLENDING_BROKEN";
	case ANY_MAP_BUFFER_RANGE_SLOW: return "ANY_MAP_BUFFER_RANGE_SLOW";
	case PVR_GENMIPMAP_HEIGHT_GREATER: return "PVR_GENMIPMAP_HEIGHT_GREATER";
	case BROKEN_NAN_IN_CONDITIONAL: return "BROKEN_NAN_IN_CONDITIONAL";
	case COLORWRITEMASK_BROKEN_WITH_DEPTHTEST: return "COLORWRITEMASK_BROKEN_WITH_DEPTHTEST";
	case BROKEN_FLAT_IN_SHADER: return "BROKEN_FLAT_IN_SHADER";
	case EQUAL_WZ_CORRUPTS_DEPTH: return "EQUAL_WZ_CORRUPTS_DEPTH";
	case RASPBERRY_SHADER_COMP_HANG: return "RASPBERRY_SHADER_COMP_HANG";
	case MALI_CONSTANT_LOAD_BUG: return "MALI_CONSTANT_LOAD_BUG";
	case SUBPASS_FEEDBACK_BROKEN: return "SUBPASS_FEEDBACK_BROKEN";
	case GEOMETRY_SHADERS_SLOW_OR_BROKEN: return "GEOMETRY_SHADERS_SLOW_OR_BROKEN";
	case ADRENO_RESOURCE_DEADLOCK: return "ADRENO_RESOURCE_DEADLOCK";
	case PVR_BAD_16BIT_TEXFORMATS: return "PVR_BAD_16BIT_TEXFORMATS";
	case EMPTY_RENDERPASS_BROKEN_MALI: return "EMPTY_RENDERPASS_BROKEN_MALI";
	default: return "(N/A)";
	}
}

const char *PresentModeToString(PresentMode presentMode) {
	// All 8 possible cases, with three flags, for simplicity.
	switch ((int)presentMode) {
	case 0: return "NONE";
	case (int)PresentMode::FIFO: return "FIFO";
	case (int)PresentMode::IMMEDIATE: return "IMMEDIATE";
	case (int)PresentMode::MAILBOX: return "MAILBOX";
	case ((int)PresentMode::FIFO | (int)PresentMode::MAILBOX) : return "FIFO|MAILBOX";
	case ((int)PresentMode::FIFO | (int)PresentMode::IMMEDIATE) : return "FIFO|IMMEDIATE";
	case ((int)PresentMode::MAILBOX | (int)PresentMode::IMMEDIATE) : return "MAILBOX|IMMEDIATE";  // Not gonna happen
	case ((int)PresentMode::FIFO | (int)PresentMode::MAILBOX | (int)PresentMode::IMMEDIATE) : return "FIFO|MAILBOX|IMMEDIATE";
	default:
		return "INVALID";
	}
}

}  // namespace Draw
