#include <algorithm>

#include "Common/StringUtils.h"

#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderCommon.h"
#include "GPU/Common/GPUStateUtils.h"
#include "Common/Data/Random/Rng.h"

#include "GPU/Vulkan/VulkanContext.h"

#include "GPU/Common/FragmentShaderGenerator.h"

#include "GPU/Directx9/VertexShaderGeneratorHLSL.h"
#include "GPU/GLES/VertexShaderGeneratorGLES.h"

#include "GPU/D3D11/D3D11Util.h"
#include "GPU/D3D11/D3D11Loader.h"

#include "GPU/D3D9/D3DCompilerLoader.h"
#include "GPU/D3D9/D3D9ShaderCompiler.h"

bool GenerateFShader(FShaderID id, char *buffer, ShaderLanguage lang, std::string *errorString) {
	uint64_t uniformMask;
	switch (lang) {
	case ShaderLanguage::GLSL_VULKAN:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_VULKAN);
		return GenerateFragmentShader(id, buffer, compat, &uniformMask, errorString);
	}
	case ShaderLanguage::GLSL_140:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_140);
		return GenerateFragmentShader(id, buffer, compat, &uniformMask, errorString);
	}
	case ShaderLanguage::GLSL_300:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_140);
		return GenerateFragmentShader(id, buffer, compat, &uniformMask, errorString);
	}
	case ShaderLanguage::HLSL_D3D9:
	{
		ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D9);
		return GenerateFragmentShader(id, buffer, compat, &uniformMask, errorString);
	}
	case ShaderLanguage::HLSL_D3D11:
	{
		ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D11);
		return GenerateFragmentShader(id, buffer, compat, &uniformMask, errorString);
	}
	default:
		return false;
	}
}

bool GenerateVShader(VShaderID id, char *buffer, ShaderLanguage lang, std::string *errorString) {
	uint32_t attrMask;
	uint64_t uniformMask;
	switch (lang) {
	case ShaderLanguage::GLSL_VULKAN:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_VULKAN);
		return GenerateVertexShaderGLSL(id, buffer, compat, &attrMask, &uniformMask, errorString);
	}
	case ShaderLanguage::GLSL_140:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_140);
		return GenerateVertexShaderGLSL(id, buffer, compat, &attrMask, &uniformMask, errorString);
	}
	case ShaderLanguage::GLSL_300:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_140);
		return GenerateVertexShaderGLSL(id, buffer, compat, &attrMask, &uniformMask, errorString);
	}
	case ShaderLanguage::HLSL_D3D9:
	{
		//ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D9);
		return GenerateVertexShaderHLSL(id, buffer, ShaderLanguage::HLSL_D3D9, errorString);
	}
	case ShaderLanguage::HLSL_D3D11:
	{
		//ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D11);
		return GenerateVertexShaderHLSL(id, buffer, ShaderLanguage::HLSL_D3D11, errorString);
	}
	default:
		return false;
	}
}

bool TestCompileShader(const char *buffer, ShaderLanguage lang, bool vertex, std::string *errorMessage) {
	std::vector<uint32_t> spirv;
	switch (lang) {
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

	case ShaderLanguage::GLSL_VULKAN:
		return GLSLtoSPV(vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, buffer, GLSLVariant::VULKAN, spirv, errorMessage);
	case ShaderLanguage::GLSL_140:
		return GLSLtoSPV(vertex ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT, buffer, GLSLVariant::GL140, spirv, errorMessage);
	case ShaderLanguage::GLSL_300:
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


bool TestShaderGenerators() {
	LoadD3D11();
	init_glslang();
	LoadD3DCompilerDynamic();

	ShaderLanguage languages[] = {
		ShaderLanguage::HLSL_D3D9,
		ShaderLanguage::HLSL_D3D11,
		ShaderLanguage::GLSL_VULKAN,
		ShaderLanguage::GLSL_140,
		ShaderLanguage::GLSL_300,
	};
	const int numLanguages = ARRAY_SIZE(languages);

	char *buffer[numLanguages];

	for (int i = 0; i < numLanguages; i++) {
		buffer[i] = new char[65536];
	}
	GMRng rng;
	int successes = 0;
	int count = 700;

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
			generateSuccess[j] = GenerateFShader(id, buffer[j], languages[j], &genErrorString[j]);
			if (!genErrorString[j].empty()) {
				printf("%s\n", genErrorString[j].c_str());
			}
			// We ignore the contents of the error string here, not even gonna try to compile if it errors.
		}

		// KEEPING FOR REUSE LATER: Defunct temporary test.
		/*
		if (generateSuccess[0] != generateSuccess[1]) {
			printf("mismatching success! %s %s\n", genErrorString[0].c_str(), genErrorString[1].c_str());
			printf("%s\n", buffer[0]);
			printf("%s\n", buffer[1]);
			return 1;
		}
		if (generateSuccess[0] && strcmp(buffer[0], buffer[1])) {
			printf("mismatching shaders! a=glsl b=hlsl\n");
			PrintDiff(buffer[0], buffer[1]);
			return 1;
		}
		if (generateSuccess[2] && strcmp(buffer[2], buffer[3])) {
			printf("mismatching shaders! a=glsl b=hlsl\n");
			PrintDiff(buffer[2], buffer[3]);
			return 1;
		}*/
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

	successes = 0;
	count = 200;

	/*
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

		// KEEPING FOR REUSE LATER: Defunct temporary test: Compare GLSL-in-Vulkan-mode vs Vulkan
		if (generateSuccess[0] != generateSuccess[1]) {
			printf("mismatching success! '%s' '%s'\n", genErrorString[0].c_str(), genErrorString[1].c_str());
			printf("%s\n", buffer[0]);
			printf("%s\n", buffer[1]);
			return false;
		}
		if (generateSuccess[0] && strcmp(buffer[0], buffer[1])) {
			printf("mismatching shaders!\n");
			PrintDiff(buffer[0], buffer[1]);
			return false;
		}

		// Now that we have the strings ready for easy comparison (buffer,4 in the watch window),
		// let's try to compile them.
		for (int j = 0; j < numLanguages; j++) {
			if (generateSuccess[j]) {
				std::string errorMessage;
				if (!TestCompileShader(buffer[j], languages[j], true, &errorMessage)) {
					printf("Error compiling vertex shader:\n\n%s\n\n%s\n", LineNumberString(buffer[j]).c_str(), errorMessage.c_str());
					return false;
				}
				successes++;
			}
		}
	}

	printf("%d/%d vertex shaders generated (it's normal that it's not all, there are invalid bit combos)\n", successes, count * numLanguages);

	successes = 0;
	count = 200;
	*/

	_CrtCheckMemory();

	for (int i = 0; i < numLanguages; i++) {
		delete[] buffer[i];
	}

	return true;
} 
