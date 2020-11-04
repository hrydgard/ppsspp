#pragma once

enum ShaderLanguage {
	GLSL_140,  // really covers a lot more. This set of languages is not good.
	GLSL_300,
	GLSL_VULKAN,
	HLSL_D3D9,
	HLSL_D3D11,
};

inline bool ShaderLanguageIsOpenGL(ShaderLanguage lang) {
	return lang == GLSL_140 || lang == GLSL_300;
}

enum class ShaderStage {
	Vertex,
	Fragment
};

struct ShaderLanguageDesc {
	explicit ShaderLanguageDesc(ShaderLanguage lang);

	int glslVersionNumber = 0;
	ShaderLanguage shaderLanguage;
	bool gles = false;
	const char *varying_fs = nullptr;
	const char *varying_vs = nullptr;
	const char *attribute = nullptr;
	const char *fragColor0 = nullptr;
	const char *fragColor1 = nullptr;
	const char *texture = nullptr;
	const char *texelFetch = nullptr;
	const char *lastFragData = nullptr;
	const char *framebufferFetchExtension = nullptr;
	const char *vsOutPrefix = "";
	bool glslES30 = false;
	bool bitwiseOps = false;
	bool forceMatrix4x4 = false;
	bool coefsFromBuffers = false;
};
