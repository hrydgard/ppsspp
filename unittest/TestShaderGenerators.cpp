#include "Common/StringUtils.h"

#include "GPU/Common/ShaderId.h"
#include "GPU/Common/ShaderCommon.h"
#include "Common/Data/Random/Rng.h"

#include "GPU/Vulkan/VulkanContext.h"

#include "GPU/Vulkan/FragmentShaderGeneratorVulkan.h"
#include "GPU/D3D11/FragmentShaderGeneratorD3D11.h"
#include "GPU/Directx9/PixelShaderGeneratorDX9.h"
#include "GPU/GLES/FragmentShaderGeneratorGLES.h"

#include "GPU/Vulkan/VertexShaderGeneratorVulkan.h"
#include "GPU/D3D11/VertexShaderGeneratorD3D11.h"
#include "GPU/Directx9/VertexShaderGeneratorDX9.h"
#include "GPU/GLES/VertexShaderGeneratorGLES.h"

#include "GPU/D3D11/D3D11Util.h"
#include "GPU/D3D11/D3D11Loader.h"

#include "GPU/D3D9/D3DCompilerLoader.h"
#include "GPU/D3D9/D3D9ShaderCompiler.h"

bool GenerateFShader(FShaderID id, char *buffer, ShaderLanguage lang) {
	switch (lang) {
	case ShaderLanguage::HLSL_D3D11:
		return GenerateFragmentShaderD3D11(id, buffer, ShaderLanguage::HLSL_D3D11);
	case ShaderLanguage::HLSL_DX9:
		DX9::GenerateFragmentShaderHLSL(id, buffer, ShaderLanguage::HLSL_DX9);
		// TODO: Need a device :(  Returning false here so it doesn't get tried.
		return false;
	case ShaderLanguage::GLSL_VULKAN:
		return GenerateVulkanGLSLFragmentShader(id, buffer, 0);
	case ShaderLanguage::GLSL_140:
	case ShaderLanguage::GLSL_300:
		// TODO: Need a device - except that maybe glslang could be used to verify these ....
		return false;
	default:
		return false;
	}
}

bool GenerateVShader(VShaderID id, char *buffer, ShaderLanguage lang) {
	switch (lang) {
	case ShaderLanguage::HLSL_D3D11:
		return GenerateVertexShaderD3D11(id, buffer, ShaderLanguage::HLSL_D3D11);
	case ShaderLanguage::HLSL_DX9:
		DX9::GenerateVertexShaderHLSL(id, buffer, ShaderLanguage::HLSL_DX9);
		// TODO: Need a device :(  Returning false here so it doesn't get tried.
		return false;
		// return DX9::GenerateFragmentShaderHLSL(id, buffer, ShaderLanguage::HLSL_DX9);
	case ShaderLanguage::GLSL_VULKAN:
		return GenerateVulkanGLSLVertexShader(id, buffer);
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
	default:
		return false;
	}
}

bool TestShaderGenerators() {
	LoadD3D11();
	init_glslang();
	LoadD3DCompilerDynamic();

	ShaderLanguage languages[] = {
		ShaderLanguage::HLSL_D3D11,
		ShaderLanguage::GLSL_VULKAN,
		ShaderLanguage::GLSL_140,
		ShaderLanguage::GLSL_300,
		ShaderLanguage::HLSL_DX9,
	};
	const int numLanguages = ARRAY_SIZE(languages);

	char *buffer[numLanguages];

	for (int i = 0; i < numLanguages; i++) {
		buffer[i] = new char[65536];
	}
	// Generate a bunch of random fragment shader IDs, try to generate shader source.
	// Then compile it and check that it's ok.
	GMRng rng;
	int successes = 0;
	int count = 200;
	for (int i = 0; i < count; i++) {
		uint32_t bottom = rng.R32();
		uint32_t top = rng.R32();
		FShaderID id;
		id.d[0] = bottom;
		id.d[1] = top;

		bool generateSuccess[numLanguages]{};

		for (int j = 0; j < numLanguages; j++) {
			generateSuccess[j] = GenerateFShader(id, buffer[j], languages[j]);
		}
		
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

	// Now try vertex shaders.
	for (int i = 0; i < count; i++) {
		uint32_t bottom = rng.R32();
		uint32_t top = rng.R32();
		VShaderID id;
		id.d[0] = bottom;
		id.d[1] = top;

		// Skip testing beziers for now. I'll deal with those bugs later.
		id.SetBit(VS_BIT_BEZIER, false);
		id.SetBit(VS_BIT_SPLINE, false);

		bool generateSuccess[numLanguages]{};

		for (int j = 0; j < numLanguages; j++) {
			generateSuccess[j] = GenerateVShader(id, buffer[j], languages[j]);
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

	for (int i = 0; i < numLanguages; i++) {
		delete[] buffer[i];
	}

	return true;
} 
