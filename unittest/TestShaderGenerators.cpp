#include "ppsspp_config.h"
#include <algorithm>

#include "Common/StringUtils.h"

#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "Common/Data/Random/Rng.h"

#include "GPU/Vulkan/VulkanContext.h"

#include "GPU/Common/FragmentShaderGenerator.h"
#include "GPU/Common/VertexShaderGenerator.h"
#include "GPU/Common/ReinterpretFramebuffer.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include "GPU/D3D11/D3D11Util.h"
#include "GPU/D3D11/D3D11Loader.h"

#include "GPU/D3D9/D3DCompilerLoader.h"
#include "GPU/D3D9/D3D9ShaderCompiler.h"
#endif

bool GenerateFShader(FShaderID id, char *buffer, ShaderLanguage lang, Draw::Bugs bugs, std::string *errorString) {
	uint64_t uniformMask;
	switch (lang) {
	case ShaderLanguage::GLSL_VULKAN:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_VULKAN);
		return GenerateFragmentShader(id, buffer, compat, bugs, &uniformMask, errorString);
	}
	case ShaderLanguage::GLSL_1xx:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_1xx);
		return GenerateFragmentShader(id, buffer, compat, bugs, &uniformMask, errorString);
	}
	case ShaderLanguage::GLSL_3xx:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_1xx);
		return GenerateFragmentShader(id, buffer, compat, bugs, &uniformMask, errorString);
	}
	case ShaderLanguage::HLSL_D3D9:
	{
		ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D9);
		return GenerateFragmentShader(id, buffer, compat, bugs, &uniformMask, errorString);
	}
	case ShaderLanguage::HLSL_D3D11:
	{
		ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D11);
		return GenerateFragmentShader(id, buffer, compat, bugs, &uniformMask, errorString);
	}
	default:
		return false;
	}
}

bool GenerateVShader(VShaderID id, char *buffer, ShaderLanguage lang, Draw::Bugs bugs, std::string *errorString) {
	uint32_t attrMask;
	uint64_t uniformMask;
	switch (lang) {
	case ShaderLanguage::GLSL_VULKAN:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_VULKAN);
		return GenerateVertexShader(id, buffer, compat, bugs, &attrMask, &uniformMask, errorString);
	}
	case ShaderLanguage::GLSL_1xx:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_1xx);
		return GenerateVertexShader(id, buffer, compat, bugs, &attrMask, &uniformMask, errorString);
	}
	case ShaderLanguage::GLSL_3xx:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_1xx);
		return GenerateVertexShader(id, buffer, compat, bugs, &attrMask, &uniformMask, errorString);
	}
	case ShaderLanguage::HLSL_D3D9:
	{
		ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D9);
		return GenerateVertexShader(id, buffer, compat, bugs, &attrMask, &uniformMask, errorString);
	}
	case ShaderLanguage::HLSL_D3D11:
	{
		ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D11);
		return GenerateVertexShader(id, buffer, compat, bugs, &attrMask, &uniformMask, errorString);
	}
	default:
		return false;
	}
}

bool TestCompileShader(const char *buffer, ShaderLanguage lang, bool vertex, std::string *errorMessage) {
	std::vector<uint32_t> spirv;
	switch (lang) {
#if PPSSPP_PLATFORM(WINDOWS)
	case ShaderLanguage::HLSL_D3D11:
	{
		auto output = CompileShaderToBytecodeD3D11(buffer, strlen(buffer), vertex ? "vs_4_0" : "ps_4_0", 0);
		return !output.empty();
	}
	case ShaderLanguage::HLSL_D3D9:
	{
		LPD3DBLOB blob = CompileShaderToByteCodeD3D9(buffer, vertex ? "vs_2_0" : "ps_2_0", errorMessage);
		if (blob) {
			blob->Release();
			return true;
		} else {
			return false;
		}
	}
#endif

	case ShaderLanguage::GLSL_VULKAN:
		return GLSLtoSPV(vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, buffer, GLSLVariant::VULKAN, spirv, errorMessage);
	case ShaderLanguage::GLSL_1xx:
		return GLSLtoSPV(vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, buffer, GLSLVariant::GL140, spirv, errorMessage);
	case ShaderLanguage::GLSL_3xx:
		return GLSLtoSPV(vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, buffer, GLSLVariant::GLES300, spirv, errorMessage);
	default:
		return false;
	}
}

void PrintDiff(const char *a, const char *b) {
	// Stupidest diff ever: Just print both lines, and a few around it, when we find a mismatch.
	std::vector<std::string> a_lines;
	std::vector<std::string> b_lines;
	SplitString(a, '\n', a_lines);
	SplitString(b, '\n', b_lines);
	for (size_t i = 0; i < a_lines.size() && i < b_lines.size(); i++) {
		if (a_lines[i] != b_lines[i]) {
			// Print some context
			for (size_t j = std::max((int)i - 4, 0); j < i; j++) {
				printf("%s\n", a_lines[j].c_str());
			}
			printf("DIFF found at line %d:\n", (int)i);
			printf("a: %s\n", a_lines[i].c_str());
			printf("b: %s\n", b_lines[i].c_str());
			printf("...continues...\n");
			for (size_t j = i + 1; j < i + 5 && j < a_lines.size() && j < b_lines.size(); j++) {
				printf("a: %s\n", a_lines[j].c_str());
				printf("b: %s\n", b_lines[j].c_str());
			}
			printf("==================\n");
			return;
		}
	}
}

const char *ShaderLanguageToString(ShaderLanguage lang) {
	switch (lang) {
	case HLSL_D3D11: return "HLSL_D3D11";
	case HLSL_D3D9: return "HLSL_D3D9";
	case GLSL_VULKAN: return "GLSL_VULKAN";
	case GLSL_1xx: return "GLSL_1xx";
	case GLSL_3xx: return "GLSL_3xx";
	default: return "N/A";
	}
}

bool TestReinterpretShaders() {
	ShaderLanguage languages[] = {
#if PPSSPP_PLATFORM(WINDOWS)
		ShaderLanguage::HLSL_D3D11,
#endif
		ShaderLanguage::GLSL_VULKAN,
		ShaderLanguage::GLSL_3xx,
	};
	GEBufferFormat fmts[3] = {
		GE_FORMAT_565,
		GE_FORMAT_5551,
		GE_FORMAT_4444,
	};
	char *buffer = new char[65536];

	// Generate all despite failures - it's only 6.
	bool failed = false;

	for (int k = 0; k < ARRAY_SIZE(languages); k++) {
		ShaderLanguageDesc desc(languages[k]);
		if (!GenerateReinterpretVertexShader(buffer, desc)) {
			printf("Failed!\n%s\n", buffer);
			failed = true;
		} else {
			std::string errorMessage;
			if (!TestCompileShader(buffer, languages[k], true, &errorMessage)) {
				printf("Error compiling fragment shader:\n\n%s\n\n%s\n", LineNumberString(buffer).c_str(), errorMessage.c_str());
				failed = true;
				return false;
			} else {
				//printf("===\n%s\n===\n", buffer);
			}
		}
	}

	for (int k = 0; k < ARRAY_SIZE(languages); k++) {
		printf("=== %s ===\n\n", ShaderLanguageToString(languages[k]));

		ShaderLanguageDesc desc(languages[k]);

		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				if (i == j)
					continue;  // useless shader!
				if (!GenerateReinterpretFragmentShader(buffer, fmts[i], fmts[j], desc)) {
					printf("Failed!\n%s\n", buffer);
					failed = true;
				} else {
					std::string errorMessage;
					if (!TestCompileShader(buffer, languages[k], false, &errorMessage)) {
						printf("Error compiling fragment shader %d:\n\n%s\n\n%s\n", (int)j, LineNumberString(buffer).c_str(), errorMessage.c_str());
						failed = true;
						return false;
					} else {
						printf("===\n%s\n===\n", buffer);
					}
				}
			}
		}

	}
	return !failed;
}

const ShaderLanguage languages[] = {
#if PPSSPP_PLATFORM(WINDOWS)
	ShaderLanguage::HLSL_D3D9,
	ShaderLanguage::HLSL_D3D11,
#endif
	ShaderLanguage::GLSL_VULKAN,
	ShaderLanguage::GLSL_1xx,
	ShaderLanguage::GLSL_3xx,
};
const int numLanguages = ARRAY_SIZE(languages);

bool TestVertexShaders() {
	char *buffer[numLanguages];

	for (int i = 0; i < numLanguages; i++) {
		buffer[i] = new char[65536];
	}
	GMRng rng;
	int successes = 0;
	int count = 700;

	Draw::Bugs bugs;

	// Generate a bunch of random vertex shader IDs, try to generate shader source.
	// Then compile it and check that it's ok.
	for (int i = 0; i < count; i++) {
		uint32_t bottom = rng.R32();
		uint32_t top = rng.R32();
		VShaderID id;
		id.d[0] = bottom;
		id.d[1] = top;

		// The generated bits need some adjustment:

		// We don't use these bits in the HLSL shader generator.
		id.SetBits(VS_BIT_WEIGHT_FMTSCALE, 2, 0);
		// If mode is through, we won't do hardware transform.
		if (id.Bit(VS_BIT_IS_THROUGH)) {
			id.SetBit(VS_BIT_USE_HW_TRANSFORM, 0);
		}
		if (!id.Bit(VS_BIT_USE_HW_TRANSFORM)) {
			id.SetBit(VS_BIT_ENABLE_BONES, 0);
		}

		bool generateSuccess[numLanguages]{};
		std::string genErrorString[numLanguages];

		for (int j = 0; j < numLanguages; j++) {
			generateSuccess[j] = GenerateVShader(id, buffer[j], languages[j], bugs, &genErrorString[j]);
			if (!genErrorString[j].empty()) {
				printf("%s\n", genErrorString[j].c_str());
			}
		}

		// Now that we have the strings ready for easy comparison (buffer,4 in the watch window),
		// let's try to compile them.
		for (int j = 0; j < numLanguages; j++) {
			if (generateSuccess[j]) {
				std::string errorMessage;
				if (!TestCompileShader(buffer[j], languages[j], true, &errorMessage)) {
					printf("Error compiling vertex shader %d:\n\n%s\n\n%s\n", (int)j, LineNumberString(buffer[j]).c_str(), errorMessage.c_str());
					return false;
				}
				successes++;
			}
		}
	}

	printf("%d/%d vertex shaders generated (it's normal that it's not all, there are invalid bit combos)\n", successes, count * numLanguages);

	for (int i = 0; i < numLanguages; i++) {
		delete[] buffer[i];
	}
	return true;
}

bool TestFragmentShaders() {
	char *buffer[numLanguages];

	for (int i = 0; i < numLanguages; i++) {
		buffer[i] = new char[65536];
	}
	GMRng rng;
	int successes = 0;
	int count = 300;

	Draw::Bugs bugs;

	// Generate a bunch of random fragment shader IDs, try to generate shader source.
	// Then compile it and check that it's ok.
	for (int i = 0; i < count; i++) {
		uint32_t bottom = rng.R32();
		uint32_t top = rng.R32();
		FShaderID id;
		id.d[0] = bottom;
		id.d[1] = top;

		// bits we don't need to test because they are irrelevant on d3d11
		id.SetBit(FS_BIT_NO_DEPTH_CANNOT_DISCARD_STENCIL, false);
		id.SetBit(FS_BIT_SHADER_DEPAL, false);

		// DX9 disabling:
		if (static_cast<ReplaceAlphaType>(id.Bits(FS_BIT_STENCIL_TO_ALPHA, 2)) == ReplaceAlphaType::REPLACE_ALPHA_DUALSOURCE)
			continue;

		bool generateSuccess[numLanguages]{};
		std::string genErrorString[numLanguages];

		for (int j = 0; j < numLanguages; j++) {
			generateSuccess[j] = GenerateFShader(id, buffer[j], languages[j], bugs, &genErrorString[j]);
			if (!genErrorString[j].empty()) {
				printf("%s\n", genErrorString[j].c_str());
			}
			// We ignore the contents of the error string here, not even gonna try to compile if it errors.
		}

		// Now that we have the strings ready for easy comparison (buffer,4 in the watch window),
		// let's try to compile them.
		for (int j = 0; j < numLanguages; j++) {
			if (generateSuccess[j]) {
				std::string errorMessage;
				if (!TestCompileShader(buffer[j], languages[j], false, &errorMessage)) {
					printf("Error compiling fragment shader:\n\n%s\n\n%s\n", LineNumberString(buffer[j]).c_str(), errorMessage.c_str());
					return false;
				}
				successes++;
			}
		}
	}

	printf("%d/%d fragment shaders generated (it's normal that it's not all, there are invalid bit combos)\n", successes, count * numLanguages);

	for (int i = 0; i < numLanguages; i++) {
		delete[] buffer[i];
	}
	return true;
}

bool TestShaderGenerators() {
#if PPSSPP_PLATFORM(WINDOWS)
	LoadD3D11();
	init_glslang();
	LoadD3DCompilerDynamic();
#else
	init_glslang();
#endif

	if (!TestFragmentShaders()) {
		return false;
	}

	if (!TestReinterpretShaders()) {
		return false;
	}

	if (!TestVertexShaders()) {
		return false;
	}

	return true;
} 
