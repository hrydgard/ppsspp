#pragma once

// GLSL_1xx and GLSL_3xx each cover a lot of sub variants. All the little quirks
// that differ are covered in ShaderLanguageDesc.
// Defined as a bitmask so stuff like GetSupportedShaderLanguages can return combinations.
enum ShaderLanguage {
	GLSL_1xx = 1,
	GLSL_3xx = 2,
	GLSL_VULKAN = 4,
	HLSL_D3D9 = 8,
	HLSL_D3D11 = 16,
};

inline bool ShaderLanguageIsOpenGL(ShaderLanguage lang) {
	return lang == GLSL_1xx || lang == GLSL_3xx;
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

// For passing error messages from shader compilation (and other critical issues) back to the host.
// This can run on any thread - be aware!
// TODO: See if we can find a less generic name for this.
typedef void (*ErrorCallbackFn)(const char *shortDesc, const char *details, void *userdata);
