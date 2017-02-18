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

#ifndef ANDROID

#include <memory>
#include <vector>

// DbgNew is not compatible with Glslang
#ifdef DBG_NEW
#undef new
#endif

#include "base/logging.h"
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

bool TranslateShader(std::string *dest, ShaderLanguage destLang, TranslatedShaderMetadata *destMetadata, std::string src, ShaderLanguage srcLang, Draw::ShaderStage stage, std::string *errorMessage) {
	if (srcLang != GLSL_300)
		return false;

	glslang::TProgram program;
	const char *shaderStrings[1];

	TBuiltInResource Resources;
	init_resources(Resources);

	// Enable SPIR-V and Vulkan rules when parsing GLSL
	EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);

	EShLanguage shaderStage = GetLanguage(stage);
	glslang::TShader shader(shaderStage);

	shaderStrings[0] = src.c_str();
	shader.setStrings(shaderStrings, 1);

	if (!shader.parse(&Resources, 100, false, messages)) {
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

	// Alright, step 1 done. Now let's takes this SPIR-V shader and output in our desired format.

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
		spirv_cross::CompilerHLSL::Options options{};
		options.fixup_clipspace = true;
		options.shader_model = 50;
		hlsl.set_options(options);
		*dest = hlsl.compile();
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