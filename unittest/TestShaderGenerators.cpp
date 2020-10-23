#include <algorithm>

#include "Common/StringUtils.h"

#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderCommon.h"
#include "Common/Data/Random/Rng.h"

#include "GPU/Vulkan/VulkanContext.h"

#include "GPU/Directx9/FragmentShaderGeneratorHLSL.h"
#include "GPU/GLES/FragmentShaderGeneratorGLES.h"

#include "GPU/Vulkan/VertexShaderGeneratorVulkan.h"
#include "GPU/Directx9/VertexShaderGeneratorHLSL.h"
#include "GPU/GLES/VertexShaderGeneratorGLES.h"

#include "GPU/D3D11/D3D11Util.h"
#include "GPU/D3D11/D3D11Loader.h"

#include "GPU/D3D9/D3DCompilerLoader.h"
#include "GPU/D3D9/D3D9ShaderCompiler.h"


bool GenerateFShader(FShaderID id, char *buffer, ShaderLanguage lang, std::string *errorString) {
	switch (lang) {
	case ShaderLanguage::HLSL_D3D11:
		return GenerateFragmentShaderHLSL(id, buffer, ShaderLanguage::HLSL_D3D11, errorString);
	case ShaderLanguage::HLSL_DX9:
		GenerateFragmentShaderHLSL(id, buffer, ShaderLanguage::HLSL_DX9, errorString);
		// TODO: Need a device :(  Returning false here so it doesn't get tried.
		return false;
	case ShaderLanguage::GLSL_VULKAN:
	{
		GLSLShaderCompat compat{};
		compat.SetupForVulkan();
		uint64_t uniformMask;
		return GenerateFragmentShaderGLSL(id, buffer, compat, &uniformMask, errorString);
	}
	case ShaderLanguage::GLSL_140:
	case ShaderLanguage::GLSL_300:
		// TODO: Need a device - except that maybe glslang could be used to verify these ....
		return false;
	case ShaderLanguage::TEST_GLSL_VULKAN:
		return false;
	default:
		return false;
	}
}

bool GenerateVShader(VShaderID id, char *buffer, ShaderLanguage lang, std::string *errorString) {
	switch (lang) {
	case ShaderLanguage::HLSL_D3D11:
		return GenerateVertexShaderHLSL(id, buffer, ShaderLanguage::HLSL_D3D11, errorString);
	case ShaderLanguage::HLSL_DX9:
		GenerateVertexShaderHLSL(id, buffer, ShaderLanguage::HLSL_DX9, errorString);
		// TODO: Need a device :(  Returning false here so it doesn't get tried.
		return false;
		// return DX9::GenerateFragmentShaderHLSL(id, buffer, ShaderLanguage::HLSL_DX9);
	case ShaderLanguage::GLSL_VULKAN:
		return GenerateVertexShaderVulkanGLSL(id, buffer, errorString);
	case ShaderLanguage::TEST_GLSL_VULKAN:
	{
		GLSLShaderCompat compat{};
		compat.SetupForVulkan();
		uint32_t attrMask;
		uint64_t uniformMask;
		return GenerateVertexShaderGLSL(id, buffer, compat, &attrMask, &uniformMask, errorString);
	}
	default:
		return false;
	}
}

bool TestCompileShader(const char *buffer, ShaderLanguage lang, bool vertex) {
	switch (lang) {
	case ShaderLanguage::HLSL_D3D11:
	{
		auto output = CompileShaderToBytecodeD3D11(buffer, strlen(buffer), vertex ? "vs_4_0" : "ps_4_0", 0);
		return !output.empty();
	}
	case ShaderLanguage::HLSL_DX9:
		return false;
	case ShaderLanguage::GLSL_VULKAN:
	{
		std::vector<uint32_t> spirv;
		std::string errorMessage;
		bool result = GLSLtoSPV(vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, buffer, spirv, &errorMessage);
		if (!result) {
			printf("GLSLtoSPV ERROR:\n%s\n\n", errorMessage.c_str());
		}
		return result;
	}
	case ShaderLanguage::GLSL_140:
		return false;
	case ShaderLanguage::GLSL_300:
		return false;
	case ShaderLanguage::TEST_GLSL_VULKAN:
		return true;
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
			for (size_t j = i + 1; j < i + 5 && j < a_lines.size(); j++) {
				printf("a: %s\n", a_lines[j].c_str());
				printf("b: %s\n", b_lines[j].c_str());
			}
			printf("==================\n");
			return;
		}
	}
}


bool TestShaderGenerators() {
	LoadD3D11();
	init_glslang();
	LoadD3DCompilerDynamic();

	ShaderLanguage languages[] = {
		ShaderLanguage::TEST_GLSL_VULKAN,
		ShaderLanguage::GLSL_VULKAN,
		ShaderLanguage::HLSL_D3D11,
		ShaderLanguage::GLSL_140,
		ShaderLanguage::GLSL_300,
		ShaderLanguage::HLSL_DX9,
	};
	const int numLanguages = ARRAY_SIZE(languages);

	char *buffer[numLanguages];

	for (int i = 0; i < numLanguages; i++) {
		buffer[i] = new char[65536];
	}
	GMRng rng;
	int successes = 0;
	int count = 700;

	// Generate a bunch of random vertex shader IDs, try to generate shader source.
	// Then compile it and check that it's ok.
	for (int i = 0; i < count; i++) {
		uint32_t bottom = rng.R32();
		uint32_t top = rng.R32();
		VShaderID id;
		id.d[0] = bottom;
		id.d[1] = top;

		bool generateSuccess[numLanguages]{};
		std::string genErrorString[numLanguages];

		for (int j = 0; j < numLanguages; j++) {
			generateSuccess[j] = GenerateVShader(id, buffer[j], languages[j], &genErrorString[j]);
			if (!genErrorString[j].empty()) {
				printf("%s\n", genErrorString[j].c_str());
			}
		}

		if (generateSuccess[0] != generateSuccess[1]) {
			printf("mismatching success! %s %s\n", genErrorString[0].c_str(), genErrorString[1].c_str());
			printf("%s\n", buffer[0]);
			printf("%s\n", buffer[1]);
			return 1;
		}
		if (generateSuccess[0] && strcmp(buffer[0], buffer[1])) {
			printf("mismatching shaders! a=glsl b=vulkan\n");
			PrintDiff(buffer[0], buffer[1]);
			return 1;
		}

		// Now that we have the strings ready for easy comparison (buffer,4 in the watch window),
		// let's try to compile them.
		for (int j = 0; j < numLanguages; j++) {
			if (generateSuccess[j]) {
				if (!TestCompileShader(buffer[j], languages[j], true)) {
					printf("Error compiling vertex shader:\n\n%s\n\n", LineNumberString(buffer[j]).c_str());
					return false;
				}
				successes++;
			}
		}
	}

	printf("%d/%d vertex shaders generated (it's normal that it's not all, there are invalid bit combos)\n", successes, count * numLanguages);

	successes = 0;
	count = 200;

	// Generate a bunch of random fragment shader IDs, try to generate shader source.
	// Then compile it and check that it's ok.
	for (int i = 0; i < count; i++) {
		uint32_t bottom = rng.R32();
		uint32_t top = rng.R32();
		FShaderID id;
		id.d[0] = bottom;
		id.d[1] = top;

		bool generateSuccess[numLanguages]{};
		std::string genErrorString[numLanguages];

		for (int j = 0; j < numLanguages; j++) {
			generateSuccess[j] = GenerateFShader(id, buffer[j], languages[j], &genErrorString[j]);
			if (!genErrorString[j].empty()) {
				printf("%s\n", genErrorString[j].c_str());
			}
			// We ignore the contents of the error string here, not even gonna try to compile if it errors.
		}

		/*
		// KEEPING FOR REUSE LATER: Defunct temporary test: Compare GLSL-in-Vulkan-mode vs Vulkan
		if (generateSuccess[0] != generateSuccess[1]) {
			printf("mismatching success! %s %s\n", genErrorString[0].c_str(), genErrorString[1].c_str());
			printf("%s\n", buffer[0]);
			printf("%s\n", buffer[1]);
			return 1;
		}
		if (generateSuccess[0] && strcmp(buffer[0], buffer[1])) {
			printf("mismatching shaders!\n");
			PrintDiff(buffer[0], buffer[1]);
			return 1;
		}
		*/

		// Now that we have the strings ready for easy comparison (buffer,4 in the watch window),
		// let's try to compile them.
		for (int j = 0; j < numLanguages; j++) {
			if (generateSuccess[j]) {
				if (!TestCompileShader(buffer[j], languages[j], false)) {
					printf("Error compiling fragment shader:\n\n%s\n\n", LineNumberString(buffer[j]).c_str());
					return false;
				}
				successes++;
			}
		}
	}

	printf("%d/%d fragment shaders generated (it's normal that it's not all, there are invalid bit combos)\n", successes, count * numLanguages);

	successes = 0;
	count = 200;

	for (int i = 0; i < numLanguages; i++) {
		delete[] buffer[i];
	}

	return true;
} 
