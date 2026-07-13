// Copyright (c) 2026- PPSSPP Project.

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

#include "ppsspp_config.h"
#ifdef DBG_NEW
#undef new
#undef free
#undef malloc
#undef realloc
#endif

#include <vector>
#include "Common/Log.h"
#include "Common/StringUtils.h"
#include "Common/GPU/Shader.h"
#include "GPU/Common/Slang/SlangPassCompiler.h"
#include "ext/glslang/SPIRV/GlslangToSpv.h"
#include "ext/SPIRV-Cross/spirv_cross.hpp"

static bool CompileStageToSpirv(EShLanguage stage, const std::string &src,
                                std::vector<unsigned int> *spirv, std::string *error) {
	TBuiltInResource resources{};
	InitShaderResources(resources);
	// Mirror the verified pattern in VulkanContext.cpp GLSLtoSPV (GLSLVariant::VULKAN):
	// Vulkan+SPIR-V rules, defaultVersion 450, ECoreProfile, forwardCompatible=true.
	// Do NOT add setEnvInput/setEnvClient/setEnvTarget — this glslang fork compiles
	// #version 450 + layout(set=,binding=) with the messages flags alone.
	glslang::TShader shader(stage);
	const char *strings[1] = { src.c_str() };
	shader.setStrings(strings, 1);
	EShMessages messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
	if (!shader.parse(&resources, 450, ECoreProfile, false, true, messages)) {
		*error = std::string("slang parse: ") + shader.getInfoLog() + shader.getInfoDebugLog();
		return false;
	}
	glslang::TProgram program;
	program.addShader(&shader);
	if (!program.link(messages)) {
		*error = std::string("slang link: ") + program.getInfoLog() + program.getInfoDebugLog();
		return false;
	}
	glslang::SpvOptions options;
	options.disableOptimizer = false;
	glslang::GlslangToSpv(*program.getIntermediate(stage), *spirv, &options);
	return !spirv->empty();
}

static uint32_t MemberSizeBytes(const spirv_cross::Compiler &comp, const spirv_cross::SPIRType &type) {
	// vec4 = 16, mat4 = 64, float = 4, uint/int = 4.
	uint32_t base = 4;  // float/int/uint base
	uint32_t comps = type.vecsize * type.columns;
	return base * comps;
}

bool ReflectSlangSource(const SlangSource &src, PassReflection *out, std::string *error) {
	std::vector<unsigned int> vspv, fspv;
	if (!CompileStageToSpirv(EShLangVertex, src.vertex, &vspv, error)) return false;
	if (!CompileStageToSpirv(EShLangFragment, src.fragment, &fspv, error)) return false;

	std::vector<std::string> paramNames;
	for (const auto &p : src.params) paramNames.push_back(p.name);

	// Reflect the fragment stage for UBO + samplers; merge vertex-only UBO members if present.
	spirv_cross::Compiler frag(fspv);
	spirv_cross::ShaderResources res = frag.get_shader_resources();

	out->uboMembers.clear();
	out->textures.clear();
	out->uboBinding = -1;
	out->uboSizeBytes = 0;

	if (!res.uniform_buffers.empty()) {
		const auto &ubo = res.uniform_buffers[0];
		out->uboBinding = frag.get_decoration(ubo.id, spv::DecorationBinding);
		const spirv_cross::SPIRType &blockType = frag.get_type(ubo.base_type_id);
		out->uboSizeBytes = (uint32_t)frag.get_declared_struct_size(blockType);
		uint32_t count = (uint32_t)blockType.member_types.size();
		for (uint32_t i = 0; i < count; i++) {
			SlangUniformMember m;
			m.name = frag.get_member_name(ubo.base_type_id, i);
			m.offsetBytes = frag.type_struct_member_offset(blockType, i);
			m.sizeBytes = MemberSizeBytes(frag, frag.get_type(blockType.member_types[i]));
			m.semantic = ClassifyUniform(m.name, paramNames);
			if (m.semantic == SlangSemantic::Unknown) {
				*error = "unsupported uniform member in Phase 1: " + m.name;
				return false;
			}
			out->uboMembers.push_back(m);
		}
	}

	for (const auto &img : res.sampled_images) {
		SlangTextureBinding t;
		t.name = frag.get_name(img.id);
		t.binding = frag.get_decoration(img.id, spv::DecorationBinding);
		t.semantic = ClassifyTexture(t.name);
		if (t.semantic == SlangSemantic::Unknown) {
			*error = "unsupported texture in Phase 1: " + t.name;
			return false;
		}
		out->textures.push_back(t);
	}
	return true;
}

bool CompileSlangPass(Draw::DrawContext *draw, const SlangSource &src,
                      SlangCompiledPass *out, std::string *error) {
	// First reflect to get the semantics
	if (!ReflectSlangSource(src, &out->reflection, error)) {
		return false;
	}

	// For now, just return true - full pipeline creation requires more setup
	// This will be exercised in Task 9 integration
	*error = "CompileSlangPass: not yet implemented";
	return false;
}
