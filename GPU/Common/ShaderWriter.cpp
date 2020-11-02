#include "GPU/Common/ShaderWriter.h"

const char *vulkan_glsl_preamble_fs =
"#version 450\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"#extension GL_ARB_conservative_depth : enable\n"
"#extension GL_ARB_shader_image_load_store : enable\n"
"#define splat3(x) vec3(x)\n"
"#define lowp\n"
"#define mediump\n"
"#define highp\n"
"#define DISCARD discard\n"
"\n";

const char *hlsl_preamble_fs =
"#define vec2 float2\n"
"#define vec3 float3\n"
"#define vec4 float4\n"
"#define uvec3 uint3\n"
"#define ivec3 int3\n"
"#define ivec4 int4\n"
"#define mat4 float4x4\n"
"#define mat3x4 float4x3\n"  // note how the conventions are backwards
"#define splat3(x) float3(x, x, x)\n"
"#define mix lerp\n"
"#define mod(x, y) fmod(x, y)\n";

const char *hlsl_d3d11_preamble_fs =
"#define DISCARD discard\n"
"#define DISCARD_BELOW(x) clip(x);\n";
const char *hlsl_d3d9_preamble_fs =
"#define DISCARD clip(-1)\n"
"#define DISCARD_BELOW(x) clip(x)\n";

const char *vulkan_glsl_preamble_vs =
"#version 450\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"#define mul(x, y) ((x) * (y))\n"
"#define splat3(x) vec3(x)\n"
"#define lowp\n"
"#define mediump\n"
"#define highp\n"
"\n";

const char *hlsl_preamble_vs =
"#define vec2 float2\n"
"#define vec3 float3\n"
"#define vec4 float4\n"
"#define ivec2 int2\n"
"#define ivec4 int4\n"
"#define mat4 float4x4\n"
"#define mat3x4 float4x3\n"  // note how the conventions are backwards
"#define splat3(x) vec3(x, x, x)\n"
"#define lowp\n"
"#define mediump\n"
"#define highp\n"
"\n";

// Unsafe. But doesn't matter, we'll use big buffers for shader gen.
void ShaderWriter::F(const char *format, ...) {
	va_list args;
	va_start(args, format);
	p_ += vsprintf(p_, format, args);
	va_end(args);
}

void ShaderWriter::Preamble(const char **gl_extensions, size_t num_gl_extensions) {
	switch (lang_.shaderLanguage) {
	case GLSL_VULKAN:
		switch (stage_) {
		case ShaderStage::Vertex:
			W(vulkan_glsl_preamble_vs);
			break;
		case ShaderStage::Fragment:
			W(vulkan_glsl_preamble_fs);
			break;
		}
		break;
	case HLSL_D3D11:
	case HLSL_D3D9:
		switch (stage_) {
		case ShaderStage::Vertex:
			W(hlsl_preamble_vs);
			break;
		case ShaderStage::Fragment:
			W(hlsl_preamble_fs);
			if (lang_.shaderLanguage == HLSL_D3D9) {
				W(hlsl_d3d9_preamble_fs);
			} else {
				W(hlsl_d3d11_preamble_fs);
			}
			break;
		}
		break;
	default:  // OpenGL
		F("#version %d%s\n", lang_.glslVersionNumber, lang_.gles && lang_.glslES30 ? " es" : "");
		switch (stage_) {
		case ShaderStage::Fragment:
			C("#define DISCARD discard\n");
			if (lang_.gles) {
				C("precision lowp float;\n");
			}
			break;
		case ShaderStage::Vertex:
			if (lang_.gles) {
				C("precision highp float;\n");
			}
			break;
		}
		for (size_t i = 0; i < num_gl_extensions; i++) {
			F("%s\n", gl_extensions[i]);
		}
		if (!lang_.gles) {
			C("#define lowp\n");
			C("#define mediump\n");
			C("#define highp\n");
		}
		C("#define splat3(x) vec3(x)\n");
		C("#define mul(x, y) ((x) * (y))\n");
		break;
	}
}
