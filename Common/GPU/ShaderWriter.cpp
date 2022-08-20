#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/Log.h"

const char * const vulkan_glsl_preamble_fs =
"#version 450\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"#extension GL_ARB_conservative_depth : enable\n"
"#extension GL_ARB_shader_image_load_store : enable\n"
"#define splat3(x) vec3(x)\n"
"#define DISCARD discard\n"
"precision lowp float;\n"
"precision highp int;\n"
"\n";

const char * const hlsl_preamble_fs =
"#define vec2 float2\n"
"#define vec3 float3\n"
"#define vec4 float4\n"
"#define uvec3 uint3\n"
"#define uvec4 uint4\n"
"#define ivec3 int3\n"
"#define ivec4 int4\n"
"#define mat4 float4x4\n"
"#define mat3x4 float4x3\n"  // note how the conventions are backwards
"#define splat3(x) float3(x, x, x)\n"
"#define mix lerp\n"
"#define lowp\n"
"#define mediump\n"
"#define highp\n"
"#define fract frac\n"
"#define mod(x, y) fmod(x, y)\n";

static const char * const hlsl_d3d11_preamble_fs =
"#define DISCARD discard\n"
"#define DISCARD_BELOW(x) clip(x);\n";
static const char * const hlsl_d3d9_preamble_fs =
"#define DISCARD clip(-1)\n"
"#define DISCARD_BELOW(x) clip(x)\n";

static const char * const vulkan_glsl_preamble_vs =
"#version 450\n"
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"#define mul(x, y) ((x) * (y))\n"
"#define splat3(x) vec3(x)\n"
"precision highp float;\n"
"\n";

static const char * const hlsl_preamble_vs =
"#define vec2 float2\n"
"#define vec3 float3\n"
"#define vec4 float4\n"
"#define ivec2 int2\n"
"#define ivec4 int4\n"
"#define mat2 float2x2\n"
"#define mat4 float4x4\n"
"#define mat3x4 float4x3\n"  // note how the conventions are backwards
"#define splat3(x) vec3(x, x, x)\n"
"#define lowp\n"
"#define mediump\n"
"#define highp\n"
"\n";

static const char * const semanticNames[8] = {
	"POSITION",
	"COLOR0",
	"TEXCOORD0",
	"TEXCOORD1",
	"NORMAL",
	"TANGENT",
	"BINORMAL",
};

// Unsafe. But doesn't matter, we'll use big buffers for shader gen.
ShaderWriter & ShaderWriter::F(const char *format, ...) {
	va_list args;
	va_start(args, format);
	p_ += vsprintf(p_, format, args);
	va_end(args);
	return *this;
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
		default:
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
		default:
			break;
		}
		break;
	default:  // OpenGL
		F("#version %d%s\n", lang_.glslVersionNumber, lang_.gles && lang_.glslES30 ? " es" : "");
		// IMPORTANT! Extensions must be the first thing after #version.
		for (size_t i = 0; i < num_gl_extensions; i++) {
			F("%s\n", gl_extensions[i]);
		}
		// Print some system info - useful to gather information directly from screenshots.
		if (strlen(lang_.driverInfo) != 0) {
			F("// Driver: %s\n", lang_.driverInfo);
		}
		switch (stage_) {
		case ShaderStage::Fragment:
			C("#define DISCARD discard\n");
			if (lang_.gles) {
				C("precision lowp float;\n");
				if (lang_.glslES30) {
					C("precision highp int;\n");
				}
			}
			break;
		case ShaderStage::Vertex:
			if (lang_.gles) {
				C("precision highp float;\n");
			}
			C("#define gl_VertexIndex gl_VertexID\n");
			break;
		default:
			break;
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

void ShaderWriter::BeginVSMain(Slice<InputDef> inputs, Slice<UniformDef> uniforms, Slice<VaryingDef> varyings) {
	_assert_(this->stage_ == ShaderStage::Vertex);
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
	case HLSL_D3D9:
	{
		C("struct VS_OUTPUT {\n");
		for (auto &varying : varyings) {
			F("  %s %s : %s;\n", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		F("  vec4 pos : %s;\n", lang_.shaderLanguage == HLSL_D3D11 ? "SV_Position" : "POSITION");
		C("};\n");

		C("VS_OUTPUT main(  ");  // 2 spaces for the D3D9 rewind
		if (lang_.shaderLanguage == HLSL_D3D11) {
			C("uint gl_VertexIndex : SV_VertexID, ");
		}
		// List the inputs.
		for (auto &input : inputs) {
			F("in %s %s : %s, ", input.type, input.name, semanticNames[input.semantic]);
		}

		Rewind(2);  // Get rid of the last comma.
		C(") {\n");
		C("  vec4 gl_Position;\n");
		for (auto &varying : varyings) {
			F("  %s %s;  // %s\n", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		break;
	}
	case GLSL_VULKAN:
	{
		for (auto &input : inputs) {
			F("layout(location = %d) in %s %s;\n", input.semantic /*index*/, input.type, input.name);
		}
		for (auto &varying : varyings) {
			F("layout(location = %d) %s out %s %s;  // %s\n",
				varying.index, varying.precision ? varying.precision : "", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		C("void main() {\n");
		break;
	}
	default:  // OpenGL
		for (auto &input : inputs) {
			F("%s %s %s;\n", lang_.attribute, input.type, input.name);
		}
		for (auto &varying : varyings) {
			F("%s %s %s %s;  // %s (%d)\n", lang_.varying_vs, varying.precision ? varying.precision : "", varying.type, varying.name, semanticNames[varying.semantic], varying.index);
		}
		C("void main() {\n");
		break;
	}
}

void ShaderWriter::BeginFSMain(Slice<UniformDef> uniforms, Slice<VaryingDef> varyings, FSFlags flags) {
	_assert_(this->stage_ == ShaderStage::Fragment);
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		if (!uniforms.is_empty()) {
			C("cbuffer base : register(b0) {\n");

			for (auto &uniform : uniforms) {
				F("  %s %s;\n", uniform.type, uniform.name);
			}

			C("};\n");
		}

		if (flags & FSFLAG_WRITEDEPTH) {
			C("float gl_FragDepth;\n");
		}

		C("struct PS_OUT {\n");
		C("  vec4 target : SV_Target0;\n");
		if (flags & FSFLAG_WRITEDEPTH) {
			C("  float depth : SV_Depth;\n");
		}
		C("};\n");

		// Let's do the varyings as parameters to main, no struct.
		C("PS_OUT main(");
		for (auto &varying : varyings) {
			F("  %s %s : %s, ", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		// Erase the last comma
		Rewind(2);

		F(") {\n");
		C("  PS_OUT ps_out;\n");
		if (flags & FSFLAG_WRITEDEPTH) {
			C("  float gl_FragDepth;\n");
		}
		break;
	case HLSL_D3D9:
		C("struct PS_OUT {\n");
		C("  vec4 target : SV_Target0;\n");
		if (flags & FSFLAG_WRITEDEPTH) {
			C("  float depth : DEPTH;\n");
		}
		C("};\n");

		for (auto &uniform : uniforms) {
			F("  %s %s : register(c%d);\n", uniform.type, uniform.name, uniform.index);
		}
		// Let's do the varyings as parameters to main, no struct.
		C("PS_OUT main(");
		for (auto &varying : varyings) {
			F("  %s %s : %s, ", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		// Erase the last comma
		Rewind(2);

		F(") {\n");
		C("  PS_OUT ps_out;\n");
		if (flags & FSFLAG_WRITEDEPTH) {
			C("  float gl_FragDepth;\n");
		}

		break;
	case GLSL_VULKAN:
		for (auto &varying : varyings) {
			F("layout(location = %d) %s in %s %s;  // %s\n", varying.index, varying.precision ? varying.precision : "", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		C("layout(location = 0, index = 0) out vec4 fragColor0;\n");
		if (!uniforms.is_empty()) {
			C("layout(std140, set = 0, binding = 0) uniform bufferVals {\n");
			for (auto &uniform : uniforms) {
				F("%s %s;\n", uniform.type, uniform.name);
			}
			C("};\n");
		}
		C("\nvoid main() {\n");
		break;

	default:  // GLSL OpenGL
		for (auto &varying : varyings) {
			F("%s %s %s %s;  // %s\n", lang_.varying_fs, varying.precision ? varying.precision : "", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		for (auto &uniform : uniforms) {
			F("uniform %s %s;\n", uniform.type, uniform.name);
		}
		if (!strcmp(lang_.fragColor0, "fragColor0")) {
			C("out vec4 fragColor0;\n");
		}
		C("\nvoid main() {\n");
		break;
	}
}

void ShaderWriter::EndVSMain(Slice<VaryingDef> varyings) {
	_assert_(this->stage_ == ShaderStage::Vertex);
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
	case HLSL_D3D9:
		C("  VS_OUTPUT vs_out;\n");
		if (strlen(lang_.viewportYSign)) {
			F("  gl_Position.y *= %s1.0;\n", lang_.viewportYSign);
		}
		C("  vs_out.pos = gl_Position;\n");
		for (auto &varying : varyings) {
			F("  vs_out.%s = %s;\n", varying.name, varying.name);
		}
		C("  return vs_out;\n");
		break;
	case GLSL_VULKAN:
	default:  // OpenGL
		break;
	}
	C("}\n");
}

void ShaderWriter::EndFSMain(const char *vec4_color_variable, FSFlags flags) {
	_assert_(this->stage_ == ShaderStage::Fragment);
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
	case HLSL_D3D9:
		F("  ps_out.target = %s;\n", vec4_color_variable);
		if (flags & FSFLAG_WRITEDEPTH) {
			C("  ps_out.depth = gl_FragDepth;\n");
		}
		C("  return ps_out;\n");
		break;
	case GLSL_VULKAN:
	default:  // OpenGL
		F("  %s = %s;\n", lang_.fragColor0, vec4_color_variable);
		break;
	}
	C("}\n");
}

void ShaderWriter::HighPrecisionFloat() {
	if ((ShaderLanguageIsOpenGL(lang_.shaderLanguage) && lang_.gles) || lang_.shaderLanguage == GLSL_VULKAN) {
		C("precision highp float;\n");
	}
}

void ShaderWriter::LowPrecisionFloat() {
	if ((ShaderLanguageIsOpenGL(lang_.shaderLanguage) && lang_.gles) || lang_.shaderLanguage == GLSL_VULKAN) {
		C("precision lowp float;\n");
	}
}

void ShaderWriter::ConstFloat(const char *name, float value) {
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
	case HLSL_D3D9:
		F("static const float %s = %f;\n", name, value);
		break;
	default:
		F("#define %s %f\n", name, value);
		break;
	}
}

void ShaderWriter::DeclareSamplers(Slice<SamplerDef> samplers) {
	for (int i = 0; i < (int)samplers.size(); i++) {
		DeclareTexture2D(samplers[i].name, i);
		DeclareSampler2D(samplers[i].name, i);
	}
}

void ShaderWriter::DeclareTexture2D(const char *name, int binding) {
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("Texture2D<float4> %s : register(t%d);\n", name, binding);
		break;
	case HLSL_D3D9:
		F("sampler %s: register(s%d);\n", name, binding);
		break;
	case GLSL_VULKAN:
		// In the thin3d descriptor set layout, textures start at 1 in set 0. Hence the +1.
		F("layout(set = 0, binding = %d) uniform sampler2D %s;\n", binding + 1, name);
		break;
	default:
		F("uniform sampler2D %s;\n", name);
		break;
	}
}

void ShaderWriter::DeclareSampler2D(const char *name, int binding) {
	// We only use separate samplers in HLSL D3D11, where we have no choice.
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("SamplerState %sSamp : register(s%d);\n", name, binding);
		break;
	default:
		break;
	}
}

ShaderWriter &ShaderWriter::SampleTexture2D(const char *sampName, const char *uv) {
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("%s.Sample(%sSamp, %s)", sampName, sampName, uv);
		break;
	case HLSL_D3D9:
		F("tex2D(%s, %s)", sampName, uv);
		break;
	default:
		// Note: we ignore the sampler. make sure you bound samplers to the textures correctly.
		F("%s(%s, %s)", lang_.texture, sampName, uv);
		break;
	}
	return *this;
}

ShaderWriter &ShaderWriter::GetTextureSize(const char *szVariable, const char *texName) {
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("  float2 %s; %s.GetDimensions(%s.x, %s.y);", szVariable, texName, szVariable, szVariable);
		break;
	case HLSL_D3D9:
		F("  float2 %s; %s.GetDimensions(%s.x, %s.y);", szVariable, texName, szVariable, szVariable);
		break;
	default:
		// Note: we ignore the sampler. make sure you bound samplers to the textures correctly.
		F("vec2 %s = textureSize(%s, 0);", szVariable, texName);
		break;
	}
	return *this;
}
