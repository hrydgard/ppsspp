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

// Helper: find matching closing brace for an opening brace at position 'start'.
// Skips // line comments, /* */ block comments, and "..." string literals so braces
// inside them do not affect depth counting (untrusted shader source may contain them).
static size_t FindMatchingBrace(const std::string &src, size_t start) {
	if (start >= src.size() || src[start] != '{') return std::string::npos;
	int depth = 0;
	bool inLineComment = false, inBlockComment = false, inString = false;
	for (size_t i = start; i < src.size(); i++) {
		char c = src[i];
		if (inString) {
			if (c == '\\' && i + 1 < src.size()) { i++; continue; }  // skip escaped char
			if (c == '"') inString = false;
		} else if (inLineComment) {
			if (c == '\n') inLineComment = false;
		} else if (inBlockComment) {
			if (c == '*' && i + 1 < src.size() && src[i + 1] == '/') { inBlockComment = false; i++; }
		} else if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
			inLineComment = true; i++;
		} else if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
			inBlockComment = true; i++;
		} else if (c == '"') {
			inString = true;
		} else if (c == '{') {
			depth++;
		} else if (c == '}') {
			depth--;
			if (depth == 0) return i;
		}
	}
	return std::string::npos;
}

// Transform slang GLSL source: merge push_constant blocks into a single std140 UBO.
// Real slang shaders declare TWO uniform blocks: a std140 UBO (e.g. "layout(std140,set=0,binding=0) uniform UBO{mat4 MVP;} global;")
// and a push_constant block (e.g. "layout(push_constant) uniform Push{vec4 SourceSize;} params;").
// PPSSPP's thin3d only exposes ONE dynamic UBO (set 0 / binding 0) — no push-constant API.
// So we rewrite the source: emit ONE combined std140 block with all members, then #define both instance names
// to point to the merged block, so existing "global.MVP" / "params.SourceSize" accesses still resolve.
// Returns true if transform succeeded OR source had no push_constant (no-op passthrough). False on parse failure.
static bool TransformPushConstantToUBO(const std::string &src, std::string *out, std::string *error) {
	*out = src;  // default: no change

	// Step 1: Find push_constant block. Pattern: "layout(...push_constant...) uniform <BlockName> { ... } <instance>;"
	size_t pushPos = src.find("push_constant");
	if (pushPos == std::string::npos) {
		return true;  // no push_constant; original source is fine
	}

	// Find the 'uniform' keyword AFTER push_constant layout (it should be within ~50 chars of push_constant)
	size_t uniformPos = src.find("uniform", pushPos);
	if (uniformPos == std::string::npos || uniformPos - pushPos > 200) {
		*error = "push_constant transform: could not find 'uniform' keyword after push_constant layout";
		return false;
	}

	// Find opening brace of push_constant block
	size_t pushBraceStart = src.find('{', uniformPos);
	if (pushBraceStart == std::string::npos) {
		*error = "push_constant transform: could not find opening brace for push_constant block";
		return false;
	}

	size_t pushBraceEnd = FindMatchingBrace(src, pushBraceStart);
	if (pushBraceEnd == std::string::npos) {
		*error = "push_constant transform: could not find matching closing brace for push_constant block";
		return false;
	}

	// Extract push_constant members (between braces)
	std::string pushMembers = src.substr(pushBraceStart + 1, pushBraceEnd - pushBraceStart - 1);

	// Extract push_constant instance name: it's between '}' and ';'
	size_t pushSemicolon = src.find(';', pushBraceEnd);
	if (pushSemicolon == std::string::npos) {
		*error = "push_constant transform: could not find semicolon after push_constant block";
		return false;
	}
	std::string pushInstanceRaw = src.substr(pushBraceEnd + 1, pushSemicolon - pushBraceEnd - 1);
	// Trim whitespace
	size_t instStart = pushInstanceRaw.find_first_not_of(" \t\n\r");
	size_t instEnd = pushInstanceRaw.find_last_not_of(" \t\n\r");
	std::string pushInstance = (instStart == std::string::npos) ? "" : pushInstanceRaw.substr(instStart, instEnd - instStart + 1);
	if (pushInstance.empty()) {
		*error = "push_constant transform: could not extract push_constant instance name";
		return false;
	}

	// Find the layout line start (scan backwards from pushPos to find start of line with 'layout')
	size_t pushLayoutStart = src.rfind("layout", pushPos);
	if (pushLayoutStart == std::string::npos) pushLayoutStart = pushPos;  // fallback

	// The push_constant block spans [pushLayoutStart, pushSemicolon].
	size_t pushBlockStart = pushLayoutStart;
	size_t pushBlockEnd = pushSemicolon + 1;

	// Step 2: Find the std140 UBO block if present, regardless of whether it appears BEFORE or AFTER
	// the push_constant block. (Real shaders order these either way — e.g. crt-lottes declares
	// push_constant first, then the std140 UBO. Requiring UBO-first previously dropped MVP/OutputSize,
	// producing black output.) We locate the std140 block anywhere EXCEPT overlapping the push block.
	std::string uboMembers;
	std::string uboInstance;
	size_t uboBlockStart = std::string::npos, uboBlockEnd = std::string::npos;

	size_t searchFrom = 0;
	while (true) {
		size_t uboPos = src.find("std140", searchFrom);
		if (uboPos == std::string::npos) break;
		// Skip a match that is inside the push_constant block itself.
		if (uboPos >= pushBlockStart && uboPos < pushBlockEnd) {
			searchFrom = pushBlockEnd;
			continue;
		}
		size_t uboUniformPos = src.find("uniform", uboPos);
		size_t uboBraceStart = (uboUniformPos == std::string::npos) ? std::string::npos : src.find('{', uboUniformPos);
		if (uboBraceStart != std::string::npos) {
			size_t uboBraceEnd = FindMatchingBrace(src, uboBraceStart);
			if (uboBraceEnd != std::string::npos) {
				size_t uboSemicolon = src.find(';', uboBraceEnd);
				if (uboSemicolon != std::string::npos) {
					uboMembers = src.substr(uboBraceStart + 1, uboBraceEnd - uboBraceStart - 1);
					std::string uboInstRaw = src.substr(uboBraceEnd + 1, uboSemicolon - uboBraceEnd - 1);
					size_t uInstStart = uboInstRaw.find_first_not_of(" \t\n\r");
					size_t uInstEnd = uboInstRaw.find_last_not_of(" \t\n\r");
					uboInstance = (uInstStart == std::string::npos) ? "" : uboInstRaw.substr(uInstStart, uInstEnd - uInstStart + 1);
					size_t uboLayoutStart = src.rfind("layout", uboPos);
					if (uboLayoutStart == std::string::npos) uboLayoutStart = uboPos;
					uboBlockStart = uboLayoutStart;
					uboBlockEnd = uboSemicolon + 1;
				}
			}
		}
		break;
	}

	// Step 3: Build merged block. Order: UBO members first (if present), then push members.
	// (std140 offsets are recomputed by SPIRV-Cross reflection on the transformed source, so the
	// textual member order here only needs to be self-consistent, not match the original.)
	std::string mergedMembers;
	if (!uboMembers.empty()) {
		mergedMembers = uboMembers;
		if (!mergedMembers.empty() && mergedMembers.back() != '\n') mergedMembers += "\n";
	}
	mergedMembers += pushMembers;

	// Step 4: Emit combined block + #defines
	std::string combined = "layout(std140, set = 0, binding = 0) uniform _SlangMergedUBO {\n";
	combined += mergedMembers;
	combined += "\n} _slang_ubo;\n";

	// Add #defines so old instance names resolve to new merged block
	if (!uboInstance.empty()) {
		combined += "#define " + uboInstance + " _slang_ubo\n";
	}
	combined += "#define " + pushInstance + " _slang_ubo\n";

	// Step 5: Delete both original blocks (order-independent) and insert the combined block where the
	// FIRST of the two blocks began. Build the output by walking the two [start,end] ranges in order.
	std::string result;
	bool haveUbo = (uboBlockStart != std::string::npos && uboBlockEnd != std::string::npos);
	if (haveUbo) {
		// Determine which block comes first in the source.
		size_t firstStart, firstEnd, secondStart, secondEnd;
		if (uboBlockStart < pushBlockStart) {
			firstStart = uboBlockStart; firstEnd = uboBlockEnd;
			secondStart = pushBlockStart; secondEnd = pushBlockEnd;
		} else {
			firstStart = pushBlockStart; firstEnd = pushBlockEnd;
			secondStart = uboBlockStart; secondEnd = uboBlockEnd;
		}
		result = src.substr(0, firstStart);              // text before the first block
		result += combined;                               // combined block replaces the first
		result += src.substr(firstEnd, secondStart - firstEnd);  // text between the two blocks
		result += src.substr(secondEnd);                  // text after the second block
	} else {
		// Only push_constant: delete it, insert combined in its place.
		result = src.substr(0, pushBlockStart);
		result += combined;
		result += src.substr(pushBlockEnd);
	}

	*out = result;
	return true;
}

bool ReflectSlangSource(const SlangSource &src, const SlangClassifyContext &ctx, PassReflection *out, std::string *error,
                        std::string *outTransformedVert, std::string *outTransformedFrag) {
	// Phase 2 Task 9c: transform push_constant blocks into a single merged UBO before compilation.
	// Real slang shaders use two blocks: a std140 UBO (e.g. global.MVP) and push_constant (e.g. params.SourceSize).
	// PPSSPP thin3d only supports one dynamic UBO (set 0, binding 0). Transform merges both into one block.
	std::string transformedVert, transformedFrag;
	if (!TransformPushConstantToUBO(src.vertex, &transformedVert, error)) {
		*error = "vertex stage push_constant transform failed: " + *error;
		return false;
	}
	if (!TransformPushConstantToUBO(src.fragment, &transformedFrag, error)) {
		*error = "fragment stage push_constant transform failed: " + *error;
		return false;
	}

	// Hand the transformed GLSL back to the caller so shader modules are built from the SAME
	// source we reflected. Compiling the original push_constant source instead would leave the
	// push members (SourceSize/OutputSize/FrameCount/...) unfed on the GPU — they'd read garbage
	// while the std140 UBO params read fine, producing frame-dependent corruption.
	if (outTransformedVert) *outTransformedVert = transformedVert;
	if (outTransformedFrag) *outTransformedFrag = transformedFrag;

	std::vector<unsigned int> vspv, fspv;
	if (!CompileStageToSpirv(EShLangVertex, transformedVert, &vspv, error)) return false;
	if (!CompileStageToSpirv(EShLangFragment, transformedFrag, &fspv, error)) return false;

	// Reflect both stages. After transform, push_constant blocks should be gone; if any remain, that's a transform bug.
	spirv_cross::Compiler vert(vspv);
	spirv_cross::ShaderResources vertRes = vert.get_shader_resources();
	if (!vertRes.push_constant_buffers.empty()) {
		*error = "push_constant transform incomplete (vertex stage still has push_constant after transform; shader: " + src.name + ")";
		return false;
	}

	// Reflect the fragment stage for UBO + samplers; merge vertex-only UBO members if present.
	spirv_cross::Compiler frag(fspv);
	spirv_cross::ShaderResources res = frag.get_shader_resources();

	// Safety net: if push_constant still present after transform, fail with clear message.
	if (!res.push_constant_buffers.empty()) {
		*error = "push_constant transform incomplete (fragment stage still has push_constant after transform; shader: " + src.name + ")";
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
			m.semantic = ClassifyUniform(m.name, ctx, &m.index);
			if (m.semantic == SlangSemantic::Unknown) {
				*error = "unsupported uniform member: " + m.name;
				return false;
			}
			out->uboMembers.push_back(m);
		}
	}

	for (const auto &img : res.sampled_images) {
		SlangTextureBinding t;
		t.name = frag.get_name(img.id);
		t.binding = frag.get_decoration(img.id, spv::DecorationBinding);
		t.semantic = ClassifyTexture(t.name, ctx, &t.index);
		if (t.semantic == SlangSemantic::Unknown) {
			*error = "unsupported texture: " + t.name;
			return false;
		}
		out->textures.push_back(t);
	}
	return true;
}

bool CompileSlangPass(Draw::DrawContext *draw, const SlangSource &src, const SlangClassifyContext &ctx,
                      SlangCompiledPass *out, std::string *error) {
	// First reflect to get the semantics (internally compiles to SPIR-V for reflection).
	// Capture the push_constant->UBO transformed GLSL so the shader modules below are built from
	// the exact source we reflected — see ReflectSlangSource's contract.
	std::string transformedVert, transformedFrag;
	if (!ReflectSlangSource(src, ctx, &out->reflection, error, &transformedVert, &transformedFrag)) {
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
		(const uint8_t *)transformedVert.c_str(), transformedVert.size(), src.name.empty() ? "slang_vs" : src.name.c_str());
	Draw::ShaderModule *fs = draw->CreateShaderModule(ShaderStage::Fragment, GLSL_VULKAN,
		(const uint8_t *)transformedFrag.c_str(), transformedFrag.size(), src.name.empty() ? "slang_fs" : src.name.c_str());

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
