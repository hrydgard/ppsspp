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
#include "GPU/Common/GeometryShaderGenerator.h"
#include "GPU/Common/ReinterpretFramebuffer.h"
#include "GPU/Common/StencilCommon.h"
#include "GPU/Common/DepalettizeShaderCommon.h"

#if PPSSPP_PLATFORM(WINDOWS)
#include <wrl/client.h>
#include "GPU/D3D11/D3D11Util.h"
#include "GPU/D3D11/D3D11Loader.h"
#endif

static constexpr size_t CODE_BUFFER_SIZE = 32768;

bool GenerateFShader(FShaderID id, char *buffer, ShaderLanguage lang, Draw::Bugs bugs, std::string *errorString) {
	buffer[0] = '\0';

	FragmentShaderFlags flags;

	uint64_t uniformMask;
	switch (lang) {
	case ShaderLanguage::GLSL_VULKAN:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_VULKAN);
		return GenerateFragmentShader(id, buffer, compat, bugs, &uniformMask, &flags, errorString);
	}
	case ShaderLanguage::GLSL_1xx:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_1xx);
		return GenerateFragmentShader(id, buffer, compat, bugs, &uniformMask, &flags, errorString);
	}
	case ShaderLanguage::GLSL_3xx:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_3xx);
		return GenerateFragmentShader(id, buffer, compat, bugs, &uniformMask, &flags, errorString);
	}
	case ShaderLanguage::HLSL_D3D11:
	{
		ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D11);
		return GenerateFragmentShader(id, buffer, compat, bugs, &uniformMask, &flags, errorString);
	}
	default:
		return false;
	}
}

bool GenerateVShader(VShaderID id, char *buffer, ShaderLanguage lang, Draw::Bugs bugs, std::string *errorString) {
	buffer[0] = '\0';

	VertexShaderFlags flags;

	uint32_t attrMask;
	uint64_t uniformMask;
	switch (lang) {
	case ShaderLanguage::GLSL_VULKAN:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_VULKAN);
		return GenerateVertexShader(id, buffer, compat, bugs, &attrMask, &uniformMask, &flags, errorString);
	}
	case ShaderLanguage::GLSL_1xx:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_1xx);
		return GenerateVertexShader(id, buffer, compat, bugs, &attrMask, &uniformMask, &flags, errorString);
	}
	case ShaderLanguage::GLSL_3xx:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_3xx);
		return GenerateVertexShader(id, buffer, compat, bugs, &attrMask, &uniformMask, &flags, errorString);
	}
	case ShaderLanguage::HLSL_D3D11:
	{
		ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D11);
		return GenerateVertexShader(id, buffer, compat, bugs, &attrMask, &uniformMask, &flags, errorString);
	}
	default:
		return false;
	}
}

bool GenerateGShader(GShaderID id, char *buffer, ShaderLanguage lang, Draw::Bugs bugs, std::string *errorString) {
	buffer[0] = '\0';

	errorString->clear();

	switch (lang) {
	case ShaderLanguage::GLSL_VULKAN:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_VULKAN);
		return GenerateGeometryShader(id, buffer, compat, bugs, errorString);
	}
	/*
	case ShaderLanguage::GLSL_3xx:
	{
		ShaderLanguageDesc compat(ShaderLanguage::GLSL_3xx);
		return GenerateGeometryShader(id, buffer, compat, bugs, errorString);
	}
	case ShaderLanguage::HLSL_D3D11:
	{
		ShaderLanguageDesc compat(ShaderLanguage::HLSL_D3D11);
		return GenerateGeometryShader(id, buffer, compat, bugs, errorString);
	}
	*/
	default:
		return false;
	}
}

static VkShaderStageFlagBits StageToVulkan(ShaderStage stage) {
	switch (stage) {
	case ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
	case ShaderStage::Geometry: return VK_SHADER_STAGE_GEOMETRY_BIT;
	case ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
	case ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
	}
	return VK_SHADER_STAGE_FRAGMENT_BIT;
}

bool TestCompileShader(const char *buffer, ShaderLanguage lang, ShaderStage stage, std::string *errorMessage) {
	std::vector<uint32_t> spirv;
	switch (lang) {
#if PPSSPP_PLATFORM(WINDOWS)
	case ShaderLanguage::HLSL_D3D11:
	{
		const char *programType = nullptr;
		switch (stage) {
		case ShaderStage::Vertex: programType = "vs_4_0"; break;
		case ShaderStage::Fragment: programType = "ps_4_0"; break;
		case ShaderStage::Geometry: programType = "gs_4_0"; break;
		default: return false;
		}
		auto output = CompileShaderToBytecodeD3D11(buffer, strlen(buffer), programType, 0);
		return !output.empty();
	}
#endif

	case ShaderLanguage::GLSL_VULKAN:
		return GLSLtoSPV(StageToVulkan(stage), buffer, GLSLVariant::VULKAN, spirv, errorMessage);
	case ShaderLanguage::GLSL_1xx:
		return GLSLtoSPV(StageToVulkan(stage), buffer, GLSLVariant::GL140, spirv, errorMessage);
	case ShaderLanguage::GLSL_3xx:
		return GLSLtoSPV(StageToVulkan(stage), buffer, GLSLVariant::GLES300, spirv, errorMessage);
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
	case GLSL_VULKAN: return "GLSL_VULKAN";
	case GLSL_1xx: return "GLSL_1xx";
	case GLSL_3xx: return "GLSL_3xx";
	default: return "N/A";
	}
}

bool TestReinterpretShaders() {
	Draw::Bugs bugs;

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
		printf("=== %s ===\n\n", ShaderLanguageToString(languages[k]));

		ShaderLanguageDesc desc(languages[k]);

		std::string errorMessage;

		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				if (i == j)
					continue;  // useless shader!
				ShaderWriter writer(buffer, desc, ShaderStage::Fragment);
				GenerateReinterpretFragmentShader(writer, fmts[i], fmts[j]);
				if (strlen(buffer) >= 8192) {
					printf("Reinterpret fragment shader %d exceeded buffer:\n\n%s\n", (int)j, LineNumberString(buffer).c_str());
					failed = true;
				}
				if (!TestCompileShader(buffer, languages[k], ShaderStage::Fragment, &errorMessage)) {
					printf("Error compiling reinterpret fragment shader %d:\n\n%s\n\n%s\n", (int)j, LineNumberString(buffer).c_str(), errorMessage.c_str());
					failed = true;
				} else {
					printf("===\n%s\n===\n", buffer);
				}
			}
		}
	}

	delete[] buffer;
	return !failed;
}

bool TestStencilShaders() {
	Draw::Bugs bugs;

	ShaderLanguage languages[] = {
#if PPSSPP_PLATFORM(WINDOWS)
		ShaderLanguage::HLSL_D3D11,
#endif
		ShaderLanguage::GLSL_VULKAN,
		ShaderLanguage::GLSL_3xx,
	};

	char *buffer = new char[65536];

	bool failed = false;

	for (int k = 0; k < ARRAY_SIZE(languages); k++) {
		printf("=== %s ===\n\n", ShaderLanguageToString(languages[k]));

		ShaderLanguageDesc desc(languages[k]);
		std::string errorMessage;

		// Generate all despite failures - it's only a few.
		// Only use export on Vulkan, because GLSL_3xx is ES which doesn't support stencil export.
		bool allowUseExport = languages[k] == ShaderLanguage::GLSL_VULKAN;
		for (int useExport = 0; useExport <= (allowUseExport ? 1 : 0); ++useExport) {
			GenerateStencilFs(buffer, desc, bugs, useExport == 1);
			if (strlen(buffer) >= 8192) {
				printf("Stencil fragment shader (useExport=%d) exceeded buffer:\n\n%s\n", useExport, LineNumberString(buffer).c_str());
				failed = true;
			}
			if (!TestCompileShader(buffer, languages[k], ShaderStage::Fragment, &errorMessage)) {
				printf("Error compiling stencil shader (useExport=%d):\n\n%s\n\n%s\n", useExport, LineNumberString(buffer).c_str(), errorMessage.c_str());
				failed = true;
			} else {
				printf("===\n%s\n===\n", buffer);
			}
		}

		GenerateStencilVs(buffer, desc);
		if (strlen(buffer) >= 8192) {
			printf("Stencil vertex shader exceeded buffer:\n\n%s\n", LineNumberString(buffer).c_str());
			failed = true;
		}
		if (!TestCompileShader(buffer, languages[k], ShaderStage::Vertex, &errorMessage)) {
			printf("Error compiling stencil shader:\n\n%s\n\n%s\n", LineNumberString(buffer).c_str(), errorMessage.c_str());
			failed = true;
		} else {
			printf("===\n%s\n===\n", buffer);
		}
	}

	delete[] buffer;
	return !failed;
}

bool TestDepalShaders() {
	Draw::Bugs bugs;

	ShaderLanguage languages[] = {
#if PPSSPP_PLATFORM(WINDOWS)
		ShaderLanguage::HLSL_D3D11,
#endif
		ShaderLanguage::GLSL_VULKAN,
		ShaderLanguage::GLSL_3xx,
		ShaderLanguage::GLSL_1xx,
	};

	char *buffer = new char[65536];

	for (int k = 0; k < ARRAY_SIZE(languages); k++) {
		printf("=== %s ===\n\n", ShaderLanguageToString(languages[k]));

		ShaderLanguageDesc desc(languages[k]);
		std::string errorMessage;

		// TODO: Try some different configurations of the fragment shader.
		// But first just try one.
		DepalConfig config{};
		config.clutFormat = GE_CMODE_16BIT_ABGR4444;
		config.shift = 8;
		config.startPos = 64;
		config.mask = 0xFF;
		config.bufferFormat = GE_FORMAT_8888;
		config.textureFormat = GE_TFMT_CLUT32;
		config.depthUpperBits = 0;

		ShaderWriter writer(buffer, desc, ShaderStage::Fragment);
		GenerateDepalFs(writer, config);
		if (strlen(buffer) >= 8192) {
			printf("Depal shader exceeded buffer:\n\n%s\n", LineNumberString(buffer).c_str());
			delete[] buffer;
			return false;
		}
		if (!TestCompileShader(buffer, languages[k], ShaderStage::Fragment, &errorMessage)) {
			printf("Error compiling depal shader:\n\n%s\n\n%s\n", LineNumberString(buffer).c_str(), errorMessage.c_str());
			delete[] buffer;
			return false;
		} else {
			printf("===\n%s\n===\n", buffer);
		}
	}

	delete[] buffer;
	return true;
}

const ShaderLanguage languages[] = {
#if PPSSPP_PLATFORM(WINDOWS)
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
		if (id.Bit(VS_BIT_VERTEX_RANGE_CULLING)) {
			continue;
		}

		bool generateSuccess[numLanguages]{};
		std::string genErrorString[numLanguages];

		for (int j = 0; j < numLanguages; j++) {
			generateSuccess[j] = GenerateVShader(id, buffer[j], languages[j], bugs, &genErrorString[j]);
			if (!genErrorString[j].empty()) {
				printf("%s\n", genErrorString[j].c_str());
			}
		}

		for (int j = 0; j < numLanguages; j++) {
			if (strlen(buffer[j]) >= CODE_BUFFER_SIZE) {
				printf("Vertex shader exceeded buffer:\n\n%s\n", LineNumberString(buffer[j]).c_str());
				for (int i = 0; i < numLanguages; i++) {
					delete[] buffer[i];
				}
				return false;
			}
		}

		// Now that we have the strings ready for easy comparison (buffer,4 in the watch window),
		// let's try to compile them.
		for (int j = 0; j < numLanguages; j++) {
			if (generateSuccess[j]) {
				std::string errorMessage;
				if (!TestCompileShader(buffer[j], languages[j], ShaderStage::Vertex, &errorMessage)) {
					printf("Error compiling vertex shader %d:\n\n%s\n\n%s\n", (int)j, LineNumberString(buffer[j]).c_str(), errorMessage.c_str());
					for (int i = 0; i < numLanguages; i++) {
						delete[] buffer[i];
					}
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

		for (int j = 0; j < numLanguages; j++) {
			if (strlen(buffer[j]) >= CODE_BUFFER_SIZE) {
				printf("Fragment shader exceeded buffer:\n\n%s\n", LineNumberString(buffer[j]).c_str());
				for (int i = 0; i < numLanguages; i++) {
					delete[] buffer[i];
				}
				return false;
			}
		}

		// Now that we have the strings ready for easy comparison (buffer,4 in the watch window),
		// let's try to compile them.
		for (int j = 0; j < numLanguages; j++) {
			if (generateSuccess[j]) {
				std::string errorMessage;
				if (!TestCompileShader(buffer[j], languages[j], ShaderStage::Fragment, &errorMessage)) {
					printf("Error compiling fragment shader:\n\n%s\n\n%s\n", LineNumberString(buffer[j]).c_str(), errorMessage.c_str());
					for (int i = 0; i < numLanguages; i++) {
						delete[] buffer[i];
					}
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


bool TestGeometryShaders() {
	char *buffer[numLanguages];

	for (int i = 0; i < numLanguages; i++) {
		buffer[i] = new char[65536];
	}
	GMRng rng;
	int successes = 0;
	int count = 30;

	Draw::Bugs bugs;

	// Generate a bunch of random fragment shader IDs, try to generate shader source.
	// Then compile it and check that it's ok.
	for (int i = 0; i < count; i++) {
		uint32_t bottom = i << 1;
		GShaderID id;
		id.d[0] = bottom;
		id.d[1] = 0;

		id.SetBit(GS_BIT_ENABLED, true);

		bool generateSuccess[numLanguages]{};
		std::string genErrorString[numLanguages];

		for (int j = 0; j < numLanguages; j++) {
			buffer[j][0] = 0;
			generateSuccess[j] = GenerateGShader(id, buffer[j], languages[j], bugs, &genErrorString[j]);
			if (!genErrorString[j].empty()) {
				printf("%s\n", genErrorString[j].c_str());
			}
			// We ignore the contents of the error string here, not even gonna try to compile if it errors.
		}

		for (int j = 0; j < numLanguages; j++) {
			if (strlen(buffer[j]) >= CODE_BUFFER_SIZE) {
				printf("Geometry shader exceeded buffer:\n\n%s\n", LineNumberString(buffer[j]).c_str());
				for (int i = 0; i < numLanguages; i++) {
					delete[] buffer[i];
				}
				return false;
			}
		}

		// Now that we have the strings ready for easy comparison (buffer,4 in the watch window),
		// let's try to compile them.
		for (int j = 0; j < numLanguages; j++) {
			if (generateSuccess[j]) {
				std::string errorMessage;
				if (!TestCompileShader(buffer[j], languages[j], ShaderStage::Geometry, &errorMessage)) {
					printf("Error compiling geometry shader:\n\n%s\n\n%s\n", LineNumberString(buffer[j]).c_str(), errorMessage.c_str());
					for (int i = 0; i < numLanguages; i++) {
						delete[] buffer[i];
					}
					return false;
				}
				successes++;
			}
		}
	}

	printf("%d/%d geometry shaders generated (it's normal that it's not all, there are invalid bit combos)\n", successes, count * numLanguages);

	for (int i = 0; i < numLanguages; i++) {
		delete[] buffer[i];
	}
	return true;
}


bool TestShaderGenerators() {
#if PPSSPP_PLATFORM(WINDOWS)
	LoadD3D11();
	init_glslang();
#else
	init_glslang();
#endif

	if (!TestStencilShaders()) {
		return false;
	}

	if (!TestGeometryShaders()) {
		return false;
	}

	if (!TestReinterpretShaders()) {
		return false;
	}

	if (!TestDepalShaders()) {
		return false;
	}

	if (!TestFragmentShaders()) {
		return false;
	}

	if (!TestVertexShaders()) {
		return false;
	}

	return true;
} 
