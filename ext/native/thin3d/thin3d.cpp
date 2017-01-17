#include <cstring>
#include <thin3d/thin3d.h>

#include "base/logging.h"

namespace Draw {

size_t DataFormatSizeInBytes(DataFormat fmt) {
	switch (fmt) {
	case DataFormat::R8_UNORM: return 1;
	case DataFormat::R8G8_UNORM: return 2;
	case DataFormat::R8G8B8_UNORM: return 3;

	case DataFormat::R4G4B4A4_UNORM: return 2;

	case DataFormat::R8G8B8A8_UNORM:
	case DataFormat::R8G8B8A8_UNORM_SRGB: return 4;

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

	default:
		return 0;
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
	"varying vec4 oColor0;\n"
	"void main() { gl_FragColor = oColor0; }\n"
	},
	{ ShaderLanguage::HLSL_D3D9,
	"struct PS_INPUT { float4 color : COLOR0; };\n"
	"float4 main(PS_INPUT input) : COLOR0 {\n"
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
	{ ShaderLanguage::GLSL_VULKAN,
	"#version 400\n"
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

static const UniformBufferDesc vsColBuf { { { 0, UniformType::MATRIX4X4, 0 } } };
struct VsColUB {
	float WorldViewProj[16];
};

static const std::vector<ShaderSource> vsTexCol = {
	{ ShaderLanguage::GLSL_ES_200,
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
	{ ShaderLanguage::GLSL_VULKAN,
	"#version 400\n"
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

static const UniformBufferDesc vsTexColDesc{ { { 0, UniformType::MATRIX4X4, 0 } } };
struct VsTexColUB {
	float WorldViewProj[16];
};

inline ShaderModule *CreateShader(DrawContext *draw, ShaderStage stage, const std::vector<ShaderSource> &sources) {
	uint32_t supported = draw->GetSupportedShaderLanguages();
	for (auto iter : sources) {
		if ((uint32_t)iter.lang & supported) {
			return draw->CreateShaderModule(stage, iter.lang, (const uint8_t *)iter.src, strlen(iter.src));
		}
	}
	return nullptr;
}

void DrawContext::CreatePresets() {
	vsPresets_[VS_TEXTURE_COLOR_2D] = CreateShader(this, ShaderStage::VERTEX, vsTexCol);
	vsPresets_[VS_COLOR_2D] = CreateShader(this, ShaderStage::VERTEX, vsCol);

	fsPresets_[FS_TEXTURE_COLOR_2D] = CreateShader(this, ShaderStage::FRAGMENT, fsTexCol);
	fsPresets_[FS_COLOR_2D] = CreateShader(this, ShaderStage::FRAGMENT, fsCol);
}

DrawContext::~DrawContext() {
	for (int i = 0; i < VS_MAX_PRESET; i++) {
		vsPresets_[i]->Release();
	}
	for (int i = 0; i < FS_MAX_PRESET; i++) {
		fsPresets_[i]->Release();
	}
}

}  // namespace Draw