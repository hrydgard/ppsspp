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
#include "Common/GPU/ShaderTranslation.h"
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

static uint32_t MemberSizeBytes(const spirv_cross::SPIRType &type) {
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

	// Reflect both stages to detect unsupported push_constant blocks (Phase 1 only supports UBOs).
	spirv_cross::Compiler vert(vspv);
	spirv_cross::ShaderResources vertRes = vert.get_shader_resources();
	if (!vertRes.push_constant_buffers.empty()) {
		*error = "slang push_constant blocks are not supported in Phase 1 (shader: " + src.name + ")";
		return false;
	}

	// Reflect the fragment stage for UBO + samplers; merge vertex-only UBO members if present.
	spirv_cross::Compiler frag(fspv);
	spirv_cross::ShaderResources res = frag.get_shader_resources();

	// Phase 1 does not support push_constant — full push-constant packing is a future-phase feature.
	if (!res.push_constant_buffers.empty()) {
		*error = "slang push_constant blocks are not supported in Phase 1 (shader: " + src.name + ")";
		return false;
	}

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
			m.sizeBytes = MemberSizeBytes(frag.get_type(blockType.member_types[i]));
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
	// First reflect to get the semantics (internally compiles to SPIR-V for reflection)
	if (!ReflectSlangSource(src, &out->reflection, error)) {
		return false;
	}

	// Phase 1: Vulkan-only. Slang source is already Vulkan GLSL (#version 450 with
	// layout(set,binding) etc.) — exactly what PPSSPP's Vulkan CreateShaderModule expects.
	// Non-Vulkan backends (D3D11/GL) are a Phase 5 concern requiring a SPIR-V→backend
	// cross-compile path (TranslateShader cannot accept Vulkan-GLSL as source).
	ShaderLanguage backendLang = draw->GetShaderLanguageDesc().shaderLanguage;
	if (backendLang != GLSL_VULKAN) {
		*error = "slang passes require the Vulkan backend in Phase 1 (got a non-Vulkan backend)";
		return false;
	}

	// Create shader modules directly from GLSL source strings.
	// PPSSPP's Vulkan CreateShaderModule (VKShaderModule::Compile) takes GLSL source text,
	// stores source_ = (const char*)data, and runs GLSLtoSPV(..., GLSLVariant::VULKAN, ...) on it.
	Draw::ShaderModule *vs = draw->CreateShaderModule(ShaderStage::Vertex, GLSL_VULKAN,
		(const uint8_t *)src.vertex.c_str(), src.vertex.size(), src.name.empty() ? "slang_vs" : src.name.c_str());
	Draw::ShaderModule *fs = draw->CreateShaderModule(ShaderStage::Fragment, GLSL_VULKAN,
		(const uint8_t *)src.fragment.c_str(), src.fragment.size(), src.name.empty() ? "slang_fs" : src.name.c_str());

	if (!vs || !fs) {
		*error = "failed to create shader modules";
		if (vs) vs->Release();
		if (fs) fs->Release();
		return false;
	}

	// Build UniformBufferDesc from reflection
	UniformBufferDesc uboDesc{};
	if (out->reflection.uboSizeBytes > 0) {
		uboDesc.uniformBufferSize = out->reflection.uboSizeBytes;
		// Populate uniforms array with member info
		for (const auto &m : out->reflection.uboMembers) {
			UniformType utype = UniformType::FLOAT4;  // default, we mostly use vec4/mat4
			if (m.sizeBytes == 64) utype = UniformType::MATRIX4X4;
			else if (m.sizeBytes == 4) utype = UniformType::FLOAT1;
			else if (m.sizeBytes == 8) utype = UniformType::FLOAT2;
			uboDesc.uniforms.push_back({m.name.c_str(), -1, -1, utype, (int16_t)m.offsetBytes});
		}
	}

	using namespace Draw;

	// The Vulkan backend maps AttributeDesc.location DIRECTLY to the shader's
	// `layout(location = N)` input. Slang shaders follow the libretro convention:
	//   layout(location = 0) in vec4 Position;
	//   layout(location = 1) in vec2 TexCoord;
	// So we MUST use explicit locations 0 and 1 here — NOT PPSSPP's Semantic enum
	// values (SEM_TEXCOORD0 == 3), which would leave TexCoord unfed (constant) and
	// break UV interpolation. Slang shaders declare no color input; we still supply
	// the quad's color attribute at an unused location (2) so it never collides.
	// Stride matches SlangFilterChain's quad vertex: float pos[3] + float uv[2] + uint32 color = 24 bytes.
	const int LOC_POSITION = 0;
	const int LOC_TEXCOORD = 1;
	const int LOC_COLOR = 2;
	InputLayoutDesc inputDesc = {
		5 * sizeof(float) + sizeof(uint32_t),  // pos(3f) + uv(2f) + color(u32) = 24 bytes
		{
			{ LOC_POSITION, DataFormat::R32G32B32_FLOAT, 0 },
			{ LOC_TEXCOORD, DataFormat::R32G32_FLOAT, 12 },
			{ LOC_COLOR, DataFormat::R8G8B8A8_UNORM, 20 },
		},
	};

	InputLayout *inputLayout = draw->CreateInputLayout(inputDesc);
	DepthStencilState *depth = draw->CreateDepthStencilState({ false, false, Comparison::LESS });
	BlendState *blendOff = draw->CreateBlendState({ false, 0xF });
	RasterState *rasterNoCull = draw->CreateRasterState({});

	PipelineDesc pipelineDesc{
		Primitive::TRIANGLE_STRIP,
		{ vs, fs },
		inputLayout,
		depth,
		blendOff,
		rasterNoCull,
		uboDesc.uniformBufferSize > 0 ? &uboDesc : nullptr
	};
	out->pipeline = draw->CreateGraphicsPipeline(pipelineDesc, "slang-pass");

	// Release intermediate objects
	inputLayout->Release();
	depth->Release();
	blendOff->Release();
	rasterNoCull->Release();
	vs->Release();
	fs->Release();

	if (!out->pipeline) {
		*error = "failed to create graphics pipeline";
		return false;
	}

	return true;
}
