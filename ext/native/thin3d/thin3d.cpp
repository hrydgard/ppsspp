#include <cassert>
#include <cstring>
#include <cstdint>

#include "base/logging.h"
#include "thin3d/thin3d.h"
#include "Common/Log.h"
#include "Common/ColorConv.h"
#include "Core/Reporting.h"

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
	case DataFormat::R16_FLOAT: return 2;
	case DataFormat::R16G16_FLOAT: return 4;
	case DataFormat::R16G16B16A16_FLOAT: return 8;
	case DataFormat::R32_FLOAT: return 4;
	case DataFormat::R32G32_FLOAT: return 8;
	case DataFormat::R32G32B32_FLOAT: return 12;
	case DataFormat::R32G32B32A32_FLOAT: return 16;

	case DataFormat::S8: return 1;
	case DataFormat::D16: return 2;
	case DataFormat::D24_S8: return 4;
	case DataFormat::D32F: return 4;
	// Or maybe 8...
	case DataFormat::D32F_S8: return 5;

	default:
		return 0;
	}
}

bool DataFormatIsDepthStencil(DataFormat fmt) {
	switch (fmt) {
	case DataFormat::D16:
	case DataFormat::D24_S8:
	case DataFormat::S8:
	case DataFormat::D32F:
	case DataFormat::D32F_S8:
		return true;
	default:
		return false;
	}
}


bool RefCountedObject::Release() {
	if (refcount_ > 0 && refcount_ < 10000) {
		refcount_--;
		if (refcount_ == 0) {
			delete this;
			return true;
		}
	}
	else {
		_dbg_assert_msg_(G3D, false, "Refcount (%d) invalid for object %p - corrupt?", refcount_, this);
	}
	return false;
}

bool RefCountedObject::ReleaseAssertLast() {
	_dbg_assert_msg_(G3D, refcount_ == 1, "RefCountedObject: Expected to be the last reference, but isn't!");
	if (refcount_ > 0 && refcount_ < 10000) {
		refcount_--;
		if (refcount_ == 0) {
			delete this;
			return true;
		}
	} else {
		ELOG("Refcount (%d) invalid for object %p - corrupt?", refcount_, this);
	}
	return false;
}


// ================================== PIXEL/FRAGMENT SHADERS

// The Vulkan ones can be re-used with modern GL later if desired, as they're just GLSL.

struct ShaderSource {
	ShaderLanguage lang;
	const char *src;
};

static const std::vector<ShaderSource> fsTexCol = {
	{ShaderLanguage::GLSL_ES_200,
	"#ifdef GL_ES\n"
	"precision lowp float;\n"
	"#endif\n"
	"#if __VERSION__ >= 130\n"
	"#define varying in\n"
	"#define texture2D texture\n"
	"#define gl_FragColor fragColor0\n"
	"out vec4 fragColor0;\n"
	"#endif\n"
	"varying vec4 oColor0;\n"
	"varying vec2 oTexCoord0;\n"
	"uniform sampler2D Sampler0;\n"
	"void main() { gl_FragColor = texture2D(Sampler0, oTexCoord0) * oColor0; }\n"
	},
	{ShaderLanguage::HLSL_D3D9,
	"struct PS_INPUT { float4 color : COLOR0; float2 uv : TEXCOORD0; };\n"
	"sampler2D Sampler0 : register(s0);\n"
	"float4 main(PS_INPUT input) : COLOR0 {\n"
	"  return input.color * tex2D(Sampler0, input.uv);\n"
	"}\n"
	},
	{ShaderLanguage::HLSL_D3D11,
	"struct PS_INPUT { float4 color : COLOR0; float2 uv : TEXCOORD0; };\n"
	"SamplerState samp : register(s0);\n"
	"Texture2D<float4> tex : register(t0);\n"
	"float4 main(PS_INPUT input) : SV_Target {\n"
	"  float4 col = input.color * tex.Sample(samp, input.uv);\n"
	"  return col;\n"
	"}\n"
	},
	{ShaderLanguage::GLSL_VULKAN,
	"#version 140\n"
	"#extension GL_ARB_separate_shader_objects : enable\n"
	"#extension GL_ARB_shading_language_420pack : enable\n"
	"layout(location = 0) in vec4 oColor0;\n"
	"layout(location = 1) in vec2 oTexCoord0;\n"
	"layout(location = 0) out vec4 fragColor0\n;"
	"layout(set = 0, binding = 1) uniform sampler2D Sampler0;\n"
	"void main() { fragColor0 = texture(Sampler0, oTexCoord0) * oColor0; }\n"
	}
};

static const std::vector<ShaderSource> fsCol = {
	{ ShaderLanguage::GLSL_ES_200,
	"#ifdef GL_ES\n"
	"precision lowp float;\n"
	"#endif\n"
	"#if __VERSION__ >= 130\n"
	"#define varying in\n"
	"#define gl_FragColor fragColor0\n"
	"out vec4 fragColor0;\n"
	"#endif\n"
	"varying vec4 oColor0;\n"
	"void main() { gl_FragColor = oColor0; }\n"
	},
	{ ShaderLanguage::HLSL_D3D9,
	"struct PS_INPUT { float4 color : COLOR0; };\n"
	"float4 main(PS_INPUT input) : COLOR0 {\n"
	"  return input.color;\n"
	"}\n"
	},
	{ ShaderLanguage::HLSL_D3D11,
	"struct PS_INPUT { float4 color : COLOR0; };\n"
	"float4 main(PS_INPUT input) : SV_Target {\n"
	"  return input.color;\n"
	"}\n"
	},
	{ ShaderLanguage::GLSL_VULKAN,
	"#version 140\n"
	"#extension GL_ARB_separate_shader_objects : enable\n"
	"#extension GL_ARB_shading_language_420pack : enable\n"
	"layout(location = 0) in vec4 oColor0;\n"
	"layout(location = 0) out vec4 fragColor0\n;"
	"void main() { fragColor0 = oColor0; }\n"
	}
};

// ================================== VERTEX SHADERS

static const std::vector<ShaderSource> vsCol = {
	{ ShaderLanguage::GLSL_ES_200,
	"#if __VERSION__ >= 130\n"
	"#define attribute in\n"
	"#define varying out\n"
	"#endif\n"
	"attribute vec3 Position;\n"
	"attribute vec4 Color0;\n"
	"varying vec4 oColor0;\n"
	"uniform mat4 WorldViewProj;\n"
	"void main() {\n"
	"  gl_Position = WorldViewProj * vec4(Position, 1.0);\n"
	"  oColor0 = Color0;\n"
	"}"
	},
	{ ShaderLanguage::HLSL_D3D9,
	"struct VS_INPUT { float3 Position : POSITION; float4 Color0 : COLOR0; };\n"
	"struct VS_OUTPUT { float4 Position : POSITION; float4 Color0 : COLOR0; };\n"
	"float4x4 WorldViewProj : register(c0);\n"
	"VS_OUTPUT main(VS_INPUT input) {\n"
	"  VS_OUTPUT output;\n"
	"  output.Position = mul(float4(input.Position, 1.0), WorldViewProj);\n"
	"  output.Color0 = input.Color0;\n"
	"  return output;\n"
	"}\n"
	},
	{ ShaderLanguage::HLSL_D3D11,
	"struct VS_INPUT { float3 Position : POSITION; float4 Color0 : COLOR0; };\n"
	"struct VS_OUTPUT { float4 Color0 : COLOR0; float4 Position : SV_Position; };\n"
	"cbuffer ConstantBuffer : register(b0) {\n"
	"  matrix WorldViewProj;\n"
	"};\n"
	"VS_OUTPUT main(VS_INPUT input) {\n"
	"  VS_OUTPUT output;\n"
	"  output.Position = mul(float4(input.Position, 1.0), WorldViewProj);\n"
	"  output.Color0 = input.Color0;\n"
	"  return output;\n"
	"}\n"
	},
	{ ShaderLanguage::GLSL_VULKAN,
	"#version 450\n"
	"#extension GL_ARB_separate_shader_objects : enable\n"
	"#extension GL_ARB_shading_language_420pack : enable\n"
	"layout (std140, set = 0, binding = 0) uniform bufferVals {\n"
	"    mat4 WorldViewProj;\n"
	"} myBufferVals;\n"
	"layout (location = 0) in vec4 pos;\n"
	"layout (location = 1) in vec4 inColor;\n"
	"layout (location = 0) out vec4 outColor;\n"
	"out gl_PerVertex { vec4 gl_Position; };\n"
	"void main() {\n"
	"   outColor = inColor;\n"
	"   gl_Position = myBufferVals.WorldViewProj * pos;\n"
	"}\n"
	}
};

const UniformBufferDesc vsColBufDesc { sizeof(VsColUB), {
	{ "WorldViewProj", 0, -1, UniformType::MATRIX4X4, 0 }
} };

static const std::vector<ShaderSource> vsTexCol = {
	{ ShaderLanguage::GLSL_ES_200,
	"#if __VERSION__ >= 130\n"
	"#define attribute in\n"
	"#define varying out\n"
	"#endif\n"
	"attribute vec3 Position;\n"
	"attribute vec4 Color0;\n"
	"attribute vec2 TexCoord0;\n"
	"varying vec4 oColor0;\n"
	"varying vec2 oTexCoord0;\n"
	"uniform mat4 WorldViewProj;\n"
	"void main() {\n"
	"  gl_Position = WorldViewProj * vec4(Position, 1.0);\n"
	"  oColor0 = Color0;\n"
	"  oTexCoord0 = TexCoord0;\n"
	"}\n"
	},
	{ ShaderLanguage::HLSL_D3D9,
	"struct VS_INPUT { float3 Position : POSITION; float2 Texcoord0 : TEXCOORD0; float4 Color0 : COLOR0; };\n"
	"struct VS_OUTPUT { float4 Position : POSITION; float2 Texcoord0 : TEXCOORD0; float4 Color0 : COLOR0; };\n"
	"float4x4 WorldViewProj : register(c0);\n"
	"VS_OUTPUT main(VS_INPUT input) {\n"
	"  VS_OUTPUT output;\n"
	"  output.Position = mul(float4(input.Position, 1.0), WorldViewProj);\n"
	"  output.Texcoord0 = input.Texcoord0;\n"
	"  output.Color0 = input.Color0;\n"
	"  return output;\n"
	"}\n"
	},
	{ ShaderLanguage::HLSL_D3D11,
	"struct VS_INPUT { float3 Position : POSITION; float2 Texcoord0 : TEXCOORD0; float4 Color0 : COLOR0; };\n"
	"struct VS_OUTPUT { float4 Color0 : COLOR0; float2 Texcoord0 : TEXCOORD0; float4 Position : SV_Position; };\n"
	"cbuffer ConstantBuffer : register(b0) {\n"
	"  matrix WorldViewProj;\n"
	"};\n"
	"VS_OUTPUT main(VS_INPUT input) {\n"
	"  VS_OUTPUT output;\n"
	"  output.Position = mul(WorldViewProj, float4(input.Position, 1.0));\n"
	"  output.Texcoord0 = input.Texcoord0;\n"
	"  output.Color0 = input.Color0;\n"
	"  return output;\n"
	"}\n"
	},
	{ ShaderLanguage::GLSL_VULKAN,
	"#version 450\n"
	"#extension GL_ARB_separate_shader_objects : enable\n"
	"#extension GL_ARB_shading_language_420pack : enable\n"
	"layout (std140, set = 0, binding = 0) uniform bufferVals {\n"
	"    mat4 WorldViewProj;\n"
	"} myBufferVals;\n"
	"layout (location = 0) in vec4 pos;\n"
	"layout (location = 1) in vec4 inColor;\n"
	"layout (location = 2) in vec2 inTexCoord;\n"
	"layout (location = 0) out vec4 outColor;\n"
	"layout (location = 1) out vec2 outTexCoord;\n"
	"out gl_PerVertex { vec4 gl_Position; };\n"
	"void main() {\n"
	"   outColor = inColor;\n"
	"   outTexCoord = inTexCoord;\n"
	"   gl_Position = myBufferVals.WorldViewProj * pos;\n"
	"}\n"
	}
};

const UniformBufferDesc vsTexColBufDesc{ sizeof(VsTexColUB),{
	{ "WorldViewProj", 0, -1, UniformType::MATRIX4X4, 0 }
} };

static ShaderModule *CreateShader(DrawContext *draw, ShaderStage stage, const std::vector<ShaderSource> &sources) {
	uint32_t supported = draw->GetSupportedShaderLanguages();
	for (auto iter : sources) {
		if ((uint32_t)iter.lang & supported) {
			return draw->CreateShaderModule(stage, iter.lang, (const uint8_t *)iter.src, strlen(iter.src));
		}
	}
	return nullptr;
}

bool DrawContext::CreatePresets() {
	vsPresets_[VS_TEXTURE_COLOR_2D] = CreateShader(this, ShaderStage::VERTEX, vsTexCol);
	vsPresets_[VS_COLOR_2D] = CreateShader(this, ShaderStage::VERTEX, vsCol);

	fsPresets_[FS_TEXTURE_COLOR_2D] = CreateShader(this, ShaderStage::FRAGMENT, fsTexCol);
	fsPresets_[FS_COLOR_2D] = CreateShader(this, ShaderStage::FRAGMENT, fsCol);

	return vsPresets_[VS_TEXTURE_COLOR_2D] && vsPresets_[VS_COLOR_2D] && fsPresets_[FS_TEXTURE_COLOR_2D] && fsPresets_[FS_COLOR_2D];
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

DrawContext::~DrawContext() {
	DestroyPresets();
}

// TODO: SSE/NEON
// Could also make C fake-simd for 64-bit, two 8888 pixels fit in a register :)
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
			for (uint32_t x = 0; x < width; ++x) {
				memcpy(dst + x * 3, src32 + x, 3);
			}
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
			WARN_LOG_REPORT_ONCE(convFromRGBA, G3D, "Unable to convert from format: %d", (int)format);
			break;
		}
	}
}

// TODO: SSE/NEON
// Could also make C fake-simd for 64-bit, two 8888 pixels fit in a register :)
void ConvertFromBGRA8888(uint8_t *dst, const uint8_t *src, uint32_t dstStride, uint32_t srcStride, uint32_t width, uint32_t height, DataFormat format) {
	// Must skip stride in the cases below.  Some games pack data into the cracks, like MotoGP.
	const uint32_t *src32 = (const uint32_t *)src;

	if (format == Draw::DataFormat::R8G8B8A8_UNORM) {
		uint32_t *dst32 = (uint32_t *)dst;
		for (uint32_t y = 0; y < height; ++y) {
			ConvertBGRA8888ToRGBA8888(dst32, src32, width);
			src32 += srcStride;
			dst32 += dstStride;
		}
	} else if (format == Draw::DataFormat::R8G8B8_UNORM) {
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				uint32_t c = src32[x];
				dst[x * 3 + 0] = (c >> 16) & 0xFF;
				dst[x * 3 + 1] = (c >> 8) & 0xFF;
				dst[x * 3 + 2] = (c >> 0) & 0xFF;
			}
			src32 += srcStride;
			dst += dstStride * 3;
		}
	} else {
		WARN_LOG_REPORT_ONCE(convFromBGRA, G3D, "Unable to convert from format to BGRA: %d", (int)format);
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


}  // namespace Draw
