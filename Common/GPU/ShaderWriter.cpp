#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "Common/GPU/Shader.h"
#include "Common/GPU/ShaderWriter.h"
#include "Common/Log.h"

const char * const vulkan_glsl_preamble_fs =
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
"#define ivec2 int2\n"
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

static const char * const vulkan_glsl_preamble_vs =
"#extension GL_ARB_separate_shader_objects : enable\n"
"#extension GL_ARB_shading_language_420pack : enable\n"
"#define mul(x, y) ((x) * (y))\n"
"#define splat3(x) vec3(x)\n"
"precision highp float;\n"
"\n";

static const char * const hlsl_preamble_gs =
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

static const char * const vulkan_glsl_preamble_gs =
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

static const char * const semanticNames[] = {
	"POSITION",
	"COLOR0",
	"COLOR1",
	"TEXCOORD0",
	"TEXCOORD1",
	"NORMAL",
	"TANGENT",
	"BINORMAL",
};
static_assert(ARRAY_SIZE(semanticNames) == Draw::SEM_MAX, "Missing semantic in semanticNames");

// Unsafe. But doesn't matter, we'll use big buffers for shader gen.
ShaderWriter & ShaderWriter::F(const char *format, ...) {
	va_list args;
	va_start(args, format);
	p_ += vsprintf(p_, format, args);
	va_end(args);
	return *this;
}

void ShaderWriter::Preamble(Slice<const char *> extensions) {
	switch (lang_.shaderLanguage) {
	case GLSL_VULKAN:
		C("#version 450\n");
		if (flags_ & ShaderWriterFlags::FS_AUTO_STEREO) {
			C("#extension GL_EXT_multiview : enable\n");
		}
		// IMPORTANT! Extensions must be the first thing after #version.
		for (size_t i = 0; i < extensions.size(); i++) {
			F("%s\n", extensions[i]);
		}
		switch (stage_) {
		case ShaderStage::Vertex:
			W(vulkan_glsl_preamble_vs);
			break;
		case ShaderStage::Fragment:
			W(vulkan_glsl_preamble_fs);
			break;
		case ShaderStage::Geometry:
			W(vulkan_glsl_preamble_gs);
			break;
		default:
			break;
		}
		break;
	case HLSL_D3D11:
		switch (stage_) {
		case ShaderStage::Vertex:
			W(hlsl_preamble_vs);
			break;
		case ShaderStage::Fragment:
			W(hlsl_preamble_fs);
			W(hlsl_d3d11_preamble_fs);
			break;
		case ShaderStage::Geometry:
			W(hlsl_preamble_gs);
			break;
		default:
			break;
		}
		break;
	default:  // OpenGL
		F("#version %d%s\n", lang_.glslVersionNumber, lang_.gles && lang_.glslES30 ? " es" : "");
		// IMPORTANT! Extensions must be the first thing after #version.
		for (size_t i = 0; i < extensions.size(); i++) {
			F("%s\n", extensions[i]);
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
		case ShaderStage::Geometry:
			if (lang_.gles) {
				C("precision highp float;\n");
			}
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
	{
		C("struct VS_OUTPUT {\n");
		for (auto &varying : varyings) {
			F("  %s %s : %s;\n", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		F("  vec4 pos : %s;\n", lang_.shaderLanguage == HLSL_D3D11 ? "SV_Position" : "POSITION");
		C("};\n");

		C("VS_OUTPUT main(  ");  // 2 spaces for the rewind
		C("uint gl_VertexIndex : SV_VertexID, ");
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

void ShaderWriter::BeginFSMain(Slice<UniformDef> uniforms, Slice<VaryingDef> varyings) {
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

		if (flags_ & ShaderWriterFlags::FS_WRITE_DEPTH) {
			C("float gl_FragDepth;\n");
		}

		C("struct PS_OUT {\n");
		C("  vec4 target : SV_Target0;\n");
		if (flags_ & ShaderWriterFlags::FS_WRITE_DEPTH) {
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
		if (flags_ & ShaderWriterFlags::FS_WRITE_DEPTH) {
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

void ShaderWriter::BeginGSMain(Slice<VaryingDef> varyings, Slice<VaryingDef> outVaryings) {
	_assert_(this->stage_ == ShaderStage::Geometry);
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		// Untested, but should work.
		C("\nstruct GS_OUTPUT {\n");
		for (auto &varying : outVaryings) {
			F("  %s %s : %s;\n", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		F("  vec4 pos : %s;\n", lang_.shaderLanguage == HLSL_D3D11 ? "SV_Position" : "POSITION");
		C("};\n");
		C("#define EmitVertex() emit.Append(gsout)\n");

		C("void main(");
		for (auto &varying : varyings) {
			F("  in %s %s : %s, ", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		C("inout TriangleStream<GS_OUTPUT> emit) {\n");
		C("  GS_OUTPUT gsout;\n");
		break;
	case GLSL_VULKAN:
		for (auto &varying : varyings) {
			F("layout(location = %d) %s in %s %s[];  // %s\n", varying.index, varying.precision ? varying.precision : "", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		for (auto &varying : outVaryings) {
			F("layout(location = %d) %s out %s %s;  // %s\n", varying.index, varying.precision ? varying.precision : "", varying.type, varying.name, semanticNames[varying.semantic]);
		}
		C("\nvoid main() {\n");
		break;
	case GLSL_3xx:
		C("\nvoid main() {\n");
		break;
	default:
		break;
	}
}

void ShaderWriter::EndVSMain(Slice<VaryingDef> varyings) {
	_assert_(this->stage_ == ShaderStage::Vertex);
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
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

void ShaderWriter::EndFSMain(const char *vec4_color_variable) {
	_assert_(this->stage_ == ShaderStage::Fragment);
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("  ps_out.target = %s;\n", vec4_color_variable);
		if (flags_ & ShaderWriterFlags::FS_WRITE_DEPTH) {
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

void ShaderWriter::EndGSMain() {
	_assert_(this->stage_ == ShaderStage::Geometry);
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
		F("static const float %s = %f;\n", name, value);
		break;
	default:
		F("#define %s %f\n", name, value);
		break;
	}
}

void ShaderWriter::DeclareSamplers(Slice<SamplerDef> samplers) {
	for (int i = 0; i < (int)samplers.size(); i++) {
		DeclareTexture2D(samplers[i]);
		DeclareSampler2D(samplers[i]);
	}
	samplerDefs_ = samplers;
}

void ShaderWriter::ApplySamplerMetadata(Slice<SamplerDef> samplers) {
	samplerDefs_ = samplers;
}

void ShaderWriter::DeclareTexture2D(const SamplerDef &def) {
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("Texture2D<float4> %s : register(t%d);\n", def.name, def.binding);
		break;
	case GLSL_VULKAN:
		// texBindingBase_ is used for the thin3d descriptor set layout, where they start at 1.
		if (def.flags & SamplerFlags::ARRAY_ON_VULKAN) {
			F("layout(set = 0, binding = %d) uniform sampler2DArray %s;\n", def.binding + texBindingBase_, def.name);
		} else {
			F("layout(set = 0, binding = %d) uniform sampler2D %s;\n", def.binding + texBindingBase_, def.name);
		}
		break;
	default:
		F("uniform sampler2D %s;\n", def.name);
		break;
	}
}

void ShaderWriter::DeclareSampler2D(const SamplerDef &def) {
	// We only use separate samplers in HLSL D3D11, where we have no choice.
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("SamplerState %sSamp : register(s%d);\n", def.name, def.binding);
		break;
	default:
		break;
	}
}

ShaderWriter &ShaderWriter::SampleTexture2D(const char *sampName, const char *uv) {
	const SamplerDef *samp = GetSamplerDef(sampName);
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("%s.Sample(%sSamp, %s)", sampName, sampName, uv);
		break;
	default:
		// Note: we ignore the sampler. make sure you bound samplers to the textures correctly.
		if (samp && (samp->flags & SamplerFlags::ARRAY_ON_VULKAN) && lang_.shaderLanguage == GLSL_VULKAN) {
			const char *index = (flags_ & ShaderWriterFlags::FS_AUTO_STEREO) ? "float(gl_ViewIndex)" : "0.0";
			F("%s(%s, vec3(%s, %s))", lang_.texture, sampName, uv, index);
		} else {
			F("%s(%s, %s)", lang_.texture, sampName, uv);
		}
		break;
	}
	return *this;
}

ShaderWriter &ShaderWriter::SampleTexture2DOffset(const char *sampName, const char *uv, int offX, int offY) {
	const SamplerDef *samp = GetSamplerDef(sampName);

	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("%s.Sample(%sSamp, %s, int2(%d, %d))", sampName, sampName, uv, offX, offY);
		break;
	default:
		// Note: we ignore the sampler. make sure you bound samplers to the textures correctly.
		if (samp && (samp->flags & SamplerFlags::ARRAY_ON_VULKAN) && lang_.shaderLanguage == GLSL_VULKAN) {
			const char *index = (flags_ & ShaderWriterFlags::FS_AUTO_STEREO) ? "float(gl_ViewIndex)" : "0.0";
			F("%sOffset(%s, vec3(%s, %s), ivec2(%d, %d))", lang_.texture, sampName, uv, index, offX, offY);
		} else {
			F("%sOffset(%s, %s, ivec2(%d, %d))", lang_.texture, sampName, uv, offX, offY);
		}
		break;
	}
	return *this;
}

ShaderWriter &ShaderWriter::LoadTexture2D(const char *sampName, const char *uv, int level) {
	const SamplerDef *samp = GetSamplerDef(sampName);

	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("%s.Load(ivec3(%s, %d))", sampName, uv, level);
		break;
	default:
		// Note: we ignore the sampler. make sure you bound samplers to the textures correctly.
		if (samp && (samp->flags & SamplerFlags::ARRAY_ON_VULKAN) && lang_.shaderLanguage == GLSL_VULKAN) {
			const char *index = (flags_ & ShaderWriterFlags::FS_AUTO_STEREO) ? "gl_ViewIndex" : "0";
			F("texelFetch(%s, vec3(%s, %s), %d)", sampName, uv, index, level);
		} else {
			F("texelFetch(%s, %s, %d)", sampName, uv, level);
		}
		break;
	}
	return *this;
}

ShaderWriter &ShaderWriter::GetTextureSize(const char *szVariable, const char *texName) {
	switch (lang_.shaderLanguage) {
	case HLSL_D3D11:
		F("  float2 %s; %s.GetDimensions(%s.x, %s.y);", szVariable, texName, szVariable, szVariable);
		break;
	default:
		// Note: we ignore the sampler. make sure you bound samplers to the textures correctly.
		F("vec2 %s = textureSize(%s, 0);", szVariable, texName);
		break;
	}
	return *this;
}

const SamplerDef *ShaderWriter::GetSamplerDef(const char *name) const {
	for (int i = 0; i < (int)samplerDefs_.size(); i++) {
		if (!strcmp(samplerDefs_[i].name, name)) {
			return &samplerDefs_[i];
		}
	}
	return nullptr;
}
