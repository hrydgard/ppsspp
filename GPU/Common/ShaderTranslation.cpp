// Copyright (c) 2017- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#if !defined(ANDROID)

#include <memory>
#include <vector>
#include <sstream>

// DbgNew is not compatible with Glslang
#ifdef DBG_NEW
#undef new
#endif

#include "base/logging.h"
#include "base/basictypes.h"
#include "base/stringutil.h"
#include "ShaderTranslation.h"
#include "ext/glslang/SPIRV/GlslangToSpv.h"
#include "thin3d/thin3d.h"
#include "ext/SPIRV-Cross/spirv.hpp"
#include "ext/SPIRV-Cross/spirv_common.hpp"
#include "ext/SPIRV-Cross/spirv_cross.hpp"
#include "ext/SPIRV-Cross/spirv_glsl.hpp"
#ifdef _WIN32
#include "ext/SPIRV-Cross/spirv_hlsl.hpp"
#endif

extern void init_resources(TBuiltInResource &Resources);

static EShLanguage GetLanguage(const Draw::ShaderStage stage) {
	switch (stage) {
	case Draw::ShaderStage::VERTEX: return EShLangVertex;
	case Draw::ShaderStage::CONTROL: return EShLangTessControl;
	case Draw::ShaderStage::EVALUATION: return EShLangTessEvaluation;
	case Draw::ShaderStage::GEOMETRY: return EShLangGeometry;
	case Draw::ShaderStage::FRAGMENT: return EShLangFragment;
	case Draw::ShaderStage::COMPUTE: return EShLangCompute;
	default: return EShLangVertex;
	}
}

void ShaderTranslationInit() {
	// TODO: We have TLS issues on mobile
#ifndef _M_ARM
	glslang::InitializeProcess();
#endif
}
void ShaderTranslationShutdown() {
#ifndef _M_ARM
	glslang::FinalizeProcess();
#endif
}

std::string Preprocess(std::string code, ShaderLanguage lang, Draw::ShaderStage stage) {
	// This takes GL up to the version we need.
	return code;
}

struct Builtin {
	const char *needle;
	const char *replacement;
};

// Workaround for deficiency in SPIRV-Cross
static const Builtin builtins[] = {
	{"lessThan",
	R"(
	bool2 lessThan(float2 a, float2 b) { return bool2(a.x < b.x, a.y < b.y); }
	bool3 lessThan(float3 a, float3 b) { return bool3(a.x < b.x, a.y < b.y, a.z < b.z); }
	bool4 lessThan(float4 a, float4 b) { return bool4(a.x < b.x, a.y < b.y, a.z < b.z, a.w < b.w); }
	)"},
	{ "lessThanEqual",
	R"(
	bool2 lessThanEqual(float2 a, float2 b) { return bool2(a.x <= b.x, a.y <= b.y); }
	bool3 lessThanEqual(float3 a, float3 b) { return bool3(a.x <= b.x, a.y <= b.y, a.z <= b.z); }
	bool4 lessThanEqual(float4 a, float4 b) { return bool4(a.x <= b.x, a.y <= b.y, a.z <= b.z, a.w <= b.w); }
	)" },
	{ "greaterThan",
	R"(
	bool2 greaterThan(float2 a, float2 b) { return bool2(a.x > b.x, a.y > b.y); }
	bool3 greaterThan(float3 a, float3 b) { return bool3(a.x > b.x, a.y > b.y, a.z > b.z); }
	bool4 greaterThan(float4 a, float4 b) { return bool4(a.x > b.x, a.y > b.y, a.z > b.z, a.w > b.w); }
	)" },
	{ "greaterThanEqual",
	R"(
	bool2 greaterThanEqual(float2 a, float2 b) { return bool2(a.x >= b.x, a.y >= b.y); }
	bool3 greaterThanEqual(float3 a, float3 b) { return bool3(a.x >= b.x, a.y >= b.y, a.z >= b.z); }
	bool4 greaterThanEqual(float4 a, float4 b) { return bool4(a.x >= b.x, a.y >= b.y, a.z >= b.z, a.w >= b.w); }
	)" },
	{ "equal",
	R"(
	bool2 equal(float2 a, float2 b) { return bool2(a.x == b.x, a.y == b.y); }
	bool3 equal(float3 a, float3 b) { return bool3(a.x == b.x, a.y == b.y, a.z == b.z); }
	bool4 equal(float4 a, float4 b) { return bool4(a.x == b.x, a.y == b.y, a.z == b.z, a.w == b.w); }
	)" },
	{ "notEqual",
	R"(
	bool2 notEqual(float2 a, float2 b) { return bool2(a.x != b.x, a.y != b.y); }
	bool3 notEqual(float3 a, float3 b) { return bool3(a.x != b.x, a.y != b.y, a.z != b.z); }
	bool4 notEqual(float4 a, float4 b) { return bool4(a.x != b.x, a.y != b.y, a.z != b.z, a.w != b.w); }
	)" },
};

static const Builtin replacements[] = {
	{ "mix(", "lerp(" },
	{ "fract(", "frac(" },
};

static const char *cbufferDecl = R"(
cbuffer data : register(b0) {
	float2 u_texelDelta;
	float2 u_pixelDelta;
	float4 u_time;
};
)";

// SPIRV-Cross' HLSL output has some deficiencies we need to work around.
// Also we need to rip out single uniforms and replace them with blocks.
// Should probably do it in the source shader instead and then back translate to old style GLSL, but
// SPIRV-Cross currently won't compile with the Android NDK so I can't be bothered.
std::string Postprocess(std::string code, ShaderLanguage lang, Draw::ShaderStage stage) {
	if (lang != HLSL_D3D11)
		return code;

	std::stringstream out;

	// Output the uniform buffer.
	out << cbufferDecl;

	// Add the builtins if required.
	for (int i = 0; i < ARRAY_SIZE(builtins); i++) {
		if (!builtins[i].needle)
			continue;
		if (code.find(builtins[i].needle) != std::string::npos) {
			out << builtins[i].replacement;
		}
	}

	// Perform some replacements
	for (int i = 0; i < ARRAY_SIZE(replacements); i++) {
		code = ReplaceAll(code, replacements[i].needle, replacements[i].replacement);
	}

	// Alright, now let's go through it line by line and zap the single uniforms.
	std::string line;
	std::stringstream instream(code);
	while (std::getline(instream, line)) {
		if (line.find("uniform float") != std::string::npos)
			continue;
		out << line << "\n";
	}
	std::string output = out.str();
	return output;
}

bool TranslateShader(std::string *dest, ShaderLanguage destLang, TranslatedShaderMetadata *destMetadata, std::string src, ShaderLanguage srcLang, Draw::ShaderStage stage, std::string *errorMessage) {
	if (srcLang != GLSL_300 && srcLang != GLSL_140)
		return false;

#ifdef _M_ARM
	return false;
#endif

	glslang::TProgram program;
	const char *shaderStrings[1];

	TBuiltInResource Resources;
	init_resources(Resources);

	// Enable SPIR-V and Vulkan rules when parsing GLSL
	EShMessages messages = EShMessages::EShMsgDefault; // (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

	EShLanguage shaderStage = GetLanguage(stage);
	glslang::TShader shader(shaderStage);

	std::string preprocessed = Preprocess(src, srcLang, stage);

	shaderStrings[0] = src.c_str();
	shader.setStrings(shaderStrings, 1);
	if (!shader.parse(&Resources, 100, EProfile::ECompatibilityProfile, false, false, messages)) {
		ELOG("%s", shader.getInfoLog());
		ELOG("%s", shader.getInfoDebugLog());
		if (errorMessage) {
			*errorMessage = shader.getInfoLog();
			(*errorMessage) += shader.getInfoDebugLog();
		}
		return false; // something didn't work
	}

	// Note that program does not take ownership of &shader, so this is fine.
	program.addShader(&shader);

	if (!program.link(messages)) {
		ELOG("%s", shader.getInfoLog());
		ELOG("%s", shader.getInfoDebugLog());
		if (errorMessage) {
			*errorMessage = shader.getInfoLog();
			(*errorMessage) += shader.getInfoDebugLog();
		}
		return false;
	}

	std::vector<unsigned int> spirv;
	// Can't fail, parsing worked, "linking" worked.
	glslang::GlslangToSpv(*program.getIntermediate(shaderStage), spirv);

	// Alright, step 1 done. Now let's take this SPIR-V shader and output in our desired format.

	switch (destLang) {
	case GLSL_VULKAN:
		return false;  // TODO
#ifdef _WIN32
	case HLSL_DX9:
	{
		spirv_cross::CompilerHLSL hlsl(spirv);
		spirv_cross::CompilerHLSL::Options options{};
		options.fixup_clipspace = true;
		options.shader_model = 30;
		hlsl.set_options(options);
		*dest = hlsl.compile();
		return true;
	}
	case HLSL_D3D11:
	{
		spirv_cross::CompilerHLSL hlsl(spirv);
		spirv_cross::ShaderResources resources = hlsl.get_shader_resources();

		int i = 0;
		for (auto &resource : resources.sampled_images) {
			// int location = hlsl.get_decoration(resource.id, spv::DecorationLocation);
			hlsl.set_decoration(resource.id, spv::DecorationLocation, i);
			i++;
		}
		spirv_cross::CompilerHLSL::Options options{};
		options.fixup_clipspace = true;
		options.shader_model = 50;
		hlsl.set_options(options);
		std::string raw = hlsl.compile();
		*dest = Postprocess(raw, destLang, stage);
		return true;
	}
#endif
	case GLSL_140:
	{
		spirv_cross::CompilerGLSL glsl(std::move(spirv));
		// The SPIR-V is now parsed, and we can perform reflection on it.
		spirv_cross::ShaderResources resources = glsl.get_shader_resources();
		// Get all sampled images in the shader.
		for (auto &resource : resources.sampled_images) {
			unsigned set = glsl.get_decoration(resource.id, spv::DecorationDescriptorSet);
			unsigned binding = glsl.get_decoration(resource.id, spv::DecorationBinding);
			printf("Image %s at set = %u, binding = %u\n", resource.name.c_str(), set, binding);
			// Modify the decoration to prepare it for GLSL.
			glsl.unset_decoration(resource.id, spv::DecorationDescriptorSet);
			// Some arbitrary remapping if we want.
			glsl.set_decoration(resource.id, spv::DecorationBinding, set * 16 + binding);
		}
		// Set some options.
		spirv_cross::CompilerGLSL::Options options;
		options.version = 140;
		options.es = true;
		glsl.set_options(options);

		// Compile to GLSL, ready to give to GL driver.
		*dest = glsl.compile();
		return true;
	}
	default:
		return false;
	}
}

#endif
